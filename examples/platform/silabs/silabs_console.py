#!/usr/bin/env python3
"""
Silicon Labs Console - Dual Terminal Interface for Serial Communication

This script provides a split-screen terminal interface for monitoring and interacting
with a Silicon Labs device over a serial connection. Messages are routed to different
terminal panes based on their header byte.
"""

import argparse
import serial
import sys
import threading
import signal
import time
from datetime import datetime
from queue import Queue, Empty
from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
                             QTextEdit, QLineEdit, QSplitter, QLabel, QPushButton, QCheckBox, QDialog, QFileDialog)
from PyQt5.QtCore import Qt, pyqtSignal, QObject
from PyQt5.QtGui import QTextCursor, QColor, QTextCharFormat, QFont

class SignalEmitter(QObject):
    """Qt signal emitter for thread-safe UI updates"""
    log_message = pyqtSignal(str)
    interactive_message = pyqtSignal(str)
    error_message = pyqtSignal(str)

class SilabsConsole(QMainWindow):
    """Dual-terminal interface for serial communication with header-based message routing"""
    
    START_OF_FRAME = 0x01
    END_OF_FRAME = 0x04
    MESSAGE_TERMINATOR = b'\r\n'
    
    def __init__(self, port, baudrate):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        self.running = False
        self.connected = False  # Track if serial port is connected
        self.serial_thread = None  # Track the serial reading thread
        self.signals = SignalEmitter()
        self.font_size = 10  # Default font size
        self.replay_mode = False  # Track if viewing saved logs
        
        # Log filtering state
        self.log_filters = {
            'error': True,
            'warn': True,
            'info': True,
            'detail': True,
            'silabs': True
        }
        
        # Module filtering state (for advanced filters)
        self.module_filters = {
            'zcl': True,
            'dl': True,
            'im': True,
            'ot': True,
            'svr': True,
            'dis': True,
            'swu': True,
            'tst': True
        }
        
        # Store all log messages with their category and module
        self.all_log_messages = []  # List of tuples: (message, category, module)
        
        self.init_ui()
        self.setup_connections()
    
    def init_ui(self):
        """Initialize the Qt UI"""
        self.setWindowTitle(f"Silicon Labs Console - {self.port}")
        self.setGeometry(100, 100, 1200, 800)
        
        # Central widget and layout
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        layout = QVBoxLayout(central_widget)
        
        # Splitter for two terminals
        splitter = QSplitter(Qt.Vertical)
        
        # Log terminal (top)
        log_container = QWidget()
        log_layout = QVBoxLayout(log_container)
        log_layout.setContentsMargins(0, 0, 0, 0)
        
        # Log terminal header with font controls
        log_header = QWidget()
        log_header_layout = QHBoxLayout(log_header)
        log_header_layout.setContentsMargins(5, 5, 5, 5)
        
        log_label = QLabel("=== Log Terminal ===")
        log_label.setStyleSheet("color: white; font-weight: bold;")
        log_header_layout.addWidget(log_label)
        
        log_header_layout.addStretch()
        
        # Log filter buttons
        filter_label = QLabel("Filters:")
        filter_label.setStyleSheet("color: #999; font-size: 9pt; margin-right: 5px;")
        log_header_layout.addWidget(filter_label)
        
        self.filter_error_btn = QPushButton("[error]")
        self.filter_error_btn.setCheckable(True)
        self.filter_error_btn.setChecked(True)
        self.filter_error_btn.setFixedSize(60, 25)
        self.filter_error_btn.setStyleSheet("""
            QPushButton {
                background-color: #3c3c3c;
                color: #ff6464;
                border: 1px solid #555;
                font-size: 9pt;
            }
            QPushButton:hover {
                background-color: #4c4c4c;
            }
            QPushButton:checked {
                background-color: #ff6464;
                color: #1e1e1e;
                border: 1px solid #ff6464;
            }
        """)
        self.filter_error_btn.clicked.connect(lambda: self.toggle_filter('error'))
        log_header_layout.addWidget(self.filter_error_btn)
        
        self.filter_warn_btn = QPushButton("[warn]")
        self.filter_warn_btn.setCheckable(True)
        self.filter_warn_btn.setChecked(True)
        self.filter_warn_btn.setFixedSize(55, 25)
        self.filter_warn_btn.setStyleSheet("""
            QPushButton {
                background-color: #3c3c3c;
                color: #ffaa00;
                border: 1px solid #555;
                font-size: 9pt;
            }
            QPushButton:hover {
                background-color: #4c4c4c;
            }
            QPushButton:checked {
                background-color: #ffaa00;
                color: #1e1e1e;
                border: 1px solid #ffaa00;
            }
        """)
        self.filter_warn_btn.clicked.connect(lambda: self.toggle_filter('warn'))
        log_header_layout.addWidget(self.filter_warn_btn)
        
        self.filter_info_btn = QPushButton("[info]")
        self.filter_info_btn.setCheckable(True)
        self.filter_info_btn.setChecked(True)
        self.filter_info_btn.setFixedSize(50, 25)
        self.filter_info_btn.setStyleSheet("""
            QPushButton {
                background-color: #3c3c3c;
                color: #d4d4d4;
                border: 1px solid #555;
                font-size: 9pt;
            }
            QPushButton:hover {
                background-color: #4c4c4c;
            }
            QPushButton:checked {
                background-color: #d4d4d4;
                color: #1e1e1e;
                border: 1px solid #d4d4d4;
            }
        """)
        self.filter_info_btn.clicked.connect(lambda: self.toggle_filter('info'))
        log_header_layout.addWidget(self.filter_info_btn)
        
        self.filter_detail_btn = QPushButton("[detail]")
        self.filter_detail_btn.setCheckable(True)
        self.filter_detail_btn.setChecked(True)
        self.filter_detail_btn.setFixedSize(60, 25)
        self.filter_detail_btn.setStyleSheet("""
            QPushButton {
                background-color: #3c3c3c;
                color: #aaaaaa;
                border: 1px solid #555;
                font-size: 9pt;
            }
            QPushButton:hover {
                background-color: #4c4c4c;
            }
            QPushButton:checked {
                background-color: #aaaaaa;
                color: #1e1e1e;
                border: 1px solid #aaaaaa;
            }
        """)
        self.filter_detail_btn.clicked.connect(lambda: self.toggle_filter('detail'))
        log_header_layout.addWidget(self.filter_detail_btn)
        
        self.filter_silabs_btn = QPushButton("[silabs]")
        self.filter_silabs_btn.setCheckable(True)
        self.filter_silabs_btn.setChecked(True)
        self.filter_silabs_btn.setFixedSize(60, 25)
        self.filter_silabs_btn.setStyleSheet("""
            QPushButton {
                background-color: #3c3c3c;
                color: #6496ff;
                border: 1px solid #555;
                font-size: 9pt;
            }
            QPushButton:hover {
                background-color: #4c4c4c;
            }
            QPushButton:checked {
                background-color: #6496ff;
                color: #1e1e1e;
                border: 1px solid #6496ff;
            }
        """)
        self.filter_silabs_btn.clicked.connect(lambda: self.toggle_filter('silabs'))
        log_header_layout.addWidget(self.filter_silabs_btn)
        
        # More Options button
        self.more_options_btn = QPushButton("More Options")
        self.more_options_btn.setFixedSize(95, 25)
        self.more_options_btn.setStyleSheet("""
            QPushButton {
                background-color: #3c3c3c;
                color: #d4d4d4;
                border: 1px solid #555;
                font-size: 9pt;
            }
            QPushButton:hover {
                background-color: #4c4c4c;
            }
        """)
        self.more_options_btn.clicked.connect(self.show_module_filter_dialog)
        log_header_layout.addWidget(self.more_options_btn)
        
        # Separator
        separator = QLabel("|")
        separator.setStyleSheet("color: #555; margin: 0 5px;")
        log_header_layout.addWidget(separator)
        
        # Save button
        self.save_btn = QPushButton("Save")
        self.save_btn.setFixedSize(50, 25)
        self.save_btn.setStyleSheet("""
            QPushButton {
                background-color: #3c3c3c;
                color: #4CAF50;
                border: 1px solid #555;
                font-size: 9pt;
            }
            QPushButton:hover {
                background-color: #4c4c4c;
            }
        """)
        self.save_btn.clicked.connect(self.save_logs)
        log_header_layout.addWidget(self.save_btn)
        
        # Load button
        self.load_btn = QPushButton("Load")
        self.load_btn.setFixedSize(50, 25)
        self.load_btn.setStyleSheet("""
            QPushButton {
                background-color: #3c3c3c;
                color: #2196F3;
                border: 1px solid #555;
                font-size: 9pt;
            }
            QPushButton:hover {
                background-color: #4c4c4c;
            }
        """)
        self.load_btn.clicked.connect(self.load_logs)
        log_header_layout.addWidget(self.load_btn)
        
        # Close replay button (hidden by default)
        self.close_replay_btn = QPushButton("Close")
        self.close_replay_btn.setFixedSize(50, 25)
        self.close_replay_btn.setStyleSheet("""
            QPushButton {
                background-color: #f44336;
                color: white;
                border: 1px solid #d32f2f;
                font-size: 9pt;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #d32f2f;
            }
        """)
        self.close_replay_btn.clicked.connect(self.close_replay_mode)
        self.close_replay_btn.hide()  # Hidden by default
        log_header_layout.addWidget(self.close_replay_btn)
        
        # Separator
        separator2 = QLabel("|")
        separator2.setStyleSheet("color: #555; margin: 0 5px;")
        log_header_layout.addWidget(separator2)
        
        self.log_minus_btn = QPushButton("-")
        self.log_minus_btn.setFixedSize(30, 25)
        self.log_minus_btn.setStyleSheet("QPushButton { background-color: #3c3c3c; color: white; border: 1px solid #555; } QPushButton:hover { background-color: #4c4c4c; }")
        self.log_minus_btn.clicked.connect(self.decrease_font_size)
        log_header_layout.addWidget(self.log_minus_btn)
        
        self.log_plus_btn = QPushButton("+")
        self.log_plus_btn.setFixedSize(30, 25)
        self.log_plus_btn.setStyleSheet("QPushButton { background-color: #3c3c3c; color: white; border: 1px solid #555; } QPushButton:hover { background-color: #4c4c4c; }")
        self.log_plus_btn.clicked.connect(self.increase_font_size)
        log_header_layout.addWidget(self.log_plus_btn)
        
        log_header.setStyleSheet("background-color: #2b2b2b;")
        log_layout.addWidget(log_header)
        
        self.log_terminal = QTextEdit()
        self.log_terminal.setReadOnly(True)
        self.log_terminal.setStyleSheet("""
            QTextEdit {
                background-color: #1e1e1e;
                color: #d4d4d4;
                font-family: 'Courier New', monospace;
            }
        """)
        self.log_terminal.setLineWrapMode(QTextEdit.WidgetWidth)
        self.update_log_font()
        log_layout.addWidget(self.log_terminal)
        
        # Interactive terminal (bottom)
        interactive_container = QWidget()
        interactive_layout = QVBoxLayout(interactive_container)
        interactive_layout.setContentsMargins(0, 0, 0, 0)
        
        # Interactive terminal header with font controls
        interactive_header = QWidget()
        interactive_header_layout = QHBoxLayout(interactive_header)
        interactive_header_layout.setContentsMargins(5, 5, 5, 5)
        
        interactive_label = QLabel("=== Interactive Terminal ===")
        interactive_label.setStyleSheet("color: white; font-weight: bold;")
        interactive_header_layout.addWidget(interactive_label)
        
        interactive_header_layout.addStretch()
        
        # Connect/Disconnect button
        self.connect_btn = QPushButton("Connect")
        self.connect_btn.setFixedSize(75, 25)
        self.connect_btn.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                border: 1px solid #45a049;
                font-size: 9pt;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #45a049;
            }
        """)
        self.connect_btn.clicked.connect(self.toggle_connection)
        interactive_header_layout.addWidget(self.connect_btn)
        
        # Separator
        separator3 = QLabel("|")
        separator3.setStyleSheet("color: #555; margin: 0 5px;")
        interactive_header_layout.addWidget(separator3)
        
        self.interactive_minus_btn = QPushButton("-")
        self.interactive_minus_btn.setFixedSize(30, 25)
        self.interactive_minus_btn.setStyleSheet("QPushButton { background-color: #3c3c3c; color: white; border: 1px solid #555; } QPushButton:hover { background-color: #4c4c4c; }")
        self.interactive_minus_btn.clicked.connect(self.decrease_font_size)
        interactive_header_layout.addWidget(self.interactive_minus_btn)
        
        self.interactive_plus_btn = QPushButton("+")
        self.interactive_plus_btn.setFixedSize(30, 25)
        self.interactive_plus_btn.setStyleSheet("QPushButton { background-color: #3c3c3c; color: white; border: 1px solid #555; } QPushButton:hover { background-color: #4c4c4c; }")
        self.interactive_plus_btn.clicked.connect(self.increase_font_size)
        interactive_header_layout.addWidget(self.interactive_plus_btn)
        
        interactive_header.setStyleSheet("background-color: #2b2b2b;")
        interactive_layout.addWidget(interactive_header)
        
        self.interactive_terminal = QTextEdit()
        self.interactive_terminal.setReadOnly(True)
        self.interactive_terminal.setStyleSheet("""
            QTextEdit {
                background-color: #1e1e1e;
                color: #d4d4d4;
                font-family: 'Courier New', monospace;
            }
        """)
        self.interactive_terminal.setLineWrapMode(QTextEdit.WidgetWidth)
        self.update_interactive_font()
        interactive_layout.addWidget(self.interactive_terminal)
        
        # Command input
        self.command_input = QLineEdit()
        self.command_input.setPlaceholderText("Type command and press Enter...")
        self.command_input.setStyleSheet("""
            QLineEdit {
                background-color: #2b2b2b;
                color: white;
                padding: 5px;
                font-family: 'Courier New', monospace;
                border: 1px solid #555;
            }
        """)
        self.update_input_font()
        self.command_input.returnPressed.connect(self.send_command_from_input)
        self.command_input.setEnabled(False)  # Disabled until connected
        self.command_input.setPlaceholderText("[Not Connected - Click Connect to start]")
        interactive_layout.addWidget(self.command_input)
        
        # Add both containers to splitter
        splitter.addWidget(log_container)
        splitter.addWidget(interactive_container)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 1)
        
        layout.addWidget(splitter)
        
        # Status bar
        self.statusBar().showMessage(f"Ready - Not Connected to {self.port}")
    
    def setup_connections(self):
        """Setup Qt signal connections"""
        self.signals.log_message.connect(self.append_log_message)
        self.signals.interactive_message.connect(self.append_interactive_message)
        self.signals.error_message.connect(self.show_error)
        
    def get_message_category(self, message):
        """Determine the category of a log message"""
        message_lower = message.lower()
        if '[error]' in message_lower or '[error ]' in message_lower:
            return 'error'
        elif '[warn]' in message_lower or '[warn ]' in message_lower:
            return 'warn'
        elif '[silabs]' in message_lower or '[silabs ]' in message_lower:
            return 'silabs'
        elif '[detail]' in message_lower or '[detail ]' in message_lower:
            return 'detail'
        elif '[info]' in message_lower or '[info ]' in message_lower:
            return 'info'
        else:
            return 'info'  # Default to info
    
    def get_message_module(self, message):
        """Determine the module of a log message"""
        message_lower = message.lower()
        # Look for module tags like [ZCL], [DL], [IM], [ot], [SVR], [DIS], [SWU], [TST]
        if '[zcl]' in message_lower:
            return 'zcl'
        elif '[dl]' in message_lower:
            return 'dl'
        elif '[im]' in message_lower:
            return 'im'
        elif '[ot]' in message_lower:
            return 'ot'
        elif '[svr]' in message_lower:
            return 'svr'
        elif '[dis]' in message_lower:
            return 'dis'
        elif '[swu]' in message_lower:
            return 'swu'
        elif '[tst]' in message_lower:
            return 'tst'
        else:
            return None  # No specific module
    
    def append_log_message(self, message):
        """Append message to log terminal with color formatting"""
        # Determine category and module, then store the message
        category = self.get_message_category(message)
        module = self.get_message_module(message)
        self.all_log_messages.append((message, category, module))
        
        # Only display if both category and module filters are enabled
        category_enabled = self.log_filters.get(category, True)
        module_enabled = self.module_filters.get(module, True) if module else True
        
        if category_enabled and module_enabled:
            self.display_log_message(message, category)
    
    def display_log_message(self, message, category):
        """Display a single log message in the terminal"""
        cursor = self.log_terminal.textCursor()
        cursor.movePosition(QTextCursor.End)
        
        # Determine color based on log category
        fmt = QTextCharFormat()
        if category == 'error':
            fmt.setForeground(QColor(255, 100, 100))  # Red
        elif category == 'warn':
            fmt.setForeground(QColor(255, 170, 0))    # Orange
        elif category == 'silabs':
            fmt.setForeground(QColor(100, 150, 255))  # Blue
        elif category == 'detail':
            fmt.setForeground(QColor(170, 170, 170))  # Gray
        else:  # info or default
            fmt.setForeground(QColor(212, 212, 212))  # White
        
        cursor.setCharFormat(fmt)
        cursor.insertText(message)
        
        # Auto-scroll to bottom
        self.log_terminal.setTextCursor(cursor)
        self.log_terminal.ensureCursorVisible()
    
    def append_interactive_message(self, message):
        """Append message to interactive terminal"""
        cursor = self.interactive_terminal.textCursor()
        cursor.movePosition(QTextCursor.End)
        cursor.insertText(message)
        
        # Auto-scroll to bottom
        self.interactive_terminal.setTextCursor(cursor)
        self.interactive_terminal.ensureCursorVisible()
    
    def show_error(self, error):
        """Show error in status bar"""
        self.statusBar().showMessage(f"Error: {error}", 5000)
    
    def update_log_font(self):
        """Update log terminal font size"""
        font = QFont('Courier New', self.font_size)
        self.log_terminal.setFont(font)
    
    def update_interactive_font(self):
        """Update interactive terminal font size"""
        font = QFont('Courier New', self.font_size)
        self.interactive_terminal.setFont(font)
    
    def update_input_font(self):
        """Update command input font size"""
        font = QFont('Courier New', self.font_size)
        self.command_input.setFont(font)
    
    def increase_font_size(self):
        """Increase font size for all terminals"""
        if self.font_size < 24:
            self.font_size += 1
            self.update_log_font()
            self.update_interactive_font()
            self.update_input_font()
            self.statusBar().showMessage(f"Font size: {self.font_size}pt", 2000)
    
    def decrease_font_size(self):
        """Decrease font size for all terminals"""
        if self.font_size > 6:
            self.font_size -= 1
            self.update_log_font()
            self.update_interactive_font()
            self.update_input_font()
            self.statusBar().showMessage(f"Font size: {self.font_size}pt", 2000)
    
    def toggle_filter(self, category):
        """Toggle log filter for a specific category"""
        self.log_filters[category] = not self.log_filters[category]
        self.refresh_log_display()
        
        # Update status bar
        active_filters = [cat for cat, enabled in self.log_filters.items() if enabled]
        if active_filters:
            self.statusBar().showMessage(f"Active filters: {', '.join(active_filters)}", 2000)
        else:
            self.statusBar().showMessage("All filters disabled - no logs shown", 2000)
    
    def refresh_log_display(self):
        """Refresh the log display based on current filters"""
        # Clear the log terminal
        self.log_terminal.clear()
        
        # Redisplay all messages that pass the current filters
        for message, category, module in self.all_log_messages:
            category_enabled = self.log_filters.get(category, True)
            module_enabled = self.module_filters.get(module, True) if module else True
            
            if category_enabled and module_enabled:
                self.display_log_message(message, category)
    
    def show_module_filter_dialog(self):
        """Show a dialog with module filter options"""
        dialog = ModuleFilterDialog(self.module_filters, self)
        if dialog.exec_() == QDialog.Accepted:
            # Update module filters and refresh display
            self.module_filters = dialog.get_filters()
            self.refresh_log_display()
            
            # Update status bar
            active_modules = [mod for mod, enabled in self.module_filters.items() if enabled]
            if len(active_modules) == len(self.module_filters):
                self.statusBar().showMessage("All module filters enabled", 2000)
            else:
                self.statusBar().showMessage(f"Active module filters: {', '.join(active_modules)}", 2000)
    
    def save_logs(self):
        """Save logs and interactive terminal content to timestamped files"""
        try:
            # Generate timestamp
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            
            # Save log terminal content
            log_filename = f"silabs_logs_{timestamp}.txt"
            with open(log_filename, 'w', encoding='utf-8') as f:
                for message, category, module in self.all_log_messages:
                    f.write(message)
            
            # Save interactive terminal content
            interactive_filename = f"silabs_interactive_{timestamp}.txt"
            interactive_content = self.interactive_terminal.toPlainText()
            with open(interactive_filename, 'w', encoding='utf-8') as f:
                f.write(interactive_content)
            
            self.statusBar().showMessage(
                f"Saved: {log_filename} and {interactive_filename}", 5000
            )
        except Exception as e:
            self.signals.error_message.emit(f"Error saving logs: {e}")
    
    def load_logs(self):
        """Load a saved log file and enter replay mode"""
        try:
            # Open file dialog
            filename, _ = QFileDialog.getOpenFileName(
                self,
                "Load Log File",
                "",
                "Text Files (*.txt);;All Files (*)"
            )
            
            if not filename:
                return  # User cancelled
            
            # Close serial connection if open
            if self.connected:
                self.disconnect_serial()
            
            # Enter replay mode
            self.replay_mode = True
            
            # Clear current logs and load file
            self.all_log_messages = []
            self.log_terminal.clear()
            
            with open(filename, 'r', encoding='utf-8') as f:
                content = f.read()
                # Split by lines and process each as a log message
                for line in content.split('\n'):
                    if line.strip():  # Skip empty lines
                        category = self.get_message_category(line)
                        module = self.get_message_module(line)
                        self.all_log_messages.append((line + '\n', category, module))
            
            # Refresh display with filters
            self.refresh_log_display()
            
            # Update UI for replay mode
            self.command_input.setEnabled(False)
            self.command_input.setPlaceholderText("[Replay Mode - Input Disabled]")
            self.save_btn.setEnabled(False)
            self.load_btn.setEnabled(False)
            self.connect_btn.setEnabled(False)
            self.close_replay_btn.show()
            
            self.statusBar().showMessage(
                f"Loaded: {filename} (Replay Mode - Read Only)", 5000
            )
        except Exception as e:
            self.signals.error_message.emit(f"Error loading logs: {e}")
    
    def close_replay_mode(self):
        """Exit replay mode and return to live mode"""
        try:
            # Clear logs
            self.all_log_messages = []
            self.log_terminal.clear()
            self.interactive_terminal.clear()
            
            # Exit replay mode
            self.replay_mode = False
            
            # Update UI for live mode
            self.save_btn.setEnabled(True)
            self.load_btn.setEnabled(True)
            self.close_replay_btn.hide()
            
            # Update connection UI
            self.connect_btn.setEnabled(True)
            self.command_input.setEnabled(False)
            self.command_input.setPlaceholderText("[Not Connected - Click Connect to start]")
            
            self.statusBar().showMessage("Exited replay mode - Click Connect to resume live monitoring", 3000)
        except Exception as e:
            self.signals.error_message.emit(f"Error closing replay mode: {e}")
    
    def toggle_connection(self):
        """Toggle serial connection on/off"""
        if self.connected:
            self.disconnect_serial()
        else:
            self.connect_serial()
    
    def connect_serial(self):
        """Connect to the serial port and start monitoring"""
        if self.replay_mode:
            self.statusBar().showMessage("Cannot connect while in replay mode", 3000)
            return
        
        if self.open_serial():
            self.connected = True
            self.running = True
            
            # Start serial reading thread
            self.serial_thread = threading.Thread(target=self.read_serial_thread, daemon=True)
            self.serial_thread.start()
            
            # Update UI
            self.command_input.setEnabled(True)
            self.command_input.setPlaceholderText("Type command and press Enter...")
            self.connect_btn.setText("Disconnect")
            self.connect_btn.setStyleSheet("""
                QPushButton {
                    background-color: #f44336;
                    color: white;
                    border: 1px solid #d32f2f;
                    font-size: 9pt;
                    font-weight: bold;
                }
                QPushButton:hover {
                    background-color: #d32f2f;
                }
            """)
            
            self.statusBar().showMessage(f"Connected to {self.port} at {self.baudrate} baud", 3000)
        else:
            self.statusBar().showMessage("Failed to connect to serial port", 5000)
    
    def disconnect_serial(self):
        """Disconnect from the serial port"""
        self.running = False
        time.sleep(0.2)  # Give thread time to stop
        self.close_serial()
        self.connected = False
        
        # Update UI
        self.command_input.setEnabled(False)
        self.command_input.setPlaceholderText("[Not Connected - Click Connect to start]")
        self.command_input.clear()
        self.connect_btn.setText("Connect")
        self.connect_btn.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                border: 1px solid #45a049;
                font-size: 9pt;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #45a049;
            }
        """)
        
        self.statusBar().showMessage("Disconnected from serial port", 3000)
    
    def open_serial(self):
        """Open the serial port connection with flow control enabled"""
        try:
            self.serial_conn = serial.Serial(
                self.port,
                self.baudrate,
                timeout=0.1,
                rtscts=True,
                dsrdtr=True
            )
            self.statusBar().showMessage(f"Successfully opened {self.port} at {self.baudrate} baud with flow control")
            return True
        except serial.SerialException as e:
            self.signals.error_message.emit(f"Error opening serial port: {e}")
            return False
    
    def close_serial(self):
        """Close the serial port connection"""
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
            self.statusBar().showMessage("Serial port closed")
    
    def read_serial_thread(self):
        """Background thread to read from serial port and parse messages"""
        buffer = bytearray()
        frame_buffer = bytearray()
        cmd_buffer = bytearray()
        in_frame = False
        first_terminator_received = False
        
        while self.running:
            try:
                if self.serial_conn and self.serial_conn.is_open and self.serial_conn.in_waiting > 0:
                    data = self.serial_conn.read(self.serial_conn.in_waiting)
                    buffer.extend(data)
                    # Process buffer byte by byte
                    i = 0
                    while i < len(buffer):
                        byte = buffer[i]
                        
                        if byte == self.START_OF_FRAME and not in_frame:
                            # Start of a new frame
                            in_frame = True
                            frame_buffer = bytearray()
                            i += 1
                            
                        elif byte == self.END_OF_FRAME and not in_frame:
                            # EOF without SOF - framing error
                            self.signals.interactive_message.emit("Framing error occurred")
                            i += 1
                            
                        elif in_frame:
                            # Check for end of frame
                            if byte == self.END_OF_FRAME:
                                # Look ahead for terminator
                                self.signals.log_message.emit(frame_buffer.decode('ascii', errors='replace'))
                                in_frame = False
                                frame_buffer = bytearray()
                            else:
                                frame_buffer.append(byte)
                            i += 1
                            

                        elif not in_frame:
                            cmd_buffer.append(byte)
                            i += 1
                            if byte == self.MESSAGE_TERMINATOR[0]:
                                first_terminator_received = True
                            elif first_terminator_received and byte == self.MESSAGE_TERMINATOR[1]:
                                self.signals.interactive_message.emit(cmd_buffer.decode('ascii', errors='replace'))
                                cmd_buffer = bytearray()
                                first_terminator_received = False
                        else:
                            i += 1
                    
            except Exception as e:
                if self.running:
                    self.signals.error_message.emit(f"Error reading serial: {e}")
    
    def send_command(self, command):
        """Send a command to the serial port"""
        if self.serial_conn and self.serial_conn.is_open:
            try:
                message = command.encode('ascii') + self.MESSAGE_TERMINATOR
                self.serial_conn.write(message)
                self.signals.interactive_message.emit(f"> {command}")
            except Exception as e:
                self.signals.error_message.emit(f"Error sending command: {e}")
    
    def send_command_from_input(self):
        """Send command from input field"""
        command = self.command_input.text().strip()
        if command:
            self.send_command(command)
            self.command_input.clear()

    
    def closeEvent(self, event):
        """Handle window close event"""
        if self.connected:
            self.running = False
            
            # Wait for serial thread to finish
            if self.serial_thread and self.serial_thread.is_alive():
                self.serial_thread.join(timeout=1.0)
            
            # Close serial connection
            self.close_serial()
        
        event.accept()
    
    def run(self):
        """Start the console application"""
        # Show the window without auto-connecting
        # User can click Connect button to start monitoring
        self.show()
        return True

class ModuleFilterDialog(QDialog):
    """Dialog for advanced module filtering options"""
    
    def __init__(self, current_filters, parent=None):
        super().__init__(parent)
        self.current_filters = current_filters.copy()
        self.init_ui()
    
    def init_ui(self):
        """Initialize the dialog UI"""
        self.setWindowTitle("Module Filters")
        self.setModal(True)
        self.setMinimumWidth(300)
        
        layout = QVBoxLayout(self)
        
        # Title
        title = QLabel("Select modules to display:")
        title.setStyleSheet("font-weight: bold; font-size: 11pt; margin-bottom: 10px;")
        layout.addWidget(title)
        
        # Module filter checkboxes
        self.checkboxes = {}
        
        # Module definitions: (module_key, display_name)
        modules = [
            ('zcl', 'Data Model'),
            ('dl', 'Device Layer'),
            ('im', 'Interaction Model'),
            ('ot', 'Open Thread'),
            ('svr', 'App Server'),
            ('dis', 'Discovery'),
            ('swu', 'Software Updates'),
            ('tst', 'Test')
        ]
        
        for module_key, display_name in modules:
            checkbox = QCheckBox(display_name)
            checkbox.setChecked(self.current_filters.get(module_key, True))
            checkbox.setStyleSheet("QCheckBox { padding: 5px; font-size: 10pt; }")
            self.checkboxes[module_key] = checkbox
            layout.addWidget(checkbox)
        
        # Buttons
        button_layout = QHBoxLayout()
        
        select_all_btn = QPushButton("Select All")
        select_all_btn.clicked.connect(self.select_all)
        select_all_btn.setStyleSheet("""
            QPushButton {
                padding: 5px 15px;
                background-color: #3c3c3c;
                color: white;
                border: 1px solid #555;
            }
            QPushButton:hover {
                background-color: #4c4c4c;
            }
        """)
        button_layout.addWidget(select_all_btn)
        
        deselect_all_btn = QPushButton("Deselect All")
        deselect_all_btn.clicked.connect(self.deselect_all)
        deselect_all_btn.setStyleSheet("""
            QPushButton {
                padding: 5px 15px;
                background-color: #3c3c3c;
                color: white;
                border: 1px solid #555;
            }
            QPushButton:hover {
                background-color: #4c4c4c;
            }
        """)
        button_layout.addWidget(deselect_all_btn)
        
        layout.addLayout(button_layout)
        
        # OK and Cancel buttons
        ok_cancel_layout = QHBoxLayout()
        ok_cancel_layout.addStretch()
        
        cancel_btn = QPushButton("Cancel")
        cancel_btn.clicked.connect(self.reject)
        cancel_btn.setStyleSheet("""
            QPushButton {
                padding: 5px 20px;
                background-color: #555;
                color: white;
                border: 1px solid #666;
            }
            QPushButton:hover {
                background-color: #666;
            }
        """)
        ok_cancel_layout.addWidget(cancel_btn)
        
        ok_btn = QPushButton("Apply")
        ok_btn.clicked.connect(self.accept)
        ok_btn.setDefault(True)
        ok_btn.setStyleSheet("""
            QPushButton {
                padding: 5px 20px;
                background-color: #0066cc;
                color: white;
                border: 1px solid #0066cc;
            }
            QPushButton:hover {
                background-color: #0077dd;
            }
        """)
        ok_cancel_layout.addWidget(ok_btn)
        
        layout.addLayout(ok_cancel_layout)
        
        # Dark theme styling
        self.setStyleSheet("""
            QDialog {
                background-color: #2b2b2b;
                color: white;
            }
            QLabel {
                color: white;
            }
            QCheckBox {
                color: white;
            }
            QCheckBox::indicator {
                width: 18px;
                height: 18px;
                border: 1px solid #555;
                background-color: #1e1e1e;
            }
            QCheckBox::indicator:checked {
                background-color: #0066cc;
                border: 1px solid #0066cc;
            }
            QCheckBox::indicator:hover {
                border: 1px solid #0077dd;
            }
        """)
    
    def select_all(self):
        """Select all module filters"""
        for checkbox in self.checkboxes.values():
            checkbox.setChecked(True)
    
    def deselect_all(self):
        """Deselect all module filters"""
        for checkbox in self.checkboxes.values():
            checkbox.setChecked(False)
    
    def get_filters(self):
        """Get the current filter state"""
        return {key: checkbox.isChecked() for key, checkbox in self.checkboxes.items()}

def main():
    parser = argparse.ArgumentParser(
        description="Silicon Labs Matter Console - Dual terminal interface for serial communication",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
This script provides a Qt-based dual terminal interface for monitoring and interacting
with a Silicon Labs device over a serial connection.

Features:
  - Dedicated log terminal for framed messages (SOF 0x01 ... EOF 0x04)
  - Interactive terminal for unframed messages
  - Color-coded log levels: [error] (red), [silabs] (blue), [info]/[detail] (white)
  - Auto-scrolling terminals with smooth rendering
  - Command input with history
  - Hardware flow control (RTS/CTS) enabled
  - Resizable window

Message Format:
  Framed logs: SOF(0x01) + message + EOF(0x04) + \\r\\n
  Unframed data goes directly to interactive terminal

Requirements:
  - PyQt5: pip install PyQt5
  - pyserial: pip install pyserial
        """
    )
    
    parser.add_argument(
        "port",
        help="Serial port to connect to (e.g., /dev/ttyACM0 or COM3)"
    )
    
    parser.add_argument(
        "-b", "--baudrate",
        type=int,
        default=115200,
        help="Baudrate for serial communication (default: 115200)"
    )
    
    args = parser.parse_args()
    
    app = QApplication(sys.argv)
    
    # Set dark theme
    app.setStyle('Fusion')
    
    console = SilabsConsole(args.port, args.baudrate)
    
    if console.run():
        sys.exit(app.exec_())
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
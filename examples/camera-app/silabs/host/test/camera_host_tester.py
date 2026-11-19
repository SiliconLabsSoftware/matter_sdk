#!/usr/bin/env python3
#
#    Copyright (c) 2026 Project CHIP Authors
#    All rights reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#

"""camera_host tester: replaces the camera SoC to test camera_host on its own.

Connects to the MMIC link of camera_host exposed over TCP by socat (see README.md), sends the camera
commands the SoC forwards and displays the WebRTC stream.
"""

import argparse
import asyncio
import sys
import time
from typing import Optional

import qasync
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QColor, QFont, QImage, QPixmap
from PyQt5.QtWidgets import (QApplication, QCheckBox, QHBoxLayout, QLabel, QLineEdit, QListWidget, QListWidgetItem,
                             QMainWindow, QPlainTextEdit, QPushButton, QSpinBox, QSplitter, QVBoxLayout, QWidget)

import camera_protocol as proto
from camera_tester import MEDIA_TIMEOUT, CameraTester
from matter_tlv import TlvError, decode
from requestor import RequestorSession
from test_suite import AUDIO_STREAM, SNAPSHOT_STREAM, VIDEO_STREAM, TestResult, TestSuite

PASSED = "\u2705"
FAILED = "\u274C"


class MainWindow(QMainWindow):
    def __init__(self, host: str, port: int):
        super().__init__()
        self.setWindowTitle("camera_host tester")
        self.resize(1280, 820)

        self._tester = CameraTester(self._log, self._show_video_frame)
        self._live_session: Optional[RequestorSession] = None
        self._video_allocated = False
        self._audio_allocated = False
        self._snapshot_allocated = False
        self._busy = False

        # Connection bar
        self._host = QLineEdit(host)
        self._port = QSpinBox()
        self._port.setRange(1, 65535)
        self._port.setValue(port)
        self._connect_button = QPushButton("Connect")
        self._connect_button.clicked.connect(self._on_connect)
        self._microphone = QCheckBox("Camera has a microphone")
        self._connection_status = QLabel("Disconnected")
        bar = QHBoxLayout()
        bar.addWidget(QLabel("camera_host:"))
        bar.addWidget(self._host)
        bar.addWidget(QLabel("port:"))
        bar.addWidget(self._port)
        bar.addWidget(self._connect_button)
        bar.addWidget(self._microphone)
        bar.addStretch()
        bar.addWidget(self._connection_status)

        # Left: test suite
        self._suite_button = QPushButton("Run Test Suite")
        self._suite_button.clicked.connect(self._on_run_suite)
        self._results = QListWidget()
        self._results.setFont(QFont("monospace"))
        self._summary = QLabel("")
        self._summary.setFont(QFont("", 11, QFont.Bold))
        left = QVBoxLayout()
        left.addWidget(self._suite_button)
        left.addWidget(self._results, 1)
        left.addWidget(self._summary)
        left_widget = QWidget()
        left_widget.setLayout(left)

        # Right: stream and snapshot
        self._video = QLabel("No video")
        self._video.setAlignment(Qt.AlignCenter)
        self._video.setMinimumSize(640, 360)
        self._video.setStyleSheet("background-color: black; color: gray;")
        self._start_stream_button = QPushButton("Start Stream")
        self._start_stream_button.clicked.connect(self._on_start_stream)
        self._stop_stream_button = QPushButton("Stop Stream")
        self._stop_stream_button.clicked.connect(self._on_stop_stream)
        self._snapshot_button = QPushButton("Take Snapshot")
        self._snapshot_button.clicked.connect(self._on_snapshot)
        self._stream_status = QLabel("")
        buttons = QHBoxLayout()
        buttons.addWidget(self._start_stream_button)
        buttons.addWidget(self._stop_stream_button)
        buttons.addWidget(self._snapshot_button)
        buttons.addStretch()
        self._snapshot = QLabel("No snapshot")
        self._snapshot.setAlignment(Qt.AlignCenter)
        self._snapshot.setMinimumSize(320, 180)
        self._snapshot.setStyleSheet("background-color: #202020; color: gray;")
        right = QVBoxLayout()
        right.addWidget(QLabel("Stream"))
        right.addWidget(self._video, 3)
        right.addLayout(buttons)
        right.addWidget(self._stream_status)
        right.addWidget(QLabel("Snapshot"))
        right.addWidget(self._snapshot, 1)
        right_widget = QWidget()
        right_widget.setLayout(right)

        splitter = QSplitter(Qt.Horizontal)
        splitter.addWidget(left_widget)
        splitter.addWidget(right_widget)
        splitter.setSizes([480, 800])

        # Bottom: log
        self._log_view = QPlainTextEdit()
        self._log_view.setReadOnly(True)
        self._log_view.setMaximumBlockCount(2000)
        self._log_view.setFont(QFont("monospace"))

        vertical = QSplitter(Qt.Vertical)
        vertical.addWidget(splitter)
        vertical.addWidget(self._log_view)
        vertical.setSizes([640, 180])

        layout = QVBoxLayout()
        layout.addLayout(bar)
        layout.addWidget(vertical, 1)
        central = QWidget()
        central.setLayout(layout)
        self.setCentralWidget(central)
        self._update_buttons()

        # Notices a link closed by camera_host.
        self._refresh_timer = QTimer(self)
        self._refresh_timer.timeout.connect(self._update_buttons)
        self._refresh_timer.start(1000)

    # Helpers

    def _log(self, message: str):
        self._log_view.appendPlainText(f"{time.strftime('%H:%M:%S')} {message}")

    def _show_video_frame(self, rgb):
        height, width, _ = rgb.shape
        image = QImage(rgb.data, width, height, 3 * width, QImage.Format_RGB888).copy()
        self._video.setPixmap(QPixmap.fromImage(image).scaled(self._video.size(), Qt.KeepAspectRatio,
                                                              Qt.SmoothTransformation))

    def _update_buttons(self):
        connected = self._tester.client.connected
        self._connect_button.setText("Disconnect" if connected else "Connect")
        self._connect_button.setEnabled(not self._busy)
        self._suite_button.setEnabled(connected and not self._busy and self._live_session is None)
        self._start_stream_button.setEnabled(connected and not self._busy and self._live_session is None)
        self._stop_stream_button.setEnabled(connected and not self._busy and self._live_session is not None)
        self._snapshot_button.setEnabled(connected and not self._busy)
        self._microphone.setEnabled(not self._busy and self._live_session is None)
        self._connection_status.setText("Connected" if connected else "Disconnected")

    def _set_busy(self, busy: bool):
        self._busy = busy
        self._update_buttons()

    def _clear_video(self):
        self._video.clear()
        self._video.setText("No video")

    # Connection

    @qasync.asyncSlot()
    async def _on_connect(self):
        self._set_busy(True)
        try:
            if self._tester.client.connected:
                await self._stop_live_view()
                await self._tester.client.close()
                self._video_allocated = self._audio_allocated = self._snapshot_allocated = False
                self._log("Disconnected")
            else:
                await self._tester.client.connect(self._host.text().strip(), self._port.value())
        except (OSError, asyncio.TimeoutError) as error:
            self._log(f"Connection failed: {error}")
        finally:
            self._set_busy(False)

    # Test suite

    @qasync.asyncSlot()
    async def _on_run_suite(self):
        self._set_busy(True)
        self._results.clear()
        self._summary.setText("Running...")
        self._clear_video()
        suite = TestSuite(self._tester, self._microphone.isChecked())

        def on_result(result: TestResult):
            item = QListWidgetItem(f"{result.name} {PASSED if result.passed else FAILED}")
            item.setToolTip(result.detail)
            if not result.passed:
                item.setText(f"{result.name} {FAILED}  ({result.detail})")
                item.setForeground(QColor("red"))
            self._results.addItem(item)
            self._results.scrollToBottom()

        try:
            passed, total = await suite.run(on_result)
            self._summary.setText(f"{passed} of {total} commands succeeded")
        except ConnectionError as error:
            self._summary.setText(f"Aborted: {error}")
        finally:
            # The suite releases its streams.
            self._video_allocated = self._audio_allocated = self._snapshot_allocated = False
            self._clear_video()
            self._set_busy(False)

    # Stream

    @qasync.asyncSlot()
    async def _on_start_stream(self):
        self._set_busy(True)
        self._stream_status.setText("Starting...")
        try:
            if not self._video_allocated:
                status, _ = await self._tester.command("VideoStreamAllocate", proto.AV_STREAM_MANAGEMENT,
                                                       proto.VIDEO_STREAM_ALLOCATE, proto.video_stream(VIDEO_STREAM))
                if status != proto.SUCCESS:
                    self._stream_status.setText(f"{FAILED} VideoStreamAllocate: {proto.status_name(status)}")
                    return
                self._video_allocated = True
            audio = []
            if self._microphone.isChecked() and not self._audio_allocated:
                status, _ = await self._tester.command("AudioStreamAllocate", proto.AV_STREAM_MANAGEMENT,
                                                       proto.AUDIO_STREAM_ALLOCATE, proto.audio_stream(AUDIO_STREAM))
                self._audio_allocated = status == proto.SUCCESS
            if self._audio_allocated:
                audio = [AUDIO_STREAM]

            session, error = await self._tester.start_live_view([VIDEO_STREAM], audio)
            if session is None:
                self._stream_status.setText(f"{FAILED} {error}")
                return
            self._live_session = session
            self._stream_status.setText("Waiting for video...")
            try:
                await asyncio.wait_for(session.first_video_frame.wait(), MEDIA_TIMEOUT)
                self._stream_status.setText(f"{PASSED} Streaming (session {session.session_id})")
            except asyncio.TimeoutError:
                self._stream_status.setText(f"{FAILED} No video received within {MEDIA_TIMEOUT:.0f} s")
        except ConnectionError as error:
            self._stream_status.setText(f"{FAILED} {error}")
        finally:
            self._set_busy(False)

    async def _stop_live_view(self):
        if self._live_session is not None:
            session, self._live_session = self._live_session, None
            if self._tester.client.connected:
                await self._tester.end_session(session)
            else:
                await self._tester.close_session(session)
        if self._tester.client.connected:
            if self._video_allocated:
                await self._tester.command("VideoStreamDeallocate", proto.AV_STREAM_MANAGEMENT,
                                           proto.VIDEO_STREAM_DEALLOCATE, proto.stream_request(VIDEO_STREAM))
            if self._audio_allocated:
                await self._tester.command("AudioStreamDeallocate", proto.AV_STREAM_MANAGEMENT,
                                           proto.AUDIO_STREAM_DEALLOCATE, proto.stream_request(AUDIO_STREAM))
        self._video_allocated = self._audio_allocated = False
        self._clear_video()

    @qasync.asyncSlot()
    async def _on_stop_stream(self):
        self._set_busy(True)
        try:
            await self._stop_live_view()
            self._stream_status.setText("Stopped")
        except ConnectionError as error:
            self._stream_status.setText(f"{FAILED} {error}")
        finally:
            self._set_busy(False)

    # Snapshot

    @qasync.asyncSlot()
    async def _on_snapshot(self):
        self._set_busy(True)
        self._snapshot.clear()
        self._snapshot.setText("Capturing...")
        try:
            if not self._snapshot_allocated:
                status, _ = await self._tester.command("SnapshotStreamAllocate", proto.AV_STREAM_MANAGEMENT,
                                                       proto.SNAPSHOT_STREAM_ALLOCATE, proto.snapshot_stream(SNAPSHOT_STREAM))
                if status != proto.SUCCESS:
                    self._snapshot.setText(f"{FAILED} SnapshotStreamAllocate: {proto.status_name(status)}")
                    return
                self._snapshot_allocated = True

            status, data = await self._tester.command("CaptureSnapshot", proto.AV_STREAM_MANAGEMENT,
                                                      proto.CAPTURE_SNAPSHOT, proto.capture_snapshot(None))
            if status != proto.SUCCESS:
                note = " (not implemented by camera_host yet)" if status == proto.UNSUPPORTED_COMMAND else ""
                self._snapshot.setText(f"{FAILED} CaptureSnapshot: {proto.status_name(status)}{note}")
                return
            self._show_snapshot(data)
        except ConnectionError as error:
            self._snapshot.setText(f"{FAILED} {error}")
        finally:
            self._set_busy(False)

    def _show_snapshot(self, data: bytes):
        """Displays a CaptureSnapshotResponse: { 0: image data, 1: image codec, 2: resolution }."""
        try:
            response = decode(data)[0]
        except (TlvError, IndexError):
            self._snapshot.setText(f"{FAILED} Invalid CaptureSnapshotResponse")
            return
        pixmap = QPixmap()
        if not pixmap.loadFromData(response.get(0, b"")):
            self._snapshot.setText(f"{FAILED} Cannot decode the image")
            return
        self._snapshot.setPixmap(pixmap.scaled(self._snapshot.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation))

    def closeEvent(self, event):
        asyncio.ensure_future(self._tester.client.close())
        super().closeEvent(event)


def main():
    parser = argparse.ArgumentParser(description="camera_host tester, plays the camera SoC role over TCP")
    parser.add_argument("--host", default="127.0.0.1", help="address of the machine running camera_host and socat")
    parser.add_argument("--port", type=int, default=5555, help="TCP port of the socat bridge")
    args = parser.parse_args()

    app = QApplication(sys.argv)
    loop = qasync.QEventLoop(app)
    asyncio.set_event_loop(loop)
    window = MainWindow(args.host, args.port)
    window.show()
    with loop:
        loop.run_forever()


if __name__ == "__main__":
    main()

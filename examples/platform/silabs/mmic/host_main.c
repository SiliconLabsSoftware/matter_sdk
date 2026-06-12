


/*
 * Minimal serial shell.
 *
 * Usage: host_main <serial_device>
 *   e.g. host_main /dev/ttyUSB0                (Linux)
 *        host_main /dev/cu.usbserial-XXXX      (macOS)
 *
 * Opens the port at 115200 8N1, spawns a thread that prints bytes received
 * from the port to stdout, and reads lines from stdin. The only built-in
 * command is "exit", which closes the port and returns 0.
 *
 * Build: cc -std=c11 -Wall -Wextra -pthread host_main.c -o host_main
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "mmic.h"

static int g_serial_fd = -1;
static atomic_int g_stop = 0;

/* Configure an open fd for 115200, 8 data bits, no parity, 1 stop bit, no flow control. */
static int configure_port(int fd)
{
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        return -1;
    }

    cfmakeraw(&tio);

    if (cfsetispeed(&tio, B115200) != 0 || cfsetospeed(&tio, B115200) != 0) {
        return -1;
    }

    /* Control flags: 8N1, no flow control, enable receiver, ignore modem lines. */
    tio.c_cflag &= ~(unsigned)(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tio.c_cflag |=  (unsigned)(CS8 | CLOCAL | CREAD);

    /* Input flags: fully raw. No break handling, no parity marking/stripping,
     * no CR/LF translation, no SW flow control. cfmakeraw() already does most
     * of this; we re-clear explicitly so any platform quirk cannot leave a
     * translation bit set that would corrupt binary data. */
    tio.c_iflag &= ~(unsigned)(IGNBRK | BRKINT | PARMRK | ISTRIP
                             | INLCR  | IGNCR  | ICRNL
                             | IXON   | IXOFF  | IXANY
                             | INPCK  | IGNPAR);

    /* Output flags: no post-processing (no ONLCR/OCRNL/etc.). */
    tio.c_oflag &= ~(unsigned)OPOST;

    /* Local flags: non-canonical, no echo, no signal generation. */
    tio.c_lflag &= ~(unsigned)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    /* Non-blocking-ish read: return after up to 100 ms so the reader thread
     * can observe g_stop and exit cleanly. */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return 0;
}

/* Write `len` bytes from `data` to the serial port. Returns 0 on success. */
int serial_write(const uint8_t *data, size_t len)
{
    if (g_serial_fd < 0 || data == NULL) {
        return -1;
    }

    size_t written = 0;
    while (written < len) {
        ssize_t n = write(g_serial_fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        written += (size_t)n;
    }
    return 0;
}

/* Reader thread: prints anything received on the port to stdout. */
static void *reader_thread(void *arg)
{
    (void)arg;
    uint8_t buf[256]={0};

    while (!atomic_load(&g_stop)) {
        ssize_t n = read(g_serial_fd, buf, sizeof(buf));
        if (n > 0) {
            uint8_t * str=NULL;
            
            decodeResponse(buf, n, &str);
            if (str != NULL)
            {
                printf("%s\r\n", str);
                fflush(stdout);
                free(str);
            }
        } else if (n < 0 && errno != EINTR && errno != EAGAIN) {
            fprintf(stderr, "\n[serial read error: %s]\n", strerror(errno));
            break;
        }
        /* n == 0: VTIME timeout, just loop */
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <serial_device>\n", argv[0]);
        return 1;
    }

    const char *device = argv[1];

    g_serial_fd = open(device, O_RDWR | O_NOCTTY);
    if (g_serial_fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", device, strerror(errno));
        return 1;
    }

    if (configure_port(g_serial_fd) != 0) {
        fprintf(stderr, "Failed to configure %s: %s\n", device, strerror(errno));
        close(g_serial_fd);
        return 1;
    }

    pthread_t reader;
    if (pthread_create(&reader, NULL, reader_thread, NULL) != 0) {
        fprintf(stderr, "Failed to start reader thread\n");
        close(g_serial_fd);
        return 1;
    }

    printf("Connected to %s @ 115200 8N1. Type 'exit' to quit.\n", device);

    char line[512];
    char found=0;
    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* EOF on stdin -> treat as exit */
            printf("\n");
            break;
        }

        /* strip trailing newline */
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
            line[--l] = '\0';
        }

        if (l == 0) {
            continue;
        }

        if (strcmp(line, "exit") == 0) {
            break;
        }

        if (strcmp(line, "help") == 0) {
            printHelp();
            continue;
        }

        for(mmic_command_id_e id = 0; id < INVALID_COMMAND_ID; id++)
        {
            if (strcmp(line, commandsString[id]) == 0) {
                found=1;
                uint8_t * cmdBuffer= NULL;
                size_t size = 0;
                if(encodeCommand(id, NULL, 0, &cmdBuffer, &size)==0)
                {
                    serial_write(cmdBuffer,size);
                }
                if(cmdBuffer != NULL)
                {
                    free(cmdBuffer);
                }
                break;
            }
        }
        if(found)
        {   
            found=0;
            continue;
        }

        fprintf(stderr, "Unknown command: %s\n", line);
    }

    atomic_store(&g_stop, 1);
    pthread_join(reader, NULL);

    close(g_serial_fd);
    g_serial_fd = -1;

    return 0;
}



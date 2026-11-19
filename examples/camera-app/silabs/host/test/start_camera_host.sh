#!/usr/bin/env bash
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

# Runs camera_host on a virtual UART exposed over TCP by socat, for camera_host_tester.py.
# camera_host and socat are restarted after each tester session (socat serves one connection).
#
# Usage: start_camera_host.sh <streaming network interface> <camera device> [tcp port] [camera_host binary]
# Set USE_LIBCAMERIFY=0 to run without libcamerify (USB camera).

set -u

if [ $# -lt 2 ]; then
    echo "Usage: $0 <streaming network interface> <camera device> [tcp port] [camera_host binary]" >&2
    exit 1
fi

INTERFACE=$1
CAMERA=$2
PORT=${3:-5555}
BINARY=${4:-$(cd "$(dirname "$0")" && pwd)/../build/camera_host}
PTY=/tmp/ttyMMIC

if ! command -v socat >/dev/null; then
    echo "socat not found: sudo apt install socat" >&2
    exit 1
fi
if [ ! -x "$BINARY" ]; then
    echo "camera_host not found at $BINARY, build it first (see ../README.md)" >&2
    exit 1
fi

LAUNCHER=()
if [ "${USE_LIBCAMERIFY:-1}" = "1" ] && command -v libcamerify >/dev/null; then
    LAUNCHER=(libcamerify)
fi

SOCAT_PID=
HOST_PID=
stop() {
    [ -n "$HOST_PID" ] && kill "$HOST_PID" 2>/dev/null
    [ -n "$SOCAT_PID" ] && kill "$SOCAT_PID" 2>/dev/null
    wait 2>/dev/null
    SOCAT_PID=
    HOST_PID=
}
trap 'stop; exit 0' INT TERM

while true; do
    rm -f "$PTY"
    socat pty,link="$PTY",rawer tcp-listen:"$PORT",reuseaddr &
    SOCAT_PID=$!

    for _ in $(seq 50); do
        [ -e "$PTY" ] && break
        sleep 0.1
    done
    if [ ! -e "$PTY" ]; then
        echo "socat did not create $PTY" >&2
        stop
        exit 1
    fi

    echo "camera_host on $PTY, waiting for camera_host_tester.py on TCP port $PORT"
    "${LAUNCHER[@]}" "$BINARY" "$PTY" "$INTERFACE" "$CAMERA" &
    HOST_PID=$!

    # The tester disconnected (socat exited) or camera_host stopped: restart both.
    wait -n "$SOCAT_PID" "$HOST_PID"
    stop
    echo "Session ended, restarting"
    sleep 1
done

#!/bin/sh
set -eu
cd "$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
export QT_QPA_PLATFORM=offscreen
export QT_FORCE_STDERR_LOGGING=1
./build/mock-control &
mock_pid=$!
trap 'kill "$mock_pid" 2>/dev/null || :; wait "$mock_pid" 2>/dev/null || :' 0
qml6 tests/test-widget.qml

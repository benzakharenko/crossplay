#!/bin/sh
# Builds and runs the Tasks app core's tests. No device and no network:
# TasksCore is freestanding C++17 plus ArduinoJson (header-only).
#
#   host-tests/tasks/run.sh
#
# ArduinoJson comes from PlatformIO's libdeps after `pio pkg install`, or from
# ARDUINOJSON_DIR (a checkout's src/ folder) when set.
set -e
cd "$(dirname "$0")"

if [ -z "$ARDUINOJSON_DIR" ]; then
  for d in ../../.pio/libdeps/*/ArduinoJson/src; do
    [ -f "$d/ArduinoJson.h" ] && ARDUINOJSON_DIR="$d" && break
  done
fi
if [ -z "$ARDUINOJSON_DIR" ] || [ ! -f "$ARDUINOJSON_DIR/ArduinoJson.h" ]; then
  echo "tasks: ArduinoJson not found; run 'pio pkg install -e gh_release_sticky' or set ARDUINOJSON_DIR" >&2
  exit 1
fi

BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-tasks-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -isystem "$ARDUINOJSON_DIR" \
  ../../src/apps_local/tasks/TasksCore.cpp \
  test_tasks.cpp -o "$BUILD_DIR/test_tasks"
"$BUILD_DIR/test_tasks"

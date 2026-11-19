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
  -isystem "$ARDUINOJSON_DIR" -I../../lib/Utf8 \
  ../../lib/Utf8/Utf8.cpp \
  ../../src/apps_local/tasks/TasksCore.cpp \
  ../../src/apps_local/tasks/TasksState.cpp \
  ../../src/apps_local/tasks/TasksText.cpp \
  test_tasks.cpp -o "$BUILD_DIR/test_tasks"
"$BUILD_DIR/test_tasks"

# The sync orchestration, in its simulator build, against canned replies.
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -Istub -isystem "$ARDUINOJSON_DIR" \
  ../../src/apps_local/tasks/TasksCore.cpp \
  ../../src/apps_local/tasks/TasksState.cpp \
  ../../src/apps_local/tasks/TasksSync.cpp \
  test_sync.cpp -o "$BUILD_DIR/test_sync"
"$BUILD_DIR/test_sync" "$(pwd)/fake" > "$BUILD_DIR/test_sync.log" || { cat "$BUILD_DIR/test_sync.log"; exit 1; }
tail -1 "$BUILD_DIR/test_sync.log"

# The screens.
SDK=../../freeink-sdk/libs/ui/FreeInkUI
ICONS=../../freeink-sdk/libs/assets/Icons
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-comment \
  -I"$SDK/include" -I"$ICONS/include" \
  "$SDK/src/FreeInkUI.cpp" \
  ../../src/apps_local/tasks/TasksScreens.cpp \
  test_screens.cpp -o "$BUILD_DIR/test_screens"
"$BUILD_DIR/test_screens"

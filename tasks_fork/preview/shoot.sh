#!/bin/bash
# Photographs the Tasks app in the simulator with canned Notion replies.
#
#   tasks_fork/preview/shoot.sh <simulator-binary> <out-dir> <tag>
#
# Reads the taps and shot times from tasks_fork/preview/script.txt:
#   line 1  input script   ('<ms>:<action>;...', see scripts_local/sim-shot.sh)
#   line 2  shot script    ('<ms>:<name>;...', names become <out>/<tag>-<name>.png)
set -euo pipefail
BIN="$1"
OUT="$(cd "$2" && pwd)"
TAG="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

SD="$(mktemp -d)"
mkdir -p "$SD/.crosspoint" "$SD/books"
# A pretend token: the canned replies never check it.
cat > "$SD/tasks.json" <<JSON
{"token":"ntn_PREVIEW","database":"https://www.notion.so/me/Task-List-1a2b3c4d5e6f40718293a4b5c6d7e8f9?v=1"}
JSON

INPUT="$(sed -n 1p "$HERE/script.txt")"
SHOTS=""
IFS=';' read -ra entries <<< "$(sed -n 2p "$HERE/script.txt")"
for e in "${entries[@]}"; do
  [ -z "$e" ] && continue
  SHOTS+="${e%%:*}:$OUT/$TAG-${e#*:}.bmp;"
done

cd "$REPO"
CROSSPOINT_SIM_SD="$SD" \
TASKS_FAKE_DIR="$REPO/host-tests/tasks/fake" \
TASKS_FAKE_TODAY="2026-11-19" \
CROSSPOINT_SIM_SCREEN_TEXT=1 \
CROSSPOINT_SIM_INPUT_SCRIPT="$INPUT" \
CROSSPOINT_SIM_SCREENSHOTS="${SHOTS%;}" \
  timeout 120 xvfb-run -a "$BIN" > "$OUT/$TAG-sim.log" 2>&1 || true

python3 - "$OUT" <<'PY'
import glob, os, sys
from PIL import Image
for bmp in glob.glob(os.path.join(sys.argv[1], "*.bmp")):
    Image.open(bmp).convert("L").save(bmp[:-4] + ".png")
    os.remove(bmp)
PY
grep -E "SCREEN:|\[ERR\]|TASKS|Entering activity" "$OUT/$TAG-sim.log" | head -80 > "$OUT/$TAG-trace.txt" || true

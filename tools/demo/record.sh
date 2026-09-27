#!/bin/bash
# record.sh NAME HOME COLS ROWS SCENE [dchess args...]
# Runs dchess in a hidden tmux session, records it headless with asciinema
# and drives it from SCENE, a bash file using k (keys), t (text), w (wait)
# and cmd (a typed command).
set -u
NAME=$1 HOMEDIR=$2 C=$3 R=$4 SCENE=$5; shift 5
OUT=${OUT_DIR:-.}/$NAME.cast
APP=${DCHESS:-$PWD/dchess}
T='=dchess-rec:'
k()   { tmux send-keys -t "$T" "$@"; }
t()   { tmux send-keys -t "$T" -l "$1"; }
w()   { sleep "$1"; }
cmd() { k i; w 0.25; t "$1"; w 0.35; k Enter; }
tmux kill-session -t '=dchess-rec' 2>/dev/null
tmux new-session -d -s dchess-rec -x "$C" -y "$R" \
  "env HOME=$HOMEDIR XDG_CONFIG_HOME=$HOMEDIR/.config XDG_DATA_HOME=$HOMEDIR/.local/share USER=${DEMO_USER:-saeed} LANG=C.UTF-8 TERM=tmux-256color $APP $*; sleep 60"
tmux set -t dchess-rec status off >/dev/null
env -u TMUX TERM=xterm-256color asciinema rec --headless --overwrite -q -f asciicast-v2 \
  --window-size "${C}x${R}" -i 1.2 -c "tmux attach -t =dchess-rec" "$OUT" &
REC=$!
sleep 1.2
source "$SCENE"
tmux kill-session -t '=dchess-rec' 2>/dev/null
wait $REC
# The tmux client's "[exited]" is not part of the demo.
python3 - "$OUT" <<'PY'
import json, sys
lines = open(sys.argv[1]).read().split('\n')
out = [lines[0]]
for l in lines[1:]:
    if not l.strip(): continue
    ev = json.loads(l)
    if ev[1] == 'o' and ('[exited]' in ev[2] or '[detached' in ev[2]): break
    out.append(l)
open(sys.argv[1], 'w').write('\n'.join(out) + '\n')
PY

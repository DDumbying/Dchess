#!/bin/bash
# Re-records the README's clips into assets/gifs. Needs tmux, asciinema 3
# and agg; run from the repository root after `make`.
set -eu
DIR=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-$(mktemp -d /tmp/dchess-demo.XXXXXX)}
export OUT_DIR=$WORK
python3 "$DIR/homes.py" "$WORK"
"$DIR/record.sh" welcome "$WORK/welcome" 100 30 "$DIR/welcome.scene"
"$DIR/record.sh" play    "$WORK/play"    100 30 "$DIR/play.scene" --no-menu
"$DIR/record.sh" replay  "$WORK/replay"  100 30 "$DIR/replay.scene" --replay "$DIR/legal.pgn"
"$DIR/record.sh" stats   "$WORK/stats"   100 30 "$DIR/stats.scene" --stats
for n in welcome play replay stats; do
    gif=$n; [ $n = play ] && gif=demo
    agg -q --font-family "JetBrains Mono,Noto Sans Symbols 2" --font-size 15 --theme monokai \
        --idle-time-limit 1.5 --last-frame-duration 2.5 "$WORK/$n.cast" "assets/gifs/$gif.gif"
    cp "$WORK/$n.cast" "assets/gifs/cast/$n.cast"
done
echo "clips written to assets/gifs (work files in $WORK)"

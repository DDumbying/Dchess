#!/bin/sh
# The man page names every option --help prints, and renders cleanly.
fail=0
echo "== the man page =="
[ -f docs/dchess.6 ] || { echo "  docs/dchess.6 is missing                                   FAIL"; exit 1; }
for opt in $(./dchess --help | grep -o -- '--[a-z][a-z-]*' | sort -u); do
    esc=$(printf '%s' "$opt" | sed 's/-/\\\\-/g')
    grep -q -- "$esc" docs/dchess.6 || { printf '  %-58s FAIL\n' "$opt is documented"; fail=1; }
done
[ $fail = 0 ] && printf '  %-58s OK\n' "every --help option is documented"
if command -v groff >/dev/null 2>&1; then
    warn=$(groff -man -Tutf8 -ww docs/dchess.6 2>&1 >/dev/null)
    if [ -z "$warn" ]; then printf '  %-58s OK\n' "it renders without warnings"
    else printf '  %-58s FAIL\n%s\n' "it renders without warnings" "$warn"; fail=1; fi
else
    echo "  (no groff: render check skipped)"
fi
if [ $fail = 0 ]; then echo; echo "All man page tests passed."; else echo; echo "MAN PAGE TESTS FAILED."; exit 1; fi

#!/bin/sh
# make install / uninstall into a scratch DESTDIR.
fail=0
check() { if [ "$2" = 0 ]; then printf '  %-58s OK\n' "$1"; else printf '  %-58s FAIL\n' "$1"; fail=1; fi; }
D=$(mktemp -d) && [ -d "$D" ] || { echo "  mktemp -d failed: not installing anywhere"; exit 1; }
V=$(sed -n 's/.*DCHESS_VERSION "\(.*\)".*/\1/p' headers/utils/version.h)
echo "== install =="
make -s install DESTDIR="$D" PREFIX=/usr >/dev/null 2>&1; check "make install succeeds" $?
[ -x "$D/usr/bin/dchess" ]; check "the binary is installed" $?
grep -qF "$V" "$D/usr/share/man/man6/dchess.6" 2>/dev/null && ! grep -q '@VERSION@' "$D/usr/share/man/man6/dchess.6"
check "the man page carries the version" $?
docs=0
for f in doc/dchess/README.md doc/dchess/guide.md doc/dchess/CHANGELOG.md licenses/dchess/LICENSE; do
    [ -f "$D/usr/share/$f" ] || docs=1
done
check "the docs and the license are installed" $docs
[ "$("$D/usr/bin/dchess" --version 2>/dev/null)" = "dchess $V" ]; check "the installed binary says its version" $?
echo "== uninstall =="
make -s uninstall DESTDIR="$D" PREFIX=/usr >/dev/null 2>&1; check "make uninstall succeeds" $?
[ -z "$(find "$D" -type f)" ]; check "and leaves no files" $?
make -s uninstall DESTDIR="$D" PREFIX=/usr >/dev/null 2>&1; check "uninstalling twice is fine" $?
rm -rf "$D"
if [ $fail = 0 ]; then echo; echo "All install tests passed."; else echo; echo "INSTALL TESTS FAILED."; exit 1; fi

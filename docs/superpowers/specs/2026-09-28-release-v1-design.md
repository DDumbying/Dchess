# Release v1.0.0

**Status:** approved design, 2026-09-28
**Branch:** `release/v1.0.0`

## Goal

A tagged v1.0.0 that installs like any Unix program, with a man page, a
changelog, CI on every pull request, and an AUR package ready to publish.

## Version

- **Where it lives:** `headers/utils/version.h` defines `DCHESS_VERSION
  "1.0.0"`, the only place it appears. It moves out of `stats.h`, whose
  file format has its own magic.
- **Where it is shown:** `--version`, the UCI `id name`, and the man page
  header (the Makefile substitutes `@VERSION@` when it installs).

## Install

- **`make install`** copies:
  - `dchess` → `$(DESTDIR)$(PREFIX)/bin/dchess` (mode 755);
  - `docs/dchess.6` → `$(DESTDIR)$(PREFIX)/share/man/man6/dchess.6`, with
    the version filled in;
  - `README.md`, `docs/guide.md` and `CHANGELOG.md` →
    `$(DESTDIR)$(PREFIX)/share/doc/dchess/`;
  - `LICENSE` → `$(DESTDIR)$(PREFIX)/share/licenses/dchess/`.
- **`PREFIX`:** defaults to `/usr/local`.
- **`make uninstall`:** removes exactly those files, and the two dchess
  directories if they are empty.
- **User data:** `~/.config/dchess` and `~/.local/share/dchess` are never
  touched.

## Man page

- **The file:** `docs/dchess.6`, hand-written roff.
- **Sections:** NAME, SYNOPSIS, DESCRIPTION, OPTIONS (every `--help`
  option), KEYS (game, launcher, replay, puzzles), FILES, UCI, SEE ALSO,
  and AUTHOR (the GitHub project).
- **Kept in step:** `tests/test_manpage.sh`, run by `make test`, checks that
  every `--long` option printed by `dchess --help` appears in the man page,
  and that `man -l` (or `groff -man -Tutf8`) renders it without warnings
  when available.

## CHANGELOG.md

- **Format:** Keep a Changelog.
- **`1.0.0`:** grouped as Play, Analysis, Puzzles, Engine, Profiles and
  history, and Tools, drawn from the journal rounds since `v1.0.0-alpha`.
- **`1.0.0-alpha`:** one line pointing at the journal.

## CI

- **The workflow:** `.github/workflows/test.yml` runs on push and
  pull_request, on `ubuntu-latest`.
- **Steps:**
  - install `libncurses-dev`;
  - `make CFLAGS_EXTRA=-Werror`, so warnings fail CI only;
  - `make test`.
- **The Makefile:** gains `CFLAGS_EXTRA ?=`, appended to `CFLAGS`.

## AUR

- **The files:** `packaging/aur/PKGBUILD` and `.SRCINFO`, for `pkgname=dchess`
  and `pkgver=1.0.0`.
- **The build:** from the GitHub tag tarball; `depends=(ncurses)`;
  `make`, then `make test` in `check()`, then `make PREFIX=/usr
  DESTDIR="$pkgdir" install`.
- **Before the tag exists:** `sha256sums` stays `SKIP`, and a real checksum
  is filled in after the release.
- **Checking it:** `makepkg` locally, from the branch, if `makepkg` is
  available. Publishing to the AUR is the owner's step.

## Release flow

1. The branch goes through review and a PR.
2. After the owner merges it: tag `v1.0.0` on `main`, push the tag, and
   `gh release create v1.0.0` with the CHANGELOG entry as its notes.
3. Fill the tarball's sha256 into the PKGBUILD and `.SRCINFO` in a
   follow-up commit.

## Tests

- **The man page:** `test_manpage.sh`, as above.
- **Install:** `make install DESTDIR=$(mktemp -d)` puts every file where it
  should go, and `make uninstall` with the same `DESTDIR` leaves nothing.
  `tests/test_install.sh` checks this, run by `make test`.
- **The version:** `dchess --version` and the UCI `id name` both print
  `1.0.0`. The version check sits in `test_install.sh`; the UCI engine test
  checks the `id name`.

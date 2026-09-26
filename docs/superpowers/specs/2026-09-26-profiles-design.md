# Profiles and the launcher

**Status:** approved design, 2026-09-26
**Branch:** `feat/profiles`

## Goal

Several people can share dchess on one machine. Each person has a profile
with a name, their own game history and their own remembered settings.
Finished games go into one PGN database that the stats page (project D) will
build on. The start menu becomes a full-screen launcher in the dashboard's
style, where profiles are chosen and managed.

This is project C of five (A opponent modes, B UCI engines, C profiles,
D stats page, E opening book).

## Decisions

| Question | Decision |
|---|---|
| What a profile is | A person: name, own history, own remembered settings |
| Two-person games | Each human side is a profile (or Guest); the game counts for both |
| Old `stats.dat` | Imported into the first profile; the file is kept as a backup |
| Game records | One PGN database with standard and dchess tags |
| Start menu | Replaced by the launcher dashboard (direction C) |

## Profiles

The profiles live in `$XDG_CONFIG_HOME/dchess/profiles.conf`, falling back to
`~/.config/dchess/profiles.conf`. The format follows `engines.conf`:

```ini
active = saeed

[saeed]
theme  = gruvbox
white  = saeed
black  = dchess medium
legacy = 312 140 121 51

[alice]
theme  = catppuccin
white  = alice
black  = saeed
```

- **Names:**
  - 1–24 characters and unique.
  - They must not start or end with a space, and must not contain `[`, `]` or
    `;`.
  - They must not be `easy`, `medium`, `hard`, `human` or `guest` in any
    case, and must not equal a registered engine name.
- **`active`** names the profile the launcher opens with. If it is missing or
  unknown, the first profile is used.
- **`theme`, `white` and `black`** are remembered automatically. Starting a
  game writes the active profile's theme and its White/Black setup, and
  choosing that profile later restores them.
  - A setup value uses the words the CLI takes: a profile name, `guest`,
    `easy`, `medium`, `hard`, or an engine entry name.
  - There is no settings form.
- **`legacy`** holds the imported totals: games, wins, losses and draws.
- **Malformed input:** bad lines are skipped, and a section with an invalid
  name is dropped. At most 32 profiles are kept.
- **Saving** writes `profiles.conf.tmp` and renames it over the file, so an
  interrupted save cannot lose the list.

**Guest** is always available and never stored. It stands for a person
without a profile, and its side of a game is recorded for nobody.

### First run

When `profiles.conf` does not exist, dchess creates a profile named after
`$USER`, or `player` if that is unset or not a valid name, and makes it
active. It then imports `~/.local/share/dchess/stats.dat` if one exists:

- The file's totals become the profile's `legacy` line.
- Each of its dated results (up to 256) becomes a legacy record in the game
  database, so the win-rate history continues.
- `stats.dat` is left in place and is never written again.

The import happens once: it runs only when `profiles.conf` is created.

## Players

- `Player.engine` becomes `Player.name`.
  - For `PLAYER_UCI` it holds the engine entry name, as before.
  - For `PLAYER_HUMAN` it holds a profile name. An empty name means Guest.
- **Labels:**
  - A human with a profile shows the profile name; a lone Guest shows
    `Guest`. For example, the board title reads `saeed vs Stockfish 1500`.
  - Two Guests still show as `Player 1 vs Player 2`.
  - PGN names follow the same rules.
- **Commands:**
  - `white human` / `black human` pick the active profile.
  - `white human <name|guest>` picks a named profile, or Guest.
  - An unknown name is refused with the list of profiles.
- **CLI:** `--white` / `--black` also accept a profile name and `guest`;
  `human` means the active profile.

## Game database

The database is `~/.local/share/dchess/games.pgn`. A finished game is
appended when at least one side is a profile. Each game has:

- **The standard tags:** `Event`, `Site`, `Date`, `Round`, `White`, `Black`
  and `Result`, plus `SetUp` / `FEN` when the game did not start from the
  standard position.
- **These tags of dchess's own:**

  | Tag | Value |
  |---|---|
  | `Time` | local start time, `HH:MM:SS` |
  | `WhiteKind`, `BlackKind` | `profile`, `guest`, `dchess` or `engine` |
  | `WhiteStrength`, `BlackStrength` | for an engine: its strength label, e.g. `1s · 1500 Elo`; for dchess: `Easy`, `Medium` or `Hard`; omitted for humans |
  | `EndReason` | `checkmate`, `stalemate`, `repetition`, `fifty-move`, `material`, `resigned` or `legacy` |
  | `PlyCount` | half-moves played |
  | `Seconds` | total thinking time of both sides |

- **The movetext**, as `pgn` export writes it.

A game abandoned mid-way (`new`, `quit`, `loadfen`) is not recorded.

**Legacy records** have `EndReason "legacy"`, no movetext and `PlyCount "0"`.
The profile is `White` and `Black` is `dchess`, with `BlackKind "dchess"`, and
the result is written from the profile's side. Their only use is the win-rate
history: totals come from the `legacy` line, so legacy records are never
counted twice.

### Resign

A new `resign` command ends the game as a loss for the resigning side:

- the human side to move, when that side is human;
- otherwise the only human side.

It is refused when both sides are engines, and when the game is already over.
The result text is `White resigns — Black wins` or `Black resigns — White
wins`, and the game-over popup and the database treat it like any other
ending.

## Code

### `src/game/profiles.c` (core; in `game/` because first run writes legacy records)

```c
#define PROFILES_MAX 32
#define PROFILE_NAME_MAX 24
typedef struct {
    char name[PROFILE_NAME_MAX + 1];
    char theme[24];
    char white[48], black[48];
    int  legacy_games, legacy_wins, legacy_losses, legacy_draws;
} Profile;
typedef struct { Profile p[PROFILES_MAX]; int count; int active; } ProfileList;

int  profiles_path(char *buf, size_t n);
int  profiles_load(ProfileList *l);              /* 0 when the file does not exist */
int  profiles_save(const ProfileList *l);
int  profiles_check_name(const ProfileList *l, const EngineList *engines,
                         const char *name, int skip, char *err, size_t n);
int  profiles_add(ProfileList *l, const EngineList *engines, const char *name, char *err, size_t n);
int  profiles_rename(ProfileList *l, const EngineList *engines, int index,
                     const char *name, char *err, size_t n);
int  profiles_remove(ProfileList *l, int index);
int  profiles_find(const ProfileList *l, const char *name);   /* index or -1 */
int  profiles_first_run(ProfileList *l, const char *user, const DchessStats *old,
                        const char *games_path);               /* 1 if it ran */
```

`profiles_rename` also relabels the profile's games through
`records_rename`.

### `src/game/records.c` (core)

```c
typedef enum { KIND_PROFILE, KIND_GUEST, KIND_DCHESS, KIND_ENGINE } SideKind;
typedef struct {
    char date[11], time[9];
    char white[48], black[48];
    SideKind white_kind, black_kind;
    char white_strength[32], black_strength[32];
    int  result;             /* 1 white won, 0 draw, -1 black won */
    char end_reason[16];
    int  plies, seconds;
    int  legacy;
} Record;
typedef struct { Record *r; int count, cap; } RecordList;

int  records_path(char *buf, size_t n);
int  records_append(const char *path, const GameState *g, const Player p[2],
                    const EngineList *engines, const char *end_reason);
int  records_append_legacy(const char *path, const char *profile, long timestamp, int result);
int  records_load(const char *path, RecordList *out);   /* tags only */
void records_free(RecordList *l);
int  records_rename(const char *path, const char *old_name, const char *new_name);

typedef struct { int games, wins, draws, losses; } RecordTally;
RecordTally records_tally(const RecordList *l, const Profile *who);   /* includes legacy */
int  records_recent(const RecordList *l, const char *who, const Record **out, int max);
int  records_winrate(const RecordList *l, const char *who, float *out, int max);
void records_to_stats(const RecordList *l, const Profile *who, DchessStats *out);
```

- A record belongs to a profile when that profile's name is `White` with
  `WhiteKind "profile"`, or `Black` with `BlackKind "profile"`.
- `records_load` parses tag pairs and skips movetext. A game whose tags
  cannot be parsed is skipped, so a damaged file still loads its readable
  games.
- `records_rename` rewrites the file through a temporary copy and renames it
  over the original. It changes only `White`/`Black` values whose matching
  `Kind` tag is `profile`.
- `records_winrate` gives a rolling win rate over the profile's last `max`
  games, oldest first, with legacy records included.

### `pgn.c`

- `PgnHeader` gains an array of extra tag pairs.
- A shared writer emits a game to a `FILE *`. `pgn_write` (export) opens its
  path with `"w"`, and `pgn_append` opens it with `"a"`, putting a blank line
  between games.

### Stats

- `stats.dat` is no longer written.
- Until project D, the Tab mini-stats and `--stats` show the active profile.
  `records_to_stats` fills a `DchessStats`:
  - per-level counts come from games against dchess;
  - games against engines and people count in the totals and the colour
    split only;
  - the history comes from the win-rate series;
  - the legacy totals are added in.

## Launcher

`src/tui/launcher.c` replaces `onboard.c`. It is full screen, drawn with
`panel_frame` and the theme's colours:

```
╭─ profiles ──────╮╭─ new game ─────────────────╮
│ ▸ saeed 42-11-30 ││  White  ◂ saeed          ▸ │
│   alice   8-2-9  ││  Black    Stockfish 1500   │
│   + new profile  ││  Pos      Standard         │
╰──────────────────╯│  Theme    gruvbox          │
╭─ recent ────────╮│     ▶ START GAME           │
│ W Stockfish  2h  │╰────────────────────────────╯
│ L dchess H   1d  │ win rate ▁▂▄▅▃▆▇▅▆█ 51%
╰──────────────────╯
 tab panel  ↑↓ move  ←→ change  e engines  s stats  esc quit
```

### Panels

- **profiles:** each profile with its W-D-L (legacy included), then
  `+ new profile`.
- **recent:** the active profile's last games, as many as fit and at most 8.
  Each shows `W`/`D`/`L` from that profile's side (green, yellow, red), the
  opponent, and how long ago (`now`, `5m`, `2h`, `3d`, `4w`).
- **new game:** White, Black, Position, Theme and `▶ START GAME`.
- **Under new game:** the win-rate sparkline over the last 30 games, with the
  percentage.

### Focus and keys

Tab switches focus between profiles and new game, and the focused panel's
border takes the accent colour.

- **In profiles:**
  - ↑/↓ makes the highlighted profile active. Its theme and its White/Black
    setup apply at once, and the recent and win-rate panels follow.
  - `n`, or Enter on `+ new profile`, asks for a name.
  - `r` renames the highlighted profile.
  - `d` deletes it after a `y`. Its games stay in the database, and the last
    profile cannot be deleted.
- **In new game:** ↑/↓ moves between rows. ←/→ changes a row, and Enter on
  Start begins the game.
- **Everywhere:** `e` opens the Engines screen, `s` the stats screen, and Esc
  quits.
- **White/Black cycle:** the active profile, the other profiles in file
  order, Guest, `dchess Easy`, `dchess Medium`, `dchess Hard`, then the
  engine entries.

### Size

Below 80 columns or 20 rows, the profiles and recent panels are hidden. The
new game panel then fills the screen, with the active profile's name in its
title, and `p` cycles the active profile.

## CLI

- `--profile <name>` sets the active profile for this run, without changing
  `active` in the file. An unknown name is an error listing the profiles.
- `--profiles` prints each profile's name and W-D-L, then exits.
- `--white` / `--black` accept profile names and `guest` as well as the
  existing values.
- **Resolving `--white` / `--black` values:** a built-in word first
  (`human`, `guest`, `easy`, `medium`, `hard`), then a profile, then an
  engine.

## Testing

### Unit tests

- **`tests/test_profiles.c`:**
  - Save and load round trip through a temporary `XDG_CONFIG_HOME`.
  - Name rules: duplicate, reserved, an engine-name clash, over-long, and
    brackets.
  - Add, rename and remove.
  - `active` survives a reload.
  - A malformed file loads its good sections.
  - First run: the `$USER` profile is created and active; a hand-built
    `stats.dat` fills `legacy` and appends one legacy record per history
    entry; `stats.dat` is unchanged.
  - A second first-run call does nothing.
- **`tests/test_records.c`:**
  - An appended game reads back with every tag, including a FEN start.
  - A game between two profiles tallies for both.
  - Guest, dchess and engine sides tally for nobody.
  - A resigned game.
  - Legacy records feed the win rate but not the tally.
  - `records_rename` changes only profile-kind names.
  - A missing, empty or partly corrupt file.
  - 5,000 appended games load in under 0.5 s.
  - `records_to_stats` for a mixed history.
- **`tests/test_players.c`:** profile labels, Guest labels, and
  `white human alice`.
- **`tests/test_cli.c`:** `--profile`, an unknown `--profile`, `--profiles`,
  a profile name in `--white`, and `guest`.
- **`tests/test_pgn.c`:** extra tags, and append mode putting two games in
  one file.
- **The existing suites and perft** still pass.

### tmux runs

- First launch with an existing `stats.dat`: the profile appears with the
  imported record, and the win-rate sparkline is filled.
- Create, switch, rename and delete profiles. A switch changes the theme and
  the White/Black setup.
- A game between two profiles ends by mate. Both profiles' recent panels show
  it, as `W` for one and `L` for the other.
- `resign`.
- The launcher at 120×40 and at 60×20, where the panels are hidden and `p`
  cycles profiles.
- Tab mini-stats in a game show the active profile's numbers.

## Out of scope

- The full stats page, head-to-head and per-opponent views: project D.
- Game replay.
- Passwords or locking profiles.
- Syncing profiles between machines.
- Recording abandoned games.
- Editing profile settings through a form.

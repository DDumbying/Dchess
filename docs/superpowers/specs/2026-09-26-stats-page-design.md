# Stats page

**Status:** approved design, 2026-09-26
**Branch:** `feat/stats`, from `feat/profiles` (PR #7)

## Goal

A full-screen stats page in the dashboard style (layout A), built on the
`games.pgn` records from project C. It shows totals, trends, a table of
opponents, how games end, and recent games, for any profile.

This is project D of five.

## Data: `src/game/statsview.c` (core, pure)

```c
#define SV_TREND 60
#define SV_OPP_MAX 64

typedef struct {
    char name[PLAYER_NAME_MAX + 1];   /* "dchess Hard", an engine, a person, "Guest",
                                         or "before profiles" */
    int  games, wins, draws, losses;
    long last;                        /* records_time of the latest game; 0 if none */
} SvOpponent;

typedef struct {
    RecordTally total;                   /* includes the legacy line */
    RecordTally as_white, as_black;      /* recorded games only */
    float trend[SV_TREND]; int trend_count;   /* rolling win rate, oldest first */
    char  streak;  int streak_len;       /* 'W', 'L', 'D' or 0 */
    int   best_win_streak;
    int   per_week[4];                   /* [0] = the last 7 days, then older weeks */
    SvOpponent opp[SV_OPP_MAX]; int opp_count;   /* by games, most first */
    int   mates, resigns, stalemates, repetitions, fifty, material;
    int   avg_plies, avg_seconds;        /* recorded games only */
    const Record *recent[256]; int recent_count; /* newest first; points into the list */
} StatsView;

void stats_view_build(const RecordList *l, const Profile *p, long now, StatsView *out);
```

**Opponent names:**
- a dchess side is named `dchess <Strength>`;
- an engine by its tag name;
- a person by name, and a guest as `Guest`.

**Legacy data:**
- The profile's `legacy_*` line becomes one opponent row, `before profiles`, and
  is added into `total`.
- Legacy records count towards the trend, the streaks and `recent`.
- They count towards nothing else.

**Endings:**
- `mates` counts games whose `EndReason` is `checkmate`, `resigns` those ending
  in `resigned`, and so on for each kind of draw.
- A win and a loss by mate both count as a mate.

**Streaks:** are measured over all of the profile's games, in file order.

**Weeks:** `per_week[i]` counts games whose `records_time` falls in
`(now - 7(i+1) days, now - 7i days]`.

## Screen: `src/tui/stats_tui.c` (rewritten)

`void stats_screen(TUIState *state)` is modal and returns on Esc.

### Layout

At 80×24 or larger, the panels are:

- **summary:** the profile and its game count in the title. It shows W, D and L,
  the win %, and the win % as White and as Black.
- **trend:** a sparkline over `trend`, the current streak, the best streak, and
  games per week.
- **opponents:** a table with the columns opponent, games, W-D-L, win % and
  last. The win % is coloured green at 55% or more, yellow from 45%, and red
  below. It scrolls.
- **endings:** a bar per ending type, plus average moves and time.
- **recent:** result letter, opponent, moves, ending and age. It scrolls.

Below 80×24, only summary, opponents and recent are shown, stacked in one
column.

### Keys

| Key | Action |
|---|---|
| ←/→ | cycle profiles and rebuild the view |
| Tab | move focus between opponents and recent |
| ↑/↓ | scroll the focused panel |
| Esc | return |

### Entry points

- The launcher's `s`.
- `--stats`, which shows the active profile, or the `--profile` one.
- The in-game `stats` command, which opens the page.

The Tab mini popup is restyled to show the active profile's summary and a
trend line.

### Removed

- The old `draw_stats_overlay`, `show_stats_overlay` and `draw_stats_mini`
  bodies.
- `records_to_stats` and `profiles_stats`, and with them the "count under
  Medium" fold.

`stats.c` stays, but only for the first-run import. `TUIState.stats` goes.

## Testing

**`tests/test_statsview.c`**, with a fixture file written in the test and a
fixed `now`:

- totals include the legacy line;
- the White/Black split;
- the current streak and the best winning streak;
- per-week buckets;
- opponent rows are named correctly (`dchess Hard`, an engine, a person,
  `Guest`, `before profiles`), sorted, and carry their last-played time;
- ending counts and averages;
- `recent` is newest first;
- an empty history gives zeroes and no crash.

**tmux runs** at 120×40 and 70×20:

- switching profiles;
- Tab focus and scrolling;
- opening the page from the launcher, from the game, and with `--stats`.

## Out of scope

- Date filters.
- Exporting stats.
- A head-to-head drill-down.
- Ratings.

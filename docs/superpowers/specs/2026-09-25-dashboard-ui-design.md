# Dashboard UI redesign

**Status:** approved design, 2026-09-25
**Branch:** `feat/dashboard-ui`

## Goal

Rebuild the in-game TUI in the style of btop: bordered panels with titles inset
into the border, colour gradients, and a live evaluation graph. Gruvbox is the
default palette.

## Decisions

| Question | Decision |
|---|---|
| Layout | **A**: board on the left, one stacked column on the right |
| Palette | **Gruvbox Dark** by default |
| Existing themes | Replaced by Gruvbox, Tokyo Night, btop and Catppuccin |
| Corners | Rounded (`╭╮╰╯`), as btop draws them |
| Old theme names | `--theme classic` etc. become an error listing the new names |

## Colour architecture

Today's code uses its RGB themes only when `can_change_color()` is true, and
falls back to 8 colours otherwise. Probing showed `screen-256color` and
`tmux-256color` report 256 colours but `can_change_color = 0`, so anyone in
tmux or screen, including the maintainer's own shell, has only ever seen the
8-colour fallback.

**Themes are defined as xterm-256 palette indices.** Those work on every
256-colour terminal with no `init_color()`, so the app also stops rewriting the
user's terminal palette, a change that can outlive the process. Gruvbox uses
its official 256-colour mapping; the other themes use the nearest index to
their published RGB values.

- `COLORS >= 256`: full theme.
- `COLORS < 256`: the existing 8-colour fallback, kept legible.

The theme paints its own background across the whole screen, as btop does,
rather than relying on the terminal's.

## Layout

```
╭─ board ──────────────────╮╭─ eval ──────────╮
│                          ││ ▁▂▃▅▆▇█▇▆▅      │
│                          ││ +0.40           │
│                          │╰─────────────────╯
│         8 x 8            │╭─ clocks ────────╮
│                          ││ W ██████░░ 04:12│
│                          ││ B ████░░░░ 02:48│
│                          │╰─────────────────╯
│                          │╭─ moves ─────────╮
│                          ││ 1. e4  e5       │
╰──────────────────────────╯╰─────────────────╯
╭─ command ──────────────────────────────────╮
╰────────────────────────────────────────────╯
```

- The board panel takes the remaining width after the side column. The existing
  `fit_board()` sizing and piece-art tiers apply inside it.
- The side column holds, top to bottom, **eval**, **clocks**, **moves** and
  **engine**. **moves** takes whatever height is left.
- The side column is 26 columns wide, widening to 34 on terminals of 110
  columns or more. It is shown only when the terminal is at least 60 columns
  wide; below that the board takes the full width. The 34x20 minimum is
  unchanged.

## Panels

A shared `panel_frame()` draws a rounded border in the theme's line colour with
the title inset at the top left in the panel's accent colour. Every panel uses
it.

- **board**: the existing board, square colours and piece colours from the
  theme.
- **eval**: a block graph (`▁▂▃▄▅▆▇█`, eight levels per cell) of the evaluation
  across the game, oldest on the left. Bars grow from the bottom: the value is
  clamped to ±5 pawns and mapped so that -5 is an empty column, 0 is half
  height and +5 is full height. A dim horizontal rule at half height marks 0.
  Bars are coloured along the theme's gradient ramp by height. When there are
  more evaluations than columns, the most recent ones are shown. The current value is printed under the graph
  with a "white/black better" label.
- **clocks**: one bar per side, filled in proportion to that side's **share of
  total thinking time**, since there is no time control to count down against.
  Before any time has been spent both bars are empty. The elapsed time is
  printed beside each bar.
- **moves**: the SAN move list, latest move highlighted, scrolling to keep the
  latest visible.
- **engine**: depth reached, nodes searched and nodes per second from the last
  search.

## Theme definition

`Theme` becomes a set of 256-palette indices:

- `bg`, `fg`, `dim`, `line`: canvas, text, secondary text, borders
- one accent per panel: `acc_board`, `acc_eval`, `acc_clock`, `acc_moves`,
  `acc_engine`
- `ramp[8]`: low-to-high gradient for bars and the graph
- `sq_light`, `sq_dark`, `pc_white`, `pc_black`: board and piece colours
- highlight colours for cursor, selection, legal-move destinations, last move
  and check
- the 8-colour fallback fields, as today

Colour pairs are allocated through `colors.h` as today. The ramp needs eight
dedicated pairs.

## Other screens

The onboarding screen, the game-over popup and both statistics views move to
`panel_frame()` and the theme's colours, so no screen still uses the old look.
The drop shadows stay.

## Changes this depends on

1. **Eval perspective.** `search()` scores from the side to move's point of
   view, and every consumer got the conversion wrong: `apply_engine_result()`
   negated it in both cases (a queen up for White displayed as −9.25), the
   `eval` command never converted it, and `eval_history` stored it raw. All
   three now go through `eval_white_view()`.
2. **Engine statistics.** The last search's depth, node count and elapsed time
   are kept in `TUIState` so the engine panel can read them. The time is
   measured around `search()` in the worker thread.

## Out of scope

- Time controls: the clocks still count up.
- A transparent background option.
- New piece art: the existing tiers are reused.

## Testing

- **Unit tests** for the logic: the eval-perspective fix, mapping an
  evaluation to a graph level, and choosing a gradient step.
- **tmux captures** at 60x20, 80x24, 120x40 and 150x50, for each of the four
  themes and mid-game so every panel has data.
- A capture under `TERM=screen-256color` specifically, since that is the
  terminal the old colour path failed on.
- The full existing suite and perft must still pass.

## Risks

- **Width.** Layout A needs room. The side column must drop cleanly rather than
  squeeze the board.
- **Glyph support.** Rounded corners and the eighth-block characters need a
  font that has them. Both are in the Unicode box-drawing and block-element
  ranges the app already relies on for piece art.

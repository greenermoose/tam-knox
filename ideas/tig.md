# Tig

## What it is

- **Language:** C (`ncurses`). **License:** GPL v2 or later
  (`jonas/tig`).
- **Scope:** Classic ncurses text-mode interface for Git (`tig`,
  `tig status`, `tig blame`), around 30,000 lines of C.
- **Depth:** Design and architecture evaluation (`include/tig/graph.h`,
  `src/graph-v2.c`, `src/view.c`).

## Ideas worth taking

- **Fast C subprocess streaming and split-view inspection.** Pressing
  `Enter` on a commit in Tig's main view opens a split commit-diff pane
  while keeping the commit graph visible alongside it, so `Up`/`Down` Or
  `j`/`k` can step through commits while inspecting their changed files.
  **adopt (P1, 0.1.0)**.
- **Compact 2-column-per-lane character cell graph rendering.** Tig's
  `graph-v2.c` demonstrates that 2 terminal cells per graph column
  (one for the commit symbol / vertical line, one for horizontal
  connectors) is the sweet spot for terminal commit graphs.
  **adopt (P1, 0.1.0, `src/graph.c`)**.

## Things to avoid

- **`ncurses` / `terminfo` dependency.** Tamlinux TUI tools link only
  `libc` and `libtam.a` (`tam_tty`, `tam_key`, `tam_uni`, `tam_pager`),
  using clean ANSI/xterm sequences with DEC synchronized output (`?2026`)
  and explicit `TERM=linux` console support.
- **Single-repo scope and lack of a workspace Explorer or Markdown/HTML
  renderer.** Tig cannot browse a parent directory of 25 repositories or
  render Markdown/HTML/Mermaid documents.

## Where to look

- `src/graph-v2.c` and `src/draw.c` in `jonas/tig`.

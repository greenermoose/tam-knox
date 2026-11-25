# GitUI

## What it is

- **Language:** Rust (`ratatui`, `git2` / `gix`). **License:** MIT
  (`extrawurst/gitui`).
- **Scope:** Keyboard-driven terminal UI for Git emphasizing speed and
  discoverable bottom-bar key hints.
- **Depth:** Design evaluation.

## Ideas worth taking

- **Always-visible contextual bottom key-hint bar.** Showing the 5-7
  most relevant keys for the currently focused pane at the bottom of the
  screen (for example, `[Enter] Inspect  [Alt+S] Sync  [Alt+1/2] Sidebar
  [Alt+V/H] Split  [Ctrl+G] Keys`) makes the UI immediately usable
  without opening help. **adopt (P1, 0.1.0)**.
- **Modal confirmation before pushing protected branches.** Clear,
  centered confirmation dialog before executing a remote push on a
  protected branch (`main`, `test`). **adopt (P1, 0.1.0)**.

## Things to avoid

- **`libgit2` C/Rust dependency.** Linking `libgit2` violates our zero
  external dependency rule (`libc` + `libtam` only) and often diverges
  from the user's configured `git` binary, SSH agent, and credential
  helpers. `tam-knox` invokes `git` cleanly via `libtam`'s `tam_exec`
  with `--porcelain=v2 -z` and NUL-delimited plumbing flags.

## Where to look

- `src/components/` in `extrawurst/gitui`.

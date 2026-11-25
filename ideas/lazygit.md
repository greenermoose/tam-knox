# Lazygit

## What it is

- **Language:** Go (`gocui`). **License:** MIT (`jesseduffield/lazygit`).
- **Scope:** A full-screen terminal UI for Git commands with fixed panels
  for Status, Files, Branches, Commits, and Stash, plus a main preview
  pane on the right.
- **Depth:** Design and UX evaluation.

## Ideas worth taking

- **Live main-pane preview synchronized with sidebar selection.** As you
  move up and down the changed files list or commit list, the main pane
  can display the corresponding diff or commit details without opening
  and closing modal windows. **adopt (P1, 0.1.0)**.
- **Clear visual focus border on the active pane.** Highlighting the
  border and header of the currently focused panel (Sidebar Repositories,
  Sidebar Changes, Explorer, or Main Split Pane) prevents any ambiguity
  about where `Up`/`Down`/`Enter` will act. **adopt (P1, 0.1.0)**.
- **Command log / last-sync status message in the footer.** Showing the
  exact outcome of a `git fetch` / `pull` / `push` / `commit` operation
  in the status bar gives immediate confidence. **adopt (P1, 0.1.0)**.

## Things to avoid

- **Single-repository lock-in.** Lazygit is built around one repository
  at a time (switching repos requires a popup menu and discards view
  context), whereas Fred's primary workflow manages 20+ repositories in
  `~/Code/tamlinux` simultaneously.
- **No rendered Markdown, HTML, or Mermaid file viewer.** Lazygit only
  shows raw diffs and plain files, whereas `tam-knox` integrates
  `tam-view`'s full Markdown/HTML/Mermaid rendering engine and syntax
  highlighting across multi-repo tabs.
- **Non-standard single-letter mutations everywhere.** Accidental key
  presses in Lazygit can rebase, reset, or drop commits. `tam-knox`
  keeps destructive history mutations out of single keys and follows
  `tamlinux/arch/keybindings.md`.

## Where to look

- `pkg/gui/` in `jesseduffield/lazygit`.

# Emacs (Magit, VC-dir, Dired, and Treemacs)

## What it is

- **Language:** Emacs Lisp + C core. **License:** GPL v3 or later.
- **Scope:** Magit (`magit/magit`) is the gold-standard keyboard-driven
  Git porcelain inside Emacs; `magit-repos` (`magit-list-repositories`)
  summarizes multiple repositories; `dired` and `treemacs` provide
  directory and project-tree navigation.
- **Depth:** Design and workflow evaluation of Magit's status buffer,
  `magit-list-repositories`, commit-log graph, and transient key help.

## Ideas worth taking

- **Single-key staging and unstaging on changed files (`Space` / `s` /
  `u`) with instant diff preview.** In Magit, moving the cursor over a
  changed file and pressing a single key to toggle staged state or pressing
  `Enter`/`Tab` to inspect its diff makes reviewing changes effortless.
  In `tam-knox`, `Space` toggles stage/unstage on the focused file in
  Source Control Changes and `Enter` opens its unified diff.
  **adopt (P1, 0.1.0)**.
- **Discoverable command guidance (`?` / `Transient` -> `Ctrl+G` and
  `Ctrl+E` in Tamlinux).** Magit never forces users to memorize obscure
  flags because its transient popup shows available actions in context.
  `tam-knox` pairs a context-sensitive bottom hint bar with `Ctrl+G`
  (Guidance overlay) and `Ctrl+E` (Command Palette & Chord Composer).
  **adopt (P1, 0.1.0)**.
- **`magit-list-repositories` tabular overview (`tam-knox --status`).**
  A fast, scannable table of all repositories in a workspace showing
  name, branch, dirty indicator, changed count, and ahead/behind count,
  available both interactively in the sidebar and via
  `tam-knox --status <dir>`. **adopt (P1, 0.1.0)**.
- **Opening the real editor at the exact line (`Alt+E`).** Jumping
  straight from a file or diff line to `tam-edit` at that line keeps
  `tam-knox` focused on viewing and Git orchestration while giving full
  editing power on demand. **adopt (P1, 0.1.0)**.

## Things to avoid

- **Emacs-chord ergonomics (`C-x g`, `C-c C-c`, `M-x`).** Tamlinux has
  its own unified keybinding standard (`tamlinux/arch/keybindings.md`)
  with universal `Ctrl` meanings and a three-physical-key ceiling.
- **Relying on `git log --graph` text output.** Magit delegates graph
  drawing to `git log --graph --color`, which produces wide, jagged ASCII
  slashes (`/`, `\`, `|`) that cannot coalesce merge lanes cleanly like
  VS Code Git Graph's `determinePath` router. `tam-knox` computes its
  own graph layout in C17.

## Where to look

- `lisp/magit-status.el`, `lisp/magit-repos.el`, and `lisp/magit-log.el`
  in `magit/magit`.

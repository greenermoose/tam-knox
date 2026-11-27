# tam-knox roadmap

`tam-knox` is Tamlinux's terminal multi-repository Git workbench and file
viewer, written in C17 on top of `libtam`.

- **Current version:** `1.1.4` (branch `dev`)
- **Progress:** All 3 phases done (in maintenance)
- **Versioning:** Semantic versioning; minor adds features or capabilities,
  patch fixes defects or refines UI and bindings.

## Phase summary

| Phase | Versions | Goal | Plan | Status |
| --- | --- | --- | --- | --- |
| **P1** | 0.1.0–0.1.2 | Initial multi-repo Git workbench, commit graph, explorer, file viewer, and palette | [P1-initial-workbench.md](plans/P1-initial-workbench.md) | Done (2026-11-25) |
| **P2** | 0.2.0 | Branch checkout/creation modal, stash management, and side-by-side split diff | [P2-branch-stash-and-split-diff.md](plans/P2-branch-stash-and-split-diff.md) | Done (2026-11-25) |
| **P3** | 0.3.0–1.1.4 | File filtering, batch fetch, user keymaps, v1.0 completion, libtam integration, and UX polish | [P3-filter-batch-fetch-and-keymap.md](plans/P3-filter-batch-fetch-and-keymap.md) | 1.1.4 in Develop |

## Phases and milestones

### Phase P1: Initial workbench (0.1.0–0.1.2) — [P1-initial-workbench.md](plans/P1-initial-workbench.md)

| Version | Milestone | Status |
| --- | --- | --- |
| **0.1.0** | Multi-repo Source Control sidebar, Git Graph commit history & diff inspector, folder Explorer with Git badges, File Viewer (syntax + rendered Markdown/HTML/Mermaid), tabs, split panes, command palette, and field survey in `ideas/` | Done (2026-11-25) |
| **0.1.1** | High-contrast 256-color palette for `tam-last` and dynamic repository name column sizing in sidebar | Done (2026-11-25) |
| **0.1.2** | Single-key `g` shortcut to toggle keybinding guidance overlay alongside `Ctrl+G` | Done (2026-11-25) |

### Phase P2: Branches, stashes, and split diff (0.2.0) — [P2-branch-stash-and-split-diff.md](plans/P2-branch-stash-and-split-diff.md)

| Version | Milestone | Status |
| --- | --- | --- |
| **0.2.0** | Branch checkout/creation modal, stash management, and side-by-side diff split mode | Done (2026-11-25) |

### Phase P3: Filtering, batch fetch, keymaps, and v1.x (0.3.0–1.1.4) — [P3-filter-batch-fetch-and-keymap.md](plans/P3-filter-batch-fetch-and-keymap.md)

| Version | Milestone | Status |
| --- | --- | --- |
| **0.3.0** | File tree substring filtering, multi-repo batch fetch (`Alt+A`/`F`), and user `keymap.ini` via `tam_ini` | Done (2026-11-25) |
| **0.3.1** | Bind `s` in normal mode to sync the selected repository with its remote | Done (2026-11-25) |
| **1.0.0** | Complete v1 multi-repository Git workbench, commit graph, explorer, and file/diff viewer | Done (2026-11-25) |
| **1.0.1** | Bind `f` in normal mode (`repo-fetch`) to fetch selected repository (`git fetch --prune`) | Done (2026-11-25) |
| **1.0.2** | Explorer navigation: `Return`/`Right` opens/expands repo or folder, `Left` collapses, `Backspace` returns to Source Control | Done (2026-11-25) |
| **1.0.3** | Direct tab selection via `1`–`9`/`0`, streamlined top bar, and on-demand tab title shrinking | Done (2026-11-25) |
| **1.0.4** | Align top bar tab strip and vertical divider with main workbench pane when sidebar is visible | Done (2026-11-25) |
| **1.0.5** | Add empty black-space border around centered modals for readability | Done (2026-11-25) |
| **1.0.6** | Move keybinding documentation to `keys/keybindings.md` and switch `tab-prev`/`tab-next` to `Alt+[`/`Alt+]` | Done (2026-11-25) |
| **1.0.7** | Build against `libtam` 0.10.0; keep every key of a large paste/burst read | Done (2026-11-26) |
| **1.0.8** | Link `libtam-render` (`libtam` 0.13.0) instead of carrying a local renderer copy (~4,400 lines removed) | Done (2026-11-26) |
| **1.1.0** | Paste into prompts, `Ctrl+P` pause, crash/SIGTERM restore, TTY check, editor run, OSC 52, and key names via `libtam` | Done (2026-11-26) |
| **1.1.1** | `Ctrl+P` pauses through `libtam` 0.14.1 `tam_tty_job_stop` (also pauses a session leader) | Done (2026-11-26) |
| **1.1.2** | Responsive guidance modal (`g`) with word-wrapped descriptions, complete keybindings, and vertical scrolling | Done (2026-11-26) |
| **1.1.3** | Guidance sorted by key or command name, plus `u` to refresh open files from disk | Develop |
| **1.1.4** | Guidance key order groups chords by modifier, then letters, digits, and special keys | Develop |

## How a milestone is done

A milestone is done when its acceptance checks in the phase plan pass and
Fred has tried the build. When a milestone lands, bump `VERSION`, update
the phase plan's status, and update this roadmap in the same change.

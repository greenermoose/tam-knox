# tam-knox roadmap

`tam-knox` is Tamlinux's terminal multi-repository Git workbench and file
viewer, written in C11 on top of `libtam`.

## Milestones

| Version | Phase | Milestone | Status |
| --- | --- | --- | --- |
| **0.1.0** | [P1](plans/P1-initial-workbench.md) | Multi-repo Source Control sidebar (discovery, branch, dirty status, changed files, ahead/behind, stage/unstage/commit, remote sync), Git Graph commit history & diff inspector, folder Explorer with Git badges, File Viewer (raw syntax coloring + rendered Markdown/HTML/Mermaid from `tam-view`), tabs, split panes, command palette, and field survey in `ideas/` | Done (2026-11-25) |
| **0.1.1** | [P1](plans/P1-initial-workbench.md) | High-contrast 256-color selection, header, top-bar, and muted-text palette for `tam-last`, plus dynamic repository name column sizing in the sidebar | Done (2026-11-25) |
| **0.1.2** | [P1](plans/P1-initial-workbench.md) | Add `g` single-key shortcut to toggle the keybinding guidance overlay alongside `Ctrl+G` | Done (2026-11-25) |
| **0.2.0** | [P2](plans/P2-branch-stash-and-split-diff.md) | Branch checkout / creation modal, stash management, and side-by-side diff split mode | Done (2026-11-25) |
| **0.3.0** | [P3](plans/P3-filter-batch-fetch-and-keymap.md) | File tree substring filtering, multi-repo batch fetch (`Alt+A` / `F`), and user `keymap.ini` customization via `libtam` `tam_ini` | Done (2026-11-25) |
| **0.3.1** | [P3](plans/P3-filter-batch-fetch-and-keymap.md) | Bind `s` in normal mode to sync the selected repository with its remote | Done (2026-11-25) |
| **1.0.0** | [P1](plans/P1-initial-workbench.md)–[P3](plans/P3-filter-batch-fetch-and-keymap.md) | Complete v1 multi-repository Git workbench, commit graph, explorer, and file/diff viewer | Done (2026-11-25) |
| **1.0.1** | [P3](plans/P3-filter-batch-fetch-and-keymap.md) | Bind `f` in normal mode (`repo-fetch`) to fetch the selected repository from its remote (`git fetch --prune`) | Done (2026-11-25) |
| **1.0.2** | [P3](plans/P3-filter-batch-fetch-and-keymap.md) | Press `Return`/`Right` on a repo to open and expand it in Explorer, `Right`/`Left` to expand/collapse folders and move between Explorer and File Viewer, and `Backspace` to return to Source Control | Done (2026-11-25) |
| **1.0.3** | [P3](plans/P3-filter-batch-fetch-and-keymap.md) | Direct tab focus and selection via `1`–`9`, `0` (10 tabs in keyboard order), streamlined top bar without sidebar mode hints, and on-demand tab title shrinking so all 10 tabs fit | Done (2026-11-25) |
| **1.0.4** | [P3](plans/P3-filter-batch-fetch-and-keymap.md) | Align the top bar tab strip and vertical divider with the main workbench pane when the sidebar is visible | Done (2026-11-25) |
| **1.0.5** | [P3](plans/P3-filter-batch-fetch-and-keymap.md) | Add an empty black-space border (1 row top/bottom, 2 columns left/right) around all centered modals (Keybinding Guidance, Command Palette, Branches, Stashes) for readability | Done (2026-11-25) |

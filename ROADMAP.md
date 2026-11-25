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

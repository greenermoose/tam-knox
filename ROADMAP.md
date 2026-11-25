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
| **0.3.0** | P3 | File tree filtering, multi-repo batch fetch, and user `keymap.ini` customization | Planned |

# Code taken from other projects

This file lists every piece of code tam-knox takes from another project.
An idea taken without its code goes in `ideas/`, not here.

## tam-view layout and Markdown/HTML/Mermaid rendering engine

- **Project:** tam-view (https://github.com/greenermoose/tam-view)
- **License:** GNU General Public License, version 3 or later
- **Source:** `include/view.h`, `include/render_internal.h`,
  `src/source.c`, `src/layout.c`, `src/render.c`, `src/render_md.c`,
  `src/render_html.c`, and `src/render_mermaid.c` at commit `3119315`
- **Here:** `include/view.h`, `include/render_internal.h`,
  `src/source.c`, `src/layout.c`, `src/render.c`, `src/render_md.c`,
  `src/render_html.c`, and `src/render_mermaid.c`
- **Changes:** Used unmodified as the document source, UTF-8 line layout,
  and Markdown/HTML/Mermaid rendering backend for the `tam-knox` file
  viewer.
- **Taken:** 2026-11-25

## How to record a piece

One section per piece, newest last:

    ## <what it is>

    - **Project:** name and URL
    - **License:** the project's license
    - **Source:** file path in the project, and the commit it came from
    - **Here:** the file or files it now lives in
    - **Changes:** how it was rewritten or reshaped, and why
    - **Taken:** YYYY-MM-DD

Add the project's copyright notice to `COPYRIGHT` in the same commit.
When upstream fixes a bug in code we took, check whether our copy has
the same bug, and record what we did in `bugs/`.

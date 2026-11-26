# Code taken from other projects

This file lists every piece of code tam-knox takes from another project.
An idea taken without its code goes in `ideas/`, not here.

Nothing taken now. Until 1.0.8, tam-knox carried an unmodified copy of
tam-view's document source, layout and Markdown/HTML/Mermaid renderers
(taken at tam-view `3119315` on 2026-11-25). That code moved into libtam's
`libtam-render.a` in libtam 0.13.0, and tam-knox links it instead.

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

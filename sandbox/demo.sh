#!/bin/sh
# Create a rich multi-repository demo workspace in /tmp/tam-knox-demo
# and launch tam-knox on it.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/tam-knox"
if [ ! -x "$BIN" ]; then
    make -C "$ROOT"
fi

DEMO="/tmp/tam-knox-demo"
rm -rf "$DEMO"
mkdir -p "$DEMO/core-engine" "$DEMO/docs-portal"

# 1. core-engine: branch graph with merge + dirty files
git -C "$DEMO/core-engine" init -q -b dev
git -C "$DEMO/core-engine" config user.name "Fred"
git -C "$DEMO/core-engine" config user.email "fred@tamlinux.org"

cat > "$DEMO/core-engine/engine.c" << 'EOF'
#include <stdio.h>
#include <stdint.h>

/* Core engine sample */
int engine_start(uint32_t flags)
{
    if (flags == 0)
        return -1;
    printf("engine started: 0x%x\n", flags);
    return 0;
}
EOF
git -C "$DEMO/core-engine" add -- engine.c
git -C "$DEMO/core-engine" commit -q -m "Add initial C17 engine_start implementation"

git -C "$DEMO/core-engine" checkout -q -b feature/fast-path
cat >> "$DEMO/core-engine/engine.c" << 'EOF'

int engine_fast_path(void)
{
    return 42;
}
EOF
git -C "$DEMO/core-engine" add -- engine.c
git -C "$DEMO/core-engine" commit -q -m "Implement engine_fast_path on feature branch"

git -C "$DEMO/core-engine" checkout -q dev
cat > "$DEMO/core-engine/README.md" << 'EOF'
# Core Engine

High-speed C17 engine for Tamlinux.

```mermaid
flowchart LR
    Init["engine_start()"] --> Fast["engine_fast_path()"]
    Fast --> Done["Ready"]
```
EOF
git -C "$DEMO/core-engine" add -- README.md
git -C "$DEMO/core-engine" commit -q -m "Document architecture with Mermaid diagram"
git -C "$DEMO/core-engine" merge -q --no-ff feature/fast-path -m "Merge branch 'feature/fast-path' into dev"

echo "/* Uncommitted change */" >> "$DEMO/core-engine/engine.c"

# 2. docs-portal: clean repo with Markdown and HTML
git -C "$DEMO/docs-portal" init -q -b main
git -C "$DEMO/docs-portal" config user.name "Fred"
git -C "$DEMO/docs-portal" config user.email "fred@tamlinux.org"
cat > "$DEMO/docs-portal/index.html" << 'EOF'
<!DOCTYPE html>
<html>
<head><title>Tamlinux Portal</title></head>
<body>
  <h1>Tamlinux Documentation Portal</h1>
  <p>Viewable in both <b>Rendered</b> and <code>Raw Syntax</code> modes.</p>
</body>
</html>
EOF
git -C "$DEMO/docs-portal" add -- index.html
git -C "$DEMO/docs-portal" commit -q -m "Add portal index.html"

exec "$BIN" "$DEMO"

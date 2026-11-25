#!/bin/sh
set -eu

BIN="${1:-build/tam-knox}"
VER="${2:-0.1.0}"

out_ver="$("$BIN" --version)"
case "$out_ver" in
    "tam-knox $VER") ;;
    *) echo "cli.sh: unexpected --version: $out_ver" >&2; exit 1 ;;
esac

out_v="$("$BIN" -v)"
[ "$out_v" = "$out_ver" ] || { echo "cli.sh: -v mismatch" >&2; exit 1; }

out_help="$("$BIN" --help)"
echo "$out_help" | grep -q "Multi-repository Git workbench" || {
    echo "cli.sh: --help missing summary" >&2
    exit 1
}

tmp="$(mktemp -d /tmp/knox-cli-XXXXXX)"
trap 'rm -rf "$tmp"' EXIT

mkdir -p "$tmp/alpha"
git -C "$tmp/alpha" init -q -b dev
git -C "$tmp/alpha" config user.name "Test"
git -C "$tmp/alpha" config user.email "test@example.com"
printf '# Alpha\n' > "$tmp/alpha/README.md"
git -C "$tmp/alpha" add -- README.md
git -C "$tmp/alpha" commit -q -m "First commit on dev"

out_st="$("$BIN" --no-color --status "$tmp")"
echo "$out_st" | grep -q "alpha" || { echo "cli.sh: --status missing alpha" >&2; exit 1; }
echo "$out_st" | grep -q "dev"   || { echo "cli.sh: --status missing branch" >&2; exit 1; }
echo "$out_st" | grep -q "clean" || { echo "cli.sh: --status missing clean" >&2; exit 1; }

out_gr="$("$BIN" --no-color --ascii --graph "$tmp/alpha")"
echo "$out_gr" | grep -q "First commit on dev" || {
    echo "cli.sh: --graph missing commit subject" >&2
    exit 1
}

echo "cli.sh: all CLI checks passed"

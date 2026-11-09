#!/usr/bin/env bash
# Vendor the Vditor editor distribution into resources/editor/vditor/dist/.
# Vditor resolves everything it loads lazily as <cdn>/dist/<path>, so the
# cdn option in editor.js must be the qrc URL of resources/editor/vditor/.
# Only the subpackages the LLocr editor page loads are kept (v4 layout: heavy
# renderers under dist/js/); a missing path fails loudly so a dist relayout
# between versions cannot silently strip assets the page depends on.
set -euo pipefail

VDITOR_VERSION="4.0.1"

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
dest="$repo_root/resources/editor/vditor/dist"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

curl -fsSL "https://registry.npmjs.org/vditor/-/vditor-$VDITOR_VERSION.tgz" -o "$tmp/vditor.tgz"
tar -xzf "$tmp/vditor.tgz" -C "$tmp"

keep_files="index.min.js index.css"
keep_dirs="js/lute js/katex js/highlight.js js/i18n js/icons css/content-theme images"

for entry in $keep_files $keep_dirs; do
    [ -e "$tmp/package/dist/$entry" ] || { echo "vditor $VDITOR_VERSION dist is missing $entry" >&2; exit 1; }
done

rm -rf "$repo_root/resources/editor/vditor"
mkdir -p "$dest/js" "$dest/css"
for entry in $keep_files; do
    cp "$tmp/package/dist/$entry" "$dest/$entry"
done
for entry in $keep_dirs; do
    cp -R "$tmp/package/dist/$entry" "$dest/$entry"
done

echo "vendored vditor $VDITOR_VERSION into $repo_root/resources/editor/vditor"
du -sh "$repo_root/resources/editor/vditor"

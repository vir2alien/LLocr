#!/bin/sh
# Prints the byte sizes of every .gguf in the Hugging Face repos referenced by
# resources/profiles/models/*.json — the numbers that go into the profiles'
# "sizeBytes" fields (ADR 168: quant sizes are curated in the profiles, not
# fetched at runtime). Rerun when a repo gains quants, then update the JSONs.
#
# Usage: scripts/fetch-quant-sizes.sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

repos=$(python3 -c "
import json, glob
for f in sorted(glob.glob('$root/resources/profiles/models/*.json')):
    d = json.load(open(f))
    if d.get('files', {}).get('repo'):
        print(d['id'], d['files']['repo'])
")

echo "$repos" | while read -r id repo; do
    echo "=== $id  ($repo)"
    curl -sf "https://huggingface.co/api/models/$repo/tree/main?recursive=true" | python3 -c "
import json, sys
for f in json.load(sys.stdin):
    if f['path'].endswith('.gguf'):
        print(f\"{f['size']:>13,}  {f['path']}\")
"
done

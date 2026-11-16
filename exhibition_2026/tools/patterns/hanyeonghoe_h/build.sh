#!/usr/bin/env bash
# Regenerates the HANYEONGHOE exhibit header from patterns/3_hanyeonghoe/hanyeonghoe.project.json.
#
#   tools/patterns/hanyeonghoe_h/build.sh            # writes patterns/3_hanyeonghoe/hanyeonghoe.h
#   tools/patterns/hanyeonghoe_h/build.sh <out.h>    # writes somewhere else (to diff first)
#   tools/patterns/hanyeonghoe_h/build.sh --hashes   # print the JSON's current layer hashes
#
# PF_WEB overrides the main repo's web/ folder (read only: its Pattern Lab exporter and tsx).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
EX="$(cd "$HERE/../../.." && pwd)"
WEB="${PF_WEB:-$EX/../랜딩페이지_opensorce/web}"
PROJECT="$EX/patterns/3_hanyeonghoe/hanyeonghoe.project.json"
UNITS="$HERE/units"
OUT="$HERE/out"
DEST="$EX/patterns/3_hanyeonghoe/hanyeonghoe.h"

if [ "${1:-}" = "--hashes" ]; then
  node "$HERE/gen/extract.js" "$PROJECT" "$UNITS/sources.sha256" "$OUT" --print-hashes
  exit 0
fi
[ -n "${1:-}" ] && DEST="$1"
[ -d "$WEB/node_modules" ] || { echo "no $WEB/node_modules (set PF_WEB)" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/units"

# 1. Layer JS out of the project JSON; stop if the hand translations are stale.
node "$HERE/gen/extract.js" "$PROJECT" "$UNITS/sources.sha256" "$OUT"

# 2. Scaffold (buffers, knobs, ramps, pixel art, compositing) from Pattern Lab's own exporter.
( cd "$WEB" && NODE_PATH="$WEB/node_modules" npx tsx --tsconfig ./tsconfig.json "$HERE/gen/hgen.ts" scaffold "$PROJECT" "$OUT" )

# 3. Units: L1 = hand template + dab table baked from the Cloud layer's setup(); L2 = hand.
node "$HERE/gen/dabs.js" "$OUT/layer_Cloud.js" > "$OUT/dabs.inc"
node "$HERE/gen/splice.js" unit "$UNITS/L1.tpl" "$OUT/dabs.inc" "$OUT/units/L1.cpp"
cp "$UNITS/L2.cpp" "$OUT/units/L2.cpp"
cp "$OUT/scaffold.h" "$OUT/units/scaffold.h"

# 4. Splice the units between the scaffold's >>> markers, apply the exhibit speedups,
#    add the author block and the community footer.
( cd "$WEB" && NODE_PATH="$WEB/node_modules" npx tsx --tsconfig ./tsconfig.json "$HERE/gen/hgen.ts" assemble "$OUT/units" "$OUT/assembled.h" )
node "$HERE/gen/patch_scaffold.js" "$OUT/assembled.h"
node "$HERE/gen/splice.js" finish "$OUT/assembled.h" "$UNITS/author.txt" "$UNITS/footer.txt" "$DEST"
echo "wrote $DEST"

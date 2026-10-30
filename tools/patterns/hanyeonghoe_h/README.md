# HANYEONGHOE header generator

Rebuilds `patterns/3_hanyeonghoe/hanyeonghoe.h`
from `patterns/3_hanyeonghoe/hanyeonghoe.project.json`, the Pattern Lab project beside it. Nothing is
read from a scratch copy. Each layer's JavaScript is pulled out of the JSON on every
run.

```
tools/patterns/hanyeonghoe_h/build.sh            # write the clone's hanyeonghoe.h
tools/patterns/hanyeonghoe_h/build.sh <out.h>    # write elsewhere (diff before replacing)
tools/patterns/hanyeonghoe_h/build.sh --hashes   # print the JSON's current layer hashes
```

Needs Git Bash, node, and the main repo's `web/node_modules`, which provides `tsx`
and the Pattern Lab exporter (`web/src/lib/lab/hExport.ts`, `serialize.ts`). The
script finds `../랜딩페이지_opensorce/web` on its own; set `PF_WEB` to use another
folder. The main repo is only read. Intermediate files go to `out/`, which git
ignores.

After a rebuild, prove it with the host harness:

```
python tools/patterns/host/pfhost.py current hanyeonghoe
python tools/patterns/check_sync.py --accept hanyeonghoe   # once it says OK
```

## What is generated and what is hand-made

| Part of the header | Source | Kind |
|---|---|---|
| Scaffold: buffers, shared knobs (labels, ranges, steps), ramp LUTs, the `Pixel 1` art, masks, blending, compositing, `>>>` markers | `hgen.ts scaffold` → Pattern Lab's `buildHExport` on the project JSON | generated |
| Exhibit speedups (the `alpha255` table, the 66-row memset of L1) | `gen/patch_scaffold.js`, exact string edits that must each match once | hand-written patch, applied mechanically |
| `L1` "Cloud": `DABS` table | `gen/dabs.js` runs the Cloud layer's own `setup()` from the JSON in node | generated |
| `L1` "Cloud": everything else, including the `CLOUDS` table | `units/L1.tpl` (the table goes in at `@@DABS@@`) | **hand translation** |
| `L2` "flower": all of it | `units/L2.cpp` | **hand translation** |
| Author block, community footer | `units/author.txt`, `units/footer.txt` | hand-made text |

The build runs in this order: extract and check the layer JS, write the scaffold,
bake the dabs into L1, splice L1 and L2 into the scaffold (`hgen.ts assemble`),
apply the speedup patch, then add the author block and the footer.

## When a layer's JS changes

`units/sources.sha256` holds the sha256 of each code layer's JS (the layer's `code`
string, UTF-8) that the hand units were translated from. `gen/extract.js` compares
it with the JSON on every run. If a layer changed, was renamed or was added, the
build **stops** before it writes anything and names the unit that is stale. That
keeps a header built from new JSON from carrying an old hand translation.

To bring a unit up to date:

1. Diff the old and new layer JS. `out/layer_<name>.js` holds the new code, and git
   history of the project JSON holds the old.
2. Change `units/L1.tpl` or `units/L2.cpp` to match. Keep float math, `f`-suffixed
   literals and the JS order of operations. In L1, anything in `setup()` that feeds
   the dabs is baked by `dabs.js` and needs no hand change. A change to the six
   `parentClouds` must be copied into `CLOUDS` by hand.
3. Put the new hash from `build.sh --hashes` into `units/sources.sha256`.
4. Rebuild, then run the harness parity set above.

Changes to ramps, pixel art, knob ranges or labels, opacity, or blend modes need no
hand work: the scaffold comes from the JSON on every run. If a newer main-repo
exporter changes the scaffold text, `patch_scaffold.js` fails loudly on the anchor
that no longer matches.

The author block's `Date:` is set by hand in `units/author.txt`.

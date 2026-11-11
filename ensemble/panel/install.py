#!/usr/bin/env python3
"""Put the ensemble feature into a Patternflow checkout.

  python ensemble/panel/install.py <path to a Patternflow checkout>
  python ensemble/panel/install.py <checkout> --remove

It only adds files, in two places the firmware keeps for exactly this:

  firmware/patternflow/features/ensemble/   the feature, with the three
                                            headers of ensemble/core/ it runs
  firmware/bundles/ensemble/                the two files that name the build

No core file is touched, so the checkout can move to a newer Patternflow tag
afterwards and this is run again. Then, in the checkout (Git Bash on Windows):

  ./firmware/bundles/build.sh ensemble
"""
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CORE = HERE.parent / "core"
# What a panel runs of the core: the bytes, and its own side of them.
CORE_HEADERS = ("pfe_util.h", "pfe_proto.h", "pfe_follower.h")


def main() -> int:
    args = [a for a in sys.argv[1:] if a != "--remove"]
    remove = "--remove" in sys.argv[1:]
    if len(args) != 1:
        print(__doc__)
        return 2
    checkout = Path(args[0]).resolve()
    sketch = checkout / "firmware" / "patternflow"
    if not (sketch / "features" / "pf_feature.h").exists() or not (checkout / "firmware" / "bundles" / "build.sh").exists():
        print(f"{checkout} does not look like a Patternflow checkout "
              "(no firmware/patternflow/features/pf_feature.h or firmware/bundles/build.sh)")
        return 1
    feature = sketch / "features" / "ensemble"
    bundle = checkout / "firmware" / "bundles" / "ensemble"
    if remove:
        for d in (feature, bundle):
            if d.exists():
                shutil.rmtree(d)
                print(f"removed {d.relative_to(checkout)}")
        return 0

    for d in (feature, bundle):
        d.mkdir(parents=True, exist_ok=True)
    copied = []
    for src in sorted((HERE / "features" / "ensemble").glob("*.h")):
        shutil.copy(src, feature / src.name)
        copied.append(feature / src.name)
    for name in CORE_HEADERS:
        shutil.copy(CORE / name, feature / name)
        copied.append(feature / name)
    for src in sorted((HERE / "bundles" / "ensemble").glob("*.h")):
        shutil.copy(src, bundle / src.name)
        copied.append(bundle / src.name)
    for path in copied:
        print(f"  {path.relative_to(checkout)}")
    print(f"\n{len(copied)} files in {checkout}")
    if not (sketch / "patternflow_secrets.h").exists():
        print("\nNo firmware/patternflow/patternflow_secrets.h there. With PF_WIFI_SSID / PF_WIFI_PASS set in it to")
        print("the hub's network, every panel flashed from this build joins the hub by itself.")
    print("\nNext, in the checkout:  ./firmware/bundles/build.sh ensemble")
    return 0


if __name__ == "__main__":
    sys.exit(main())

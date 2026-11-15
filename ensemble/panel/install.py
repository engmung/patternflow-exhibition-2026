#!/usr/bin/env python3
"""Put the ensemble feature into a Patternflow checkout.

  python ensemble/panel/install.py <path to a Patternflow checkout>
  python ensemble/panel/install.py <checkout> --remove

It adds files, in two places the firmware keeps for exactly this:

  firmware/patternflow/features/ensemble/   the feature, with the three
                                            headers of ensemble/core/ it runs
  firmware/bundles/ensemble/                the two files that name the build

and it edits two lines of the show player (features/show), which the bundle
carries: the sizes of its two tables become something a bundle can set
(SHOW_EDITS below says why). --remove puts those lines back.

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

# The show player keeps room for a hundred shows and two playlists of 64 at
# all times: 12.9 KB of a panel's memory, taken out of the one piece a
# pattern module has to fit in (57 KB without the player, 45 KB with it,
# measured 2026-11-15). A panel of an ensemble holds a handful of shows. The
# sizes were fixed numbers; these edits make them something a bundle sets
# (bundles/ensemble/overrides.h) and leave them what they were for every
# other build. (file under features/show, the line as it is, the line as it becomes)
SHOW_EDITS = (
    ("core_show.h",
     "constexpr uint8_t PLAYLIST_MAX = 64;\n",
     "#ifndef PF_SHOW_PLAYLIST_MAX\n#define PF_SHOW_PLAYLIST_MAX 64\n#endif\n"
     "constexpr uint8_t PLAYLIST_MAX = PF_SHOW_PLAYLIST_MAX;\n"),
    ("core_show_http.h",
     "constexpr uint8_t SHOW_CACHE_MAX = 100;\n",
     "#ifndef PF_SHOW_CACHE_MAX\n#define PF_SHOW_CACHE_MAX 100\n#endif\n"
     "constexpr uint8_t SHOW_CACHE_MAX = PF_SHOW_CACHE_MAX;\n"),
)


def edit_show(sketch, undo):
    """Make (or take back) SHOW_EDITS. Returns what was done, and what could not be."""
    done, problems = [], []
    for name, before, after in SHOW_EDITS:
        path = sketch / "features" / "show" / name
        if not path.is_file():
            problems.append(f"{path} is not there: this checkout has no show player")
            continue
        raw = path.read_bytes().decode("utf-8")
        newline = "\r\n" if "\r\n" in raw else "\n"
        text = raw.replace("\r\n", "\n")
        have, want = (after, before) if undo else (before, after)
        if want in text and have not in text:
            continue                              # already so
        if text.count(have) != 1:
            problems.append(f"{path}: the line to change is not there as expected "
                            "(another Patternflow version?). Nothing was changed in this file")
            continue
        path.write_bytes(text.replace(have, want).replace("\n", newline).encode("utf-8"))
        done.append(path)
    return done, problems


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
        done, problems = edit_show(sketch, undo=True)
        for path in done:
            print(f"put back {path.relative_to(checkout)}")
        for problem in problems:
            print(problem)
        return 1 if problems else 0

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
    done, problems = edit_show(sketch, undo=False)
    for path in done:
        print(f"edited {path.relative_to(checkout)}  (the show player's table sizes: SHOW_EDITS)")
    if problems:
        print("\nThe show player could not be prepared, and the bundle will not build without it:")
        for problem in problems:
            print("  " + problem)
        return 1
    if not (sketch / "patternflow_secrets.h").exists():
        print("\nNo firmware/patternflow/patternflow_secrets.h there. With PF_WIFI_SSID / PF_WIFI_PASS set in it to")
        print("the hub's network, every panel flashed from this build joins the hub by itself.")
    print("\nNext, in the checkout:  ./firmware/bundles/build.sh ensemble")
    return 0


if __name__ == "__main__":
    sys.exit(main())

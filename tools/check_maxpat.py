#!/usr/bin/env python3
"""Structural check for a Max patcher (.maxpat) file.

json.load only proves the file parses. This also checks what Max itself
would trip over when the patch is opened or pasted into a device:
  - every box has a unique id and the fields Max expects
  - every patchline points at boxes that exist, through an outlet/inlet
    index the box actually has
  - live.* objects carry a parameter longname, and longnames are unique
  - presentation rectangles fit a Max for Live device (169 px tall)
  - a js box names a file that exists next to the patch, and the js
    declares the same number of outlets the box has
  - objects whose inlet/outlet counts are fixed by their class and
    arguments (midiin, midiout, gate N, t b b b, prepend, live.text ...)
    declare exactly those counts: a patchline to an inlet the real object
    does not have is dropped silently when Max opens the patch
  - live.* parameters: the initial value lies in the range, a toggle's
    enum has two entries, and a momentary live.text button is a hidden
    parameter with no initial value (a stored press must not replay at load)

Usage: python tools/check_maxpat.py ableton/pf-exhibit-scenes/pf_exhibit_scenes.maxpat
Exit code 0 = all checks passed.
"""

import argparse
import json
import os
import re
import sys

M4L_DEVICE_HEIGHT = 169.0

# (inlets, outlets) of objects whose shape does not depend on their contents.
FIXED_SHAPES = {
    "comment": (1, 0),
    "message": (2, 1),
    "live.toggle": (1, 1),
    "live.numbox": (1, 2),
    "live.text": (1, 2),
}
NEWOBJ_FIXED_SHAPES = {
    "midiin": (1, 1),
    "midiout": (1, 0),
    "deferlow": (1, 1),
    "defer": (1, 1),
    "live.thisdevice": (1, 3),
    "prepend": (1, 1),
}

PARAM_INVISIBLE_HIDDEN = 2


def expected_shape(box):
    """(inlets, outlets) the object will really have, or None if not known here."""
    cls = box.get("maxclass", "")
    if cls in FIXED_SHAPES:
        return FIXED_SHAPES[cls]
    if cls != "newobj":
        return None
    words = str(box.get("text", "")).split()
    if not words:
        return None
    name, args = words[0], words[1:]
    if name in NEWOBJ_FIXED_SHAPES:
        return NEWOBJ_FIXED_SHAPES[name]
    if name in ("t", "trigger"):
        return (1, max(1, len(args)))
    if name == "gate":
        n = int(args[0]) if args and re.fullmatch(r"\d+", args[0]) else 1
        return (2, max(1, n))
    return None


def check_parameter(bid, box, errors):
    vo = box.get("saved_attribute_attributes", {}).get("valueof", {})
    cls = box.get("maxclass", "")
    initial = vo.get("parameter_initial")
    if vo.get("parameter_initial_enable"):
        if not isinstance(initial, list) or len(initial) != 1:
            errors.append("%s: parameter_initial_enable without a one-value parameter_initial" % bid)
            initial = None
    if cls == "live.numbox":
        lo, hi = vo.get("parameter_mmin", 0.0), vo.get("parameter_mmax", 127.0)
        if lo > hi:
            errors.append("%s: parameter range %s..%s is empty" % (bid, lo, hi))
        if vo.get("parameter_initial_enable") and initial and not (lo <= initial[0] <= hi):
            errors.append("%s: initial %s outside %s..%s" % (bid, initial[0], lo, hi))
    if cls in ("live.toggle", "live.text"):
        if len(vo.get("parameter_enum", [])) != 2:
            errors.append("%s: %s needs a two-entry parameter_enum" % (bid, cls))
        if vo.get("parameter_initial_enable") and initial and initial[0] not in (0, 1):
            errors.append("%s: %s initial %s is not 0 or 1" % (bid, cls, initial[0]))
    if cls == "live.text" and box.get("mode", 1) == 0:
        if vo.get("parameter_invisible") != PARAM_INVISIBLE_HIDDEN:
            errors.append("%s: momentary live.text should be a hidden parameter (parameter_invisible 2)" % bid)
        if vo.get("parameter_initial_enable"):
            errors.append("%s: momentary live.text must not have an initial value" % bid)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("maxpat", help="path to the .maxpat file")
    args = ap.parse_args()

    errors = []
    with open(args.maxpat, "r", encoding="utf-8") as f:
        doc = json.load(f)

    patcher = doc.get("patcher")
    if not isinstance(patcher, dict):
        print("FAIL: no top-level 'patcher' object")
        return 1

    boxes = {}
    longnames = {}
    for entry in patcher.get("boxes", []):
        box = entry.get("box", {})
        bid = box.get("id")
        if not bid:
            errors.append("box without id: %r" % box)
            continue
        if bid in boxes:
            errors.append("duplicate box id %s" % bid)
        boxes[bid] = box
        for key in ("maxclass", "numinlets", "numoutlets", "patching_rect"):
            if key not in box:
                errors.append("%s: missing %s" % (bid, key))
        outs = box.get("numoutlets", 0)
        if outs and len(box.get("outlettype", [])) != outs:
            errors.append("%s: outlettype has %d entries, numoutlets is %d"
                          % (bid, len(box.get("outlettype", [])), outs))
        if box.get("maxclass") == "newobj" and not box.get("text"):
            errors.append("%s: newobj without text" % bid)
        shape = expected_shape(box)
        if shape and (box.get("numinlets"), box.get("numoutlets")) != shape:
            errors.append("%s: %s declares %s inlets / %s outlets, the object has %d / %d"
                          % (bid, box.get("text") or box.get("maxclass"), box.get("numinlets"),
                             box.get("numoutlets"), shape[0], shape[1]))

        cls = box.get("maxclass", "")
        if cls.startswith("live.") and box.get("parameter_enable"):
            vo = box.get("saved_attribute_attributes", {}).get("valueof", {})
            ln = vo.get("parameter_longname")
            if not ln:
                errors.append("%s: %s without parameter_longname" % (bid, cls))
            elif ln in longnames:
                errors.append("%s: parameter_longname %r also used by %s"
                              % (bid, ln, longnames[ln]))
            else:
                longnames[ln] = bid
            if box.get("varname") and box.get("varname") != ln:
                errors.append("%s: varname %r differs from longname %r (getnamed lookups use varname)"
                              % (bid, box.get("varname"), ln))
            check_parameter(bid, box, errors)

        if box.get("presentation"):
            rect = box.get("presentation_rect")
            if not rect or len(rect) != 4:
                errors.append("%s: presentation without presentation_rect" % bid)
            elif rect[1] + rect[3] > M4L_DEVICE_HEIGHT:
                errors.append("%s: presentation rect bottom %.0f exceeds device height %.0f"
                              % (bid, rect[1] + rect[3], M4L_DEVICE_HEIGHT))

        if cls == "newobj" and str(box.get("text", "")).startswith("js "):
            jsname = box["text"].split()[1]
            jspath = os.path.join(os.path.dirname(os.path.abspath(args.maxpat)), jsname)
            if not os.path.isfile(jspath):
                errors.append("%s: %s not found next to the patch" % (bid, jsname))
            else:
                with open(jspath, "r", encoding="utf-8") as jf:
                    src = jf.read()
                m = re.search(r"^\s*outlets\s*=\s*(\d+)\s*;", src, re.M)
                if not m:
                    errors.append("%s: %s does not declare outlets" % (bid, jsname))
                elif int(m.group(1)) != outs:
                    errors.append("%s: box has %d outlets, %s declares %s"
                                  % (bid, outs, jsname, m.group(1)))
                m = re.search(r"^\s*inlets\s*=\s*(\d+)\s*;", src, re.M)
                if m and int(m.group(1)) != box.get("numinlets"):
                    errors.append("%s: box has %d inlets, %s declares %s"
                                  % (bid, box.get("numinlets"), jsname, m.group(1)))

    lines = patcher.get("lines", [])
    seen = set()
    for entry in lines:
        pl = entry.get("patchline", {})
        src = pl.get("source", [None, None])
        dst = pl.get("destination", [None, None])
        key = (src[0], src[1], dst[0], dst[1])
        if key in seen:
            errors.append("duplicate patchline %r" % (key,))
        seen.add(key)
        if src[0] not in boxes:
            errors.append("patchline from unknown box %s" % src[0])
            continue
        if dst[0] not in boxes:
            errors.append("patchline to unknown box %s" % dst[0])
            continue
        if not (0 <= src[1] < boxes[src[0]].get("numoutlets", 0)):
            errors.append("patchline %s outlet %s: box has %s outlets"
                          % (src[0], src[1], boxes[src[0]].get("numoutlets", 0)))
        if not (0 <= dst[1] < boxes[dst[0]].get("numinlets", 0)):
            errors.append("patchline %s inlet %s: box has %s inlets"
                          % (dst[0], dst[1], boxes[dst[0]].get("numinlets", 0)))

    if errors:
        for e in errors:
            print("FAIL:", e)
        return 1
    print("OK: %d boxes, %d patchlines, %d live parameters (%s)"
          % (len(boxes), len(lines), len(longnames), ", ".join(sorted(longnames))))
    return 0


if __name__ == "__main__":
    sys.exit(main())

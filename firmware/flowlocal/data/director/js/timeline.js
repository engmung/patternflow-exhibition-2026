/**
 * Timeline UI: fixed label gutter + scrollable tracks.
 * Param cues render one chip per param lane (shared t).
 * v2 eased param cues get an "ease" class (lerp to next).
 */
import { durationOf, stateAt, quantizeTime, PFST_VERSION_2 } from "./performance.js";

const LANE_KEYS = [
  { id: "pattern", label: "Pattern" },
  { id: "param0", label: "Param 1" },
  { id: "param1", label: "Param 2" },
  { id: "param2", label: "Param 3" },
  { id: "param3", label: "Param 4" },
  { id: "message", label: "Message" }
];

export function createTimeline(root, opts) {
  const onSelect = opts.onSelect || (() => {});
  const onSeek = opts.onSeek || (() => {});
  const onLaneInsert = opts.onLaneInsert || (() => {});
  let perf = opts.performance;
  let pxPerSec = opts.pxPerSec || 40;
  let playheadT = 0;
  let selectedIndex = -1;
  let paramLabels = ["Param 1", "Param 2", "Param 3", "Param 4"];

  root.innerHTML = "";
  const wrap = document.createElement("div");
  wrap.className = "tl-wrap";

  const gutter = document.createElement("div");
  gutter.className = "tl-gutter";
  const gutterRuler = document.createElement("div");
  gutterRuler.className = "tl-gutter-cell tl-gutter-ruler";
  gutterRuler.textContent = "t";
  gutter.appendChild(gutterRuler);
  const gutterCells = {};
  for (const lane of LANE_KEYS) {
    const cell = document.createElement("div");
    cell.className = "tl-gutter-cell";
    cell.dataset.lane = lane.id;
    cell.textContent = lane.label;
    gutter.appendChild(cell);
    gutterCells[lane.id] = cell;
  }

  const scroll = document.createElement("div");
  scroll.className = "tl-scroll";
  const inner = document.createElement("div");
  inner.className = "tl-inner";
  scroll.appendChild(inner);

  wrap.appendChild(gutter);
  wrap.appendChild(scroll);
  root.appendChild(wrap);

  function q(t) {
    return quantizeTime(t, perf.version);
  }

  function laneLabelText(lane) {
    if (lane.id.startsWith("param")) {
      const idx = Number(lane.id.slice(5));
      return paramLabels[idx] || lane.label;
    }
    return lane.label;
  }

  function refreshGutterLabels() {
    for (const lane of LANE_KEYS) {
      const cell = gutterCells[lane.id];
      if (cell) cell.textContent = laneLabelText(lane);
    }
  }

  function widthFor() {
    return Math.max(640, Math.ceil(durationOf(perf) * pxPerSec) + 80);
  }

  function cueClass(cue) {
    let kind = "";
    if (cue.pattern != null && (cue.param || cue.message != null)) {
      kind = "pattern";
    } else if (cue.pattern != null) kind = "pattern";
    else if (cue.message != null) kind = "message";
    else if (cue.param) kind = "param";
    if (cue.ease && cue.param) kind += " ease";
    return kind;
  }

  function formatCueTime(t) {
    if (perf.version === PFST_VERSION_2) {
      return (Math.round(t * 10) / 10).toFixed(1) + "s";
    }
    return Math.round(t) + "s";
  }

  /** @returns {{ lane: string, text: string, title: string }[]} */
  function chipSpecs(cue) {
    const specs = [];
    if (cue.pattern != null) {
      specs.push({
        lane: "pattern",
        text: cue.pattern,
        title: "pattern @ " + formatCueTime(cue.t)
      });
    }
    if (cue.message != null) {
      specs.push({
        lane: "message",
        text: cue.message === "" ? "(clear)" : cue.message,
        title: "message @ " + formatCueTime(cue.t)
      });
    }
    if (cue.param) {
      for (let i = 0; i < 4; i++) {
        if (cue.param[i] == null) continue;
        const name = paramLabels[i] || "P" + (i + 1);
        const easeMark = cue.ease ? " ~" : "";
        specs.push({
          lane: "param" + i,
          text: String(cue.param[i]) + easeMark,
          title:
            name +
            " = " +
            cue.param[i] +
            " @ " +
            formatCueTime(cue.t) +
            (cue.ease ? " (ease to next)" : "")
        });
      }
    }
    return specs;
  }

  function timeFromClientX(clientX) {
    const rect = inner.getBoundingClientRect();
    const x = clientX - rect.left + scroll.scrollLeft;
    return q(x / pxPerSec);
  }

  function bindChipDrag(chip, cue, index, laneId) {
    let dragging = false;
    let startX = 0;
    let startT = 0;

    function chipsForCue() {
      return inner.querySelectorAll('.tl-cue[data-index="' + index + '"]');
    }

    chip.addEventListener("pointerdown", (ev) => {
      dragging = true;
      startX = ev.clientX;
      startT = cue.t;
      chip.setPointerCapture(ev.pointerId);
      chip.style.cursor = "grabbing";
      ev.stopPropagation();
    });
    chip.addEventListener("pointermove", (ev) => {
      if (!dragging) return;
      const dt = (ev.clientX - startX) / pxPerSec;
      const nt = Math.min(durationOf(perf), q(startT + dt));
      cue.t = nt;
      const left = nt * pxPerSec + "px";
      chipsForCue().forEach((c) => {
        c.style.left = left;
      });
    });
    chip.addEventListener("pointerup", () => {
      if (!dragging) return;
      dragging = false;
      chip.style.cursor = "grab";
      perf.timeline.sort((a, b) => a.t - b.t);
      selectedIndex = perf.timeline.indexOf(cue);
      onSelect(selectedIndex, cue, { lane: laneId });
      render();
    });
    chip.addEventListener("click", (ev) => {
      ev.stopPropagation();
      selectedIndex = index;
      onSelect(index, cue, { lane: laneId });
      render();
    });
  }

  function render() {
    refreshGutterLabels();
    const w = widthFor();
    inner.style.width = w + "px";
    inner.innerHTML = "";

    const ruler = document.createElement("div");
    ruler.className = "tl-ruler";
    const dur = durationOf(perf);
    const v2 = perf.version === PFST_VERSION_2;
    const majorEvery = v2 && pxPerSec >= 60 ? 1 : 5;
    const step = v2 && pxPerSec >= 80 ? 0.1 : 1;
    for (let s = 0; s <= dur + 1e-9; s = Math.round((s + step) * 10) / 10) {
      if (step >= 1 && s % 5 !== 0 && pxPerSec < 30) continue;
      if (step < 1 && Math.round(s * 10) % 10 !== 0 && pxPerSec < 100) continue;
      const tick = document.createElement("div");
      tick.className = "tick";
      tick.style.left = s * pxPerSec + "px";
      const showLabel =
        step < 1
          ? Math.round(s * 10) % 10 === 0 && Math.round(s) % majorEvery === 0
          : s % majorEvery === 0;
      tick.textContent = showLabel ? String(Math.round(s * 10) / 10) + "s" : "";
      ruler.appendChild(tick);
    }
    ruler.style.cursor = "ew-resize";
    ruler.title = "Click to seek";
    ruler.addEventListener("click", (ev) => {
      ev.stopPropagation();
      const t = Math.min(durationOf(perf), timeFromClientX(ev.clientX));
      playheadT = t;
      onSeek(t);
      render();
    });
    inner.appendChild(ruler);

    const laneEls = {};
    for (const lane of LANE_KEYS) {
      const el = document.createElement("div");
      el.className = "tl-lane";
      el.dataset.lane = lane.id;
      el.title = "Click to insert a cue on this lane at this time";
      el.addEventListener("click", (ev) => {
        if (ev.target.closest(".tl-cue")) return;
        ev.stopPropagation();
        const t = Math.min(durationOf(perf), timeFromClientX(ev.clientX));
        playheadT = t;
        onSeek(t);
        onLaneInsert(lane.id, t);
        render();
      });
      inner.appendChild(el);
      laneEls[lane.id] = el;
    }

    const cues = perf.timeline || [];
    cues.forEach((cue, index) => {
      const specs = chipSpecs(cue);
      specs.forEach((spec) => {
        const lane = laneEls[spec.lane];
        if (!lane) return;
        const chip = document.createElement("div");
        chip.className =
          "tl-cue " +
          cueClass(cue) +
          (index === selectedIndex ? " selected" : "");
        chip.style.left = cue.t * pxPerSec + "px";
        chip.textContent = spec.text;
        chip.title = spec.title;
        chip.dataset.index = String(index);
        chip.dataset.lane = spec.lane;
        bindChipDrag(chip, cue, index, spec.lane);
        lane.appendChild(chip);
      });
    });

    const head = document.createElement("div");
    head.className = "tl-playhead";
    head.style.left = playheadT * pxPerSec + "px";
    inner.appendChild(head);
  }

  function setParamLabels(labels) {
    if (!Array.isArray(labels) || labels.length < 4) return;
    let changed = false;
    for (let i = 0; i < 4; i++) {
      if (paramLabels[i] !== labels[i]) {
        changed = true;
        break;
      }
    }
    if (!changed) return;
    paramLabels = labels.slice(0, 4);
    refreshGutterLabels();
    render();
  }

  function setPerformance(p, keepSelection) {
    perf = p;
    if (!keepSelection) selectedIndex = -1;
    render();
  }

  function setPlayhead(t) {
    playheadT = Math.max(0, t);
    const head = inner.querySelector(".tl-playhead");
    if (head) head.style.left = playheadT * pxPerSec + "px";
    else render();
  }

  function setPxPerSec(v) {
    pxPerSec = v;
    render();
  }

  function getSelectedIndex() {
    return selectedIndex;
  }

  function selectIndex(i) {
    selectedIndex = i;
    render();
  }

  refreshGutterLabels();
  render();

  return {
    setPerformance,
    setPlayhead,
    setPxPerSec,
    setParamLabels,
    getSelectedIndex,
    selectIndex,
    render,
    stateAt: (t) => stateAt(perf, t)
  };
}

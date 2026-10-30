/**
 * Patternflow Director — boot, tabs, transport, file IO.
 */
import { createMqttController } from "./mqtt.js";
import {
  emptyPerformance,
  durationOf,
  stateAt,
  clamp1000,
  quantizeTime,
  clampToDuration,
  mergeParamSparse,
  parseParamField,
  compareCues,
  formatParamSparse,
  timelinePatternNames,
  PFST_VERSION_2,
  upgradePerformanceToV2,
  compressStaircaseToEase
} from "./performance.js";
import { createTimeline } from "./timeline.js";
import { initBrokerGuide } from "./broker-guide.js";
import { downloadShowTable, openPerformanceFile, saveShowToLibrary, openShowFromLibrary, showTableFilename } from "./show-table.js";
import { initLaunchpad } from "./launchpad.js";
import { paramLabelsFor, paramEffectsFor, searchPatterns, canonicalPatternName, patternSlugFor } from "./patterns-meta.js";
import {
  isFlowLocalHost,
  listLibrary,
  panelPullLibrary,
  getPanelHttpBase,
  setPanelHttpBase,
  slugifyName
} from "./library.js";

const mqtt = createMqttController();
let perf = emptyPerformance();
let playing = false;
let playStartedWall = 0;
let playStartedT = 0;
let raf = 0;
let playheadT = 0;
let selectedLane = null; // 'param0'..'param3' | 'pattern' | 'message' | null
let stripSilent = false;
let lastPub = { pattern: null, message: null, param: [null, null, null, null] };
let lastInventory = [];
let launchPad = null;
let performanceMode = false;

const $ = (id) => document.getElementById(id);

function showView(name) {
  // Outside Performance mode every tab works. In Performance mode the tab
  // row is hidden; only Launch (and Connect when offline) is allowed.
  if (performanceMode && name !== "launch" && name !== "connect") {
    name = "launch";
  }
  document.querySelectorAll("nav.tabs button").forEach((b) => {
    b.classList.toggle("on", b.dataset.view === name);
  });
  document.querySelectorAll(".view").forEach((v) => {
    v.classList.toggle("on", v.id === "view-" + name);
  });
}

function exitPerformanceMode(opts) {
  const goConnect = !!(opts && opts.connect);
  if (performanceMode) {
    setPerformanceMode(false);
    try {
      localStorage.setItem("pf-director-performance-mode", "0");
    } catch (_) {}
  }
  if (goConnect) showView("connect");
}

function setConnUi(state) {
  const dot = $("connDot");
  const label = $("connLabel");
  const st = $("connectStatus");
  const banner = $("offlineBanner");
  if (!dot || !label) return;
  dot.classList.remove("on", "bad");
  if (state.kind === "ok") dot.classList.add("on");
  else if (state.kind === "err") dot.classList.add("bad");
  label.textContent = state.message || "";
  if (st) {
    st.textContent = state.message || "";
    st.className = "status " + (state.kind === "ok" ? "ok" : state.kind === "warn" ? "warn" : "err");
  }
  const offline = state.kind !== "ok";
  document.body.classList.toggle("mqtt-offline", offline);
  if (banner) {
    banner.hidden = !offline;
  }
}

mqtt.setStatus((state) => {
  setConnUi(state);
});

function setPerformanceMode(on) {
  performanceMode = !!on;
  document.body.classList.toggle("perf-mode", performanceMode);
  const btn = $("btnPerfMode");
  if (btn) {
    btn.classList.toggle("on", performanceMode);
    btn.textContent = performanceMode ? "Authoring" : "Performance";
  }
  const note = document.querySelector(".lp-perf-note");
  if (note) note.hidden = !performanceMode;
  if (launchPad && launchPad.setPerformanceMode) {
    launchPad.setPerformanceMode(performanceMode);
  }
  if (performanceMode) showView("launch");
}

function formatTime(t) {
  const s = Math.max(0, t);
  const m = Math.floor(s / 60);
  const r = s - m * 60;
  return m + ":" + r.toFixed(2).padStart(5, "0");
}

function applyMetaFromForm() {
  perf.title = $("perfTitle").value.trim() || "Untitled";
  perf.id = $("perfId").value.trim() || perf.id;
  perf.loop = $("perfLoop").checked;
  perf.utcStart = $("perfUtc").value.trim();
  perf.channel = Number($("perfChannel").value) || 0;
  const rawLen = Number($("perfLength").value) || 60;
  perf.length =
    perf.version === PFST_VERSION_2
      ? Math.max(0.1, Math.round(rawLen * 10) / 10)
      : Math.max(1, Math.round(rawLen));
  $("perfLength").value = String(perf.length);
  refreshFormatBadge();
}

function refreshFormatBadge() {
  const el = $("perfFormat");
  if (!el) return;
  el.textContent =
    perf.version === PFST_VERSION_2
      ? "PFST v2 · 0.1 s · ease"
      : "PFST v1 · whole seconds";
  el.title =
    perf.version === PFST_VERSION_2
      ? "Exports version byte 2 (deciseconds + EASE). Needs a v2 panel player."
      : "Exports version byte 1. Upgrade on the File tab for smooth param ramps.";
}

function fillMetaForm() {
  $("perfTitle").value = perf.title || "";
  $("perfId").value = perf.id || "";
  $("perfLoop").checked = !!perf.loop;
  $("perfUtc").value = perf.utcStart || "";
  $("perfChannel").value = String(perf.channel || 0);
  $("perfLength").value = String(perf.length || 60);
  const lenEl = $("perfLength");
  if (lenEl) {
    lenEl.step = perf.version === PFST_VERSION_2 ? "0.1" : "1";
    lenEl.min = perf.version === PFST_VERSION_2 ? "0.1" : "1";
  }
  const cueT = $("cueT");
  if (cueT) {
    cueT.step = perf.version === PFST_VERSION_2 ? "0.1" : "1";
  }
  const easeRow = $("cueEaseRow");
  if (easeRow) {
    easeRow.style.display = perf.version === PFST_VERSION_2 ? "" : "none";
  }
  refreshFormatBadge();
  mergeTimelinePatternsIntoRequired();
  renderRequiredChips();
  refreshPatternSelects();
  refreshUsedNote();
}

function qTime(t) {
  return quantizeTime(t, perf.version);
}

function cueMergeEps() {
  return perf.version === PFST_VERSION_2 ? 0.05 : 0.5;
}

function defaultEaseOn() {
  const el = $("cueEase");
  return perf.version === PFST_VERSION_2 && el && el.checked;
}

function mergeTimelinePatternsIntoRequired() {
  if (!Array.isArray(perf.required)) perf.required = [];
  (perf.timeline || []).forEach((c) => {
    const name = canonicalPatternName(c.pattern);
    if (!name) return;
    if (!perf.required.some((n) => canonicalPatternName(n).toLowerCase() === name.toLowerCase())) {
      perf.required.push(name);
    }
  });
}

function requiredNames() {
  const seen = Object.create(null);
  const out = [];
  for (const raw of perf.required || []) {
    const name = canonicalPatternName(raw);
    if (!name || seen[name.toLowerCase()]) continue;
    seen[name.toLowerCase()] = true;
    out.push(name);
  }
  return out;
}

function fillPatternSelect(el, current) {
  if (!el) return;
  const names = requiredNames();
  const cur = canonicalPatternName(current) || names[0] || "Origin";
  if (cur && !names.some((n) => n.toLowerCase() === cur.toLowerCase())) {
    names.unshift(cur);
  }
  if (!names.length) names.push("Origin");
  const prevSilent = stripSilent;
  stripSilent = true;
  el.innerHTML = names
    .map(
      (n) =>
        '<option value="' +
        escapeHtml(n) +
        '">' +
        escapeHtml(n) +
        "</option>"
    )
    .join("");
  el.value = names.find((n) => n.toLowerCase() === cur.toLowerCase()) || names[0];
  stripSilent = prevSilent;
}

function refreshPatternSelects() {
  fillPatternSelect($("livePattern"), $("livePattern").value || lastLabelPattern);
  fillPatternSelect($("cuePattern"), $("cuePattern").value || lastLabelPattern);
}

function renderRequiredChips() {
  const box = $("reqChips");
  if (!box) return;
  const names = requiredNames();
  perf.required = names.slice();
  box.innerHTML = "";
  if (!names.length) {
    const hint = document.createElement("span");
    hint.className = "empty-hint";
    hint.textContent = "None yet — search the catalog and Add.";
    box.appendChild(hint);
    return;
  }
  names.forEach((name) => {
    const chip = document.createElement("span");
    const current = canonicalPatternName($("livePattern").value);
    chip.className = "req-chip" + (current.toLowerCase() === name.toLowerCase() ? " on" : "");
    chip.title = "Preview on the panel";
    chip.appendChild(document.createTextNode(name));
    chip.addEventListener("click", () => previewRequiredPattern(name));
    const btn = document.createElement("button");
    btn.type = "button";
    btn.setAttribute("aria-label", "Remove " + name);
    btn.textContent = "×";
    btn.addEventListener("click", (ev) => {
      ev.stopPropagation();
      removeRequiredPattern(name);
    });
    chip.appendChild(btn);
    box.appendChild(chip);
  });
}

function previewRequiredPattern(name) {
  const v = canonicalPatternName(name);
  if (!v) return;
  stripSilent = true;
  fillPatternSelect($("livePattern"), v);
  applyParamLabels(v);
  stripSilent = false;
  renderRequiredChips();
  previewAligned();
  const st = $("cueStatus");
  if (st) {
    st.textContent = mqtt.isConnected()
      ? "preview " + v
      : "preview " + v + " (connect MQTT to send to the panel)";
    st.className = "status " + (mqtt.isConnected() ? "ok" : "warn");
  }
}

function addRequiredPattern(raw) {
  const name = canonicalPatternName(raw);
  if (!name) return false;
  if (!Array.isArray(perf.required)) perf.required = [];
  if (perf.required.some((n) => canonicalPatternName(n).toLowerCase() === name.toLowerCase())) {
    return false;
  }
  perf.required.push(name);
  renderRequiredChips();
  refreshPatternSelects();
  return true;
}

function removeRequiredPattern(name) {
  const key = String(name || "").toLowerCase();
  perf.required = (perf.required || []).filter(
    (n) => canonicalPatternName(n).toLowerCase() !== key
  );
  renderRequiredChips();
  refreshPatternSelects();
}

function usedTimelineNames() {
  const seen = Object.create(null);
  const out = [];
  timelinePatternNames(perf).forEach((raw) => {
    const name = canonicalPatternName(raw);
    if (!name || seen[name.toLowerCase()]) return;
    seen[name.toLowerCase()] = true;
    out.push(name);
  });
  return out;
}

function identifierForPattern(name) {
  const key = String(name || "").toLowerCase();
  const fromPanel = lastInventory.find((p) => {
    const n = canonicalPatternName(p && p.name);
    const s = String((p && p.slug) || "").toLowerCase();
    return (n && n.toLowerCase() === key) || s === key;
  });
  if (fromPanel && fromPanel.slug) return fromPanel.slug;
  const catalogSlug = patternSlugFor(name);
  return catalogSlug || name;
}

function refreshUsedNote() {
  const names = usedTimelineNames();
  const text = names.length
    ? "Used in the timeline: " + names.join(", ") + " (" + names.length + ")"
    : "No pattern cues on the timeline yet.";
  ["usedNote", "usedNoteFile"].forEach((id) => {
    const el = $(id);
    if (el) el.textContent = text;
  });
}

function sendStatusEl() {
  return $("fileStatus") || $("cueStatus");
}

function patternFileCandidates(name, libraryFiles) {
  const id = identifierForPattern(name);
  const slug = slugifyName(id || name, "pattern");
  const out = [];
  const files = libraryFiles || [];
  const lowerSlug = slug.toLowerCase();
  const baseOf = (n) => {
    const s = String(n || "");
    const i = s.lastIndexOf("/");
    return (i >= 0 ? s.slice(i + 1) : s).toLowerCase();
  };
  const prefer = (ext) => {
    const want = lowerSlug + "." + ext;
    return (
      files.find((f) => baseOf(f.name) === want && String(f.name).startsWith("128x64/")) ||
      files.find((f) => baseOf(f.name) === want && !String(f.name).includes("/")) ||
      files.find((f) => baseOf(f.name) === want && String(f.name).startsWith("64x64/")) ||
      files.find(
        (f) =>
          String(f.slug || "").toLowerCase() === lowerSlug &&
          String(f.name || "").toLowerCase().endsWith("." + ext)
      )
    );
  };
  const pfm = prefer("pfm");
  if (pfm) out.push(pfm.name);
  else out.push("128x64/" + slug + ".pfm");
  const json = prefer("json");
  if (json) out.push(json.name);
  return out;
}

async function sendUsedPatternsToPanel() {
  const st = sendStatusEl();
  const cueSt = $("cueStatus");
  function note(text, cls) {
    [st, cueSt].forEach((el) => {
      if (!el) return;
      el.textContent = text;
      el.className = "status " + (cls || "");
    });
  }
  const names = usedTimelineNames();
  if (!names.length) {
    note("plant pattern cues on the timeline first", "warn");
    return;
  }
  note("resolving " + names.length + " pattern" + (names.length === 1 ? "" : "s") + " on FlowLocal…");
  let libraryFiles = [];
  try {
    libraryFiles = await listLibrary("patterns");
  } catch (err) {
    note("FlowLocal patterns library unavailable: " + (err.message || err), "err");
    return;
  }
  const toPull = [];
  const seen = Object.create(null);
  names.forEach((n) => {
    patternFileCandidates(n, libraryFiles).forEach((file) => {
      const key = String(file).toLowerCase();
      if (seen[key]) return;
      seen[key] = true;
      toPull.push(file);
    });
  });
  if (!toPull.length) {
    note("no matching .pfm files on FlowLocal", "warn");
    return;
  }
  const panel = getPanelHttpBase();
  note("asking panel (" + panel + ") to pull " + toPull.length + " file" + (toPull.length === 1 ? "" : "s") + "…");
  try {
    const result = await panelPullLibrary("patterns", toPull);
    const installed = Number(result.installed) || 0;
    const failed = Number(result.failed) || 0;
    let text =
      "panel installed " +
      installed +
      " file" +
      (installed === 1 ? "" : "s") +
      " from FlowLocal";
    if (failed) text += ", " + failed + " failed";
    const cls = failed && !installed ? "err" : failed ? "warn" : "ok";
    note(text, cls);
  } catch (err) {
    note(String(err.message || err), "err");
  }
}

async function sendShowToPanel() {
  const st = sendStatusEl();
  function note(text, cls) {
    if (!st) return;
    st.textContent = text;
    st.className = "status " + (cls || "");
  }
  applyMetaFromForm();
  const name = showTableFilename(perf);
  note("saving " + name + " to FlowLocal…");
  try {
    await saveShowToLibrary(perf, name, { overwrite: true });
  } catch (err) {
    note("save to FlowLocal failed: " + (err.message || err), "err");
    return;
  }
  const panel = getPanelHttpBase();
  note("asking panel (" + panel + ") to pull " + name + "…");
  try {
    const result = await panelPullLibrary("shows", [name]);
    const installed = Number(result.installed) || 0;
    const failed = Number(result.failed) || 0;
    if (installed) note("panel installed show " + name, "ok");
    else note("panel did not install show" + (failed ? " (pull failed)" : ""), "err");
  } catch (err) {
    note(String(err.message || err), "err");
  }
}

mqtt.onSelectAck((d) => {
  const matched = Number(d && d.matched) || 0;
  const total = d && d.total != null ? Number(d.total) : matched;
  const presets = Number(d && d.presets) || 0;
  const missing = Number(d && d.missing) || 0;
  let text = "panel marked " + total + " module" + (total === 1 ? "" : "s") + " on /patterns";
  if (presets) text += ", " + presets + " preset" + (presets === 1 ? "" : "s") + " skipped (in firmware)";
  if (missing) text += ", " + missing + " not installed";
  if (total) text += " — open the device Patterns page and Download ZIP";
  const cls = missing && !total ? "err" : missing || presets ? "warn" : "ok";
  const st = sendStatusEl();
  const cueSt = $("cueStatus");
  [st, cueSt].forEach((el) => {
    if (!el) return;
    el.textContent = text;
    el.className = "status " + cls;
  });
});

function queryStatusEl() {
  return $("queryStatus") || $("cueStatus");
}

async function queryDevicePatterns() {
  const st = queryStatusEl();
  function note(text, cls) {
    if (!st) return;
    st.textContent = text;
    st.className = "status " + (cls || "");
  }
  if (!mqtt.isConnected()) {
    note("connect the Director broker first", "warn");
    return;
  }
  note("querying panel…");
  try {
    const items = await mqtt.queryPatterns(12000);
    if (!items.length) {
      note("no list yet — panel MQTT must be in Director mode on this PC’s IP", "warn");
      return;
    }
    items.forEach((p) => addRequiredPattern(p.name));
    lastInventory = items.slice();
    if (launchPad) launchPad.refreshPatterns();
    note("loaded " + items.length + " pattern" + (items.length === 1 ? "" : "s") + " from panel", "ok");
  } catch (err) {
    note(String(err.message || err), "err");
  }
}

let reqHighlight = 0;

function hideReqResults() {
  const list = $("reqResults");
  if (list) {
    list.hidden = true;
    list.innerHTML = "";
  }
  reqHighlight = 0;
}

function showReqResults(query) {
  const list = $("reqResults");
  if (!list) return;
  const hits = searchPatterns(query, String(query || "").trim() ? 40 : 0);
  list.innerHTML = "";
  if (!hits.length) {
    const li = document.createElement("li");
    li.className = "empty";
    li.textContent = query.trim()
      ? "No catalog match — Add to use this name as typed"
      : "Type to filter the catalog";
    list.appendChild(li);
    list.hidden = false;
    reqHighlight = 0;
    return;
  }
  hits.forEach((p, i) => {
    const li = document.createElement("li");
    li.dataset.name = p.name;
    if (i === 0) li.classList.add("active");
    const nm = document.createElement("span");
    nm.className = "name";
    nm.textContent = p.name;
    const meta = document.createElement("span");
    meta.className = "meta";
    meta.innerHTML =
      escapeHtml(p.slug || "") +
      (p.absoluteReady ? ' <span class="ready">abs</span>' : "");
    li.appendChild(nm);
    li.appendChild(meta);
    li.addEventListener("mousedown", (ev) => {
      ev.preventDefault();
      pickRequiredFromList(p.name);
    });
    list.appendChild(li);
  });
  list.hidden = false;
  reqHighlight = 0;
}

function reqResultItems() {
  return Array.from(($("reqResults") && $("reqResults").querySelectorAll("li[data-name]")) || []);
}

function moveReqHighlight(delta) {
  const items = reqResultItems();
  if (!items.length) return;
  items[reqHighlight] && items[reqHighlight].classList.remove("active");
  reqHighlight = (reqHighlight + delta + items.length) % items.length;
  items[reqHighlight].classList.add("active");
  items[reqHighlight].scrollIntoView({ block: "nearest" });
}

function pickRequiredFromList(name) {
  addRequiredPattern(name);
  const search = $("reqSearch");
  if (search) search.value = "";
  hideReqResults();
}

function commitRequiredSearch() {
  const search = $("reqSearch");
  const typed = (search && search.value) || "";
  const items = reqResultItems();
  const picked =
    (items[reqHighlight] && items[reqHighlight].dataset.name) ||
    (items[0] && items[0].dataset.name) ||
    typed;
  if (!picked.trim()) return;
  pickRequiredFromList(picked);
}

function publishState(state, force) {
  if (!mqtt.isConnected()) return;
  let changed = !!force;
  if (force || state.pattern !== lastPub.pattern) {
    mqtt.publishPattern(state.pattern || "");
    lastPub.pattern = state.pattern || "";
    changed = true;
  }
  if (force || state.message !== lastPub.message) {
    mqtt.publishMessage(state.message || "");
    lastPub.message = state.message || "";
    changed = true;
  }
  const p = state.param || [500, 500, 500, 500];
  for (let i = 0; i < 4; i++) {
    if (force || p[i] !== lastPub.param[i]) {
      mqtt.publishParam(i + 1, p[i], force);
      lastPub.param[i] = p[i];
      changed = true;
    }
  }
  // Retained snapshot only when the composed look actually changed
  if (changed) {
    mqtt.publishSnapshot(
      {
        pattern: state.pattern,
        param: p,
        message: state.message,
        t: state.t,
        id: perf.id
      },
      force
    );
  }
}

let lastLabelPattern = "Origin";

function applyParamLabels(patternName) {
  const trimmed = String(patternName || "").trim();
  if (trimmed) lastLabelPattern = trimmed;
  const labels = paramLabelsFor(trimmed || lastLabelPattern);
  const effects = paramEffectsFor(trimmed || lastLabelPattern);
  for (let i = 0; i < 4; i++) {
    const live = $("liveP" + (i + 1) + "Label");
    const cue = $("cueP" + (i + 1) + "Label");
    const effect =
      (effects[i] && effects[i].effect) || labels[i] || "Param " + (i + 1);
    if (live) {
      live.textContent = labels[i];
      live.title = effect + " (MQTT param/" + (i + 1) + ", 0..1000)";
    }
    if (cue) {
      cue.textContent = labels[i];
      cue.title = effect + " (MQTT param/" + (i + 1) + ", 0..1000)";
    }
  }
  timeline.setParamLabels(labels);
}

function syncLiveStrip(state) {
  stripSilent = true;
  fillPatternSelect(
    $("livePattern"),
    state.pattern || $("livePattern").value || lastLabelPattern
  );
  $("liveMessage").value = state.message || "";
  applyParamLabels(state.pattern || $("livePattern").value || lastLabelPattern);
  for (let i = 0; i < 4; i++) {
    const v = clamp1000(state.param[i]);
    $("liveP" + (i + 1)).value = String(v);
    $("liveP" + (i + 1) + "Val").value = String(v);
  }
  $("clock").textContent = formatTime(state.t);
  stripSilent = false;
}

function setPlayhead(t, publish) {
  playheadT = clampToDuration(perf, t);
  timeline.setPlayhead(playheadT);
  const state = stateAt(perf, playheadT);
  syncLiveStrip(state);
  if (Number($("cueIndex").value) < 0) {
    cueEditorSilent = true;
    $("cueT").value = String(playheadT);
    cueEditorSilent = false;
  }
  if (publish) publishState(liveStateAtPlayhead(), true);
}

/** Full look from live strip (preview / snapshot source). */
function liveStateAtPlayhead() {
  return {
    t: playheadT,
    pattern: $("livePattern").value.trim() || "Origin",
    message: $("liveMessage").value,
    param: liveParamVector()
  };
}

function previewAligned() {
  publishState(liveStateAtPlayhead(), true);
}

let lastPlayTick = -1;

function tick() {
  if (!playing) return;
  const wall = performance.now();
  let t = playStartedT + (wall - playStartedWall) / 1000;
  const dur = durationOf(perf);
  if (t >= dur) {
    if (perf.loop) {
      playStartedWall = wall;
      playStartedT = 0;
      t = 0;
      lastPlayTick = -1;
      lastPub = { pattern: null, message: null, param: [null, null, null, null] };
    } else {
      playing = false;
      t = dur;
      $("btnPlay").textContent = "Play";
      setPlayhead(t, true);
      return;
    }
  }
  timeline.setPlayhead(t);
  $("clock").textContent = formatTime(t);
  // v2 eases need sub-second publishes; sample on the 0.1 s grid (or whole
  // seconds for v1) so MQTT tracks the same values the panel player lerps.
  const grid = perf.version === PFST_VERSION_2 ? 0.1 : 1;
  const tickKey = Math.floor(t / grid + 1e-9);
  if (tickKey !== lastPlayTick) {
    lastPlayTick = tickKey;
    const state = stateAt(perf, t);
    syncLiveStrip(state);
    publishState(state, false);
  }
  raf = requestAnimationFrame(tick);
}

function play() {
  applyMetaFromForm();
  if (playing) {
    playing = false;
    cancelAnimationFrame(raf);
    $("btnPlay").textContent = "Play";
    return;
  }
  playing = true;
  $("btnPlay").textContent = "Pause";
  playStartedWall = performance.now();
  playStartedT = playheadT;
  lastPlayTick = -1;
  lastPub = { pattern: null, message: null, param: [null, null, null, null] };
  const state = stateAt(perf, playStartedT);
  publishState(state, true);
  raf = requestAnimationFrame(tick);
}

function parseClock(text) {
  const m = /^(\d+):(\d+(?:\.\d+)?)$/.exec(String(text || "0:00.00"));
  if (!m) return 0;
  return Number(m[1]) * 60 + Number(m[2]);
}

function stop() {
  playing = false;
  cancelAnimationFrame(raf);
  $("btnPlay").textContent = "Play";
  playStartedT = 0;
  lastPlayTick = -1;
  setPlayhead(0, true);
}

const timeline = createTimeline($("timelineMount"), {
  performance: perf,
  pxPerSec: Number($("zoom").value) || 40,
  onSelect(index, cue, meta) {
    selectedLane = (meta && meta.lane) || null;
    playheadT = clampToDuration(perf, cue.t);
    timeline.setPlayhead(playheadT);
    syncLiveStrip(stateAt(perf, playheadT));
    fillCueEditor(index, cue);
    refreshCueList();
    setSelectionStatus(index, cue);
    previewAligned();
  },
  onSeek(t) {
    selectedLane = null;
    $("cueIndex").value = "-1";
    if (playing) {
      playStartedWall = performance.now();
      playStartedT = t;
    }
    setPlayhead(t, true);
    $("cueStatus").textContent =
      "playhead @ " + playheadT + "s — adjust sliders or Capture snapshot";
    $("cueStatus").className = "status";
  },
  onLaneInsert(laneId, t) {
    selectedLane = laneId;
    playheadT = clampToDuration(perf, t);
    insertCueFromLane(laneId, playheadT);
  }
});

let cueEditTimer = 0;
let cueEditorSilent = false;

function setSelectionStatus(index, cue) {
  const lane = selectedLane || "";
  $("cueStatus").textContent =
    "selected #" +
    index +
    " @ " +
    cue.t +
    "s" +
    (lane ? " (" + lane + ")" : "") +
    " — slider/type previews; release updates the block";
  $("cueStatus").className = "status ok";
}

function findParamCueIndex(t) {
  t = qTime(t);
  const eps = cueMergeEps();
  return (perf.timeline || []).findIndex(
    (c) => c.param && Math.abs(c.t - t) <= eps
  );
}

function upsertParamCue(t, patch, select) {
  t = clampToDuration(perf, t);
  const sparse = parseParamField(patch);
  if (!sparse) return -1;
  const idx = findParamCueIndex(t);
  let cue;
  if (idx >= 0) {
    cue = {
      t: perf.timeline[idx].t,
      param: mergeParamSparse(perf.timeline[idx].param, sparse)
    };
    if (perf.timeline[idx].pattern != null) cue.pattern = perf.timeline[idx].pattern;
    if (perf.timeline[idx].message != null) cue.message = perf.timeline[idx].message;
    if (perf.timeline[idx].ease) cue.ease = true;
    else if (defaultEaseOn()) cue.ease = true;
    perf.timeline[idx] = cue;
  } else {
    cue = { t, param: sparse };
    if (defaultEaseOn()) cue.ease = true;
    perf.timeline.push(cue);
  }
  perf.timeline.sort(compareCues);
  reloadTimeline();
  const ni = perf.timeline.indexOf(cue);
  if (select !== false && ni >= 0) {
    timeline.selectIndex(ni);
    fillCueEditor(ni, cue);
  }
  refreshCueList();
  $("cueStatus").textContent =
    "block @ " +
    cue.t +
    "s (" +
    formatParamSparse(cue.param) +
    (cue.ease ? ", ease" : "") +
    ")";
  $("cueStatus").className = "status ok";
  return ni;
}

function upsertPatternCue(t, name) {
  t = clampToDuration(perf, t);
  name = (name || "Origin").trim() || "Origin";
  const eps = cueMergeEps();
  let idx = (perf.timeline || []).findIndex(
    (c) =>
      c.pattern != null &&
      !c.param &&
      c.message == null &&
      Math.abs(c.t - t) <= eps
  );
  if (idx < 0) {
    idx = (perf.timeline || []).findIndex(
      (c) => c.pattern != null && Math.abs(c.t - t) <= eps
    );
  }
  let cue;
  if (idx >= 0) {
    cue = { ...perf.timeline[idx], t: perf.timeline[idx].t, pattern: name };
    perf.timeline[idx] = cue;
  } else {
    cue = { t, pattern: name };
    perf.timeline.push(cue);
  }
  perf.timeline.sort(compareCues);
  reloadTimeline();
  const ni = perf.timeline.indexOf(cue);
  timeline.selectIndex(ni);
  fillCueEditor(ni, cue);
  refreshCueList();
  $("cueStatus").textContent = "pattern block @ " + cue.t + "s";
  $("cueStatus").className = "status ok";
}

function upsertMessageCue(t, message) {
  t = clampToDuration(perf, t);
  const eps = cueMergeEps();
  let idx = (perf.timeline || []).findIndex(
    (c) =>
      c.message != null &&
      !c.param &&
      c.pattern == null &&
      Math.abs(c.t - t) <= eps
  );
  if (idx < 0) {
    idx = (perf.timeline || []).findIndex(
      (c) => c.message != null && Math.abs(c.t - t) <= eps
    );
  }
  let cue;
  if (idx >= 0) {
    cue = {
      ...perf.timeline[idx],
      t: perf.timeline[idx].t,
      message: message == null ? "" : String(message)
    };
    perf.timeline[idx] = cue;
  } else {
    cue = { t, message: message == null ? "" : String(message) };
    perf.timeline.push(cue);
  }
  perf.timeline.sort(compareCues);
  reloadTimeline();
  const ni = perf.timeline.indexOf(cue);
  timeline.selectIndex(ni);
  fillCueEditor(ni, cue);
  refreshCueList();
  $("cueStatus").textContent = "message block @ " + cue.t + "s";
  $("cueStatus").className = "status ok";
}

function liveParamVector() {
  return [
    clamp1000($("liveP1").value),
    clamp1000($("liveP2").value),
    clamp1000($("liveP3").value),
    clamp1000($("liveP4").value)
  ];
}

function sparseOne(channel, value) {
  const patch = [null, null, null, null];
  patch[channel] = clamp1000(value);
  return patch;
}

function insertCueFromLane(laneId, t) {
  t = clampToDuration(perf, t);
  playheadT = t;
  if (laneId === "pattern") {
    upsertPatternCue(t, $("livePattern").value);
    selectedLane = "pattern";
    previewAligned();
    return;
  }
  if (laneId === "message") {
    upsertMessageCue(t, $("liveMessage").value);
    selectedLane = "message";
    previewAligned();
    return;
  }
  if (laneId.startsWith("param")) {
    const channel = Number(laneId.slice(5));
    if (channel < 0 || channel > 3) return;
    selectedLane = laneId;
    upsertParamCue(
      t,
      sparseOne(channel, $("liveP" + (channel + 1)).value),
      true
    );
    previewAligned();
  }
}

function commitLiveChannel(channel) {
  if (stripSilent) return;
  const t = clampToDuration(perf, playheadT);
  const v = clamp1000($("liveP" + (channel + 1)).value);
  $("liveP" + (channel + 1) + "Val").value = String(v);
  selectedLane = "param" + channel;
  upsertParamCue(t, sparseOne(channel, v), true);
  previewAligned();
}

function commitLivePattern() {
  if (stripSilent) return;
  const v = $("livePattern").value.trim() || "Origin";
  applyParamLabels(v);
  selectedLane = "pattern";
  upsertPatternCue(clampToDuration(perf, playheadT), v);
  previewAligned();
}

function commitLiveMessage() {
  if (stripSilent) return;
  selectedLane = "message";
  upsertMessageCue(clampToDuration(perf, playheadT), $("liveMessage").value);
  previewAligned();
}

function upsertFromEditor() {
  if (cueEditorSilent) return;
  const cue = readCueFromEditor();
  if (!cue) return;
  cue.t = clampToDuration(perf, cue.t);
  const idx = Number($("cueIndex").value);
  if (Number.isInteger(idx) && idx >= 0 && idx < perf.timeline.length) {
    perf.timeline[idx] = cue;
    perf.timeline.sort(compareCues);
    reloadTimeline();
    const ni = perf.timeline.indexOf(cue);
    timeline.selectIndex(ni);
    fillCueEditor(ni, cue);
    refreshCueList();
    $("cueStatus").textContent = "cue updated @ " + cue.t + "s";
    $("cueStatus").className = "status ok";
    previewAligned();
    return;
  }
  const multi =
    (cue.pattern != null ? 1 : 0) +
      (cue.param ? 1 : 0) +
      (cue.message != null ? 1 : 0) >
    1;
  if (multi || (cue.pattern != null && cue.param)) {
    perf.timeline = (perf.timeline || []).filter((c) => c.t !== cue.t);
    perf.timeline.push(cue);
    perf.timeline.sort(compareCues);
    reloadTimeline();
    const ni = perf.timeline.indexOf(cue);
    timeline.selectIndex(ni);
    fillCueEditor(ni, cue);
    refreshCueList();
    $("cueStatus").textContent = "snapshot @ " + cue.t + "s";
    $("cueStatus").className = "status ok";
    previewAligned();
    return;
  }
  if (cue.pattern != null) upsertPatternCue(cue.t, cue.pattern);
  else if (cue.message != null) upsertMessageCue(cue.t, cue.message);
  else if (cue.param) upsertParamCue(cue.t, cue.param, true);
  previewAligned();
}

function captureSnapshotAtPlayhead() {
  const t = clampToDuration(perf, playheadT);
  const cue = {
    t,
    pattern: $("livePattern").value.trim() || "Origin",
    param: liveParamVector(),
    message: $("liveMessage").value
  };
  perf.timeline = (perf.timeline || []).filter((c) => c.t !== t);
  perf.timeline.push(cue);
  perf.timeline.sort(compareCues);
  reloadTimeline();
  const ni = perf.timeline.indexOf(cue);
  selectedLane = null;
  timeline.selectIndex(ni);
  fillCueEditor(ni, cue);
  refreshCueList();
  $("cueStatus").textContent = "snapshot @ " + t + "s (full look)";
  $("cueStatus").className = "status ok";
  previewAligned();
}

function fillCueEditor(index, cue) {
  cueEditorSilent = true;
  clearTimeout(cueEditTimer);
  $("cueIndex").value = String(index);
  $("cueT").value = String(cue.t);
  const easeEl = $("cueEase");
  if (easeEl) easeEl.checked = !!cue.ease;
  const nFields =
    (cue.pattern != null ? 1 : 0) +
    (cue.param ? 1 : 0) +
    (cue.message != null ? 1 : 0);
  if (nFields > 1 || (cue.pattern != null && cue.param)) {
    $("cueKind").value = "snapshot";
  } else if (cue.pattern != null) {
    $("cueKind").value = "pattern";
  } else if (cue.message != null) {
    $("cueKind").value = "message";
  } else {
    $("cueKind").value = "param";
  }
  fillPatternSelect($("cuePattern"), cue.pattern || $("cuePattern").value || lastLabelPattern);
  $("cueMessage").value = cue.message != null ? cue.message : "";
  for (let i = 0; i < 4; i++) {
    const el = $("cueP" + (i + 1));
    if (cue.param && cue.param[i] != null) el.value = String(cue.param[i]);
    else if ($("cueKind").value === "snapshot") el.value = "500";
    else el.value = "";
  }
  updateCueKindFields();
  cueEditorSilent = false;
}

function updateCueKindFields() {
  const kind = $("cueKind").value;
  const snap = kind === "snapshot";
  $("cueFieldsPattern").style.display =
    kind === "pattern" || snap ? "" : "none";
  $("cueFieldsParam").style.display =
    kind === "param" || snap ? "" : "none";
  $("cueFieldsMessage").style.display =
    kind === "message" || snap ? "" : "none";
  const easeRow = $("cueEaseRow");
  if (easeRow) {
    const show =
      perf.version === PFST_VERSION_2 && (kind === "param" || snap);
    easeRow.style.display = show ? "" : "none";
  }
}

function readCueFromEditor() {
  const t = qTime($("cueT").value);
  const kind = $("cueKind").value;
  const ease =
    perf.version === PFST_VERSION_2 && $("cueEase") && $("cueEase").checked;
  if (kind === "snapshot") {
    const param = parseParamField([
      $("cueP1").value === "" ? null : $("cueP1").value,
      $("cueP2").value === "" ? null : $("cueP2").value,
      $("cueP3").value === "" ? null : $("cueP3").value,
      $("cueP4").value === "" ? null : $("cueP4").value
    ]);
    const cue = {
      t,
      pattern: $("cuePattern").value.trim() || "Origin",
      param: param || [500, 500, 500, 500],
      message: $("cueMessage").value
    };
    if (ease) cue.ease = true;
    return cue;
  }
  if (kind === "pattern") {
    return { t, pattern: $("cuePattern").value.trim() || "Origin" };
  }
  if (kind === "message") {
    return { t, message: $("cueMessage").value };
  }
  const param = parseParamField([
    $("cueP1").value === "" ? null : $("cueP1").value,
    $("cueP2").value === "" ? null : $("cueP2").value,
    $("cueP3").value === "" ? null : $("cueP3").value,
    $("cueP4").value === "" ? null : $("cueP4").value
  ]);
  if (!param) {
    $("cueStatus").textContent = "set at least one param (leave others blank)";
    $("cueStatus").className = "status warn";
    return null;
  }
  const cue = { t, param };
  if (ease) cue.ease = true;
  return cue;
}

function refreshCueList() {
  const box = $("cueList");
  box.innerHTML = "";
  (perf.timeline || []).forEach((cue, i) => {
    const row = document.createElement("div");
    row.className = "row";
    const kind =
      cue.pattern != null && (cue.param || cue.message != null)
        ? "snapshot"
        : cue.pattern != null
          ? "pattern"
          : cue.message != null
            ? "message"
            : "param";
    const detail =
      kind === "snapshot"
        ? (cue.pattern || "") +
          (cue.param ? " · " + formatParamSparse(cue.param) : "") +
          (cue.ease ? " · ease" : "") +
          (cue.message != null && cue.message !== ""
            ? " · " + cue.message
            : "")
        : cue.pattern != null
          ? cue.pattern
          : cue.message != null
            ? cue.message === ""
              ? "(clear)"
              : cue.message
            : formatParamSparse(cue.param) + (cue.ease ? " · ease" : "");
    const tLabel =
      perf.version === PFST_VERSION_2
        ? (Math.round(cue.t * 10) / 10).toFixed(1)
        : cue.t.toFixed(0);
    row.innerHTML =
      '<span class="t">' +
      tLabel +
      's</span><span class="kind">' +
      kind +
      "</span><span>" +
      escapeHtml(detail) +
      "</span>";
    row.addEventListener("click", () => {
      timeline.selectIndex(i);
      fillCueEditor(i, cue);
    });
    box.appendChild(row);
  });
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

function reloadTimeline() {
  // Keep chip selection while editing / planting
  timeline.setPerformance(perf, true);
  refreshCueList();
  refreshUsedNote();
}

// Tabs
document.querySelectorAll("nav.tabs button").forEach((btn) => {
  btn.addEventListener("click", () => showView(btn.dataset.view));
});

// Connect
$("btnConnect").addEventListener("click", () => {
  mqtt.connect({
    url: $("wsUrl").value.trim() || (window.PF_RUNTIME && window.PF_RUNTIME.wsUrl) || "ws://127.0.0.1:9001",
    prefix: $("mqttPrefix").value.trim() || "patternflow",
    username: $("mqttUser").value.trim(),
    password: $("mqttPass").value
  });
});
$("btnDisconnect").addEventListener("click", () => mqtt.disconnect());
const btnQuery = $("btnQueryPatterns");
if (btnQuery) btnQuery.addEventListener("click", () => queryDevicePatterns());
const btnQueryTl = $("btnQueryPatternsTl");
if (btnQueryTl) btnQueryTl.addEventListener("click", () => queryDevicePatterns());
const btnSendUsed = $("btnSendUsed");
if (btnSendUsed) btnSendUsed.addEventListener("click", () => sendUsedPatternsToPanel());
const btnSendUsedTl = $("btnSendUsedTl");
if (btnSendUsedTl) btnSendUsedTl.addEventListener("click", () => sendUsedPatternsToPanel());
const btnSendShow = $("btnSendShow");
if (btnSendShow) btnSendShow.addEventListener("click", () => sendShowToPanel());
const panelHttpEl = $("panelHttp");
if (panelHttpEl) {
  try {
    const saved = localStorage.getItem("pf-director-panel-http");
    if (saved) panelHttpEl.value = saved;
  } catch (_) {}
  panelHttpEl.addEventListener("change", () => setPanelHttpBase(panelHttpEl.value));
}

// Transport
$("btnPlay").addEventListener("click", play);
$("btnStop").addEventListener("click", stop);
$("btnSnapshot").addEventListener("click", () => {
  applyMetaFromForm();
  captureSnapshotAtPlayhead();
});
$("zoom").addEventListener("input", () => {
  timeline.setPxPerSec(Number($("zoom").value) || 40);
});

const reqSearch = $("reqSearch");
reqSearch.addEventListener("focus", () => showReqResults(reqSearch.value));
reqSearch.addEventListener("input", () => showReqResults(reqSearch.value));
reqSearch.addEventListener("keydown", (ev) => {
  if (ev.key === "ArrowDown") {
    ev.preventDefault();
    if ($("reqResults").hidden) showReqResults(reqSearch.value);
    else moveReqHighlight(1);
  } else if (ev.key === "ArrowUp") {
    ev.preventDefault();
    moveReqHighlight(-1);
  } else if (ev.key === "Enter") {
    ev.preventDefault();
    commitRequiredSearch();
  } else if (ev.key === "Escape") {
    hideReqResults();
  }
});
reqSearch.addEventListener("blur", () => {
  setTimeout(hideReqResults, 120);
});
$("btnReqAdd").addEventListener("click", () => commitRequiredSearch());

// Live strip: drag = aligned MQTT preview; release/type = plant or update block at playhead
$("livePattern").addEventListener("input", () => {
  if (stripSilent) return;
  applyParamLabels($("livePattern").value.trim());
  previewAligned();
});
$("livePattern").addEventListener("change", () => {
  commitLivePattern();
});
$("liveMessage").addEventListener("input", () => {
  if (stripSilent) return;
  previewAligned();
});
$("liveMessage").addEventListener("change", () => {
  commitLiveMessage();
});
for (let i = 1; i <= 4; i++) {
  const el = $("liveP" + i);
  const num = $("liveP" + i + "Val");
  el.addEventListener("input", () => {
    if (stripSilent) return;
    const v = clamp1000(el.value);
    num.value = String(v);
    previewAligned();
  });
  el.addEventListener("change", () => {
    commitLiveChannel(i - 1);
  });
  num.addEventListener("input", () => {
    if (stripSilent) return;
    const v = clamp1000(num.value);
    el.value = String(v);
    previewAligned();
  });
  num.addEventListener("change", () => {
    const v = clamp1000(num.value);
    el.value = String(v);
    num.value = String(v);
    commitLiveChannel(i - 1);
  });
}

// Cue editor — explicit Update only (no auto-insert on every keystroke)
$("cueKind").addEventListener("change", updateCueKindFields);
$("cuePattern").addEventListener("change", () => {
  applyParamLabels($("cuePattern").value);
});

$("btnCueAdd").addEventListener("click", () => {
  applyMetaFromForm();
  $("cueIndex").value = "-1";
  upsertFromEditor();
});
$("btnCueSave").addEventListener("click", () => {
  applyMetaFromForm();
  upsertFromEditor();
});
$("btnCueDelete").addEventListener("click", () => {
  const idx = Number($("cueIndex").value);
  if (!Number.isInteger(idx) || idx < 0 || idx >= perf.timeline.length) return;
  perf.timeline.splice(idx, 1);
  selectedLane = null;
  reloadTimeline();
  $("cueIndex").value = "-1";
  $("cueStatus").textContent = "cue deleted";
  $("cueStatus").className = "status ok";
  refreshCueList();
  setPlayhead(playheadT, true);
});

// File
$("btnNew").addEventListener("click", () => {
  if (!confirm("Replace the current performance with a new blank one?")) return;
  stop();
  perf = emptyPerformance();
  selectedLane = null;
  fillMetaForm();
  timeline.setPerformance(perf, false);
  refreshCueList();
  setPlayhead(0, false);
});
$("btnUpgradeV2") &&
  $("btnUpgradeV2").addEventListener("click", () => {
    applyMetaFromForm();
    if (perf.version === PFST_VERSION_2) {
      $("fileStatus").textContent = "already PFST v2";
      $("fileStatus").className = "status ok";
      return;
    }
    perf = upgradePerformanceToV2(perf);
    fillMetaForm();
    timeline.setPerformance(perf, false);
    refreshCueList();
    $("fileStatus").textContent =
      "upgraded to PFST v2 (0.1 s grid). Tick Ease on param cues to glide.";
    $("fileStatus").className = "status ok";
  });
$("btnCompressEase") &&
  $("btnCompressEase").addEventListener("click", () => {
    applyMetaFromForm();
    const before = (perf.timeline || []).length;
    perf = compressStaircaseToEase(perf);
    fillMetaForm();
    timeline.setPerformance(perf, false);
    refreshCueList();
    $("fileStatus").textContent =
      "compressed " +
      before +
      " → " +
      (perf.timeline || []).length +
      " cues (v2 + linear ease on staircases)";
    $("fileStatus").className = "status ok";
  });
$("btnOpen").addEventListener("click", async () => {
  try {
    if (isFlowLocalHost()) {
      perf = await openShowFromLibrary();
    } else {
      perf = await openPerformanceFile();
    }
    selectedLane = null;
    fillMetaForm();
    timeline.setPerformance(perf, false);
    refreshCueList();
    setPlayhead(0, false);
    $("fileStatus").textContent = "loaded " + (perf.id || "file");
    $("fileStatus").className = "status ok";
  } catch (err) {
    if (String(err.message) === "cancelled") return;
    $("fileStatus").textContent = String(err.message || err);
    $("fileStatus").className = "status err";
  }
});
$("btnSave").addEventListener("click", async () => {
  applyMetaFromForm();
  try {
    if (isFlowLocalHost()) {
      const d = await saveShowToLibrary(perf);
      $("fileStatus").textContent =
        "saved " + d.bytes + " bytes to FlowLocal (" + d.name + ")";
      $("fileStatus").className = "status ok";
    } else {
      const n = downloadShowTable(perf);
      $("fileStatus").textContent =
        "saved " + n + " bytes — same .pfs the panel plays on /show";
      $("fileStatus").className = "status ok";
    }
  } catch (err) {
    $("fileStatus").textContent = String(err.message || err);
    $("fileStatus").className = "status err";
  }
});

$("btnSaveLocal") && $("btnSaveLocal").addEventListener("click", () => {
  applyMetaFromForm();
  try {
    const n = downloadShowTable(perf);
    $("fileStatus").textContent = "downloaded " + n + " bytes";
    $("fileStatus").className = "status ok";
  } catch (err) {
    $("fileStatus").textContent = String(err.message || err);
    $("fileStatus").className = "status err";
  }
});

$("btnOpenLocal") && $("btnOpenLocal").addEventListener("click", async () => {
  try {
    perf = await openPerformanceFile();
    selectedLane = null;
    fillMetaForm();
    timeline.setPerformance(perf, false);
    refreshCueList();
    setPlayhead(0, false);
    $("fileStatus").textContent = "loaded " + (perf.id || "file");
    $("fileStatus").className = "status ok";
  } catch (err) {
    if (String(err.message) === "cancelled") return;
    $("fileStatus").textContent = String(err.message || err);
    $("fileStatus").className = "status err";
  }
});

["perfTitle", "perfId", "perfLoop", "perfUtc", "perfChannel", "perfLength"].forEach(
  (id) => {
    $(id).addEventListener("change", () => {
      applyMetaFromForm();
      if (id === "perfLength") {
        reloadTimeline();
        setPlayhead(Math.min(playheadT, durationOf(perf)), false);
      }
    });
  }
);

(function applyRuntime() {
  const rt = window.PF_RUNTIME || {};
  const ws = $("wsUrl");
  const host = location.hostname;
  const fromFile = location.protocol === "file:";
  const flowlocal =
    !!rt.flowlocal ||
    (!fromFile &&
      (host === "flowlocal.local" ||
        host === "192.168.66.1" ||
        /^192\.168\.\d+\.\d+$/.test(host)));
  window.PF_IS_FLOWLOCAL = flowlocal;
  if (flowlocal) {
    const setupPc = $("setupPc");
    const setupFl = $("setupFlowlocal");
    if (setupPc) setupPc.hidden = true;
    if (setupFl) setupFl.hidden = false;
    const home = $("flHomeLink");
    if (home) home.hidden = false;
    if (ws) {
      ws.value =
        !fromFile && host
          ? "ws://" + host + ":9001"
          : rt.wsUrl || "ws://192.168.66.1:9001";
    }
    return;
  }
  if (host && host !== "127.0.0.1" && host !== "localhost" && ws) {
    if (!ws.value || ws.value.indexOf("127.0.0.1") >= 0) {
      ws.value = "ws://" + host + ":9001";
    }
  } else if (rt.wsUrl && ws && (!ws.value || ws.value.indexOf("127.0.0.1") >= 0)) {
    ws.value = rt.wsUrl;
  }
})();
if (!window.PF_IS_FLOWLOCAL) {
  initBrokerGuide($("view-setup"));
}
launchPad = initLaunchpad({
  mqtt,
  getInventory: () => lastInventory
});
fillMetaForm();
reloadTimeline();
updateCueKindFields();
applyParamLabels("Origin");
setPlayhead(0, false);
showView("setup");

const savedPerfMode = localStorage.getItem("pf-director-performance-mode") === "1";
setPerformanceMode(savedPerfMode);
const perfBtn = $("btnPerfMode");
if (perfBtn) {
  perfBtn.addEventListener("click", () => {
    setPerformanceMode(!performanceMode);
    localStorage.setItem("pf-director-performance-mode", performanceMode ? "1" : "0");
  });
}
// Escape hatch: status / banner always open Connect (and leave Performance).
function goReconnect() {
  exitPerformanceMode({ connect: true });
}
const connHit = $("connHit");
if (connHit) {
  connHit.addEventListener("click", goReconnect);
  connHit.addEventListener("keydown", (ev) => {
    if (ev.key === "Enter" || ev.key === " ") {
      ev.preventDefault();
      goReconnect();
    }
  });
}
const offlineBanner = $("offlineBanner");
if (offlineBanner) {
  offlineBanner.addEventListener("click", goReconnect);
}
// Seed offline UI before MQTT reports (banner + body.mqtt-offline).
setConnUi({ kind: "err", message: "offline" });


if ("serviceWorker" in navigator && !window.PF_IS_FLOWLOCAL) {
  navigator.serviceWorker.register("./sw.js").catch(() => {});
} else if ("serviceWorker" in navigator) {
  navigator.serviceWorker.getRegistrations().then(function(rs){
    rs.forEach(function(r){ r.unregister(); });
  });
}

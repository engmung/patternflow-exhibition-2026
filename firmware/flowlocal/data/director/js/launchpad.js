/**
 * Director Launch page — 4×4 clip grid, Snapshot / K1–K4 / Message, local MQTT.
 */
import {
  LAYERS,
  GRID,
  AUTOSAVE_EVERY,
  emptyDeck,
  padLabel,
  loadStoredDeck,
  storeDeck,
  downloadDeck,
  openDeckFile,
  openExampleDeck,
  saveDeckToLibrary,
  openDeckFromLibrary,
  isKnobLayer,
  knobChannel
} from "./deck.js";
import {
  listPatterns,
  paramLabelsFor,
  canonicalPatternName
} from "./patterns-meta.js";
import { isFlowLocalHost } from "./library.js";

const $ = (id) => document.getElementById(id);

function clamp1000(n) {
  let v = Math.round(Number(n));
  if (!Number.isFinite(v)) v = 500;
  if (v < 0) v = 0;
  if (v > 1000) v = 1000;
  return v;
}

export function initLaunchpad({ mqtt, getInventory }) {
  let deck = loadStoredDeck() || emptyDeck();
  let layer = "snapshot";
  let mode = "edit";
  let performanceMode = false;
  let selected = 0;
  let storesSinceSave = 0;
  let clockOrigin = performance.now();
  let pending = null;
  let pendingTimer = 0;
  let beatTimer = 0;

  function note(text, cls) {
    const el = $("lpStatus");
    if (!el) return;
    el.textContent = text;
    el.className = "status " + (cls || "");
  }

  function patternNames() {
    const seen = Object.create(null);
    const out = [];
    function add(raw) {
      const name = canonicalPatternName(raw) || String(raw || "").trim();
      if (!name) return;
      const k = name.toLowerCase();
      if (seen[k]) return;
      seen[k] = true;
      out.push(name);
    }
    const inv = getInventory() || [];
    if (inv.length) {
      inv.forEach((p) => add(p && (p.name || p.slug)));
      return out;
    }
    add("Origin");
    listPatterns().forEach((p) => add(p.name));
    return out;
  }

  function fillPatternSelect() {
    const sel = $("lpPattern");
    if (!sel) return;
    const cur = sel.value;
    sel.innerHTML = "";
    patternNames().forEach((name) => {
      const opt = document.createElement("option");
      opt.value = name;
      opt.textContent = name;
      sel.appendChild(opt);
    });
    if (cur && [...sel.options].some((o) => o.value === cur)) sel.value = cur;
    else sel.selectedIndex = 0;
    relabelParams();
  }

  function relabelParams() {
    const name = $("lpPattern").value;
    const labels = paramLabelsFor(name);
    for (let i = 0; i < 4; i++) {
      const el = $("lpP" + (i + 1) + "Label");
      if (el) el.textContent = labels[i] || "K" + (i + 1);
    }
    const one = $("lpOneLabel");
    if (one && isKnobLayer(layer)) {
      const ch = knobChannel(layer);
      const kn = labels[ch] || "Param " + (ch + 1);
      one.textContent = "K" + (ch + 1) + " · " + kn + " (param/" + (ch + 1) + ")";
    }
  }

  function liveSnapshot() {
    return {
      pattern: $("lpPattern").value.trim() || "Origin",
      param: [1, 2, 3, 4].map((i) => clamp1000($("lpP" + i).value)),
      message: $("lpMessage").value
    };
  }

  function preview() {
    if (!mqtt.isConnected()) return;
    if (layer === "snapshot") {
      const s = liveSnapshot();
      mqtt.publishPattern(s.pattern);
      mqtt.publishParams(s.param, true);
      mqtt.publishMessage(s.message);
      mqtt.publishSnapshot({ ...s, t: 0, id: deck.id, title: deck.title || deck.id }, true);
      return;
    }
    if (isKnobLayer(layer)) {
      mqtt.publishParam(knobChannel(layer) + 1, clamp1000($("lpOne").value), true);
      return;
    }
    mqtt.publishMessage($("lpLayerMsg").value);
  }

  function publishClip(clip) {
    if (!clip || !mqtt.isConnected()) return false;
    if (clip.kind === "snapshot") {
      mqtt.publishPattern(clip.pattern);
      mqtt.publishParams(clip.param, true);
      mqtt.publishMessage(clip.message || "");
      mqtt.publishSnapshot(
        {
          pattern: clip.pattern,
          param: clip.param,
          message: clip.message || "",
          t: 0,
          id: deck.id,
          title: deck.title || deck.id
        },
        true
      );
      if (!performanceMode) {
        $("lpPattern").value = clip.pattern;
        for (let i = 0; i < 4; i++) {
          $("lpP" + (i + 1)).value = String(clip.param[i]);
          $("lpP" + (i + 1) + "Val").value = String(clip.param[i]);
        }
        $("lpMessage").value = clip.message || "";
        relabelParams();
      }
      return true;
    }
    if (clip.kind === "knob") {
      mqtt.publishParam(clip.channel + 1, clip.value, true);
      if (!performanceMode) {
        $("lpOne").value = String(clip.value);
        $("lpOneVal").value = String(clip.value);
      }
      return true;
    }
    mqtt.publishMessage(clip.message || "");
    if (!performanceMode) $("lpLayerMsg").value = clip.message || "";
    return true;
  }

  function msPeriod() {
    const beat = 60000 / deck.bpm;
    return deck.quantize === "bar" ? beat * 4 : beat;
  }

  function delayToGrid() {
    if (deck.quantize === "off") return 0;
    const period = msPeriod();
    const elapsed = (performance.now() - clockOrigin) % period;
    const wait = period - elapsed;
    return wait < 8 ? 0 : wait;
  }

  function cancelPending() {
    if (pendingTimer) clearTimeout(pendingTimer);
    pendingTimer = 0;
    pending = null;
    renderGrid();
  }

  function launch(index) {
    const clip = deck.layers[layer][index];
    if (!clip) {
      note("empty pad", "warn");
      return;
    }
    if (!mqtt.isConnected()) {
      note("connect the Director broker first", "warn");
      return;
    }
    cancelPending();
    const wait = delayToGrid();
    if (wait <= 0) {
      publishClip(clip);
      note("fired " + padLabel(clip), "ok");
      return;
    }
    pending = index;
    renderGrid();
    note("queued " + padLabel(clip) + " (" + Math.round(wait) + " ms)", "warn");
    pendingTimer = setTimeout(() => {
      pendingTimer = 0;
      pending = null;
      publishClip(clip);
      note("fired " + padLabel(clip), "ok");
      renderGrid();
    }, wait);
  }

  function bumpAutosave() {
    storesSinceSave++;
    storeDeck(deck);
    if (storesSinceSave >= AUTOSAVE_EVERY) {
      storesSinceSave = 0;
      note("autosaved “" + (deck.title || deck.id) + "”", "ok");
    }
  }

  function ensureNamed() {
    if (deck.title && deck.title.trim()) return true;
    const t = window.prompt("Name this deck before storing clips", "Deck");
    if (!t || !t.trim()) {
      note("name the deck first", "warn");
      return false;
    }
    deck.title = t.trim();
    $("lpTitle").value = deck.title;
    storeDeck(deck);
    return true;
  }

  function captureClip() {
    if (layer === "snapshot") {
      const s = liveSnapshot();
      return {
        kind: "snapshot",
        pattern: s.pattern,
        param: s.param,
        message: s.message,
        label: s.pattern
      };
    }
    if (isKnobLayer(layer)) {
      const value = clamp1000($("lpOne").value);
      const ch = knobChannel(layer);
      return { kind: "knob", channel: ch, value, label: String(value) };
    }
    const message = $("lpLayerMsg").value;
    return {
      kind: "message",
      message,
      label: message || "(clear)"
    };
  }

  function storeSelected() {
    if (mode !== "edit") {
      note("switch to Edit to store", "warn");
      return;
    }
    if (!ensureNamed()) return;
    const clip = captureClip();
    if (layer === "snapshot" && !(clip && clip.pattern)) {
      note("choose a pattern first", "warn");
      return;
    }
    deck.layers[layer][selected] = clip;
    bumpAutosave();
    renderGrid();
    note("stored pad " + (selected + 1) + " · " + padLabel(clip), "ok");
  }

  function clearSelected() {
    if (mode !== "edit") return;
    deck.layers[layer][selected] = null;
    bumpAutosave();
    renderGrid();
    note("cleared pad " + (selected + 1), "ok");
  }

  function renderGrid() {
    const grid = $("lpGrid");
    if (!grid) return;
    grid.dataset.layer = layer;
    const pads = grid.querySelectorAll(".lp-pad");
    pads.forEach((btn, i) => {
      const clip = deck.layers[layer][i];
      btn.classList.toggle("filled", !!clip);
      btn.classList.toggle("sel", mode === "edit" && i === selected);
      btn.classList.toggle("pending", pending === i);
      btn.querySelector(".lp-lab").textContent = padLabel(clip);
    });
  }

  function setLayer(next) {
    layer = next;
    LAYERS.forEach((id) => {
      const b = $("lpLayer-" + id);
      if (b) b.classList.toggle("on", id === layer);
    });
    $("lpEditSnapshot").hidden = layer !== "snapshot";
    $("lpEditOne").hidden = !isKnobLayer(layer);
    $("lpEditMsg").hidden = layer !== "message";
    relabelParams();
    renderGrid();
  }

  function setMode(next) {
    mode = next;
    $("lpModeEdit").classList.toggle("on", mode === "edit");
    $("lpModePlay").classList.toggle("on", mode === "play");
    $("lpEditBox").hidden = mode !== "edit";
    cancelPending();
    renderGrid();
  }

  function setPerformanceMode(enabled) {
    performanceMode = !!enabled;
    if (performanceMode) setMode("play");
  }

  function syncFormFromDeck() {
    $("lpTitle").value = deck.title || "";
    $("lpBpm").value = String(deck.bpm);
    $("lpQuantize").value = deck.quantize;
  }

  function applyForm() {
    deck.title = $("lpTitle").value.trim();
    let bpm = Math.round(Number($("lpBpm").value) || 120);
    if (bpm < 40) bpm = 40;
    if (bpm > 240) bpm = 240;
    deck.bpm = bpm;
    $("lpBpm").value = String(bpm);
    deck.quantize = $("lpQuantize").value;
    clockOrigin = performance.now();
  }

  function pulseBeat() {
    const el = $("lpBeat");
    if (!el) return;
    if (deck.quantize === "off") {
      el.classList.remove("on");
      return;
    }
    const period = 60000 / deck.bpm;
    const phase = (performance.now() - clockOrigin) % period;
    el.classList.toggle("on", phase < 80);
  }

  function buildGrid() {
    const grid = $("lpGrid");
    grid.innerHTML = "";
    for (let i = 0; i < GRID; i++) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "lp-pad";
      btn.innerHTML =
        '<span class="lp-idx">' +
        (i + 1) +
        '</span><span class="lp-lab"></span>';
      btn.addEventListener("click", () => {
        if (mode === "edit") {
          selected = i;
          const clip = deck.layers[layer][i];
          if (clip) publishClip(clip);
          renderGrid();
        } else {
          launch(i);
        }
      });
      grid.appendChild(btn);
    }
  }

  LAYERS.forEach((id) => {
    const b = $("lpLayer-" + id);
    if (b) b.addEventListener("click", () => setLayer(id));
  });
  $("lpModeEdit").addEventListener("click", () => setMode("edit"));
  $("lpModePlay").addEventListener("click", () => setMode("play"));
  $("lpStore").addEventListener("click", storeSelected);
  $("lpClear").addEventListener("click", clearSelected);
  $("lpSave").addEventListener("click", async () => {
    applyForm();
    if (!deck.title) {
      if (!ensureNamed()) return;
    }
    try {
      if (isFlowLocalHost()) {
        const d = await saveDeckToLibrary(deck);
        storesSinceSave = 0;
        storeDeck(deck);
        note("saved “" + deck.title + "” to FlowLocal (" + (d.name || "deck") + ")", "ok");
      } else {
        downloadDeck(deck);
        storesSinceSave = 0;
        storeDeck(deck);
        note("saved “" + deck.title + "”", "ok");
      }
    } catch (err) {
      note(String(err.message || err), "err");
    }
  });
  $("lpOpen").addEventListener("click", async () => {
    try {
      if (isFlowLocalHost()) {
        deck = await openDeckFromLibrary();
      } else {
        deck = await openDeckFile();
      }
      storesSinceSave = 0;
      storeDeck(deck);
      syncFormFromDeck();
      renderGrid();
      note("loaded “" + (deck.title || deck.id) + "”", "ok");
    } catch (err) {
      if (String(err.message) === "cancelled") return;
      note(String(err.message || err), "err");
    }
  });
  $("lpExample").addEventListener("click", async () => {
    try {
      deck = await openExampleDeck();
      storesSinceSave = 0;
      storeDeck(deck);
      syncFormFromDeck();
      renderGrid();
      note("loaded example “" + (deck.title || deck.id) + "”", "ok");
    } catch (err) {
      note(String(err.message || err), "err");
    }
  });
  $("lpNew").addEventListener("click", () => {
    if (!confirm("Replace the current deck? Knob pads reset to 0…1000.")) return;
    deck = emptyDeck();
    storesSinceSave = 0;
    syncFormFromDeck();
    if (!ensureNamed()) {
      renderGrid();
      return;
    }
    storeDeck(deck);
    renderGrid();
    note("new deck “" + deck.title + "”", "ok");
  });
  ["lpTitle", "lpBpm", "lpQuantize"].forEach((id) => {
    $(id).addEventListener("change", () => {
      applyForm();
      storeDeck(deck);
    });
  });

  function bindPair(rangeId, valId) {
    $(rangeId).addEventListener("input", () => {
      $(valId).value = $(rangeId).value;
      preview();
    });
    $(valId).addEventListener("change", () => {
      $(rangeId).value = String(clamp1000($(valId).value));
      $(valId).value = $(rangeId).value;
      preview();
    });
  }
  for (let i = 1; i <= 4; i++) bindPair("lpP" + i, "lpP" + i + "Val");
  bindPair("lpOne", "lpOneVal");
  $("lpPattern").addEventListener("change", () => {
    relabelParams();
    preview();
  });
  $("lpMessage").addEventListener("change", preview);
  $("lpLayerMsg").addEventListener("change", preview);

  buildGrid();
  fillPatternSelect();
  syncFormFromDeck();
  setLayer("snapshot");
  setMode("edit");
  renderGrid();
  if (!beatTimer) beatTimer = setInterval(pulseBeat, 40);

  if (deck.title) {
    note("deck “" + deck.title + "” (autosave on)", "ok");
  } else {
    note("name the deck, then store clips on pads", "");
  }

  return {
    refreshPatterns: fillPatternSelect,
    setPerformanceMode
  };
}

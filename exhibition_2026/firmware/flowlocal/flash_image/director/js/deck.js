/**
 * Launchpad deck — local Director cache (JSON), not a panel .pfs show.
 *
 * Layers: snapshots, K1–K4 (param/1..4), message. Knob pads start loaded
 * with 0..1000 across the 16 buttons. Autosave counts Store/Clear only.
 */

export const LAYERS = ["snapshot", "k1", "k2", "k3", "k4", "message"];
export const GRID = 16;
export const AUTOSAVE_EVERY = 3;
export const STORAGE_KEY = "pf-director-launch-deck-v2";

export function emptyLayer() {
  return Array.from({ length: GRID }, () => null);
}

export function knobChannel(layer) {
  if (layer === "k1") return 0;
  if (layer === "k2") return 1;
  if (layer === "k3") return 2;
  if (layer === "k4") return 3;
  return -1;
}

export function isKnobLayer(layer) {
  return knobChannel(layer) >= 0;
}

/** 16 steps from 0 to 1000 inclusive. */
export function knobSpreadValue(i) {
  return Math.round((i * 1000) / (GRID - 1));
}

export function defaultKnobLayer(channel) {
  return Array.from({ length: GRID }, (_, i) => {
    const value = knobSpreadValue(i);
    return {
      kind: "knob",
      channel,
      value,
      label: String(value)
    };
  });
}

export function emptyDeck() {
  return {
    version: 1,
    id: "deck-" + new Date().toISOString().slice(0, 10),
    title: "",
    bpm: 120,
    quantize: "off",
    layers: {
      snapshot: emptyLayer(),
      k1: defaultKnobLayer(0),
      k2: defaultKnobLayer(1),
      k3: defaultKnobLayer(2),
      k4: defaultKnobLayer(3),
      message: emptyLayer()
    }
  };
}

export function normalizeDeck(raw) {
  const base = emptyDeck();
  if (!raw || typeof raw !== "object") return base;
  const out = { ...base, ...raw };
  out.version = 1;
  out.id = String(out.id || base.id);
  out.title = String(out.title || "");
  let bpm = Math.round(Number(out.bpm) || 120);
  if (bpm < 40) bpm = 40;
  if (bpm > 240) bpm = 240;
  out.bpm = bpm;
  out.quantize =
    out.quantize === "beat" || out.quantize === "bar" ? out.quantize : "off";
  out.layers = {};
  for (const layer of LAYERS) {
    const src = (raw.layers && raw.layers[layer]) || [];
    const ch = knobChannel(layer);
    const hasAny = Array.isArray(src) && src.some((c) => c && typeof c === "object");
    if (ch >= 0 && !hasAny) {
      out.layers[layer] = defaultKnobLayer(ch);
      continue;
    }
    out.layers[layer] = emptyLayer();
    for (let i = 0; i < GRID; i++) {
      out.layers[layer][i] = normalizeClip(layer, src[i]);
    }
    if (ch >= 0 && !out.layers[layer].some(Boolean)) {
      out.layers[layer] = defaultKnobLayer(ch);
    }
  }
  return out;
}

function clamp1000(n) {
  let v = Math.round(Number(n));
  if (!Number.isFinite(v)) v = 500;
  if (v < 0) v = 0;
  if (v > 1000) v = 1000;
  return v;
}

function normalizeClip(layer, clip) {
  if (!clip || typeof clip !== "object") return null;
  if (layer === "snapshot") {
    const param = [500, 500, 500, 500];
    const src = Array.isArray(clip.param) ? clip.param : [];
    for (let i = 0; i < 4; i++) param[i] = clamp1000(src[i]);
    const pattern = String(clip.pattern || "").trim();
    if (!pattern) return null;
    return {
      kind: "snapshot",
      pattern,
      param,
      message: clip.message == null ? "" : String(clip.message),
      label: String(clip.label || pattern)
    };
  }
  if (isKnobLayer(layer) || clip.kind === "knob") {
    const ch = isKnobLayer(layer) ? knobChannel(layer) : Number(clip.channel);
    const channel = ch >= 0 && ch <= 3 ? ch : 0;
    return {
      kind: "knob",
      channel,
      value: clamp1000(clip.value),
      label: String(clip.label || clamp1000(clip.value))
    };
  }
  const message = clip.message == null ? "" : String(clip.message);
  return {
    kind: "message",
    message,
    label: String(clip.label || message || "(clear)")
  };
}

export function padLabel(clip) {
  if (!clip) return "";
  if (clip.kind === "snapshot") return String(clip.pattern || clip.label || "").slice(0, 14);
  if (clip.kind === "knob" || clip.kind === "speed" || clip.kind === "hue") {
    return String(clip.value);
  }
  const m = String(clip.message || "");
  return m ? m.slice(0, 14) : "(clear)";
}

export function loadStoredDeck() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    return normalizeDeck(JSON.parse(raw));
  } catch (_) {
    return null;
  }
}

export function storeDeck(deck) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(deck));
}

export function downloadDeck(deck) {
  const body = JSON.stringify(normalizeDeck(deck), null, 2);
  const blob = new Blob([body], { type: "application/json" });
  const a = document.createElement("a");
  const raw = String(deck.title || deck.id || "deck");
  const slug = raw
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_|_$/g, "")
    .slice(0, 32);
  a.href = URL.createObjectURL(blob);
  a.download = (slug || "deck") + ".json";
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 2000);
}

export function deckFilename(deck) {
  const raw = String((deck && (deck.title || deck.id)) || "deck");
  const slug = raw
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_|_$/g, "")
    .slice(0, 32);
  return (slug || "deck") + ".json";
}

export async function saveDeckToLibrary(deck, filename, opts) {
  const { putLibraryFile, listLibrary } = await import("./library.js");
  const name = filename || deckFilename(deck);
  if (!(opts && opts.overwrite)) {
    try {
      const files = await listLibrary("launchpads");
      if (files.some((f) => f.name === name)) {
        if (!window.confirm("Overwrite " + name + " on FlowLocal?")) {
          throw new Error("cancelled");
        }
      }
    } catch (err) {
      if (String(err.message) === "cancelled") throw err;
    }
  }
  const body = JSON.stringify(normalizeDeck(deck), null, 2);
  const d = await putLibraryFile("launchpads", name, body, "application/json");
  return { ...d, name };
}

export async function openDeckFromLibrary(name) {
  const { getLibraryFile, pickLibraryFile } = await import("./library.js");
  const file = name ? { name } : await pickLibraryFile("launchpads");
  const r = await getLibraryFile("launchpads", file.name);
  const text = await r.text();
  return normalizeDeck(JSON.parse(text));
}

export function openDeckFile() {
  return new Promise((resolve, reject) => {
    const input = document.createElement("input");
    input.type = "file";
    input.accept = "application/json,.json";
    input.onchange = () => {
      const file = input.files && input.files[0];
      if (!file) {
        reject(new Error("cancelled"));
        return;
      }
      const reader = new FileReader();
      reader.onload = () => {
        try {
          resolve(normalizeDeck(JSON.parse(String(reader.result))));
        } catch (err) {
          reject(err);
        }
      };
      reader.onerror = () => reject(reader.error || new Error("read failed"));
      reader.readAsText(file);
    };
    input.click();
  });
}

/** Bundled Launch example under examples/ (ready-to-perform pad grid). */
export async function openExampleDeck(
  path = "./examples/simone_performative_deck.json"
) {
  const r = await fetch(path, { cache: "no-store" });
  if (!r.ok) throw new Error("example deck missing");
  return normalizeDeck(await r.json());
}

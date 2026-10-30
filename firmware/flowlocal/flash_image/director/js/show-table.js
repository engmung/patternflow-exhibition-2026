/**
 * Director → ESP32 show table (PFST v1 + v2).
 *
 * Director Open/Save uses this packed table. The panel walks the same bytes
 * — no ArduinoJson, no heap parser. JSON is only for local catalog/cache
 * (knob names), not for performances.
 *
 * Layout (byte-identical for v1 and v2):
 *   header 76 bytes
 *     magic[4] "PFST"
 *     version u8 = 1 | 2
 *     flags   u8 bit0 = loop
 *     length  u16  — seconds (v1) or deciseconds (v2)
 *     cueCount u16
 *     poolBytes u16
 *     title[32]  NUL-padded UTF-8
 *     id[32]     NUL-padded UTF-8
 *   string pool  poolBytes (NUL-terminated strings)
 *   cues         cueCount × 16 bytes
 *     t u16      — seconds (v1) or deciseconds (v2)
 *     flags u8   PATTERN=1 PARAM1=2 PARAM2=4 PARAM3=8 PARAM4=16 MESSAGE=32
 *                EASE=64 (v2 only: lerp set params toward each channel's next)
 *     reserved u8
 *     patternOff u16  0xFFFF = none; else byte offset in pool
 *     param[4] u16    0..1000, ignored unless the matching PARAM bit is set
 *     messageOff u16  0xFFFF = none; offset 0 is empty (clear banner)
 *
 * See docs/pfst-v2-proposal.md (upstream Patternflow).
 */

import {
  normalizePerformance,
  durationOf,
  PFST_VERSION,
  PFST_VERSION_2,
  clamp1000
} from "./performance.js";

export const PFST_MAGIC = "PFST";
export { PFST_VERSION, PFST_VERSION_2 };
export const PFST_HEADER_BYTES = 76;
export const PFST_CUE_BYTES = 16;
export const PFST_MAX_CUES = 256;
export const PFST_MAX_POOL = 4096;
export const PFST_OFF_NONE = 0xffff;

export const FLAG_PATTERN = 1;
export const FLAG_PARAM1 = 2;
export const FLAG_PARAM2 = 4;
export const FLAG_PARAM3 = 8;
export const FLAG_PARAM4 = 16;
export const FLAG_MESSAGE = 32;
export const FLAG_EASE = 64;
const FLAG_PARAM_ANY = FLAG_PARAM1 | FLAG_PARAM2 | FLAG_PARAM3 | FLAG_PARAM4;

function u8(n) {
  return n & 255;
}

function pad32(str) {
  const out = new Uint8Array(32);
  const s = String(str || "");
  for (let i = 0; i < 32 && i < s.length; i++) out[i] = s.charCodeAt(i) & 255;
  return out;
}

function writeU16(view, offset, value) {
  view.setUint16(offset, value & 0xffff, true);
}

class StringPool {
  constructor() {
    this.bytes = [0]; // offset 0 = empty string
    this.map = Object.create(null);
    this.map[""] = 0;
  }

  intern(raw) {
    const s = raw == null ? "" : String(raw);
    if (this.map[s] != null) return this.map[s];
    const off = this.bytes.length;
    for (let i = 0; i < s.length; i++) this.bytes.push(s.charCodeAt(i) & 255);
    this.bytes.push(0);
    this.map[s] = off;
    return off;
  }

  finish() {
    if (this.bytes.length > PFST_MAX_POOL) {
      throw new Error(
        "string pool is " + this.bytes.length + " bytes (max " + PFST_MAX_POOL + ")"
      );
    }
    return new Uint8Array(this.bytes);
  }
}

/**
 * Pack a Director performance into a PFST ArrayBuffer (v1 or v2 per perf.version).
 */
export function encodeShowTable(perf) {
  const p = normalizePerformance(perf);
  const cuesIn = p.timeline || [];
  if (cuesIn.length > PFST_MAX_CUES) {
    throw new Error(
      "too many cues (" + cuesIn.length + ", max " + PFST_MAX_CUES + ")"
    );
  }
  const version =
    p.version === PFST_VERSION_2 ? PFST_VERSION_2 : PFST_VERSION;
  const tick = version === PFST_VERSION_2 ? 10 : 1;
  const pool = new StringPool();
  const cueBytes = new Uint8Array(cuesIn.length * PFST_CUE_BYTES);
  const cueView = new DataView(cueBytes.buffer);

  cuesIn.forEach((cue, i) => {
    const base = i * PFST_CUE_BYTES;
    let t = Math.round(Number(cue.t) * tick);
    if (t < 0) t = 0;
    if (t > 65535) t = 65535;
    let flags = 0;
    let patternOff = PFST_OFF_NONE;
    let messageOff = PFST_OFF_NONE;
    const param = [0, 0, 0, 0];
    if (cue.pattern != null && String(cue.pattern).length) {
      flags |= FLAG_PATTERN;
      patternOff = pool.intern(String(cue.pattern));
    }
    if (cue.param) {
      for (let c = 0; c < 4; c++) {
        if (cue.param[c] == null) continue;
        flags |= FLAG_PARAM1 << c;
        param[c] = clamp1000(cue.param[c]);
      }
    }
    if (cue.message != null) {
      flags |= FLAG_MESSAGE;
      messageOff = pool.intern(String(cue.message));
    }
    if (version === PFST_VERSION_2 && cue.ease === true && flags & FLAG_PARAM_ANY) {
      flags |= FLAG_EASE;
    }
    writeU16(cueView, base, t);
    cueBytes[base + 2] = u8(flags);
    cueBytes[base + 3] = 0;
    writeU16(cueView, base + 4, patternOff);
    writeU16(cueView, base + 6, param[0]);
    writeU16(cueView, base + 8, param[1]);
    writeU16(cueView, base + 10, param[2]);
    writeU16(cueView, base + 12, param[3]);
    writeU16(cueView, base + 14, messageOff);
  });

  const poolBytes = pool.finish();
  const out = new Uint8Array(PFST_HEADER_BYTES + poolBytes.length + cueBytes.length);
  const view = new DataView(out.buffer);
  out[0] = 80;
  out[1] = 70;
  out[2] = 83;
  out[3] = 84; // PFST
  out[4] = version;
  out[5] = p.loop ? 1 : 0;
  writeU16(view, 6, Math.min(65535, Math.round(durationOf(p) * tick)));
  writeU16(view, 8, cuesIn.length);
  writeU16(view, 10, poolBytes.length);
  out.set(pad32(p.title), 12);
  out.set(pad32(p.id), 44);
  out.set(poolBytes, PFST_HEADER_BYTES);
  out.set(cueBytes, PFST_HEADER_BYTES + poolBytes.length);
  return out;
}

function cString(bytes, start, maxLen) {
  let s = "";
  for (let i = 0; i < maxLen; i++) {
    const b = bytes[start + i];
    if (!b) break;
    s += String.fromCharCode(b);
  }
  return s;
}

function poolString(pool, off) {
  if (off === PFST_OFF_NONE || off >= pool.length) return "";
  let s = "";
  for (let i = off; i < pool.length; i++) {
    if (pool[i] === 0) break;
    s += String.fromCharCode(pool[i]);
  }
  return s;
}

/**
 * Unpack a PFST buffer into a Director performance (same cues the panel plays).
 * Session-only fields (MQTT channel, required shortlist, utcStart) are defaults;
 * timeline pattern names are merged into required on load.
 */
export function decodeShowTable(buf) {
  const bytes = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  if (bytes.length < PFST_HEADER_BYTES) {
    throw new Error("file too small");
  }
  if (
    bytes[0] !== 80 ||
    bytes[1] !== 70 ||
    bytes[2] !== 83 ||
    bytes[3] !== 84
  ) {
    throw new Error("not a PFST table");
  }
  const version = bytes[4];
  if (version !== PFST_VERSION && version !== PFST_VERSION_2) {
    throw new Error("unsupported PFST version " + version);
  }
  const tick = version === PFST_VERSION_2 ? 10 : 1;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const loop = (bytes[5] & 1) !== 0;
  const length = view.getUint16(6, true) / tick;
  const cueCount = view.getUint16(8, true);
  const poolLen = view.getUint16(10, true);
  if (cueCount > PFST_MAX_CUES || poolLen > PFST_MAX_POOL) {
    throw new Error("table too large");
  }
  const need = PFST_HEADER_BYTES + poolLen + cueCount * PFST_CUE_BYTES;
  if (bytes.length < need) {
    throw new Error("truncated table");
  }
  const title = cString(bytes, 12, 32);
  const id = cString(bytes, 44, 32);
  const pool = bytes.subarray(PFST_HEADER_BYTES, PFST_HEADER_BYTES + poolLen);
  const cuesOff = PFST_HEADER_BYTES + poolLen;
  const timeline = [];
  for (let i = 0; i < cueCount; i++) {
    const base = cuesOff + i * PFST_CUE_BYTES;
    const flags = bytes[base + 2];
    const cue = { t: view.getUint16(base, true) / tick };
    if (version === PFST_VERSION_2 && flags & FLAG_EASE) cue.ease = true;
    if (flags & FLAG_PATTERN) {
      cue.pattern = poolString(pool, view.getUint16(base + 4, true));
    }
    if (flags & FLAG_PARAM_ANY) {
      const param = [null, null, null, null];
      for (let c = 0; c < 4; c++) {
        if (flags & (FLAG_PARAM1 << c)) {
          param[c] = view.getUint16(base + 6 + c * 2, true);
        }
      }
      cue.param = param;
    }
    if (flags & FLAG_MESSAGE) {
      cue.message = poolString(pool, view.getUint16(base + 14, true));
    }
    if (cue.pattern != null || cue.param || cue.message != null) {
      timeline.push(cue);
    }
  }
  const required = [];
  const seen = Object.create(null);
  for (const cue of timeline) {
    const name = String(cue.pattern || "").trim();
    if (!name) continue;
    const key = name.toLowerCase();
    if (seen[key]) continue;
    seen[key] = true;
    required.push(name);
  }
  return normalizePerformance({
    version,
    id: id || undefined,
    title: title || undefined,
    loop,
    length: length || (version === PFST_VERSION_2 ? 0.1 : 1),
    required: required.length ? required : undefined,
    timeline
  });
}

function isPfst(bytes) {
  return (
    bytes.length >= 4 &&
    bytes[0] === 80 &&
    bytes[1] === 70 &&
    bytes[2] === 83 &&
    bytes[3] === 84
  );
}

/**
 * File picker: .pfs is the performance format. Old .json shows still load once.
 */
export async function openPerformanceFile() {
  return new Promise((resolve, reject) => {
    const input = document.createElement("input");
    input.type = "file";
    input.accept = ".pfs,application/octet-stream,.json,application/json";
    input.onchange = () => {
      const file = input.files && input.files[0];
      if (!file) {
        reject(new Error("cancelled"));
        return;
      }
      const reader = new FileReader();
      reader.onload = () => {
        try {
          const bytes = new Uint8Array(reader.result);
          if (isPfst(bytes)) {
            resolve(decodeShowTable(bytes));
            return;
          }
          const text = new TextDecoder("utf-8").decode(bytes);
          resolve(normalizePerformance(JSON.parse(text)));
        } catch (err) {
          reject(err);
        }
      };
      reader.onerror = () => reject(reader.error || new Error("read failed"));
      reader.readAsArrayBuffer(file);
    };
    input.click();
  });
}

export function showTableFilename(perf) {
  const raw = String((perf && (perf.id || perf.title)) || "performance");
  const slug = raw
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_|_$/g, "")
    .slice(0, 32);
  return (slug || "performance") + ".pfs";
}

export function downloadShowTable(perf, filename) {
  const bytes = encodeShowTable(perf);
  const blob = new Blob([bytes], { type: "application/octet-stream" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = filename || showTableFilename(perf);
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 2000);
  return bytes.length;
}

export async function saveShowToLibrary(perf, filename, opts) {
  const { putLibraryFile, listLibrary } = await import("./library.js");
  const name = filename || showTableFilename(perf);
  if (!(opts && opts.overwrite)) {
    try {
      const files = await listLibrary("shows");
      if (files.some((f) => f.name === name)) {
        if (!window.confirm("Overwrite " + name + " on FlowLocal?")) {
          throw new Error("cancelled");
        }
      }
    } catch (err) {
      if (String(err.message) === "cancelled") throw err;
    }
  }
  const bytes = encodeShowTable(perf);
  await putLibraryFile("shows", name, bytes, "application/octet-stream");
  return { name, bytes: bytes.length };
}

export async function openShowFromLibrary(name) {
  const { getLibraryFile, pickLibraryFile } = await import("./library.js");
  const file = name ? { name } : await pickLibraryFile("shows");
  const r = await getLibraryFile("shows", file.name);
  const buf = await r.arrayBuffer();
  const bytes = new Uint8Array(buf);
  if (
    bytes.length >= 4 &&
    bytes[0] === 80 &&
    bytes[1] === 70 &&
    bytes[2] === 83 &&
    bytes[3] === 84
  ) {
    return decodeShowTable(bytes);
  }
  const text = new TextDecoder("utf-8").decode(bytes);
  return normalizePerformance(JSON.parse(text));
}

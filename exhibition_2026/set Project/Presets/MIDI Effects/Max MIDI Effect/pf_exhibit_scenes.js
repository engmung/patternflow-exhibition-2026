// ===========================================================
// PF Exhibit Scenes - Max for Live MIDI Effect logic
//
// The panel sends Program Change n whenever its pattern changes (K4, idle
// return, boot) and again when its RTP session (re)connects. FlowLocal
// forwards it to USB and sends the last one again when
//   - that Program Change could not be written (USB buffer full, no host),
//   - its USB port (re)mounts or the laptop wakes,
//   - the laptop sends the resync request, Control Change 119 = 127 on
//     channel 16 (bytes BF 77 7F). This device sends it once after it loads
//     and whenever its Resync button is pressed,
//   - the operator presses "Send current pattern to Live" on
//     http://192.168.66.1/.
// Right after every Program Change it forwards or re-sends, FlowLocal also
// sends Control Change 118 on channel 16 with the program number as the
// value (BF 76 n). That mirror is the fallback for a Live that does not hand
// Program Change to this device: Alt CC (off by default) reads it as a
// program. Alt CC matches the CC number on any channel: Live hands a MIDI
// track's input to its devices merged onto channel 1, so the mirror most
// likely reaches [midiin] as B0 76 n, not BF 76 n. The panel's own CCs are
// 24-27, so 118 never collides.
// This device fires Live scene n + offset for each program, so the sound
// follows the pattern.
//
// A Live-only restart leaves USB mounted, and an untouched panel sends
// nothing, so it is the resync request on load that brings the current
// pattern back. FlowLocal answers it only when the panel has sent a program
// since FlowLocal started; with no answer, Autostart plays Start PC's scene.
//
// Wiring in pf_exhibit_scenes.maxpat:
//   [midiin] -> [deferlow] -> [js pf_exhibit_scenes.js]
//   [midiin] -> [gate 1] (right inlet) -> [midiout]    Thru, closed by default
//   [js] outlet 2 -> [midiout]                         the resync request
//   [js] outlet 3 -> [gate 1] (left inlet)             Thru on/off
//   [live.thisdevice] -> [t b b b] -> reset both status lines, then "init"
// The js parses the raw byte stream itself (running status, real-time
// bytes, sysex). [midiin] outputs one int per byte from the scheduler
// thread; [js] moves such calls to the low-priority queue and Max does not
// document whether that keeps their order. [deferlow] does: it always
// appends to the tail of the queue, so the js already runs at low priority
// and handles every byte immediately, in order.
// The status lines are reset in the patch, not here: a comment saves the
// text it shows, so without the reset a device saved after testing would
// open showing "ready" even when this file cannot be found.
//
// Routing. The device sits alone on a MIDI track with MIDI From "Patternflow
// Exhibit" and MIDI To "Patternflow Exhibit" (channel 16), so the resync
// request reaches FlowLocal. Thru is off: sending the panel's traffic back
// out to FlowLocal would only be thrown away there. Tracks that play the
// panel's notes take MIDI From "Patternflow Exhibit" themselves. Nothing can
// loop: FlowLocal discards every message from the laptop except the resync
// request and never passes anything on to the panel, and this device sends
// the request only on load and on the button - never in answer to a message
// it received. (With Thru on, [midiout] gets bytes from two threads, the
// gate's and the js's, and a request could be interleaved with panel bytes.
// Thru is for other routings, where the request has nowhere to go anyway.)
//
// Scene numbers are counted the way Program Change is, everywhere - in
// the UI, the status line and the LiveAPI path: the top scene is 0, so
// Program Change 0 fires "live_set scenes 0". Start PC is a program number
// too, shifted by Offset like every other: with the exhibit firmware the
// panel's patterns are Origin = 0 and the six artists = 1..6, and it boots
// and returns from idle to pattern 1, which is Start PC's default.
//
// Runs in the classic [js] object: ES5 only (no let/const/arrow/template).
// autowatch is off on purpose: a recompile resets every global and
// live.thisdevice does not bang again, so an edited js needs the device
// reloaded anyway (delete + re-drag, or reopen the set).
// License: MIT
// ===========================================================

autowatch = 0;
inlets = 1;
outlets = 4;
// 0 -> info comment   ("set <text>"): ready / waiting / errors
// 1 -> last comment   ("set <text>"): last program and the scene it fired
// 2 -> [midiout]      MIDI this device sends: the resync request, one int per byte
// 3 -> [gate 1]       Thru: 1 = MIDI in passes to MIDI out, 0 = closed

var AUTOSTART_DELAY_MS = 1500;   // let the rest of the set finish loading
var PLAY_CHECK_DELAY_MS = 500;   // after the start row: start transport if still stopped
var RESYNC_DELAY_MS = 500;       // after init: Live's MIDI output is up; FlowLocal's
                                 // answer lands well before Autostart looks

// FlowLocal's side of the contract (flowlocal: src/fl_midi_bridge.h, EXHIBIT.md)
var RESYNC_REQUEST = [0xBF, 119, 127];  // Control Change 119 = 127, channel 16
var ALT_CC_DEFAULT = 118;               // the program mirror's CC (FL_EXHIBIT_PROGRAM_CC, sent on channel 16)
var ALT_CC_MAX = 118;                   // 119 is the resync request's CC, 120+ are mode messages

// Scripting names (varname) of the live.* objects in the patch.
var UI_ENABLE = "Enable";
var UI_AUTOSTART = "Autostart";
var UI_START_PC = "StartPC";
var UI_OFFSET = "Offset";
var UI_THRU = "Thru";
var UI_ALT_CC = "AltCC";
var UI_ALT_CC_NUM = "AltCCNum";

// Settings. The live.* objects own the saved values; these mirror them.
var optEnable = 1;
var optAutostart = 0;
var optStartPC = 1;      // a program number: fires scene optStartPC + optOffset;
                         // 1 = the panel's boot/idle pattern
var optOffset = 0;       // scene index = program + offset (Start PC included)
var optThru = 0;         // 1 = the patch's gate passes MIDI in -> out
var optAltCC = 0;        // 1 = CC optAltCCNum (any channel) with value v acts as Program Change v
var optAltCCNum = ALT_CC_DEFAULT;

// State
var initialized = false;  // true once live.thisdevice has banged
var pendingProgram = -1;  // program received before init, applied at init
var lastProgram = -1;     // last program received (any state)
var lastFiredScene = -1;  // 0-based index of the last scene this device fired
var bytesIn = 0;          // MIDI bytes received (for "dump": did anything arrive?)
var programsIn = 0;       // Program Changes parsed (did Live pass them?)
var altccIn = 0;          // Alt CC's CC (any channel), counted even with Alt CC off
var resyncsSent = 0;      // resync requests this device sent

var autostartTask = new Task(runAutostart);
var playCheckTask = new Task(playCheck);
var resyncTask = new Task(autoResync);

// -- MIDI byte parser -----------------------------------------------------

var runStatus = 0;     // current channel-voice status (0x80..0xEF), 0 = none
var runNeed = 0;       // data bytes that status takes (1 or 2)
var dataBytes = [0, 0];
var dataCount = 0;
var inSysex = false;

function dataLengthFor(status) {
  var hi = status & 0xF0;
  return (hi === 0xC0 || hi === 0xD0) ? 1 : 2;
}

function feedByte(value) {
  var b = Number(value);
  if (isNaN(b)) return;
  b = Math.floor(b);
  if (b < 0 || b > 255) return;
  bytesIn++;

  // Real-time (F8..FF) may appear between any two bytes, even inside a
  // message or a sysex. It changes nothing: no reset, no running status.
  if (b >= 0xF8) return;

  if (b >= 0x80) {
    dataCount = 0;               // a status byte aborts any partial message
    if (b === 0xF0) {            // sysex start: swallow until any status byte
      inSysex = true;
      runStatus = 0;
      return;
    }
    inSysex = false;
    if (b >= 0xF1) {             // system common + F7: cancel running status;
      runStatus = 0;             // their data bytes are dropped as orphans
      return;
    }
    runStatus = b;
    runNeed = dataLengthFor(b);
    return;
  }

  // Data byte
  if (inSysex || runStatus === 0) return;
  dataBytes[dataCount] = b;
  dataCount++;
  if (dataCount < runNeed) return;
  dataCount = 0;                 // running status: the next data byte starts
  dispatch(runStatus, dataBytes[0], dataBytes[1]); // another message of this kind
}

function dispatch(status, d1, d2) {
  var hi = status & 0xF0;
  var ch = (status & 0x0F) + 1;
  if (hi === 0xC0) {
    programsIn++;
    onProgram(d1, "PC " + d1 + " ch" + ch);
    return;
  }
  // Alt CC matches the number on any channel. FlowLocal sends its mirror on
  // channel 16, but Live merges a track's input onto channel 1 before the
  // first device, so the channel seen here says nothing about the source.
  // The label shows the channel that did arrive.
  if (hi === 0xB0 && d1 === optAltCCNum) {
    altccIn++;
    if (optAltCC) onProgram(d2, "CC" + d1 + "=" + d2 + " ch" + ch);
  }
}

// Max entry points for raw bytes from [midiin]
function msg_int(v) { feedByte(v); }
function msg_float(v) { feedByte(v); }
function list() {
  var a = arrayfromargs(arguments);
  for (var i = 0; i < a.length; i++) feedByte(a[i]);
}

// -- program -> scene -------------------------------------------------------

function onProgram(program, label) {
  lastProgram = program;
  if (!optEnable) {
    showLast(label + ": ignored (Enable off)");
    return;
  }
  if (!initialized) {
    // LiveAPI is not usable before live.thisdevice bangs: keep the latest.
    pendingProgram = program;
    showInfo("waiting for Live - " + label + " held");
    return;
  }
  fireScene(program + optOffset, true, label);
}

// Fire scene idx (0 = top). dedupe: skip when it is the scene this device
// fired last, the transport is running and that row is still what plays -
// that is the panel (or FlowLocal) re-sending the current Program Change
// after a reconnect or a resync request, or FlowLocal's CC mirror arriving
// right after a Program Change that already fired, and re-firing would
// restart the music for nothing.
function fireScene(idx, dedupe, label) {
  var set = new LiveAPI("live_set");
  if (!set || Number(set.id) === 0) {
    showInfo("Live set not reachable");
    return false;
  }
  var count = set.getcount("scenes");
  if (idx < 0 || idx >= count) {
    showLast(label + " -> scene " + idx + " missing (scenes 0-" + (count - 1) + ")");
    return false;
  }
  var scene = new LiveAPI("live_set scenes " + idx);
  if (!scene || Number(scene.id) === 0) {
    showLast(label + " -> scene " + idx + " not reachable");
    return false;
  }
  var name = sceneName(scene);
  if (dedupe && idx === lastFiredScene && firstInt(set.get("is_playing")) === 1 &&
      rowStillOn(idx)) {
    showLast(label + " -> " + idx + ": " + name + " (already on)");
    return true;
  }
  scene.call("fire");
  lastFiredScene = idx;
  showLast(label + " -> " + idx + ": " + name);
  showInfo("fired " + clockText());
  return true;
}

// Is scene row idx still what the tracks play (or are about to)? The scene
// this device fired last can have been replaced without it knowing: another
// scene launched with the mouse, Stop All Clips, one-shot clips that ended.
// A track counts when it has a clip in that row and is playing it with
// nothing else queued, or has it queued. One such track is enough, so a
// one-shot layer ending does not restart the rest. A row with no clips at
// all (a silence scene) has nothing to check and stays "on".
function rowStillOn(idx) {
  var set = new LiveAPI("live_set");
  var tracks = set.getcount("tracks");
  var withClip = 0;
  for (var t = 0; t < tracks; t++) {
    var slot = new LiveAPI("live_set tracks " + t + " clip_slots " + idx);
    if (!slot || Number(slot.id) === 0) continue;
    if (firstInt(slot.get("has_clip")) !== 1) continue;
    withClip++;
    var track = new LiveAPI("live_set tracks " + t);
    var playing = intOr(track.get("playing_slot_index"), -99);
    var fired = intOr(track.get("fired_slot_index"), -99);
    // fired: -1 nothing queued, -2 a stop button queued, n a clip queued
    if (fired === idx) return true;
    if (playing === idx && fired === -1) return true;
  }
  return withClip === 0;
}

// -- resync request -> FlowLocal --------------------------------------------

// Out through [midiout] and the track's MIDI To. One int per byte, in order.
function sendResync() {
  for (var i = 0; i < RESYNC_REQUEST.length; i++) outlet(2, RESYNC_REQUEST[i]);
  resyncsSent++;
}

// Once per load, RESYNC_DELAY_MS after init. Enable off parks the device:
// no request then (the Resync button still sends one).
function autoResync() {
  if (!initialized || !optEnable) return;
  sendResync();
}

// The Resync button through [prepend resync]. A live.text in button mode
// sends a bang on press, so this gets "resync bang". A number above 0 (a
// toggle-mode live.text, "resync 1") sends too, and so does a bare "resync";
// a 0 (release, or a value restored at load) sends nothing.
function resync(v) {
  if (arguments.length > 0 && v !== "bang" && !(firstNumber(v) > 0)) return;
  sendResync();
  showInfo("resync request sent " + clockText());
}

// -- init / autostart -------------------------------------------------------

// Bang from [live.thisdevice] (through the "init" message box).
function init() {
  initialized = true;
  pullSettings(this.patcher);
  outlet(3, optThru);
  var set = new LiveAPI("live_set");
  var count = (set && Number(set.id) !== 0) ? set.getcount("scenes") : 0;
  showInfo("ready - scenes 0-" + (count - 1) + (optEnable ? "" : " - Enable off"));
  if (pendingProgram >= 0) {
    var p = pendingProgram;
    pendingProgram = -1;
    if (optEnable) fireScene(p + optOffset, true, "PC " + p + " (held)");
  }
  if (optEnable) resyncTask.schedule(RESYNC_DELAY_MS);
  if (optEnable && optAutostart) autostartTask.schedule(AUTOSTART_DELAY_MS);
}

function runAutostart() {
  if (!initialized || !optEnable || !optAutostart) return;
  // A program that already arrived (the resync answer, or the panel) is the
  // panel's real state: keep it.
  if (lastFiredScene < 0) {
    fireScene(optStartPC + optOffset, false, "autostart PC " + optStartPC);
  }
  playCheckTask.schedule(PLAY_CHECK_DELAY_MS);
}

// Firing a scene with clips starts the transport by itself; an empty start
// scene does not. Start it here only if it is still stopped.
function playCheck() {
  if (!initialized) return;
  var set = new LiveAPI("live_set");
  if (!set || Number(set.id) === 0) return;
  if (firstInt(set.get("is_playing")) !== 1) set.call("start_playing");
}

// Read the saved values straight from the live.* objects, so init does not
// depend on whether they already output their restored values.
function pullSettings(patcher) {
  optEnable = readUI(patcher, UI_ENABLE, optEnable) ? 1 : 0;
  optAutostart = readUI(patcher, UI_AUTOSTART, optAutostart) ? 1 : 0;
  optStartPC = clampInt(readUI(patcher, UI_START_PC, optStartPC), 0, 127);
  optOffset = clampInt(readUI(patcher, UI_OFFSET, optOffset), -127, 127);
  optThru = readUI(patcher, UI_THRU, optThru) ? 1 : 0;
  optAltCC = readUI(patcher, UI_ALT_CC, optAltCC) ? 1 : 0;
  optAltCCNum = clampInt(readUI(patcher, UI_ALT_CC_NUM, optAltCCNum), 1, ALT_CC_MAX);
}

function readUI(patcher, name, fallback) {
  try {
    if (!patcher) return fallback;
    var obj = patcher.getnamed(name);
    if (!obj) return fallback;
    var v = firstNumber(obj.getvalueof());
    return isNaN(v) ? fallback : v;
  } catch (e) {
    return fallback;
  }
}

// -- messages from the UI (through [prepend ...]) ---------------------------

function enable(v) {
  optEnable = firstNumber(v) ? 1 : 0;
  if (!optEnable) {
    autostartTask.cancel();
    resyncTask.cancel();
    showInfo("Enable off - programs ignored");
  } else {
    showInfo(initialized ? "Enable on" : "waiting for Live");
  }
}

function autostart(v) {
  optAutostart = firstNumber(v) ? 1 : 0;
}

function startpc(v) {
  optStartPC = clampInt(v, 0, 127);
}

function offset(v) {
  var o = clampInt(v, -127, 127);
  if (o !== optOffset) lastFiredScene = -1; // the mapping moved: no dedupe
  optOffset = o;
}

// The gate is set from here, not straight from the toggle, so init can put
// it right from the saved value even if the toggle did not output it.
function thru(v) {
  optThru = firstNumber(v) ? 1 : 0;
  outlet(3, optThru);
}

function altcc(v) {
  optAltCC = firstNumber(v) ? 1 : 0;
}

function altccnum(v) {
  optAltCCNum = clampInt(v, 1, ALT_CC_MAX);
}

// -- test / debug (message boxes in the patching view) ----------------------

// "test N": behave as if Program Change N arrived.
function test(n) {
  var p = clampInt(n, 0, 127);
  onProgram(p, "test PC " + p);
}

// "gostart": fire the Start PC's scene (+ offset) and make sure the
// transport runs.
function gostart() {
  if (!initialized) {
    showInfo("waiting for Live");
    return;
  }
  fireScene(optStartPC + optOffset, false, "start PC " + optStartPC);
  playCheckTask.schedule(PLAY_CHECK_DELAY_MS);
}

// "dump" (Max Console): bytes=0 after a test send = nothing reaches the
// device (track input / Monitor); bytes>0 with programs=0 after an unmapped
// "note 60" then "pc 1" = Live passes notes but not Program Change (turn
// Alt CC on). altccIn counts FlowLocal's CC 118 mirrors (on any channel:
// Live merges them onto channel 1) even while Alt CC is off. Messages mapped
// with Ctrl-M never reach a track, so test unmapped.
function dump() {
  post("pf_exhibit_scenes: init=" + initialized +
       " enable=" + optEnable + " autostart=" + optAutostart +
       " startPC=" + optStartPC + " offset=" + optOffset +
       " thru=" + optThru + " altCC=" + optAltCC + " altCCNum=" + optAltCCNum +
       " bytes=" + bytesIn + " programs=" + programsIn + " altccIn=" + altccIn +
       " resyncsSent=" + resyncsSent + " lastProgram=" + lastProgram +
       " lastFiredScene=" + lastFiredScene + " pending=" + pendingProgram + "\n");
}

function bang() { dump(); }

function anything() {
  // unknown messages are ignored on purpose
}

// -- helpers ----------------------------------------------------------------

function showInfo(text) { outlet(0, "set", text); }
function showLast(text) { outlet(1, "set", text); }

function isArrayLike(v) {
  return v !== null && typeof v === "object" && typeof v.length === "number";
}

function firstNumber(v) {
  if (isArrayLike(v)) v = v.length > 0 ? v[0] : NaN;
  return Number(v);
}

function firstInt(v) {
  var n = firstNumber(v);
  return isNaN(n) ? 0 : Math.floor(n);
}

// Like firstInt, but a value that is not a number reads as `fallback`, so a
// failed read can never pass for slot 0.
function intOr(v, fallback) {
  var n = firstNumber(v);
  return isNaN(n) ? fallback : Math.floor(n);
}

function clampInt(v, lo, hi) {
  var n = Math.round(firstNumber(v));
  if (isNaN(n)) n = lo;
  if (n < lo) n = lo;
  if (n > hi) n = hi;
  return n;
}

function sceneName(api) {
  var v = api.get("name");
  var s = isArrayLike(v) ? Array.prototype.join.call(v, " ") : String(v);
  s = s.replace(/^"+/, "").replace(/"+$/, "");
  return s === "" ? "(unnamed)" : s;
}

function pad2(n) { return (n < 10 ? "0" : "") + n; }

function clockText() {
  var t = new Date();
  return pad2(t.getHours()) + ":" + pad2(t.getMinutes()) + ":" + pad2(t.getSeconds());
}

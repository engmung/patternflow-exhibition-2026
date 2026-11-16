#!/usr/bin/env node
// Offline test for ableton/pf-exhibit-scenes/pf_exhibit_scenes.js.
//
// Runs the device script inside a Node vm with small fakes for what Max
// provides (outlet, post, Task, arrayfromargs, LiveAPI, this.patcher), then
// feeds raw MIDI bytes the way [midiin] does - one int per byte - and checks
// which scenes get fired. Tasks run on a virtual clock, in time order. A fake
// FlowLocal reads what the js sends out of [midiout] and answers the resync
// request (CC 119 = 127, channel 16) the way flowlocal/src/fl_midi_bridge.h
// does: the last Program Change, then its CC 118 channel 16 mirror. The fake
// Live merges every channel-voice message onto channel 1 before the js sees
// it, as Live does with a MIDI track's input (Cycling '74 forum threads on
// [midiin] channels); `mergeChannels: false` keeps the source channels, so
// the Alt CC tests run both ways.
// It proves the parser (test 26: FlowLocal's SysEx status replies), the
// program -> scene logic, the resync request and Alt CC, and (test 19)
// reads the .maxpat next to the js to check the wiring
// the js relies on: midiin -> deferlow -> js, the Thru gate, js -> midiout,
// the status-line resets on live.thisdevice, and that every setting name,
// default and message has its counterpart. It cannot prove how Live or Max
// behave.
//
// Usage:  node tools/test_pf_exhibit_scenes.js [path/to/pf_exhibit_scenes.js]
// Optional ES5 syntax check: set ACORN_PATH to an acorn package directory
// (or have acorn resolvable by require) - the [js] object is ES5 only.

"use strict";

var fs = require("fs");
var path = require("path");
var vm = require("vm");

var jsPath = process.argv[2] ||
  path.join(__dirname, "..", "ableton", "pf-exhibit-scenes", "pf_exhibit_scenes.js");
var source = fs.readFileSync(jsPath, "utf8");

var failures = 0;
var passes = 0;
function check(cond, label) {
  if (cond) { passes++; }
  else { failures++; console.log("FAIL: " + label); }
}

// ---- optional ES5 syntax check ------------------------------------------
(function es5Check() {
  var acorn = null;
  var candidates = [process.env.ACORN_PATH, "acorn"].filter(Boolean);
  for (var i = 0; i < candidates.length && !acorn; i++) {
    try { acorn = require(candidates[i]); } catch (e) { /* next */ }
  }
  if (!acorn) {
    console.log("note: acorn not found, ES5 syntax check skipped (set ACORN_PATH)");
    return;
  }
  try {
    acorn.parse(source, { ecmaVersion: 5, sourceType: "script" });
    console.log("ES5 syntax check: OK (acorn, ecmaVersion 5)");
    passes++;
  } catch (e) {
    failures++;
    console.log("FAIL: not ES5: " + e.message);
  }
})();

// ---- fake Max / Live / FlowLocal environment ------------------------------

// The fake set: scenes by name, and TRACKS tracks. A scene named "(empty)"
// has no clip on any track (a silence row); every other row has a clip on
// every track. Firing a row launches at once (tracks with a clip there play
// it, the others stop: playing_slot_index -2) - unless live.quantized is set
// and the transport runs, when it only queues (fired_slot_index) until
// live.launchQueued().
var TRACKS = 2;
var RESYNC_BYTES = [0xBF, 0x77, 0x7F];

var UI_DEFAULTS = { Enable: 1, Autostart: 0, StartPC: 1, Offset: 0, Thru: 0, AltCC: 0, AltCCNum: 118 };
function uiWith(over) {
  var ui = {};
  Object.keys(UI_DEFAULTS).forEach(function (k) { ui[k] = UI_DEFAULTS[k]; });
  Object.keys(over || {}).forEach(function (k) { ui[k] = over[k]; });
  return ui;
}

function makeEnv(opts) {
  opts = opts || {};
  var env = {
    now: 0,                         // virtual ms, advanced by runTasks
    live: {
      ready: false,                 // flips to true when the test "bangs" live.thisdevice
      mergeChannels: opts.mergeChannels !== false, // track input reaches devices on channel 1
      scenes: opts.scenes || ["Origin", "Artist A / One", "Artist B / Two", "Artist C / Three",
                               "Artist D / Four", "Artist E / Five", "Artist F / Six"],
      playing: 0,
      quantized: false,
      playingSlot: [],              // per track, Track.playing_slot_index
      firedSlot: [],                // per track, Track.fired_slot_index
      fired: [],                    // scene indices, in order
      calls: [],                    // every LiveAPI call, "path:fn"
      constructedBeforeReady: 0
    },
    ui: opts.ui || uiWith(),
    outlets: [[], [], [], []],
    midiOut: [],                    // every int the js sent to [midiout] (outlet 2)
    // FlowLocal as the js sees it through Live: it reads the track's MIDI To,
    // answers the resync request, and forwards what the panel sends.
    flowlocal: {
      program: -1,                  // last program the panel sent; -1 = none since FlowLocal started
      channel: 1,                   // the panel's channel
      mirror: true,                 // FL_EXHIBIT_PROGRAM_CC 118 on channel 16
      livePassesPC: true,           // false: Live drops Program Change before [midiin]
      requests: 0,
      discarded: 0,
      seen: 0, status: 0, data: []
    },
    posts: [],
    tasks: []
  };
  for (var t = 0; t < TRACKS; t++) { env.live.playingSlot.push(-1); env.live.firedSlot.push(-1); }

  function rowHasClip(track, idx) {
    return idx < env.live.scenes.length && env.live.scenes[idx] !== "(empty)";
  }
  env.rowHasClip = rowHasClip;

  function LiveAPI(a, b) {
    var p = (typeof a === "string") ? a : b;
    if (!env.live.ready) {
      env.live.constructedBeforeReady++;
      this.id = 0;
      this.path = p;
      return;
    }
    this.path = p;
    var m;
    if (p === "live_set") {
      this.id = 1;
    } else if ((m = /^live_set scenes (\d+)$/.exec(p))) {
      var idx = parseInt(m[1], 10);
      this.id = idx < env.live.scenes.length ? 100 + idx : 0;
      this.sceneIndex = idx;
    } else if ((m = /^live_set tracks (\d+)$/.exec(p))) {
      this.trackIndex = parseInt(m[1], 10);
      this.id = this.trackIndex < TRACKS ? 1000 + this.trackIndex : 0;
    } else if ((m = /^live_set tracks (\d+) clip_slots (\d+)$/.exec(p))) {
      this.trackIndex = parseInt(m[1], 10);
      this.slotIndex = parseInt(m[2], 10);
      this.id = (this.trackIndex < TRACKS && this.slotIndex < env.live.scenes.length)
        ? 2000 + this.trackIndex * 100 + this.slotIndex : 0;
    } else {
      this.id = 0;
    }
  }
  LiveAPI.prototype.getcount = function (child) {
    if (this.path === "live_set" && child === "scenes") return env.live.scenes.length;
    if (this.path === "live_set" && child === "tracks") return TRACKS;
    return 0;
  };
  LiveAPI.prototype.get = function (prop) {
    if (this.path === "live_set" && prop === "is_playing") return [env.live.playing];
    if (this.sceneIndex !== undefined && prop === "name") return [env.live.scenes[this.sceneIndex]];
    if (this.slotIndex !== undefined && prop === "has_clip") {
      return [rowHasClip(this.trackIndex, this.slotIndex) ? 1 : 0];
    }
    if (this.trackIndex !== undefined && this.slotIndex === undefined) {
      if (prop === "playing_slot_index") return [env.live.playingSlot[this.trackIndex]];
      if (prop === "fired_slot_index") return [env.live.firedSlot[this.trackIndex]];
    }
    return [0];
  };
  LiveAPI.prototype.call = function (fn) {
    env.live.calls.push(this.path + ":" + fn);
    if (fn === "fire" && this.sceneIndex !== undefined) {
      var idx = this.sceneIndex;
      env.live.fired.push(idx);
      var queue = env.live.quantized && env.live.playing === 1;
      for (var t = 0; t < TRACKS; t++) {
        if (queue) {
          env.live.firedSlot[t] = rowHasClip(t, idx) ? idx : -2;
        } else {
          env.live.playingSlot[t] = rowHasClip(t, idx) ? idx : -2;
          env.live.firedSlot[t] = -1;
        }
      }
      if (!queue && env.live.scenes[idx] !== "(empty)") env.live.playing = 1;
    }
    if (fn === "start_playing") env.live.playing = 1;
  };
  // The quantization boundary: queued rows start.
  env.live.launchQueued = function () {
    for (var t = 0; t < TRACKS; t++) {
      if (env.live.firedSlot[t] !== -1) {
        env.live.playingSlot[t] = env.live.firedSlot[t];
        env.live.firedSlot[t] = -1;
      }
    }
  };

  function Task(fn, obj) {
    this.fn = fn;
    this.obj = obj;
    this.due = -1;
    env.tasks.push(this);
  }
  Task.prototype.schedule = function (ms) { this.due = env.now + (ms || 0); };
  Task.prototype.cancel = function () { this.due = -1; };
  Task.prototype.repeat = function () { this.due = env.now; };

  var patcher = {
    getnamed: function (name) {
      if (!(name in env.ui)) return null;
      return { getvalueof: function () { return env.ui[name]; } };
    }
  };

  var ctx = {
    outlet: function (n) {
      var args = Array.prototype.slice.call(arguments, 1);
      env.outlets[n].push(args.join(" "));
      if (n === 2) {
        check(args.length === 1 && typeof args[0] === "number", "outlet 2 gets one number per call (a MIDI byte)");
        env.midiOut.push(args[0]);
      }
    },
    post: function () { env.posts.push(Array.prototype.slice.call(arguments).join(" ")); },
    arrayfromargs: function (args) { return Array.prototype.slice.call(args); },
    LiveAPI: LiveAPI,
    Task: Task,
    patcher: patcher,
    messagename: ""
  };
  vm.createContext(ctx);
  vm.runInContext(source, ctx, { filename: path.basename(jsPath) });

  // What FlowLocal sends for the panel's program p (forward or re-send).
  function flowlocalSends(p) {
    var f = env.flowlocal;
    var b = [];
    if (f.livePassesPC) b.push(0xC0 | ((f.channel - 1) & 0x0F), p);
    if (f.mirror) b.push(0xBF, 118, p);
    return b;
  }

  // FlowLocal reads the bytes the js sent since the last call. Like
  // isResyncRequest(): CC 119 on channel 16 with a value above 0 re-sends the
  // last program; every other message is discarded. The answer arrives after
  // the js call that sent the request has returned, as it would over USB.
  env.deliver = function () {
    var f = env.flowlocal;
    var reply = [];
    while (f.seen < env.midiOut.length) {
      var b = env.midiOut[f.seen++];
      if (b >= 0x80) { f.status = b; f.data = []; continue; }
      if (!f.status) continue;
      f.data.push(b);
      var need = ((f.status & 0xF0) === 0xC0 || (f.status & 0xF0) === 0xD0) ? 1 : 2;
      if (f.data.length < need) continue;
      if (f.status === 0xBF && f.data[0] === 119 && f.data[1] > 0) {
        f.requests++;
        if (f.program >= 0) reply = reply.concat(flowlocalSends(f.program));
      } else {
        f.discarded++;
      }
      f.data = [];
    }
    if (reply.length) env.bytes(reply);
  };

  env.ctx = ctx;
  // Bytes through the track into [midiin]. With mergeChannels (the default,
  // like Live) a channel-voice status byte arrives on channel 1.
  env.bytes = function (list) {
    for (var i = 0; i < list.length; i++) {
      var b = list[i];
      if (env.live.mergeChannels && typeof b === "number" && b >= 0x80 && b <= 0xEF) b = b & 0xF0;
      ctx.msg_int(b);
    }
  };
  // The panel changes pattern: FlowLocal forwards the Program Change and its mirror.
  env.panelProgram = function (p) {
    env.flowlocal.program = p;
    env.bytes(flowlocalSends(p));
  };
  // A message into the js (a prepend / message box), then FlowLocal reads.
  env.msg = function (code) {
    vm.runInContext(code, ctx);
    env.deliver();
  };
  env.init = function () {
    env.live.ready = true;
    vm.runInContext("init()", ctx);   // same `this` as Max: the global object
    env.deliver();
  };
  // Run due tasks in time order, up to `until` ms (default: all). Tasks can
  // schedule tasks.
  env.runTasks = function (until) {
    for (var guard = 0; guard < 50; guard++) {
      var next = null;
      for (var i = 0; i < env.tasks.length; i++) {
        var t = env.tasks[i];
        if (t.due >= 0 && (until === undefined || t.due <= until) && (!next || t.due < next.due)) next = t;
      }
      if (!next) break;
      env.now = Math.max(env.now, next.due);
      next.due = -1;
      next.fn.call(next.obj || ctx);
      env.deliver();
    }
    if (until !== undefined) env.now = Math.max(env.now, until);
  };
  env.last = function (n) {
    var o = env.outlets[n];
    return o.length ? o[o.length - 1] : "";
  };
  return env;
}

function same(a, b) { return JSON.stringify(a) === JSON.stringify(b); }
function hex(list) { return list.map(function (b) { return ("0" + b.toString(16).toUpperCase()).slice(-2); }).join(" "); }
function onlyRequests(bytes) {
  if (bytes.length % 3 !== 0) return false;
  for (var i = 0; i < bytes.length; i++) if (bytes[i] !== RESYNC_BYTES[i % 3]) return false;
  return true;
}

// ---- tests ---------------------------------------------------------------

// 1. Declared outlets match what the patch wires (0 info, 1 last, 2 midiout, 3 gate)
(function () {
  var env = makeEnv();
  check(env.ctx.outlets === 4 && env.ctx.inlets === 1, "js declares 1 inlet / 4 outlets");
  check(env.ctx.autowatch === 0, "autowatch is off");
})();

// 2. No LiveAPI before live.thisdevice; pending program applied at init
(function () {
  var env = makeEnv();
  env.bytes([0xC0, 3]);
  check(env.live.constructedBeforeReady === 0, "no LiveAPI constructed before init");
  check(env.live.fired.length === 0, "nothing fired before init");
  check(/waiting for Live/.test(env.last(0)), "info says waiting before init: " + env.last(0));
  env.bytes([0xC0, 4]);            // the latest one wins
  env.init();
  check(same(env.live.fired, [4]), "pending PC 4 fired at init, got " + JSON.stringify(env.live.fired));
  check(/-> 4: Artist D \/ Four$/.test(env.last(1)), "status names the scene: " + env.last(1));
})();

// 3. Basic PC after init, 0-based: PC 0 -> top scene
(function () {
  var env = makeEnv();
  env.init();
  env.bytes([0xC0, 0]);
  check(same(env.live.fired, [0]), "PC 0 fires scene index 0");
  check(/PC 0 ch1 -> 0: Origin$/.test(env.last(1)), "status for PC 0: " + env.last(1));
  env.bytes([0xC5, 2]);            // any channel
  check(same(env.live.fired, [0, 2]), "PC on channel 6 also fires");
})();

// 4. Running status: C0 05 06 = two program changes
(function () {
  var env = makeEnv();
  env.init();
  env.bytes([0xC0, 5, 6]);
  check(same(env.live.fired, [5, 6]), "running status PC 5 then 6, got " + JSON.stringify(env.live.fired));
  // CC running status must not turn into program changes
  env.bytes([0xB0, 24, 64, 24, 65, 25, 10]);
  check(same(env.live.fired, [5, 6]), "CC running-status data bytes fire nothing");
  // ...and a status change back to PC works mid-stream
  env.bytes([0xC0, 1]);
  check(same(env.live.fired, [5, 6, 1]), "PC after CC running status fires");
})();

// 5. Real-time bytes inside messages change nothing
(function () {
  var env = makeEnv();
  env.init();
  env.bytes([0xC0, 0xF8, 2]);                  // clock between status and data
  check(same(env.live.fired, [2]), "F8 between PC status and data still fires 2");
  env.bytes([0xB0, 24, 0xFE, 64, 0xC0, 0xFA, 3]);
  check(same(env.live.fired, [2, 3]), "active sensing / start interleaved");
  // between the two data bytes of a CC (checked through the Alt CC path)
  vm.runInContext("altcc(1); altccnum(102)", env.ctx);
  env.bytes([0xBF, 102, 0xF8, 5, 102, 0xFC, 6]);
  check(same(env.live.fired, [2, 3, 5, 6]), "F8/FC between CC data bytes keep the message whole, got " +
        JSON.stringify(env.live.fired));
})();

// 6. Sysex payload never parses as data; system common cancels running status
(function () {
  var env = makeEnv();
  env.init();
  env.bytes([0xC0, 1]);                        // running status = C0
  env.bytes([0xF0, 0x7D, 0x03, 0x04, 0xF7]);   // sysex with data bytes 3, 4
  check(same(env.live.fired, [1]), "sysex data bytes do not fire (running status cancelled)");
  env.bytes([0x05]);                           // orphan data after F7
  check(same(env.live.fired, [1]), "orphan data byte after sysex ignored");
  env.bytes([0xC0, 2, 0xF2, 0x10, 0x20, 0x03]); // song position pointer + orphan
  check(same(env.live.fired, [1, 2]), "system common data and orphan bytes ignored");
  env.bytes([0xF0, 0x01, 0xC0, 0x04]);         // unterminated sysex, then PC 4
  check(same(env.live.fired, [1, 2, 4]), "a status byte ends an unterminated sysex");
})();

// 7. Partial message aborted by a new status byte
(function () {
  var env = makeEnv();
  env.init();
  env.bytes([0xB0, 24, 0xC0, 3]);
  check(same(env.live.fired, [3]), "partial CC then PC 3 fires 3 only");
  env.bytes([0x90, 60, 127, 0x80, 60, 0]);     // notes
  check(same(env.live.fired, [3]), "notes fire nothing");
  env.bytes([0xD0, 5, 0xE0, 1, 2]);            // channel pressure (1 byte), pitch bend (2)
  check(same(env.live.fired, [3]), "channel pressure / pitch bend fire nothing");
})();

// 8. Range guard
(function () {
  var env = makeEnv();
  env.init();
  env.bytes([0xC0, 7]);                        // 7 scenes: indices 0..6
  check(env.live.fired.length === 0, "PC 7 with 7 scenes fires nothing");
  check(/-> scene 7 missing \(scenes 0-6\)/.test(env.last(1)), "status says missing: " + env.last(1));
  env.bytes([0xC0, 127]);
  check(env.live.fired.length === 0, "PC 127 fires nothing");
})();

// 9. Offset (and negative offset guard)
(function () {
  var env = makeEnv({ ui: uiWith({ Offset: 1 }) });
  env.init();
  env.bytes([0xC0, 0]);
  check(same(env.live.fired, [1]), "offset 1: PC 0 fires index 1");
  vm.runInContext("offset(-2)", env.ctx);
  env.bytes([0xC0, 1]);
  check(same(env.live.fired, [1]), "offset -2: PC 1 -> index -1 fires nothing");
  env.bytes([0xC0, 3]);
  check(same(env.live.fired, [1, 1]), "offset -2: PC 3 fires index 1 again (offset change clears dedupe)");
})();

// 10. Enable off ignores programs, also the pending one at init
(function () {
  var env = makeEnv({ ui: uiWith({ Enable: 0, Autostart: 1 }) });
  vm.runInContext("enable(0)", env.ctx);       // live.toggle output before init
  env.bytes([0xC0, 2]);
  env.init();
  env.runTasks();
  check(env.live.fired.length === 0, "Enable off: nothing fired, no autostart");
  check(env.live.calls.indexOf("live_set:start_playing") < 0, "Enable off: transport not started");
  vm.runInContext("enable(1)", env.ctx);
  env.bytes([0xC0, 2]);
  check(same(env.live.fired, [2]), "after Enable on, PC fires");
})();

// 11. Dedupe: same scene while playing is skipped; after stop it fires again
(function () {
  var env = makeEnv();
  env.init();
  env.bytes([0xC0, 3]);
  env.bytes([0xC0, 3]);                        // reconnect re-send
  check(same(env.live.fired, [3]), "same PC while playing is not re-fired");
  check(/already on/.test(env.last(1)), "status says already on: " + env.last(1));
  env.live.playing = 0;
  env.bytes([0xC0, 3]);
  check(same(env.live.fired, [3, 3]), "same PC after transport stop fires again");
  env.bytes([0xC0, 4, 0xC0, 3]);
  check(same(env.live.fired, [3, 3, 4, 3]), "3 -> 4 -> 3 fires every change");
})();

// 12. Autostart: Start PC is a program number (0 = top scene); default 1 is
//     the panel's boot/idle pattern. Transport started only if still stopped.
(function () {
  var env = makeEnv();                         // StartPC 1 (the default)
  vm.runInContext("autostart(1)", env.ctx);
  env.ui.Autostart = 1;
  env.init();
  check(env.live.fired.length === 0, "autostart waits for its task");
  env.runTasks();
  check(same(env.live.fired, [1]), "autostart with Start PC 1 fires index 1, got " + JSON.stringify(env.live.fired));
  check(env.live.calls.indexOf("live_set:start_playing") < 0, "clip scene started transport: no start_playing");
  check(/autostart PC 1 -> 1: Artist A \/ One$/.test(env.last(1)), "autostart status: " + env.last(1));
})();
(function () {
  var env = makeEnv({ ui: uiWith({ Autostart: 1, StartPC: 0 }), scenes: ["(empty)", "A", "B"] });
  env.init();
  env.runTasks();
  check(same(env.live.fired, [0]), "Start PC 0 fires the top row");
  check(env.live.calls.indexOf("live_set:start_playing") >= 0, "empty start row: transport started by playCheck");
})();

// 13. Autostart keeps a program that already arrived
(function () {
  var env = makeEnv({ ui: uiWith({ Autostart: 1 }) });
  env.bytes([0xC0, 2]);
  env.init();
  env.runTasks();
  check(same(env.live.fired, [2]), "held PC 2 wins over Start PC, got " + JSON.stringify(env.live.fired));
  check(env.live.calls.indexOf("live_set:start_playing") < 0, "clip scene already started transport: no start_playing");
})();

// 14. Start PC whose scene is out of range reports and does not fire
(function () {
  var env = makeEnv({ ui: uiWith({ Autostart: 1, StartPC: 20 }) });
  env.init();
  env.runTasks();
  check(env.live.fired.length === 0, "Start PC 20 of 7 scenes fires nothing");
  check(/autostart PC 20 -> scene 20 missing/.test(env.outlets[1].join("|")), "status for a missing start row");
})();

// 14b. Offset moves the Start PC's scene exactly like a panel program: with
//      a spare row on top (Offset 1), autostart and the panel's boot PC 1
//      land on the same row.
(function () {
  var scenes = ["spare", "Origin", "Artist A", "Artist B"];
  var env = makeEnv({ ui: uiWith({ Autostart: 1, Offset: 1 }), scenes: scenes });
  env.init();
  env.runTasks();
  check(same(env.live.fired, [2]), "Offset 1: autostart of Start PC 1 fires row 2, got " + JSON.stringify(env.live.fired));
  check(/autostart PC 1 -> 2: Artist A$/.test(env.last(1)), "offset autostart status: " + env.last(1));
  vm.runInContext("gostart()", env.ctx);
  check(same(env.live.fired, [2, 2]), "Offset 1: gostart fires row 2 too");

  var env2 = makeEnv({ ui: uiWith({ Offset: 1 }), scenes: scenes });
  env2.init();
  env2.bytes([0xC0, 1]);
  check(same(env2.live.fired, [2]), "Offset 1: the panel's boot PC 1 fires the same row 2");

  var env3 = makeEnv({ ui: uiWith({ Autostart: 1, Offset: -5 }), scenes: scenes });
  env3.init();
  env3.runTasks();
  check(env3.live.fired.length === 0, "Start PC + Offset below 0 fires nothing");
  check(/autostart PC 1 -> scene -4 missing/.test(env3.outlets[1].join("|")), "status for negative start row");
})();

// 15. Alt CC: off by default, CC 118 by default, any channel - whether Live
//     merges the track's input onto channel 1 (as it does) or not
(function () {
  [true, false].forEach(function (merge) {
    var how = merge ? " [Live merges to ch1]" : " [channels kept]";
    var env = makeEnv({ mergeChannels: merge });
    check(env.ctx.optAltCC === 0 && env.ctx.optAltCCNum === 118, "js defaults: Alt CC off, CC 118" + how);
    env.init();
    env.bytes([0xBF, 118, 5]);
    check(env.live.fired.length === 0, "Alt CC off: CC 118 fires nothing" + how);
    vm.runInContext("altcc(1)", env.ctx);
    env.bytes([0xBF, 117, 5, 0xB0, 24, 5, 0xB0, 25, 5, 0xB0, 26, 5, 0xB0, 27, 5]); // other CCs, the panel's
    check(env.live.fired.length === 0, "Alt CC ignores other CC numbers" + how);
    env.bytes([0xBF, 118, 5]);                 // FlowLocal's mirror as it leaves FlowLocal
    check(same(env.live.fired, [5]), "Alt CC on: CC 118 value 5 from channel 16 fires index 5" + how);
    check(new RegExp("^set CC118=5 ch" + (merge ? "1" : "16") + " -> 5: Artist E / Five$").test(env.last(1)),
          "Alt CC status shows the channel that arrived: " + env.last(1) + how);
    env.bytes([0xB0, 118, 6]);                 // channel 1
    env.bytes([0xBE, 118, 2]);                 // channel 15
    check(same(env.live.fired, [5, 6, 2]), "Alt CC reads CC 118 on any channel" + how);
    env.bytes(RESYNC_BYTES);                   // a request echoed back is never a program
    check(same(env.live.fired, [5, 6, 2]), "CC 119 = 127 fires nothing" + how);
    vm.runInContext("altccnum(20)", env.ctx);
    env.bytes([0xBF, 20, 3, 0xBF, 118, 4]);
    check(same(env.live.fired, [5, 6, 2, 3]), "Alt CC number 20: CC 20 fires, CC 118 no longer does" + how);
    vm.runInContext("altccnum(119)", env.ctx);
    check(env.ctx.optAltCCNum === 118, "Alt CC number stops at 118 (119 is the resync request's CC)" + how);
    vm.runInContext("altccnum(0)", env.ctx);
    check(env.ctx.optAltCCNum === 1, "Alt CC number starts at 1" + how);
    vm.runInContext("altccnum(118); altcc(0)", env.ctx);
    env.bytes([0xBF, 118, 1]);
    check(same(env.live.fired, [5, 6, 2, 3]), "altcc 0 turns the CC path off again" + how);
  });

  var saved = makeEnv({ ui: uiWith({ AltCC: 1, AltCCNum: 30 }) });
  saved.init();
  saved.bytes([0xBF, 30, 4]);
  check(same(saved.live.fired, [4]), "init reads Alt CC and its number from the saved values");
})();

// 16. test / gostart / dump messages
(function () {
  var env = makeEnv();
  env.init();
  vm.runInContext("test(6)", env.ctx);
  check(same(env.live.fired, [6]), "test 6 fires index 6");
  vm.runInContext("gostart()", env.ctx);
  check(same(env.live.fired, [6, 1]), "gostart fires Start PC 1 (row 1) even when another is on");
  vm.runInContext("dump()", env.ctx);
  check(env.posts.length === 1 && /lastFiredScene=1/.test(env.posts[0]), "dump posts state");
  env.bytes([-1, 256, 3.7]);                   // garbage never throws
  check(true, "garbage bytes do not throw");
})();

// 16b. dump counts bytes and parsed programs, so a first test in Live can
//      tell "nothing arrives" from "notes arrive but Program Change does not"
//      (README 9.1: send_test.py "note 60" then "pc 1"), and counts
//      FlowLocal's CC 118 mirrors even while Alt CC is off.
(function () {
  var env = makeEnv();
  env.init();
  env.bytes([0x90, 60, 127, 0x80, 60, 0]);     // note 60 on + off
  vm.runInContext("dump()", env.ctx);
  check(/ bytes=6 programs=0 /.test(env.posts[0]), "dump after a note only: " + env.posts[0]);
  env.bytes([0xC0, 1]);                        // pc 1
  vm.runInContext("dump()", env.ctx);
  check(/ bytes=8 programs=1 /.test(env.posts[1]), "dump after note + PC: " + env.posts[1]);
  vm.runInContext("enable(0)", env.ctx);
  env.bytes([0xF8, 0xC0, 2]);                  // a clock byte counts as a byte
  vm.runInContext("dump()", env.ctx);
  check(/ bytes=11 programs=2 /.test(env.posts[2]), "programs count even with Enable off: " + env.posts[2]);
  env.bytes([-1, 256]);
  vm.runInContext("dump()", env.ctx);
  check(/ bytes=11 /.test(env.posts[3]), "out-of-range values are not counted as bytes");

  [true, false].forEach(function (merge) {
    var e = makeEnv({ mergeChannels: merge });
    e.init();
    e.bytes([0xBF, 118, 3, 0xBF, 118, 4, 0xB0, 118, 1, 0xBE, 118, 2, 0xB0, 24, 9, 0xBF, 117, 1]);
    vm.runInContext("dump()", e.ctx);
    check(/ altCC=0 altCCNum=118 /.test(e.posts[0]) && / altccIn=4 /.test(e.posts[0]),
          "dump counts CC 118 on any channel, and no other CC, with Alt CC off" +
          (merge ? " [Live merges to ch1]: " : " [channels kept]: ") + e.posts[0]);
  });

  var env2 = makeEnv();
  env2.init();
  env2.bytes([0xBF, 118, 3, 0xBF, 118, 4]);
  env2.runTasks();
  vm.runInContext("dump()", env2.ctx);
  check(/ resyncsSent=1 /.test(env2.posts[0]) && / thru=0 /.test(env2.posts[0]), "dump shows requests sent and Thru: " + env2.posts[0]);
  check(env2.live.fired.length === 0, "counting the mirror fires nothing while Alt CC is off");
})();

// 17. "already on" only while that row is really what plays
(function () {
  var env = makeEnv();
  env.init();
  env.bytes([0xC0, 3]);
  // someone launches scene 5 with the mouse; the panel re-sends PC 3
  for (var t = 0; t < TRACKS; t++) env.live.playingSlot[t] = 5;
  env.bytes([0xC0, 3]);
  check(same(env.live.fired, [3, 3]), "row replaced by a manual launch: PC 3 fires again, got " +
        JSON.stringify(env.live.fired));

  // Stop All Clips with the transport still running
  for (t = 0; t < TRACKS; t++) env.live.playingSlot[t] = -1;
  env.bytes([0xC0, 3]);
  check(same(env.live.fired, [3, 3, 3]), "clips stopped: PC 3 fires again");

  // one layer was a one-shot and ended; the other still plays row 3
  env.live.playingSlot[0] = -1;
  env.bytes([0xC0, 3]);
  check(same(env.live.fired, [3, 3, 3]), "one track still on row 3: not re-fired");
  check(/\(already on\)$/.test(env.last(1)), "status says already on: " + env.last(1));

  // another row queued (quantization) on every track, row 3 still sounding
  for (t = 0; t < TRACKS; t++) { env.live.playingSlot[t] = 3; env.live.firedSlot[t] = 5; }
  env.bytes([0xC0, 3]);
  check(same(env.live.fired, [3, 3, 3, 3]), "another row queued: PC 3 fires to win it back");

  // row 3 itself queued (fired, not yet playing): a re-send is not a new launch
  for (t = 0; t < TRACKS; t++) { env.live.playingSlot[t] = 2; env.live.firedSlot[t] = 3; }
  env.bytes([0xC0, 3]);
  check(same(env.live.fired, [3, 3, 3, 3]), "row 3 already queued: not re-fired");

  // a read that fails must not pass for slot 0
  var env2 = makeEnv();
  env2.init();
  env2.bytes([0xC0, 0]);
  for (t = 0; t < TRACKS; t++) { env2.live.playingSlot[t] = "?"; env2.live.firedSlot[t] = -1; }
  env2.bytes([0xC0, 0]);
  check(same(env2.live.fired, [0, 0]), "unreadable slot state re-fires rather than trusting slot 0");
})();

// 18. A silence row (no clips anywhere) re-sent while playing stays put
(function () {
  var env = makeEnv({ scenes: ["Origin", "(empty)", "B"] });
  env.init();
  env.bytes([0xC0, 2]);                        // music on, transport running
  env.bytes([0xC0, 1]);                        // silence row
  check(same(env.live.fired, [2, 1]), "silence row fires");
  env.bytes([0xC0, 1]);
  check(same(env.live.fired, [2, 1]), "silence row re-sent while transport runs: not re-fired");
})();

// 19. The patch around the js (pf_exhibit_scenes.maxpat next to the js).
//     What only the wiring can guarantee: bytes reach the js through
//     [deferlow] (order kept), panel MIDI reaches [midiout] only through the
//     Thru gate the js controls, the js's request reaches [midiout], the
//     status lines are reset without the js, and the saved settings start
//     where the js expects them.
(function () {
  var patchPath = jsPath.replace(/\.js$/, ".maxpat");
  if (!fs.existsSync(patchPath)) {
    failures++;
    console.log("FAIL: patch not found next to the js: " + patchPath);
    return;
  }
  var patcher = JSON.parse(fs.readFileSync(patchPath, "utf8")).patcher;
  var boxes = {};
  patcher.boxes.forEach(function (e) { boxes[e.box.id] = e.box; });
  var lines = patcher.lines.map(function (e) { return e.patchline; });
  function idsWhere(pred) { return Object.keys(boxes).filter(function (id) { return pred(boxes[id]); }); }
  function textIs(t) { return function (b) { return String(b.text || "") === t; }; }
  // destinations "id:inlet" of box src (outlet optional)
  function outPorts(src, outlet) {
    return lines.filter(function (l) { return l.source[0] === src && (outlet === undefined || l.source[1] === outlet); })
                .map(function (l) { return l.destination[0] + ":" + l.destination[1]; }).sort();
  }
  function out(src, outlet) {
    return lines.filter(function (l) { return l.source[0] === src && (outlet === undefined || l.source[1] === outlet); })
                .map(function (l) { return l.destination[0]; });
  }
  // sources "id:outlet" into box dst (inlet optional)
  function intoPorts(dst, inlet) {
    return lines.filter(function (l) { return l.destination[0] === dst && (inlet === undefined || l.destination[1] === inlet); })
                .map(function (l) { return l.source[0] + ":" + l.source[1]; }).sort();
  }
  function into(dst) {
    return lines.filter(function (l) { return l.destination[0] === dst; }).map(function (l) { return l.source[0]; });
  }
  function initialOf(b) {
    var vo = (b.saved_attribute_attributes || {}).valueof || {};
    return vo.parameter_initial_enable && vo.parameter_initial ? vo.parameter_initial[0] : undefined;
  }

  var js = idsWhere(function (b) { return /^js /.test(String(b.text || "")); });
  var midiin = idsWhere(textIs("midiin"));
  var midiout = idsWhere(textIs("midiout"));
  var deferlow = idsWhere(textIs("deferlow"));
  var gate = idsWhere(function (b) { return /^gate\b/.test(String(b.text || "")); });
  check(js.length === 1 && midiin.length === 1 && midiout.length === 1 && deferlow.length === 1 && gate.length === 1,
        "patch has one js, midiin, midiout, deferlow and gate");
  if (js.length !== 1 || midiin.length !== 1 || midiout.length !== 1 || deferlow.length !== 1 || gate.length !== 1) return;
  js = js[0]; midiin = midiin[0]; midiout = midiout[0]; deferlow = deferlow[0]; gate = gate[0];

  // input side
  check(same(outPorts(midiin), [deferlow + ":0", gate + ":1"].sort()),
        "midiin goes only to deferlow and the gate's right (data) inlet, got " + outPorts(midiin).join(", "));
  check(same(outPorts(deferlow), [js + ":0"]), "midiin -> deferlow -> js");
  check(same(into(deferlow), [midiin]), "only midiin feeds deferlow");

  // Thru gate
  check(boxes[gate].text === "gate 1", "the gate is [gate 1]: one outlet, closed until told (got " + boxes[gate].text + ")");
  check(same(intoPorts(gate, 0), [js + ":3"]), "only js outlet 3 opens/closes the gate, got " + intoPorts(gate, 0).join(", "));
  check(same(intoPorts(gate, 1), [midiin + ":0"]), "only midiin feeds the gate's data inlet");
  check(same(outPorts(gate), [midiout + ":0"]), "the gate goes to midiout only");

  // output side: exactly the gate and the js's request
  check(same(intoPorts(midiout), [gate + ":0", js + ":2"].sort()),
        "midiout is fed by the gate and js outlet 2 only, got " + intoPorts(midiout).join(", "));
  check(same(outPorts(js, 2), [midiout + ":0"]), "js outlet 2 -> midiout only");
  check(same(outPorts(js, 3), [gate + ":0"]), "js outlet 3 -> gate control only");

  var thisdevice = idsWhere(textIs("live.thisdevice"))[0];
  var trig = out(thisdevice, 0);
  check(trig.length === 1 && /^(t|trigger) b b b$/.test(String(boxes[trig[0]].text)),
        "live.thisdevice bangs a [t b b b] only");
  trig = trig[0];
  var initBox = out(trig, 0);
  check(initBox.length === 1 && boxes[initBox[0]].text === "init" && same(out(initBox[0]), [js]),
        "trigger's left (last) outlet sends init to the js");
  var info = idsWhere(function (b) { return b.varname === "status_info"; })[0];
  var last = idsWhere(function (b) { return b.varname === "status_last"; })[0];
  function resetsTo(outlet, comment, text) {
    var m = out(trig, outlet);
    return m.length === 1 && boxes[m[0]].maxclass === "message" && boxes[m[0]].text === "set " + text &&
      same(out(m[0]), [comment]) && boxes[comment].text === text;
  }
  check(resetsTo(2, info, "waiting for Live"), "trigger's first outlet resets the info line to 'waiting for Live'");
  check(resetsTo(1, last, "no program yet"), "trigger's middle outlet resets the last line to 'no program yet'");
  check(same(out(js, 0), [info]) && same(out(js, 1), [last]), "js outlet 0 -> info line, 1 -> last line");

  // Settings: every name the js looks up exists as a live.* varname, shown in
  // the device, feeding the js through [prepend <handler>], and saved with the
  // same default the js starts with.
  var env = makeEnv();
  var settings = {
    UI_ENABLE: ["optEnable", "enable"], UI_AUTOSTART: ["optAutostart", "autostart"],
    UI_START_PC: ["optStartPC", "startpc"], UI_OFFSET: ["optOffset", "offset"],
    UI_THRU: ["optThru", "thru"], UI_ALT_CC: ["optAltCC", "altcc"], UI_ALT_CC_NUM: ["optAltCCNum", "altccnum"]
  };
  Object.keys(settings).forEach(function (k) {
    var name = env.ctx[k];
    var hit = idsWhere(function (b) { return b.varname === name && /^live\./.test(b.maxclass); });
    check(typeof name === "string" && hit.length === 1, "js " + k + " = " + name + " is a live.* varname in the patch");
    if (hit.length !== 1) return;
    var b = boxes[hit[0]];
    check(b.presentation === 1, name + " is shown in the device");
    check(initialOf(b) === env.ctx[settings[k][0]],
          name + " saves initial " + initialOf(b) + ", the js starts with " + env.ctx[settings[k][0]]);
    var pre = out(hit[0], 0);
    check(pre.length === 1 && boxes[pre[0]].text === "prepend " + settings[k][1] && same(out(pre[0]), [js]),
          name + " -> [prepend " + settings[k][1] + "] -> js");
  });
  check(UI_DEFAULTS.Thru === 0 && UI_DEFAULTS.AltCC === 0 && UI_DEFAULTS.AltCCNum === 118,
        "(test setup) the fake UI defaults are Thru off, Alt CC off, 118");
  var altNum = idsWhere(function (b) { return b.varname === env.ctx.UI_ALT_CC_NUM; })[0];
  if (altNum) {
    var vo = boxes[altNum].saved_attribute_attributes.valueof;
    check(vo.parameter_mmin === 1 && vo.parameter_mmax === env.ctx.ALT_CC_MAX,
          "Alt CC number box range 1.." + env.ctx.ALT_CC_MAX + " matches the js clamp");
  }

  // Resync: a momentary live.text that is never saved or automated (so no
  // stored value can replay a press at load), -> [prepend resync] -> js.
  var resync = idsWhere(function (b) { return b.varname === "Resync"; });
  check(resync.length === 1, "one Resync button");
  if (resync.length === 1) {
    var rb = boxes[resync[0]];
    var rvo = (rb.saved_attribute_attributes || {}).valueof || {};
    check(rb.maxclass === "live.text" && rb.mode === 0, "Resync is a live.text in button (momentary) mode");
    check(rvo.parameter_invisible === 2 && !rvo.parameter_initial_enable, "Resync is a hidden parameter with no initial value");
    check(rb.presentation === 1, "Resync is shown in the device");
    var rp = out(resync[0], 0);
    check(rp.length === 1 && boxes[rp[0]].text === "prepend resync" && same(out(rp[0]), [js]),
          "Resync -> [prepend resync] -> js");
    check(out(resync[0], 1).length === 0, "nothing hangs off Resync's second outlet");
    // A button-mode live.text sends a bang out its left outlet (Max reference),
    // and [prepend resync] turns a bang into "resync bang": the js must send
    // the request for exactly that message.
    var press = makeEnv();
    press.init();
    press.msg('resync("bang")');
    check(same(press.midiOut, RESYNC_BYTES),
          "the button's press as it arrives ('resync bang') sends the request, got " + hex(press.midiOut));
  }

  into(js).forEach(function (src) {
    var t = String(boxes[src].text || "");
    var m = /^prepend (\w+)$/.exec(t);
    var sel = m ? m[1] : (boxes[src].maxclass === "message" ? t.split(" ")[0] : null);
    if (sel === null) return;
    check(typeof env.ctx[sel] === "function", "message '" + sel + "' into the js has a handler");
  });
})();

// 20. The resync request after load: once, RESYNC_DELAY_MS after init, BF 77 7F
(function () {
  var env = makeEnv();
  env.runTasks();
  env.bytes([0xC0, 2]);                        // held until init
  check(env.midiOut.length === 0, "nothing is sent before live.thisdevice bangs");
  env.init();
  check(env.midiOut.length === 0, "no request at init itself: it waits for the delay");
  check(env.ctx.RESYNC_DELAY_MS > 0 && env.ctx.RESYNC_DELAY_MS < env.ctx.AUTOSTART_DELAY_MS,
        "resync delay is short and ahead of Autostart");
  env.runTasks(env.ctx.RESYNC_DELAY_MS - 1);
  check(env.midiOut.length === 0, "still nothing just before the delay");
  var infoWrites = env.outlets[0].length;
  env.runTasks(env.ctx.RESYNC_DELAY_MS);
  check(same(env.midiOut, RESYNC_BYTES), "one resync request: BF 77 7F, got " + hex(env.midiOut));
  check(env.flowlocal.requests === 1 && env.flowlocal.discarded === 0, "FlowLocal reads it as a resync request");
  check(env.outlets[0].length === infoWrites, "the automatic request does not write the info line");
  env.runTasks();
  check(env.midiOut.length === 3, "the request is sent once per load, not repeated");

  var off = makeEnv({ ui: uiWith({ Enable: 0 }) });
  off.init();
  off.runTasks();
  check(off.midiOut.length === 0, "Enable off: no automatic request");

  var late = makeEnv();
  late.init();
  late.msg("enable(0)");
  late.runTasks();
  check(late.midiOut.length === 0, "Enable switched off before the delay cancels the request");

  var guard = makeEnv();                       // the task re-checks Enable itself, like Autostart
  guard.init();
  vm.runInContext("optEnable = 0", guard.ctx); // Enable off without the cancel
  guard.runTasks();
  check(guard.midiOut.length === 0, "the load request's task does not send while Enable is off");

  var quiet = makeEnv();
  quiet.init();
  quiet.runTasks();
  check(/^set ready - scenes 0-6$/.test(quiet.last(0)),
        "with nothing to answer, the info line stays on ready: " + quiet.last(0));
})();

// 21. Live-only restart: FlowLocal answers the request before Autostart looks
(function () {
  var env = makeEnv({ ui: uiWith({ Autostart: 1 }) });
  env.flowlocal.program = 4;                   // the panel is on 4; USB stayed mounted
  env.init();
  env.runTasks(env.ctx.RESYNC_DELAY_MS);
  check(same(env.live.fired, [4]), "the answer to the load request fires the panel's pattern 4, got " +
        JSON.stringify(env.live.fired));
  check(/^set PC 4 ch1 -> 4: Artist D \/ Four$/.test(env.last(1)), "status for the answer: " + env.last(1));
  env.runTasks();
  check(same(env.live.fired, [4]), "Autostart keeps it: no Start PC scene, got " + JSON.stringify(env.live.fired));
  check(env.live.playing === 1, "transport runs");

  var nothing = makeEnv({ ui: uiWith({ Autostart: 1 }) });
  nothing.init();
  nothing.runTasks();
  check(nothing.flowlocal.requests === 1 && same(nothing.live.fired, [1]),
        "FlowLocal with no pattern yet: no answer, Autostart fires Start PC 1");

  var noPC = makeEnv({ ui: uiWith({ Autostart: 1 }) });
  noPC.flowlocal.program = 5;
  noPC.flowlocal.livePassesPC = false;
  noPC.init();
  noPC.runTasks();
  check(same(noPC.live.fired, [1]), "Live drops Program Change, Alt CC off: only Autostart's scene 1");

  var alt = makeEnv({ ui: uiWith({ Autostart: 1, AltCC: 1 }) });
  alt.flowlocal.program = 5;
  alt.flowlocal.livePassesPC = false;
  alt.init();
  alt.runTasks();
  check(same(alt.live.fired, [5]), "Live drops Program Change, Alt CC on: the CC 118 mirror fires 5, got " +
        JSON.stringify(alt.live.fired));
  check(/^set CC118=5 ch1 -> 5: Artist E \/ Five$/.test(alt.last(1)), "status for the mirror (merged to ch1 by Live): " + alt.last(1));
})();

// 22. The Resync button
(function () {
  var env = makeEnv();
  env.init();
  env.runTasks();                              // the load request
  env.msg('resync("bang")');                   // the button's press: live.text sends bang
  check(same(env.midiOut, RESYNC_BYTES.concat(RESYNC_BYTES)), "a press sends BF 77 7F, got " + hex(env.midiOut));
  check(/^set resync request sent \d\d:\d\d:\d\d$/.test(env.last(0)), "info line says it was sent: " + env.last(0));
  env.msg("resync(0)");
  check(env.midiOut.length === 6, "0 (release, or a value restored at load) sends nothing");
  env.msg("resync()");
  check(env.midiOut.length === 9, "a bare 'resync' message sends");
  env.msg("resync(1)");
  check(env.midiOut.length === 12, "'resync 1' (a toggle-mode press) sends");
  env.msg('resync("set")');
  check(env.midiOut.length === 12, "another word after resync sends nothing");

  env.panelProgram(3);                         // K4: scene 3 plays
  env.msg('resync("bang")');                   // FlowLocal answers PC 3 (+ mirror)
  check(same(env.live.fired, [3]), "the answer while scene 3 plays does not relaunch it, got " + JSON.stringify(env.live.fired));
  check(/^set PC 3 ch1 -> 3: Artist C \/ Three \(already on\)$/.test(env.last(1)), "status: " + env.last(1));

  env.msg("enable(0)");
  var n = env.midiOut.length;
  env.msg('resync("bang")');
  check(env.midiOut.length === n + 3, "the button sends even with Enable off");
  check(same(env.live.fired, [3]), "...and the answer is ignored while Enable is off");
  check(onlyRequests(env.midiOut), "the js never sends anything but the request: " + hex(env.midiOut));
  check(env.flowlocal.requests === 6 && env.flowlocal.discarded === 0, "every request reached FlowLocal as one");
})();

// 23. Thru: the js drives the gate; it never echoes MIDI itself
(function () {
  var env = makeEnv();
  env.msg("thru(1)");                          // the toggle's value can arrive before init
  check(env.last(3) === "1", "thru 1 opens the gate at once");
  env.msg("thru(0)");
  check(env.last(3) === "0", "thru 0 closes it");
  env.init();
  check(env.last(3) === "0", "init sets the gate from the saved Thru (off)");
  check(env.ctx.optThru === 0, "Thru is off by default");

  var on = makeEnv({ ui: uiWith({ Thru: 1 }) });
  on.init();
  check(on.last(3) === "1", "init opens the gate when Thru was saved on");
  on.panelProgram(2);
  on.bytes([0xB0, 24, 64, 0x90, 60, 127, 0x80, 60, 0]);
  on.runTasks();
  check(same(on.midiOut, RESYNC_BYTES), "with Thru on, the js still sends only its request: " + hex(on.midiOut));
  check(on.outlets[3].every(function (s) { return s === "0" || s === "1"; }), "the gate only ever gets 0 or 1");
})();

// 24. Nothing loops: answers never cause requests
(function () {
  var env = makeEnv({ ui: uiWith({ AltCC: 1, Autostart: 1 }) });
  env.flowlocal.program = 2;
  env.init();
  env.runTasks();
  for (var i = 0; i < 20; i++) {               // the panel keeps changing; FlowLocal keeps answering
    env.panelProgram(1 + (i % 6));
    env.bytes([0xB0, 24, i, 0xBF, 118, 1 + (i % 6)]);
  }
  env.bytes(RESYNC_BYTES.concat(RESYNC_BYTES)); // requests echoed back by a loopback port
  env.runTasks();
  check(env.midiOut.length === 3 && env.flowlocal.requests === 1,
        "one request per load, however much MIDI comes back: sent " + hex(env.midiOut));
})();

// 25. A Program Change and FlowLocal's CC 118 mirror both arriving (Alt CC
//     on, Live passes PC): the "already on" check keeps it to one launch.
(function () {
  [true, false].forEach(function (merge) {
    var how = merge ? " [Live merges to ch1]" : " [channels kept]";
    var ch = merge ? "1" : "16";
    var env = makeEnv({ ui: uiWith({ AltCC: 1 }), mergeChannels: merge });
    env.init();
    [1, 2, 3, 4, 5, 6, 1].forEach(function (p) { env.panelProgram(p); });
    check(same(env.live.fired, [1, 2, 3, 4, 5, 6, 1]), "K4 round with PC + mirror: each pattern fires once, got " +
          JSON.stringify(env.live.fired) + how);
    var mirrorsOn = env.outlets[1].filter(function (s) {
      return new RegExp("^set CC118=\\d+ ch" + ch + " -> .*\\(already on\\)$").test(s);
    });
    check(mirrorsOn.length === 7, "every mirror reads 'already on' (" + mirrorsOn.length + " of 7)" + how);
    check(new RegExp("^set CC118=1 ch" + ch + " -> 1: Artist A / One \\(already on\\)$").test(env.last(1)),
          "last status: " + env.last(1) + how);
  });

  var stopped = makeEnv({ ui: uiWith({ AltCC: 1 }) });
  stopped.init();
  check(stopped.live.playing === 0, "(setup) transport stopped");
  stopped.panelProgram(2);
  check(same(stopped.live.fired, [2]), "stopped transport: the PC's launch starts it, the mirror fires nothing");

  var q = makeEnv({ ui: uiWith({ AltCC: 1 }) });
  q.init();
  q.panelProgram(3);                           // launches at once and starts the transport
  q.live.quantized = true;
  q.panelProgram(4);
  check(q.live.firedSlot[0] === 4 && q.live.playingSlot[0] === 3, "(setup) row 4 queued, row 3 still playing");
  check(same(q.live.fired, [3, 4]), "quantized launch queued by the PC: the mirror does not fire it again, got " +
        JSON.stringify(q.live.fired));
  q.live.launchQueued();
  q.panelProgram(4);                           // reconnect re-send of both
  check(same(q.live.fired, [3, 4]), "re-sent PC + mirror while row 4 plays: nothing fires");

  var silence = makeEnv({ ui: uiWith({ AltCC: 1 }), scenes: ["(empty)", "A", "B"] });
  silence.init();
  silence.panelProgram(1);
  silence.panelProgram(0);
  check(same(silence.live.fired, [1, 0]), "silence row (transport running) with PC + mirror fires once");

  var held = makeEnv({ ui: uiWith({ AltCC: 1 }) });
  held.panelProgram(6);
  held.init();
  check(same(held.live.fired, [6]), "PC + mirror held before init fire once at init");

  var owed = makeEnv({ ui: uiWith({ AltCC: 1 }) });
  owed.init();
  owed.flowlocal.mirror = false;
  owed.panelProgram(5);
  owed.bytes([0xB0, 24, 70, 0xB0, 25, 12]);    // panel CCs in between
  owed.bytes([0xBF, 118, 5]);                  // the mirror FlowLocal owed, late
  check(same(owed.live.fired, [5]), "a late mirror after other CCs: not re-fired");

  var offset = makeEnv({ ui: uiWith({ AltCC: 1, Offset: 1 }) });
  offset.init();
  offset.panelProgram(2);
  check(same(offset.live.fired, [3]), "with Offset 1, PC + mirror fire row 3 once");
})();

// 26. FlowLocal's status replies to the laptop monitor (PF monitor SysEx v1:
//     F0 7D 50 46 02 <ASCII 0x20-0x7E, up to 240 bytes> F7, one every 2 s)
//     share the USB-MIDI input with Live, and Live 10.0.1+ passes SysEx
//     through a MIDI track's device chain, so this device sees them. The
//     text is all data bytes: under a stale running status C0 each would be
//     a Program Change, and under B0 its first bytes "v=" (0x76 0x3D) would
//     be CC 118 = 61, the Alt CC mirror. The set here has 128 scenes, so a
//     misparse fires instead of hiding behind the range guard.
(function () {
  var TEXT = "v=1;fw=exhibit-mon1;up=3605;ap=1;cl=1;cm=4;ps=1;pip=192.168.66.2;prs=-48;" +
             "hd=412;ra=412;lp=4;kn=64,-,127;rx=1234;tx=1234;dr=0;se=1;to=0;rj=0;gp=0;" +
             "rr=3;ud=0;sr=17;hi=201344";
  function reply(text) {
    var b = [0xF0, 0x7D, 0x50, 0x46, 0x02];
    for (var i = 0; i < text.length; i++) b.push(text.charCodeAt(i));
    return b.concat([0xF7]);
  }
  var REPLY = reply(TEXT);
  check(TEXT.length <= 240 && TEXT.charCodeAt(0) === 118 &&
        REPLY.slice(1, -1).every(function (b) { return b >= 0 && b <= 0x7F; }),
        "(setup) the reply is 7-bit inside, at most 240 text bytes, and starts with 'v' = 118");
  var many = [];
  for (var s = 0; s < 128; s++) many.push("S" + s);
  function dumpOf(env) {
    vm.runInContext("dump()", env.ctx);
    return env.posts[env.posts.length - 1];
  }
  function field(text, key) { return (new RegExp(" " + key + "=(-?\\d+)")).exec(text)[1]; }

  [true, false].forEach(function (merge) {
    var tag = merge ? " [Live merges to ch1]" : " [channels kept]";

    // After a Program Change (running status C0) and after the mirror (B0),
    // with Alt CC on: nothing fires, nothing is counted, the status lines
    // and [midiout] stay untouched.
    var env = makeEnv({ mergeChannels: merge, scenes: many, ui: uiWith({ AltCC: 1 }) });
    env.init();
    env.bytes([0xC0, 3]);
    env.bytes(REPLY);
    env.bytes([0x05]);                         // orphan: F7 must not bring C0 back
    env.bytes([0xBF, 118, 3]);                 // FlowLocal's mirror, deduped ("already on")
    var before = dumpOf(env);
    var lines = [env.outlets[0].length, env.outlets[1].length];
    env.bytes(REPLY);
    env.bytes(REPLY.concat(REPLY));
    var after = dumpOf(env);
    check(same(env.live.fired, [3]), "replies after PC / after the mirror fire nothing" + tag + ", got " +
          JSON.stringify(env.live.fired));
    check(field(after, "programs") === field(before, "programs") &&
          field(after, "altccIn") === field(before, "altccIn") &&
          field(after, "lastProgram") === "3",
          "replies count as neither programs nor Alt CC" + tag + ": " + after);
    check(env.outlets[0].length === lines[0] && env.outlets[1].length === lines[1],
          "replies leave both status lines alone" + tag);
    check(env.midiOut.length === 0, "replies send nothing out of [midiout]" + tag);
    env.bytes([0x76, 0x06]);                   // orphan data after F7 (B0 came last)
    check(same(env.live.fired, [3]), "orphan data after a reply stays orphan" + tag);
    env.panelProgram(5);
    check(same(env.live.fired, [3, 5]), "the next Program Change still fires" + tag);

    // Real-time bytes anywhere inside a reply keep it a reply.
    var rt = makeEnv({ mergeChannels: merge, scenes: many, ui: uiWith({ AltCC: 1 }) });
    rt.init();
    rt.panelProgram(2);
    var mixed = REPLY.slice();
    mixed.splice(mixed.length - 1, 0, 0xF8);   // before F7
    mixed.splice(40, 0, 0xFE, 0xF8, 0xFC);     // mid-text
    mixed.splice(1, 0, 0xF8);                  // right after F0
    rt.bytes(mixed);
    rt.bytes([0xC0, 0xF8]);                    // a clock byte between PC status and data...
    rt.bytes(REPLY.slice(0, 1));               // ...then F0: the PC is aborted, not completed by text
    rt.bytes(REPLY.slice(1));
    check(same(rt.live.fired, [2]), "real-time bytes inside a reply do not end it" + tag + ", got " +
          JSON.stringify(rt.live.fired));

    // A reply FlowLocal abandoned (no F7, USB FIFO full) ends at the next
    // status byte: the panel's Program Change right after it still fires.
    var cut = makeEnv({ mergeChannels: merge, scenes: many, ui: uiWith({ AltCC: 1 }) });
    cut.init();
    cut.panelProgram(1);
    cut.bytes(REPLY.slice(0, 57));
    cut.panelProgram(4);
    check(same(cut.live.fired, [1, 4]), "an abandoned reply, then PC 4 + mirror: fires 4 once" + tag + ", got " +
          JSON.stringify(cut.live.fired));
    cut.bytes(REPLY.slice(0, 90).concat(REPLY));
    check(same(cut.live.fired, [1, 4]), "an abandoned reply followed by a whole one: nothing" + tag);

    // Replies interleaved with pattern changes, as when the monitor polls
    // while visitors turn K4.
    var mix = makeEnv({ mergeChannels: merge, scenes: many, ui: uiWith({ AltCC: 1 }) });
    mix.init();
    for (var p = 0; p <= 6; p++) {
      mix.bytes(REPLY);
      mix.panelProgram(p);
      mix.bytes([0xB0, 24, 64]);               // a knob CC leaves running status B0
      mix.bytes(reply("v=1;lp=" + p + ";kn=64,-,-"));
    }
    check(same(mix.live.fired, [0, 1, 2, 3, 4, 5, 6]), "replies between programs change nothing" + tag +
          ", got " + JSON.stringify(mix.live.fired));
  });

  // Delivered as one Max list instead of int by int: same result.
  var asList = makeEnv({ scenes: many, ui: uiWith({ AltCC: 1 }) });
  asList.init();
  asList.bytes([0xC0, 2]);
  asList.ctx.list.apply(null, REPLY);
  check(same(asList.live.fired, [2]), "a reply as a Max list fires nothing");

  // Before live.thisdevice: a reply leaves nothing pending for init.
  var early = makeEnv({ scenes: many });
  early.bytes([0xC0, 0xF8]);                   // a PC cut off by the reply's F0
  early.bytes(REPLY);
  var held = dumpOf(early);                    // before init: nothing pending
  early.init();
  check(/ pending=-1\b/.test(held) && early.live.fired.length === 0,
        "a reply before init holds no program: " + held);
})();

console.log((failures ? "FAILED" : "OK") + ": " + passes + " passed, " + failures + " failed");
process.exit(failures ? 1 : 0);

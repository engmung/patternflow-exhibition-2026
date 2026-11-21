// Patternflow Ensemble - the console.
//
// One page, three ways to reach a hub:
//   demo    the hub's and the panels' own code, compiled to WebAssembly
//           (pfe_sim.wasm.js), running here with no hardware
//   USB     the hub's serial port, through Web Serial (Chrome / Edge)
//   Wi-Fi   the hub's WebSocket, when this page is on its hotspot
// All three speak the same lines of JSON (ensemble/PROTOCOL.md), so
// everything below the transports does not know which one is in use.
//
// License: MIT
(() => {
  'use strict';

  const $ = (id) => document.getElementById(id);
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  const KNOBS = 4;
  const LINKS = ['solo', 'sync', 'ripple'];
  const LINK_LABEL = { solo: 'Solo', sync: 'Sync', ripple: 'Ripple' };
  const CHANNEL_LABEL = ['K1', 'K2', 'K3', 'K4', 'Pattern'];
  const TILE_W = 0.84, TILE_H = 0.42;   // a panel on the map, in map units (128 x 64 LEDs)
  // The mark a panel shows while it is identified, by its number: a shape and
  // a colour, both stepping with the number (1 red circle, 2 green square,
  // 3 blue triangle, 4 yellow circle, ...; twelve different marks). The same
  // lists as core/pfe_proto.h identifyMark (tools/build_console.py --check
  // holds them together).
  const IDENTIFY_COLOURS = [[255, 0, 0], [0, 220, 0], [0, 80, 255], [255, 210, 0]];
  const IDENTIFY_COLOUR_NAMES = ['red', 'green', 'blue', 'yellow'];
  const IDENTIFY_SHAPES = ['circle', 'square', 'triangle'];
  const IDENTIFY_SHAPE_NAMES = ['circle', 'square', 'triangle'];
  const idMark = (id) => {
    const n = (id || 1) - 1;
    const [r, g, b] = IDENTIFY_COLOURS[n % IDENTIFY_COLOURS.length];
    return { shape: IDENTIFY_SHAPES[n % IDENTIFY_SHAPES.length], colour: `rgb(${r},${g},${b})`,
             name: `${IDENTIFY_COLOUR_NAMES[n % IDENTIFY_COLOURS.length]} ${IDENTIFY_SHAPE_NAMES[n % IDENTIFY_SHAPES.length]}` };
  };

  // ── What the console knows ─────────────────────────────────────────────
  const S = {
    mode: 'demo',
    transport: null,
    connected: false,
    cfg: null,
    panels: new Map(),       // hub index -> the panel's last line
    names: [],
    scenes: [],
    hub: null,
    waves: [],               // rings being drawn
    wires: [],               // hub index -> a bit for every panel it is wired to
    wireMode: false,         // the Wire tool: the left button pulls wires instead of moving panels
    midiOut: null,           // how the hub's MIDI pipe to the computer is doing
    sel: null,               // hub index of the chosen panel the side shows
    picked: new Set(),       // every chosen panel (the one above among them)
    scope: 'one',
    cam: { x: 0, y: 0, z: 90, fitted: false },
    view: '2d',              // the map flat, or '3d': as a room
    page: 'stage',           // or 'patterns': the set of patterns and the panels it goes to
    flash: new Map(),        // hub index -> time a hand was last seen there
    lastHubLineAt: 0,
  };

  // ── Small things ───────────────────────────────────────────────────────
  let toastTimer = 0;
  function toast(text) {
    const t = $('toast');
    t.textContent = text;
    t.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { t.hidden = true; }, 3800);
  }

  const nOf = (n, word) => `${n} ${word}${n === 1 ? '' : 's'}`;       // 1 panel, 3 panels

  function setPressed(ids, on) {
    for (const id of ids) $(id).setAttribute('aria-pressed', String(id === on));
  }

  const logEl = $('log');
  function log(kind, text) {
    const atBottom = logEl.scrollHeight - logEl.scrollTop - logEl.clientHeight < 30;
    const row = document.createElement('div');
    if (kind) row.className = kind;
    const d = new Date();
    const stamp = document.createElement('span');
    stamp.className = 't';
    stamp.textContent = d.toTimeString().slice(0, 8) + '  ';
    row.append(stamp, text);
    logEl.append(row);
    while (logEl.childElementCount > 400) logEl.firstElementChild.remove();
    if (atBottom) logEl.scrollTop = logEl.scrollHeight;
  }

  function patternName(i) {
    if (i == null || i < 0) return 'unknown';
    return S.names[i] ? `${i} · ${S.names[i]}` : `${i}`;
  }

  // ── Transports ─────────────────────────────────────────────────────────
  // Each has start(), stop(), send(text) and calls onLine(text) per line.

  function splitLines(buffer, chunk, onLine) {
    buffer.text += chunk;
    let i;
    while ((i = buffer.text.indexOf('\n')) >= 0) {
      const line = buffer.text.slice(0, i).replace(/\r$/, '');
      buffer.text = buffer.text.slice(i + 1);
      if (line) onLine(line);
    }
    if (buffer.text.length > 65536) buffer.text = '';   // a port spewing no newlines
  }

  class DemoTransport {
    constructor() {
      this.name = 'demo';
      this.x = null;
      this.t0 = performance.now();
      this.uidToSim = new Map();
      this.enc = new TextEncoder();
      this.dec = new TextDecoder();
      this.nextAuto = 0;
      this.auto = null;
      this.count = 10;
    }
    now() { return (5000 + (performance.now() - this.t0)) >>> 0; }
    async start() {
      try {
        if (!window.PFE_SIM_WASM) throw new Error('pfe_sim.wasm.js is missing');
        const bin = Uint8Array.from(atob(window.PFE_SIM_WASM), (c) => c.charCodeAt(0));
        const { instance } = await WebAssembly.instantiate(bin, {});
        this.x = instance.exports;
      } catch (e) {
        // Some frames refuse WebAssembly. A page may carry the same module
        // as plain JavaScript (binaryen's wasm2js) for those; the copy in
        // the repository does not, and says so instead.
        if (typeof window.PFE_SIM_JS !== 'function') {
          throw new Error('The demo cannot run in this frame (WebAssembly is blocked). Open ensemble/console/index.html directly in a browser.');
        }
        this.x = window.PFE_SIM_JS();
      }
      this.boot(this.count);
    }
    boot(count) {
      this.count = count;
      fakePanels.clear();
      const x = this.x;
      x.pfe_init(count, (Math.random() * 0x7fffffff) | 1, this.now());
      this.uidToSim.clear();
      for (let i = 0; i < count; i++) {
        x.pfe_uid(i);
        this.uidToSim.set(this.dec.decode(new Uint8Array(x.memory.buffer, x.pfe_io(), 12)), i);
      }
      this.applyNet();
      this.send(JSON.stringify({ c: 'hello' }));
      this.arranged = false;
      this.nextAuto = performance.now() + 1200;
    }
    applyNet() {
      const lat = +$('demo-lat').value, loss = +$('demo-loss').value;
      this.x.pfe_net(lat, Math.round(lat * 0.8) + 3, loss * 10);
    }
    stop() { this.x = null; }
    send(text) {
      if (!this.x) return;
      if (fakeRelay(text)) return;          // HTTP for a panel: the demo's panels have no web server
      if (fakeSetup(text)) return;          // and no radio to look for new panels with
      const bytes = this.enc.encode(text);
      if (bytes.length >= this.x.pfe_io_size()) return;
      new Uint8Array(this.x.memory.buffer, this.x.pfe_io(), bytes.length).set(bytes);
      this.x.pfe_command(bytes.length);
    }
    tick() {
      const x = this.x;
      if (!x) return;
      this.autoplay();
      if (x.pfe_solo) fakeShowsTick(this);
      x.pfe_tick(this.now());
      const n = x.pfe_out_len();
      if (n) {
        const text = this.dec.decode(new Uint8Array(x.memory.buffer, x.pfe_out(), n));
        x.pfe_out_clear();
        for (const line of text.split('\n')) if (line) onLine(line);
      }
      // The hub numbers panels as they arrive, in a row. Give the demo a room.
      if (!this.arranged && S.panels.size === this.count) {
        this.arranged = true;
        arrange('stagger');
        S.cam.fitted = false;
      }
    }
    // Somebody at a panel, now and then, so the page shows what it is for
    // before anyone touches it.
    autoplay() {
      if (!$('demo-auto').checked || !this.arranged) return;
      const now = performance.now();
      if (this.auto) {
        const a = this.auto;
        if (now >= a.next) {
          this.x.pfe_hand(a.panel, a.knob, a.dir);
          a.left--;
          a.next = now + a.every;
          if (a.left <= 0) { this.auto = null; this.nextAuto = now + 1800 + Math.random() * 2600; }
        }
        return;
      }
      if (now < this.nextAuto) return;
      const panel = Math.floor(Math.random() * this.count);
      if (!this.x.pfe_powered(panel)) { this.nextAuto = now + 500; return; }
      const k4 = S.cfg && S.cfg.k4;
      const pickPattern = k4 && Math.random() < 0.22;
      const knob = pickPattern ? 3 : Math.floor(Math.random() * 3);
      const lane = this.x.pfe_lane(panel, knob);
      const dir = pickPattern ? 1 : (lane > 0.7 ? -1 : lane < 0.3 ? 1 : (Math.random() < 0.5 ? -1 : 1));
      this.auto = { panel, knob, dir, left: pickPattern ? (S.cfg.k4s || 3) : 5 + Math.floor(Math.random() * 12),
                    every: pickPattern ? 40 : 60 + Math.random() * 60, next: now };
    }
    sim(uid) { return this.uidToSim.has(uid) ? this.uidToSim.get(uid) : -1; }
  }

  class SerialTransport {
    constructor() { this.name = 'usb'; this.port = null; this.buffer = { text: '' }; }
    async start() {
      if (!('serial' in navigator)) throw new Error('This browser has no Web Serial. Open this file in Chrome or Edge.');
      this.port = await navigator.serial.requestPort();
      await this.port.open({ baudRate: 115200 });
      // The hub's own USB jack (Espressif's vendor id) sends only while DTR
      // is up. Opening the port raises it; say so anyway, since the hub
      // goes by nothing else. On the COM jack - a USB-serial chip - DTR and
      // RTS are the board's reset lines: let go of both, or the hub
      // restarts and takes its hotspot down with it.
      const native = this.port.getInfo().usbVendorId === 0x303a;
      try {
        await this.port.setSignals(native ? { dataTerminalReady: true } : { dataTerminalReady: false, requestToSend: false });
      } catch (_) { /* not every port has them */ }
      this.writer = this.port.writable.getWriter();
      this.enc = new TextEncoder();
      this.reading = this.readLoop();
      this.send(JSON.stringify({ c: 'hello' }));
      this.ping = setInterval(() => this.send('{"c":"ping"}'), 3000);
    }
    async readLoop() {
      const dec = new TextDecoder();
      try {
        while (this.port && this.port.readable) {
          this.reader = this.port.readable.getReader();
          try {
            for (;;) {
              const { value, done } = await this.reader.read();
              if (done) break;
              splitLines(this.buffer, dec.decode(value, { stream: true }), onLine);
            }
          } finally {
            this.reader.releaseLock();
          }
          if (this.closing) break;
        }
      } catch (e) {
        if (!this.closing) dropped('The USB connection dropped: ' + e.message);
      }
    }
    send(text) {
      if (!this.writer) return;
      this.writer.write(this.enc.encode(text + '\n')).catch(() => {});
    }
    async stop() {
      this.closing = true;
      clearInterval(this.ping);
      try { if (this.reader) await this.reader.cancel(); } catch (_) { /* already gone */ }
      try { if (this.writer) { this.writer.releaseLock(); } } catch (_) { /* already gone */ }
      try { if (this.port) await this.port.close(); } catch (_) { /* already gone */ }
      this.port = null;
      this.writer = null;
    }
  }

  class SocketTransport {
    constructor(host) { this.name = 'wifi'; this.host = host; }
    start() {
      return new Promise((resolve, reject) => {
        let opened = false;
        const ws = new WebSocket(`ws://${this.host}/ws`);
        this.ws = ws;
        ws.onopen = () => {
          opened = true;
          this.send(JSON.stringify({ c: 'hello' }));
          this.ping = setInterval(() => this.send('{"c":"ping"}'), 3000);
          resolve();
        };
        ws.onmessage = (ev) => { for (const line of String(ev.data).split('\n')) if (line) onLine(line); };
        ws.onerror = () => { if (!opened) reject(new Error(`Could not connect to ${this.host}`)); };
        ws.onclose = () => { if (opened && !this.closing) dropped('The Wi-Fi connection dropped'); };
      });
    }
    send(text) { if (this.ws && this.ws.readyState === 1) this.ws.send(text); }
    stop() { this.closing = true; clearInterval(this.ping); if (this.ws) this.ws.close(); }
  }

  function send(obj) {
    const text = JSON.stringify(obj);
    if ($('log-raw').checked) log('me', '→ ' + text);
    if (S.transport) S.transport.send(text);
  }

  function resetState() {
    S.cfg = null;
    S.panels.clear();
    S.names = [];
    S.scenes = [];
    S.hub = null;
    S.waves = [];
    S.wires = [];
    S.midiOut = null;
    S.sel = null;
    S.picked = new Set();
    S.flash.clear();
    S.cam.fitted = false;
    V.fitted = false;
    renderAll();
  }

  function status(text, tone) {
    const el = $('conn-status');
    el.textContent = text;
    el.className = 'pill' + (tone ? ' ' + tone : '');
  }

  function dropped(why) {
    S.connected = false;
    relayDropped();
    status('Disconnected', 'bad');
    log('err', why);
    toast(why);
  }

  async function connect(mode, arg) {
    if (S.transport) { try { await S.transport.stop(); } catch (_) { /* gone */ } }
    S.transport = null;
    S.connected = false;
    relayDropped();
    shows.clear();
    NG.said = '';
    NG.live = null;
    NG.hub = null;
    NG.on = false;
    if (REC.on) { REC.on = false; clearInterval(REC.timer); REC.panels.clear(); }
    P.panels.clear();
    P.presets = null;
    resetState();
    S.mode = mode;
    setPressed(['conn-demo', 'conn-usb', 'conn-wifi'], 'conn-' + mode);
    $('demo-box').hidden = mode !== 'demo';
    status('Connecting', 'warn');
    const t = mode === 'demo' ? new DemoTransport() : mode === 'usb' ? new SerialTransport() : new SocketTransport(arg);
    if (mode === 'demo') t.count = +$('demo-count').value;
    try {
      S.transport = t;
      await t.start();
      S.connected = true;
      status(mode === 'demo' ? 'Demo: virtual panels' : mode === 'usb' ? 'Connected to the hub over USB' : `Wi-Fi: ${arg}`, 'ok');
      log('', mode === 'demo' ? 'Demo started: the hub\'s and the panels\' own code, with no hardware.' : 'Connected to the hub.');
    } catch (e) {
      S.transport = null;
      status('Not connected', 'bad');
      // A page inside a preview frame is refused the serial port outright;
      // a person who closes the port chooser picked nothing.
      const why = e.name === 'NotFoundError' ? 'No port was chosen.'
        : e.name === 'SecurityError' || e.name === 'NotAllowedError'
          ? 'This frame cannot open a USB port. Open ensemble/console/index.html from the repository directly in Chrome or Edge.'
          : (e.message || String(e));
      log('err', why);
      toast(why);
      if (mode !== 'demo') { connect('demo'); return; }   // never leave the page with nothing on it
    }
    renderAll();
  }

  // ── Lines from the hub ─────────────────────────────────────────────────
  function onLine(text) {
    if (text[0] !== '{') { log('raw', text); return; }   // the hub's own boot log on the same port
    let m;
    try { m = JSON.parse(text); } catch (_) { log('raw', text); return; }
    if ($('log-raw').checked && m.t !== 'p' && m.t !== 'hub' && m.t !== 'hb' && m.t !== 'hk' && m.t !== 'nv' && m.t !== 'mi' && m.t !== 'midiout' && !(m.t === 'setup' && m.op === 'net')) log('raw', '← ' + text);
    switch (m.t) {
      case 'hello':
        log('', `Hub: protocol ${m.proto}${m.fw ? ', firmware ' + m.fw : ''}${m.ssid ? ', Wi-Fi ' + m.ssid : ''}`);
        if (S.page === 'nodes') setTimeout(ngOpen, 300);      // a hub (again): what does it run, and watch it
        break;
      case 'cfg': S.cfg = m; renderRules(); renderInspector(); break;
      case 'names': {
        // In pieces: `at` is where this one starts, `of` how many there are.
        const at = m.at || 0;
        if (!at) S.names = [];
        (m.n || []).forEach((name, i) => { S.names[at + i] = name; });
        for (let i = 0; i < (m.of || 0); i++) if (S.names[i] == null) S.names[i] = '';
        renderInspector();
        renderAllPanels();
        break;
      }
      case 'hk': case 'http': case 'hb': case 'he': {      // a panel's web server, through the hub
        const r = relay.open.get(m.id);
        if (r) r.on(m);
        break;
      }
      case 'scenes': S.scenes = m.u || []; break;   // kept by the hub; this console has no buttons for them
      case 'wires': S.wires = m.m || []; renderInspector(); break;
      case 'setup': suLine(m); break;
      case 'midiout': S.midiOut = m; renderStats(); break;
      case 'usb': log('', 'USB: ' + JSON.stringify(m.d)); break;
      case 'hub':
        S.hub = m;
        S.lastHubLineAt = performance.now();
        if (S.quiet) { S.quiet = false; status(S.mode === 'usb' ? 'Connected to the hub over USB' : 'Connected to the hub over Wi-Fi', 'ok'); }
        renderStats();
        break;
      case 'p': {
        const prev = S.panels.get(m.i);
        if (prev && m.hands !== prev.hands) S.flash.set(m.i, performance.now());
        const mine = held.get(m.i);               // being moved or turned here right now
        if (mine) {
          m.x = mine.x; m.y = mine.y; m.z = mine.z;
          if (mine.r) m.r = mine.r;
        }
        S.panels.set(m.i, m);
        recSample(m);
        // It started or stopped a show: what the side column says of its player is from before.
        if (prev && ((prev.fl ^ m.fl) & RF_SOLO) && shows.has(m.uid)) loadShows(m.uid);
        if (S.page === 'nodes' && (!prev || prev.on !== m.on)) throttled('ngPanels', ngRender, 600);
        if (S.page === 'nodes' && (!prev || prev.on !== m.on || prev.x !== m.x || prev.y !== m.y || prev.id !== m.id)) throttled('ngMini', ngMini, 300);
        if (S.page === 'shows' && (!prev || prev.on !== m.on || prev.n !== m.n || ((prev.fl ^ m.fl) & RF_SOLO))) throttled('libPanels', renderLib, 300);
        if (S.page === 'setup' && (!prev || prev.on !== m.on || prev.var !== m.var || prev.n !== m.n)) throttled('suPanels', suRender, 300);
        if (S.page === 'patterns' && (!prev || prev.on !== m.on || prev.n !== m.n)) {
          renderGo();
          if (m.on) throttled('patCompare', compareAll, 1500);
        }
        if (S.sel === m.i) renderInspector();
        throttled('allPanels', renderAllPanels, 250);
        $('stage-empty').hidden = true;
        break;
      }
      case 'gone':
        S.panels.delete(m.i);
        S.picked.delete(m.i);
        if (S.sel === m.i) { S.sel = null; renderInspector(); }
        renderAllPanels();
        break;
      case 'w': {
        // A turning knob sends a value every few frames. One ring per
        // quarter second from the same place says the same thing.
        const at = performance.now();
        const last = S.waves[S.waves.length - 1];
        if (last && last.o === m.o && last.c === m.c && at - last.at < 250) break;
        // One that keeps to the wires is drawn along them: the ways it takes, as the wires lie now.
        S.waves.push({ x: m.x, y: m.y, z: m.z || 0, c: m.c, at, now: !!m.now, d: m.d, speed: S.cfg ? S.cfg.speed : 2, o: m.o,
                       wr: !!m.wr, tree: m.wr && m.o >= 0 ? wireTree(m.o) : null });
        if (S.waves.length > 60) S.waves.shift();
        break;
      }
      case 'log': log('', m.m); break;
      case 'err': if (m.c === 'graph') ngLine(m); log('err', `${m.c}: ${m.m}`); toast(m.m); break;
      case 'ack': if (m.c === 'graph') ngLine(m); break;
      case 'graph': case 'nv': case 'mi': ngLine(m); break;      // the node page's
      default: break;
    }
  }

  // ── The rules column ───────────────────────────────────────────────────
  function buildRules() {
    const box = $('links');
    for (let c = 0; c < 5; c++) {
      const row = document.createElement('div');
      row.className = 'row';
      row.style.marginBottom = '6px';
      const lab = document.createElement('span');
      lab.className = 'lab';
      lab.id = `rule-lab-${c}`;
      lab.textContent = CHANNEL_LABEL[c];
      const seg = document.createElement('div');
      seg.className = 'seg small';
      seg.setAttribute('role', 'group');
      seg.setAttribute('aria-label', 'How ' + CHANNEL_LABEL[c] + ' is linked');
      for (const l of LINKS) {
        const b = document.createElement('button');
        b.type = 'button';
        b.id = `link-${c}-${l}`;
        b.textContent = LINK_LABEL[l];
        b.setAttribute('aria-pressed', 'false');
        b.addEventListener('click', () => {
          if (c < 4) { const link = [null, null, null, null]; link[c] = l; send({ c: 'cfg', link }); }
          else send({ c: 'cfg', plink: l });
        });
        seg.append(b);
      }
      row.append(lab, seg);
      box.append(row);
    }
    // Whether a turn keeps to the wires drawn on the map. One switch for all
    // of them: the hub can be told knob by knob, and that was a row of five
    // more buttons nobody needed (2026-11-21).
    const row = document.createElement('div');
    row.className = 'row';
    row.style.marginTop = '10px';
    row.innerHTML = '<span class="lab" style="width:auto;flex:1" title="On: a turn goes only to the panels joined to that panel by wires on the map, and a ripple travels along the wires. Off: to every linked panel. Wires: right-drag from panel to panel, or F with panels selected">Only along wires</span>';
    const seg = document.createElement('div');
    seg.className = 'seg small';
    seg.setAttribute('role', 'group');
    seg.setAttribute('aria-label', 'Whether a turn goes only along the wires on the map');
    for (const on of [false, true]) {
      const b = document.createElement('button');
      b.type = 'button';
      b.id = on ? 'wired-on' : 'wired-off';
      b.textContent = on ? 'On' : 'Off';
      b.setAttribute('aria-pressed', 'false');
      b.addEventListener('click', () => send({ c: 'cfg', wired: [on, on, on, on], pwired: on }));
      seg.append(b);
    }
    row.append(seg);
    box.append(row);
  }

  function busy(el) { return document.activeElement === el; }

  // The ◀ ▶ beside a pattern list. The panels report a new pattern a moment
  // after it was asked for; until they do the list shows what was asked,
  // so that pressing three times quickly goes three on and not one.
  const stepHeld = new Map();          // list id -> { value, until }
  function heldStep(sel) {
    const h = stepHeld.get(sel.id);
    if (!h) return null;
    if (performance.now() < h.until && +h.value < sel.options.length) return h.value;
    stepHeld.delete(sel.id);
    return null;
  }
  function stepPattern(sel, by, apply) {
    const count = sel.options.length;
    if (!count || sel.disabled) return;
    const at = +(heldStep(sel) ?? sel.value) || 0;
    const next = ((at + by) % count + count) % count;      // round the ends
    stepHeld.set(sel.id, { value: String(next), until: performance.now() + 1500 });
    sel.value = String(next);
    apply(next);
  }

  function renderRules() {
    const cfg = S.cfg;
    if (!cfg) return;
    for (let c = 0; c < 5; c++) {
      const cur = c < 4 ? cfg.link[c] : cfg.plink;
      for (const l of LINKS) $(`link-${c}-${l}`).setAttribute('aria-pressed', String(l === cur));
    }
    // On when every one of them keeps to the wires (a hub set knob by knob before shows Off until switched).
    const wired = Array.isArray(cfg.wired) && cfg.wired.every(Boolean) && !!cfg.pwired;
    setPressed(['wired-off', 'wired-on'], wired ? 'wired-on' : 'wired-off');
    $(`link-3-solo`).closest('.row').hidden = !!cfg.k4;   // K4 has no value to share while it switches patterns
    if (!busy($('cfg-speed'))) $('cfg-speed').value = cfg.speed;
    $('cfg-speed-v').textContent = `${cfg.speed} units/s`;
    if (!busy($('cfg-glide'))) $('cfg-glide').value = cfg.glide;
    $('cfg-glide-v').textContent = `${cfg.glide} ms`;
    setPressed(['cfg-k4-pattern', 'cfg-k4-knob'], cfg.k4 ? 'cfg-k4-pattern' : 'cfg-k4-knob');
    $('k4-range').hidden = !cfg.k4;
    // While K4 switches patterns, the pattern row is K4's row: say so.
    $('rule-lab-4').textContent = cfg.k4 ? 'K4 pattern' : 'Pattern';
    for (const [id, v] of [['cfg-pmin', cfg.pmin], ['cfg-pmax', cfg.pmax], ['cfg-k4s', cfg.k4s]])
      if (!busy($(id))) $(id).value = v;
    $('cfg-lock').checked = !!cfg.lock;
  }

  function selUid() {
    const p = S.sel != null ? S.panels.get(S.sel) : null;
    return p ? p.uid : null;
  }

  // Choosing panels: one (the others let go), or with Shift one more or one fewer.
  function pick(i, more) {
    if (more) {
      if (S.picked.has(i)) {
        S.picked.delete(i);
        if (S.sel === i) S.sel = S.picked.size ? [...S.picked][S.picked.size - 1] : null;
      } else {
        S.picked.add(i);
        S.sel = i;
      }
    } else {
      if (!S.picked.has(i)) S.picked = new Set([i]);     // one of several already chosen keeps them, to be dragged together
      S.sel = i;
    }
    renderInspector();
  }
  function pickBox(ids, more) {
    if (!more) S.picked = new Set();
    for (const i of ids) S.picked.add(i);
    if (S.sel == null || !S.picked.has(S.sel)) S.sel = S.picked.size ? [...S.picked][S.picked.size - 1] : null;
    renderInspector();
  }
  function pickNone() {
    if (!S.picked.size && S.sel == null) return;
    S.picked = new Set();
    S.sel = null;
    renderInspector();
  }
  const pickedPanels = () => [...S.picked].map((i) => S.panels.get(i)).filter(Boolean);

  function renderStats() {
    const h = S.hub;
    const el = $('hub-stats');
    if (!h) { el.textContent = ''; return; }
    const up = Math.floor(h.ms / 1000);
    const hh = Math.floor(up / 3600), mm = Math.floor(up / 60) % 60, ss = up % 60;
    const time = (hh ? hh + ':' + String(mm).padStart(2, '0') : mm) + ':' + String(ss).padStart(2, '0');
    el.innerHTML =
      `<span>Panels <b>${h.online}/${h.panels}</b></span>` +
      `<span>In flight <b>${h.fly}</b></span>` +
      `<span>Received <b>${h.rx}</b> · sent <b>${h.tx}</b></span>` +
      `<span>Bad packets <b>${h.bad}</b> · late reports <b>${h.stale}</b></span>` +
      `<span>Hub up <b>${time}</b></span>` + midiOutNote();
  }

  // The hub's MIDI to the computer. A computer that reads the port takes a
  // message within a millisecond or two; one waiting for seconds means that
  // nothing is reading, or that the pipe has stopped (seen once, 2026-11-21:
  // it stood still until the hub restarted).
  let midiOutSaid = false;
  function midiOutNote() {
    const mo = S.midiOut;
    if (!mo || !mo.on) return '';
    const stuck = mo.wait > 3000;
    if (stuck && mo.queued > 0 && !midiOutSaid) {
      midiOutSaid = true;
      log('err', `MIDI out: the computer has not taken a message for ${Math.round(mo.wait / 1000)} s and ${mo.queued} more are waiting. If a program is listening and hears nothing, restart the hub`);
    }
    if (!stuck) { midiOutSaid = false; return `<span>MIDI out <b>${mo.sent}</b></span>`; }
    return `<span style="color:var(--bad)" title="The hub has had a MIDI message waiting for the computer this long. That is normal while no program has the MIDI port open. If one is open and hears nothing, restart the hub.">MIDI out <b style="color:var(--bad)">waiting ${Math.round(mo.wait / 1000)} s</b></span>`;
  }

  // ── The inspector ──────────────────────────────────────────────────────
  const throttles = new Map();
  function throttled(key, fn, ms = 70) {
    const t = throttles.get(key) || { timer: 0, last: 0 };
    throttles.set(key, t);
    t.fn = fn;
    const wait = ms - (performance.now() - t.last);
    if (wait <= 0) { t.last = performance.now(); fn(); return; }
    if (!t.timer) t.timer = setTimeout(() => { t.timer = 0; t.last = performance.now(); t.fn(); }, wait);
  }

  function buildInspector() {
    const knobs = $('p-knobs');
    const hand = $('p-hand');
    for (let k = 0; k < KNOBS; k++) {
      const row = document.createElement('div');
      row.className = 'knob';
      row.id = `p-knob-row-${k}`;
      row.style.marginBottom = '6px';
      row.innerHTML = `<label class="lab" for="p-k${k}">K${k + 1}</label>` +
        `<input type="range" id="p-k${k}" min="0" max="1000" step="1" value="500">` +
        `<span class="val num" id="p-k${k}-v">500</span>`;
      knobs.append(row);
      row.querySelector('input').addEventListener('input', (ev) => {
        const v = +ev.target.value;
        $(`p-k${k}-v`).textContent = v;
        throttled('knob' + k, () => setFromConsole({ kn: k, v }));
      });

      const h = document.createElement('div');
      h.className = 'hand';
      h.id = `p-hand-row-${k}`;
      h.style.marginBottom = '6px';
      h.innerHTML = `<span class="lab">K${k + 1}</span>` +
        `<button type="button" class="btn small" id="p-hand-${k}-down" aria-label="K${k + 1} one click left">◀ Left</button>` +
        `<button type="button" class="btn small" id="p-hand-${k}-up" aria-label="K${k + 1} one click right">Right ▶</button>`;
      hand.append(h);
      for (const [suffix, dir] of [['down', -1], ['up', 1]]) {
        const b = h.querySelector(`#p-hand-${k}-${suffix}`);
        let rep = 0;
        const turn = () => virtualHand(k, dir);
        b.addEventListener('pointerdown', () => { turn(); rep = setInterval(turn, 90); });
        for (const ev of ['pointerup', 'pointerleave', 'pointercancel']) b.addEventListener(ev, () => clearInterval(rep));
        b.addEventListener('keydown', (ev) => { if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); turn(); } });
      }
    }
    // Brightness: the panel's own setting, not a channel - it does not travel.
    const br = document.createElement('div');
    br.className = 'knob';
    br.id = 'p-bright-row';
    br.style.marginBottom = '6px';
    br.innerHTML = '<label class="lab" for="p-br">Bri</label>' +
      '<input type="range" id="p-br" min="5" max="255" step="1" value="128">' +
      '<span class="val num" id="p-br-v">–</span>';
    knobs.append(br);
    $('p-br').addEventListener('input', (ev) => {
      const v = +ev.target.value;
      $('p-br-v').textContent = v;
      throttled('bright', () => setFromConsole({ br: v }), 120);
    });

    // K4's place while it switches patterns: not a missing slider, a role.
    const k4 = document.createElement('div');
    k4.className = 'knob';
    k4.id = 'p-k4-pattern';
    k4.hidden = true;
    k4.style.marginBottom = '6px';
    k4.style.gridTemplateColumns = '26px minmax(0, 1fr) auto';
    k4.innerHTML = '<span class="lab">K4</span><span class="note" style="margin:0">steps patterns (choose above)</span>' +
      '<button type="button" class="btn small" id="p-k4-as-knob">Use as a knob</button>';
    knobs.append(k4);
    $('p-k4-as-knob').addEventListener('click', () => send({ c: 'cfg', k4: false }));
  }

  function virtualHand(knob, detents) {
    const uid = selUid();
    const t = S.transport;
    if (!uid || !t || t.name !== 'demo') return;
    const sim = t.sim(uid);
    if (sim < 0) return;
    $('demo-auto').checked = false;   // somebody real is here now
    t.x.pfe_hand(sim, knob, detents);
  }

  function setFromConsole(what) {
    const uid = selUid();
    if (!uid) return;
    if (S.scope === 'one') for (const p of pickedPanels()) send({ c: 'set', to: p.uid, ...what });
    // As a turn of that panel's own knob: the rules on the left (solo, sync, ripple, only along wires) say who else takes it.
    else if (S.scope === 'turn') for (const p of pickedPanels()) send({ c: 'set', to: p.uid, turn: true, ...what });
    else if (S.scope === 'wave') send({ c: 'set', to: 'all', mode: 'ripple', from: uid, ...what });
    else send({ c: 'set', to: 'all', ...what });
  }

  function renderInspector() {
    const p = S.sel != null ? S.panels.get(S.sel) : null;
    if (renderInspector.shown !== S.sel) {      // another panel: what was stepped to on the last one is not its pattern
      renderInspector.shown = S.sel;
      stepHeld.delete('p-pattern');
    }
    const demo = S.mode === 'demo';
    $('insp-none').hidden = !!p;
    for (const id of ['insp', 'insp-set', 'insp-act', 'insp-diag']) $(id).hidden = !p;
    renderShow();
    $('insp-hand').hidden = !p || !demo;
    if (!p) return;
    const k4 = S.cfg && S.cfg.k4;
    $('insp-id').textContent = p.id;
    $('insp-title').textContent = (p.n || `Panel ${p.id}`) + (S.picked.size > 1 ? ` + ${S.picked.size - 1} more` : '');
    $('insp-uid').textContent = p.uid;
    const st = $('insp-state');
    if (!p.on) { st.textContent = 'Offline'; st.className = 'pill bad'; }
    else if (isSolo(p)) { st.textContent = 'Playing a show'; st.className = 'pill ok'; }
    else if (!p.ok) { st.textContent = 'Catching up'; st.className = 'pill warn'; }
    else { st.textContent = 'In sync'; st.className = 'pill ok'; }
    const turn = p.r || [0, 0, 0];
    for (const [id, v] of [['p-id', p.id], ['p-name', p.n], ['p-x', p.x], ['p-y', p.y], ['p-z', p.z || 0],
                           ['p-rx', turn[0]], ['p-ry', turn[1]], ['p-rz', turn[2]]])
      if (!busy($(id))) $(id).value = v;
    $('p-linked').checked = !!p.l;
    const to = wiredTo(S.sel).map((j) => S.panels.get(j).id).sort((a, b) => a - b);
    $('p-wires').textContent = to.length ? `Wired to ${to.join(', ')}` : 'No wires';
    $('p-unwire').hidden = !to.length;
    $('p-sleep').textContent = p.sl ? 'Wake' : 'Sleep';

    // Patterns: as many as this panel says it has, named where a name is known.
    const sel = $('p-pattern');
    const count = Math.max(p.pc || 0, S.names.length, (p.tp >= 0 ? p.tp : 0) + 1, 1);
    if (sel.options.length !== count || sel.dataset.names !== S.names.join('|')) {
      sel.dataset.names = S.names.join('|');
      sel.textContent = '';
      for (let i = 0; i < count; i++) sel.append(new Option(patternName(i), String(i)));
    }
    if (!busy(sel)) sel.value = heldStep(sel) ?? String(p.tp >= 0 ? p.tp : p.pat);
    for (let k = 0; k < KNOBS; k++) {
      $(`p-knob-row-${k}`).hidden = k === 3 && k4;
      $(`p-hand-row-${k}`).hidden = false;
      const input = $(`p-k${k}`);
      if (!busy(input)) { input.value = p.tk[k]; $(`p-k${k}-v`).textContent = p.tk[k]; }
    }
    $('p-k4-pattern').hidden = !k4;
    // The brightness the panel reports; a panel of the first version says none.
    $('p-bright-row').hidden = !p.br;
    if (p.br && !busy($('p-br'))) { $('p-br').value = p.br; $('p-br-v').textContent = p.br; }
    $('p-hand-3-down').textContent = k4 ? '◀ Previous' : '◀ Left';
    $('p-hand-3-up').textContent = k4 ? 'Next ▶' : 'Right ▶';
    if (demo && S.transport && S.transport.x) {
      const sim = S.transport.sim(p.uid);
      $('p-power').textContent = sim >= 0 && S.transport.x.pfe_powered(sim) ? 'Power off' : 'Power on';
    }
    $('p-identify').textContent = p.idf ? 'Hide mark' : `Mark (${idMark(p.id).name})`;
    $('p-forget').disabled = !!p.on;
    $('p-forget').title = p.on ? 'Only a panel that is offline can be removed' : '';
    $('p-reboot').disabled = !p.on;

    const flags = [];
    if (p.fl & 1) flags.push('just started');
    if (p.fl & 2) flags.push('asleep');
    if (p.fl & 4) flags.push('panel menu open');
    if (p.fl & 8) flags.push('choosing a pattern');
    const up = p.up >= 3600 ? `${Math.floor(p.up / 3600)} h ${Math.floor(p.up / 60) % 60} min` : `${Math.floor(p.up / 60)} min ${p.up % 60} s`;
    const rows = [
      ['Address', p.ip], ['Signal', `${p.rssi} dBm`], ['Display', `${p.fps} fps`], ['Up for', up],
      ['Firmware', `${p.fw || '?'}${p.var ? ' · ' + p.var : ''}`],
      ['Pattern', `${patternName(p.pat)}${p.pn && !S.names[p.pat] ? ' · ' + p.pn : ''}`],
      ['Knobs', p.k.join(' / ')], ['Knob targets', p.tk.join(' / ')],
      ['Last report', `${p.age} ms ago`], ['Reports / lost', `${p.rep} / ${p.lost}`],
      ['Hand turns', p.hands], ['Restarts', p.rst], ['State', flags.join(', ') || 'normal'],
    ];
    const dl = $('p-diag');
    dl.textContent = '';
    for (const [k, v] of rows) {
      const dt = document.createElement('dt'); dt.textContent = k;
      const dd = document.createElement('dd'); dd.textContent = v;
      dl.append(dt, dd);
    }
  }

  // ── Every panel at once ────────────────────────────────────────────────
  // The controls show what most of the connected panels show now.
  function mostCommon(values) {
    const seen = new Map();
    let best = null, bestN = 0;
    for (const v of values) {
      const n = (seen.get(v) || 0) + 1;
      seen.set(v, n);
      if (n > bestN) { best = v; bestN = n; }
    }
    return best;
  }

  function anyIdentified() {
    return [...S.panels.values()].some((p) => p.on && p.idf);
  }

  function renderAllPanels() {
    const ps = [...S.panels.values()].filter((p) => p.on);
    const allAsleep = ps.length > 0 && ps.every((p) => p.sl);
    $('all-sleep').textContent = allAsleep ? 'Wake' : 'Sleep';
    $('all-sleep').setAttribute('aria-pressed', String(allAsleep));
    $('all-sleep').disabled = !ps.length;
    $('map-identify').textContent = anyIdentified() ? 'Hide marks' : 'Show all marks';
    $('map-identify').setAttribute('aria-pressed', String(anyIdentified()));
    const sel = $('all-pattern');
    const count = Math.max(1, S.names.length, ...ps.map((p) => p.pc || 0));
    const key = count + '|' + S.names.join('|');
    if (sel.dataset.key !== key) {
      sel.dataset.key = key;
      sel.textContent = '';
      for (let i = 0; i < count; i++) sel.append(new Option(patternName(i), String(i)));
    }
    const pat = mostCommon(ps.map((p) => p.pat).filter((v) => v >= 0));
    if (!busy(sel) && pat != null) sel.value = heldStep(sel) ?? String(pat);
    sel.disabled = !ps.length;
    $('all-pattern-prev').disabled = $('all-pattern-next').disabled = !ps.length;
    // Patterns travel by number: panels that hold different lists show
    // different patterns under one number. Their counts give it away.
    const counts = ps.filter((p) => p.pc > 0).sort((a, b) => a.id - b.id);
    const warn = $('all-pattern-warn');
    warn.hidden = new Set(counts.map((p) => p.pc)).size < 2;
    if (!warn.hidden) {
      warn.textContent = `The panels hold different numbers of patterns (${counts.map((p) => `#${p.id}: ${p.pc}`).join(', ')}). ` +
        'The same number is then a different pattern on each. Put one set on all of them from the Patterns page.';
    }
    const slider = $('all-bright');
    const br = mostCommon(ps.map((p) => p.br).filter((v) => v > 0));
    slider.disabled = br == null;     // no panel says its brightness: old firmware, or none connected
    if (!busy(slider)) {
      if (br != null) slider.value = br;
      $('all-bright-v').textContent = br != null ? br : '–';
    }
  }

  function renderAll() {
    renderRules();
    renderStats();
    renderInspector();
    renderAllPanels();
    $('stage-empty').hidden = S.panels.size > 0 || S.mode === 'demo';
  }

  // ── The map ────────────────────────────────────────────────────────────
  // Two views of the same places. Flat (2D): the wall as seen from the
  // front, x across and y down. Room (3D): the same with depth, z, looked at
  // the way Blender looks at things.
  const canvas = $('map');
  const ctx = canvas.getContext('2d');
  let pen = ctx;                 // what a tile is drawn on: the map itself, or a tile's face for the 3D view
  let W = 0, H = 0, dpr = 1;
  const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  let colors = {};
  function readColors() {
    colors = { stage: css('--stage'), grid: css('--stage-grid'), ink: css('--stage-ink'), muted: css('--stage-muted'),
               led: css('--led'), ok: css('--ok'), warn: css('--warn'), bad: css('--bad'), sans: css('--sans') };
  }

  function resize() {
    const r = canvas.getBoundingClientRect();
    dpr = Math.min(window.devicePixelRatio || 1, 2);
    W = Math.max(50, r.width);
    H = Math.max(50, r.height);
    canvas.width = Math.round(W * dpr);
    canvas.height = Math.round(H * dpr);
    readColors();
  }

  const toScreen = (x, y) => [W / 2 + (x - S.cam.x) * S.cam.z, H / 2 + (y - S.cam.y) * S.cam.z];
  const toWorld = (sx, sy) => [(sx - W / 2) / S.cam.z + S.cam.x, (sy - H / 2) / S.cam.z + S.cam.y];
  const round2 = (v) => Math.round(v * 100) / 100 || 0;

  // A place being moved by hand here. The hub's lines about that panel are a
  // moment behind and must not pull it back under the pointer.
  const held = new Map();        // hub index -> { x, y, z, r }
  const hold = (i, p) => held.set(i, { x: p.x, y: p.y, z: p.z || 0, r: p.r });
  function letGo(ids) {
    const mine = ids.map((i) => [i, held.get(i)]);
    setTimeout(() => { for (const [i, v] of mine) if (held.get(i) === v) held.delete(i); }, 400);
  }

  function fit() {
    const ps = [...S.panels.values()];
    if (!ps.length) return;
    let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
    for (const p of ps) { x0 = Math.min(x0, p.x); x1 = Math.max(x1, p.x); y0 = Math.min(y0, p.y); y1 = Math.max(y1, p.y); }
    const w = x1 - x0 + TILE_W + 0.6, h = y1 - y0 + TILE_H + 1.1;
    S.cam.x = (x0 + x1) / 2;
    S.cam.y = (y0 + y1) / 2;
    S.cam.z = clamp(Math.min(W / w, H / h), 18, 260);
    S.cam.fitted = true;
  }

  // What a panel shows on the map: what it is set to, not a picture of it.
  // The console cannot know how a pattern looks on a panel (two panels may
  // even hold different patterns under one number), so the tile gives the
  // pattern number and draws the four knob positions as bars.
  function drawInfo(x, y, tw, th, pat, k, lit) {
    pen.fillStyle = lit ? '#1B1813' : '#0E0D0B';
    pen.fillRect(x, y, tw, th);
    const pad = Math.max(2, Math.round(tw * 0.04));
    const head = Math.max(15, th * 0.36);       // the number chip sits at this strip's left
    pen.textBaseline = 'middle';
    pen.textAlign = 'right';
    pen.font = `700 ${clamp(Math.round(head * 0.62), 9, 26)}px ${colors.sans}`;
    pen.fillStyle = lit ? '#EDE7DB' : '#6F685D';
    pen.fillText(`Pattern ${pat}`, x + tw - pad, y + head / 2 + 1);

    const rows = th - head - pad;
    if (rows < 8) return;
    const gap = Math.max(1, rows * 0.08);
    const bh = (rows - gap * 3) / 4;
    const labelled = bh >= 7 && tw >= 70;
    pen.font = `${clamp(Math.round(bh * 0.85), 7, 12)}px ${colors.sans}`;
    pen.textAlign = 'left';
    const labelW = labelled ? pen.measureText('K4 ').width : 0;
    const bx = x + pad + labelW, bw = tw - pad * 2 - labelW;
    for (let n = 0; n < 4; n++) {
      const by = y + head + n * (bh + gap);
      if (labelled) {
        pen.fillStyle = '#8F877A';
        pen.fillText(`K${n + 1}`, x + pad, by + bh / 2 + 0.5);
      }
      pen.fillStyle = 'rgba(237,231,219,0.10)';
      pen.fillRect(bx, by, bw, bh);
      pen.fillStyle = lit ? colors.led : '#4A453D';
      pen.fillRect(bx, by, bw * clamp(k[n], 0, 1), bh);
    }
  }

  // In the demo a tile shows the virtual panel itself; with hardware, what
  // the panel last reported.
  function tileState(p) {
    const demo = S.transport && S.transport.name === 'demo' && S.transport.x ? S.transport : null;
    let k = p.k.map((v) => v / 1000), pat = p.pat, lit = !!p.on && !p.sl, idf = !!p.idf;
    if (demo) {
      const sim = demo.sim(p.uid);
      if (sim >= 0) {
        lit = !!demo.x.pfe_powered(sim) && !p.sl;
        pat = demo.x.pfe_pattern(sim);
        k = [0, 1, 2, 3].map((n) => demo.x.pfe_lane(sim, n));
        idf = !!demo.x.pfe_identify(sim);
      }
    }
    return { k, pat, lit, idf, asleep: !!p.on && !!p.sl };
  }

  // A tile's face: its information or its mark, and its number on the corner.
  function tileFace(p, st, x, y, tw, th) {
    drawInfo(x, y, tw, th, st.pat, st.k, st.lit);
    // Showing its mark: the tile shows the same shape in the same colour as
    // the panel in the room, so the one on the wall and the one on the map
    // are matched by eye. No number in it, as on the panel.
    if (st.idf && st.lit) {
      const m = idMark(p.id), cx = x + tw / 2, cy = y + th / 2;
      pen.fillStyle = '#000';
      pen.fillRect(x, y, tw, th);
      pen.fillStyle = m.colour;
      const r = th / 2 - Math.max(1, th * 0.05);
      pen.beginPath();
      if (m.shape === 'circle') pen.arc(cx, cy, r, 0, Math.PI * 2);
      else if (m.shape === 'square') pen.rect(cx - r, cy - r, 2 * r, 2 * r);
      else { pen.moveTo(cx, cy - r); pen.lineTo(cx - r * 1.2, cy + r); pen.lineTo(cx + r * 1.2, cy + r); pen.closePath(); }
      pen.fill();
    }
    if (st.asleep) {
      // Asleep: dark, as the panel is.
      pen.fillStyle = '#0B0A09';
      pen.fillRect(x, y, tw, th);
      pen.fillStyle = '#6F685D';
      pen.font = `${clamp(Math.round(th * 0.22), 9, 16)}px ${colors.sans}`;
      pen.textAlign = 'center';
      pen.textBaseline = 'middle';
      pen.fillText('Asleep', x + tw / 2, y + th / 2);
    }
    // The number rides on the tile's corner, with the state as a dot beside it.
    const chip = String(p.id);
    pen.font = `700 11px ${colors.sans}`;
    pen.textAlign = 'left';
    pen.textBaseline = 'middle';
    const cw = pen.measureText(chip).width + 17;
    pen.fillStyle = 'rgba(12,11,9,0.78)';
    pen.fillRect(x, y, cw, 15);
    pen.fillStyle = '#EDE7DB';
    pen.fillText(chip, x + 4, y + 8);
    pen.fillStyle = !p.on ? colors.bad : p.ok ? colors.ok : colors.warn;
    pen.beginPath();
    pen.arc(x + cw - 6, y + 7.5, 2.5, 0, Math.PI * 2);
    pen.fill();
  }

  const subLabel = (p, pat) => (p.n ? p.n + ' · ' : '') + (!p.on ? 'Offline' : isSolo(p) ? 'Playing a show' : (S.names[pat] || `Pattern ${pat}`)) + (p.l ? '' : ' · unlinked');

  function draw2d(now) {
    if (!S.cam.fitted && S.panels.size) fit();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = ROOM.bg;
    ctx.fillRect(0, 0, W, H);

    // A dot at every whole unit: the map's ruler.
    const z = S.cam.z;
    if (z > 26) {
      ctx.fillStyle = '#C9C4B9';
      const [wx0, wy0] = toWorld(0, 0), [wx1, wy1] = toWorld(W, H);
      for (let gx = Math.floor(wx0); gx <= wx1; gx++)
        for (let gy = Math.floor(wy0); gy <= wy1; gy++) {
          const [sx, sy] = toScreen(gx, gy);
          ctx.fillRect(sx - 1, sy - 1, 2, 2);
        }
    }

    // Values on their way: a ring from where the turn was made.
    S.waves = S.waves.filter((w) => now - w.at < (w.now ? 500 : w.d + 500));
    for (const w of S.waves) {
      if (w.wr) continue;                  // one that keeps to the wires is drawn along them
      const age = (now - w.at) / 1000;
      const [sx, sy] = toScreen(w.x, w.y);
      const radius = (w.now ? age * 14 : age * w.speed) * z;
      const fade = w.now ? 1 - age / 0.5 : clamp(1 - (age * 1000 - w.d) / 500, 0, 1);
      ctx.strokeStyle = w.c === 4 || w.c === 5 ? ROOM.dark : colors.led;
      ctx.globalAlpha = 0.5 * clamp(fade, 0, 1);
      ctx.lineWidth = w.c === 4 || w.c === 5 ? 2 : 1.25;
      ctx.beginPath();
      ctx.arc(sx, sy, Math.max(0, radius), 0, Math.PI * 2);
      ctx.stroke();
    }
    ctx.globalAlpha = 1;

    drawWires((i) => { const p = S.panels.get(i); return p ? toScreen(p.x, p.y) : null; }, now);

    const tw = TILE_W * z, th = TILE_H * z;
    for (const [i, p] of S.panels) {
      const [cx, cy] = toScreen(p.x, p.y);
      const x = cx - tw / 2, y = cy - th / 2;
      if (x > W || y > H || x + tw < 0 || y + th + 12 < 0) continue;
      const st = tileState(p);
      tileFace(p, st, x, y, tw, th);

      // The chosen ones in orange; the one the side shows a little heavier.
      const flashed = S.flash.get(i);
      const hot = flashed && now - flashed < 450, chosen = S.picked.has(i);
      ctx.lineWidth = chosen ? (i === S.sel ? 3 : 2) : hot ? 2 : 1;
      ctx.strokeStyle = hot || chosen ? colors.led : ROOM.line;
      ctx.strokeRect(x - 1.5, y - 1.5, tw + 3, th + 3);

      // The name and the pattern go underneath when there is room, and what
      // the flat map cannot show: depth, and that a panel is turned.
      if (z >= 84) {
        ctx.textAlign = 'left';
        ctx.textBaseline = 'top';
        ctx.font = `11px ${colors.sans}`;
        ctx.fillStyle = p.on ? ROOM.text : colors.bad;
        ctx.fillText(subLabel(p, st.pat) + (p.z ? ` · depth ${p.z}` : '') + (turned3(p) ? ' · rotated' : ''), x, y + th + 5, tw);
      }
    }
    drawBox();
  }

  function tileAt(sx, sy) {
    const tw = TILE_W * S.cam.z, th = TILE_H * S.cam.z;
    let hit = null;
    for (const [i, p] of S.panels) {
      const [cx, cy] = toScreen(p.x, p.y);
      if (Math.abs(sx - cx) <= tw / 2 + 3 && Math.abs(sy - cy) <= th / 2 + 3) hit = i;
    }
    return hit;
  }

  // ── Wires: panels joined two by two ────────────────────────────────────
  // The hub keeps them (core/pfe_conductor.h) and says which there are; a
  // channel whose "Wires" switch is on spreads along them only.
  const wiredTo = (i) => [...S.panels.keys()].filter((j) => j !== i && (((S.wires[i] || 0) >>> j) & 1));
  function wireList() {            // [i, j], i < j, both on the map
    const out = [];
    for (const i of S.panels.keys())
      for (const j of S.panels.keys())
        if (i < j && (((S.wires[i] || 0) >>> j) & 1)) out.push([i, j]);
    return out;
  }
  function setWire(i, j, on) {     // on: true or false, or left out to change it over
    const a = S.panels.get(i), b = S.panels.get(j);
    if (!a || !b || i === j) return;
    const m = { c: 'wire', a: a.uid, b: b.uid };
    if (on != null) m.on = !!on;
    send(m);
  }
  const apart = (p, q) => Math.hypot(p.x - q.x, p.y - q.y, (p.z || 0) - (q.z || 0));

  // The ways a value takes from panel `o` along the wires, for drawing it:
  // every wire that lies on a shortest way, from its nearer end (a) to its
  // farther (b), and how far along the way each end is.
  function wireTree(o) {
    const dist = new Map([[o, 0]]), done = new Set();
    for (;;) {
      let u = null;
      for (const [k, d] of dist) if (!done.has(k) && (u == null || d < dist.get(u))) u = k;
      if (u == null) break;
      done.add(u);
      const pu = S.panels.get(u);
      if (!pu) continue;
      for (const v of wiredTo(u)) {
        const d = dist.get(u) + apart(pu, S.panels.get(v));
        if (!dist.has(v) || d < dist.get(v)) dist.set(v, d);
      }
    }
    const segs = [];
    for (const [i, j] of wireList()) {
      if (!dist.has(i) || !dist.has(j)) continue;
      const [a, b] = dist.get(i) <= dist.get(j) ? [i, j] : [j, i];
      if (Math.abs(dist.get(a) + apart(S.panels.get(a), S.panels.get(b)) - dist.get(b)) > 1e-6) continue;
      segs.push({ a, b, d0: dist.get(a), d1: dist.get(b) });
    }
    return segs;
  }

  // The wires, and what is travelling along them. `at(i)` is where panel i
  // is on the canvas in the view being drawn, or null.
  function drawWires(at, now) {
    const list = wireList();
    ctx.lineCap = 'round';
    for (const [i, j] of list) {
      const a = at(i), b = at(j);
      if (!a || !b) continue;
      const mine = S.picked.has(i) || S.picked.has(j);
      ctx.strokeStyle = mine ? colors.led : ROOM.line;
      ctx.globalAlpha = mine ? 0.95 : 0.75;
      ctx.lineWidth = mine ? 3 : 2;
      ctx.beginPath();
      ctx.moveTo(a[0], a[1]);
      ctx.lineTo(b[0], b[1]);
      ctx.stroke();
    }
    for (const w of S.waves) {
      if (!w.wr || !w.tree) continue;
      const age = (now - w.at) / 1000, gone = age * w.speed;
      ctx.fillStyle = ctx.strokeStyle = w.c === 4 || w.c === 5 ? ROOM.dark : colors.led;
      for (const s of w.tree) {
        const a = at(s.a), b = at(s.b);
        if (!a || !b) continue;
        if (w.now) {                       // at once: the whole way lights up, and fades
          ctx.globalAlpha = 0.85 * clamp(1 - age / 0.5, 0, 1);
          ctx.lineWidth = 4.5;
          ctx.beginPath();
          ctx.moveTo(a[0], a[1]);
          ctx.lineTo(b[0], b[1]);
          ctx.stroke();
        } else if (s.d1 > s.d0 && gone >= s.d0 && gone <= s.d1) {
          const t = (gone - s.d0) / (s.d1 - s.d0);      // a dot where the value is now
          ctx.globalAlpha = 1;
          ctx.beginPath();
          ctx.arc(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, 5, 0, Math.PI * 2);
          ctx.fill();
        }
      }
    }
    ctx.globalAlpha = 1;
    ctx.lineCap = 'butt';
  }

  // Pulling a wire from a panel to another, or drawing a cut across wires.
  let wireDrag = null;             // { kind: 'wire', from, x, y } or { kind: 'cut', pts }
  const tileUnder = (sx, sy) => (S.view === '3d' ? tileAt3(sx, sy) : tileAt(sx, sy));
  function wireAt(i) {             // where panel i is on the canvas, in the view shown
    const p = S.panels.get(i);
    if (!p) return null;
    return S.view === '3d' ? project(place(p), basis()) : toScreen(p.x, p.y);
  }
  function segCross(p, q, a, b) {  // do the segments p-q and a-b meet
    const o = (u, v, w) => (v[0] - u[0]) * (w[1] - u[1]) - (v[1] - u[1]) * (w[0] - u[0]);
    const d1 = o(p, q, a), d2 = o(p, q, b), d3 = o(a, b, p), d4 = o(a, b, q);
    return d1 * d2 <= 0 && d3 * d4 <= 0 && !(d1 === 0 && d2 === 0 && d3 === 0 && d4 === 0);
  }
  function startWire(ev) {
    const [sx, sy] = pointerAt(ev);
    const i = tileUnder(sx, sy);
    // A cut: Ctrl with the right button, as in the node editor; with the Wire tool, a drag that starts on nothing.
    const cut = ev.button === 2 ? ev.ctrlKey : i == null;
    if (!cut && i == null) return false;                // the right button on nothing does nothing
    ev.preventDefault();
    canvas.focus({ preventScroll: true });
    try { canvas.setPointerCapture(ev.pointerId); } catch (_) { /* a pointer made by a script */ }
    wireDrag = cut ? { kind: 'cut', pts: [[sx, sy]] } : { kind: 'wire', from: i, x: sx, y: sy };
    return true;
  }
  function endWire() {
    const w = wireDrag;
    wireDrag = null;
    if (w.kind === 'wire') {
      const to = tileUnder(w.x, w.y);
      if (to != null && to !== w.from) setWire(w.from, to);          // one there already: it is taken away
      return;
    }
    let cut = 0;
    for (const [i, j] of wireList()) {
      const a = wireAt(i), b = wireAt(j);
      if (!a || !b) continue;
      for (let n = 1; n < w.pts.length; n++) {
        if (!segCross(w.pts[n - 1], w.pts[n], a, b)) continue;
        setWire(i, j, false);
        cut++;
        break;
      }
    }
    if (cut) toast(`${nOf(cut, 'wire')} cut`);
  }
  function drawWireTool() {
    if (!wireDrag) return;
    if (wireDrag.kind === 'wire') {
      const a = wireAt(wireDrag.from);
      if (!a) return;
      const over = tileUnder(wireDrag.x, wireDrag.y);
      const b = over != null && over !== wireDrag.from ? wireAt(over) : null;
      ctx.strokeStyle = colors.led;
      ctx.lineWidth = 2.5;
      ctx.setLineDash(b ? [] : [6, 4]);
      ctx.beginPath();
      ctx.moveTo(a[0], a[1]);
      ctx.lineTo(b ? b[0] : wireDrag.x, b ? b[1] : wireDrag.y);
      ctx.stroke();
    } else if (wireDrag.pts.length > 1) {
      ctx.strokeStyle = colors.bad;
      ctx.lineWidth = 1.5;
      ctx.setLineDash([5, 4]);
      ctx.beginPath();
      wireDrag.pts.forEach((q, n) => (n ? ctx.lineTo(q[0], q[1]) : ctx.moveTo(q[0], q[1])));
      ctx.stroke();
    }
    ctx.setLineDash([]);
  }
  // F, as Blender joins what is selected. Two panels: a wire between them,
  // or away with the one they have. More: a chain from an end, each to the
  // nearest not yet on it (and the same again takes the chain away).
  function wirePicked() {
    const ids = [...S.picked].filter((i) => S.panels.has(i));
    if (ids.length < 2) { toast('Select two or more panels to wire them'); return; }
    if (ids.length === 2) { setWire(ids[0], ids[1]); return; }
    const ps = ids.map((i) => S.panels.get(i));
    const mid = { x: ps.reduce((s, p) => s + p.x, 0) / ps.length, y: ps.reduce((s, p) => s + p.y, 0) / ps.length,
                  z: ps.reduce((s, p) => s + (p.z || 0), 0) / ps.length };
    let at = ids.reduce((best, i) => (apart(S.panels.get(i), mid) > apart(S.panels.get(best), mid) ? i : best), ids[0]);
    const left = new Set(ids);
    left.delete(at);
    const pairs = [];
    while (left.size) {
      let next = null;
      for (const j of left) if (next == null || apart(S.panels.get(at), S.panels.get(j)) < apart(S.panels.get(at), S.panels.get(next))) next = j;
      pairs.push([at, next]);
      left.delete(next);
      at = next;
    }
    const all = pairs.every(([a, b]) => ((S.wires[a] || 0) >>> b) & 1);
    for (const [a, b] of pairs) setWire(a, b, !all);
    toast(all ? 'Wires between them removed' : `${ids.length} panels wired in a chain`);
  }

  // ── The map as a room (3D) ─────────────────────────────────────────────
  // Blender's view, because that is what its user knows: Z is up, the flat
  // map is the Front view, and the mouse and the keys do what they do
  // there. A panel's place (x across, y down, z depth) is the point
  // (x, z, -y) in that space.
  //   middle button            orbit   (Alt + left button without one)
  //   Shift + middle button    pan
  //   Ctrl + middle, wheel     zoom    (Shift / Ctrl + wheel: pan up-down / sideways)
  //   numpad 1 3 7             front, right, top (Ctrl: the opposite); 9 the other side
  //   numpad 5                 perspective / orthographic; 2 4 6 8 orbit in steps
  //   Home, numpad .           everything in view, the chosen panel in view
  //   click, drag              choose a panel, move it in the plane of the view
  //   G                        grab: X Y Z along an axis, Shift + X Y Z across it,
  //                            Ctrl snaps to quarters, numbers type the distance,
  //                            click or Enter keeps, right button or Esc puts back
  //   R                        turn: about the line of sight, X Y Z about an axis,
  //                            Ctrl snaps to five degrees, numbers type the angle;
  //                            Alt + R takes every turn off
  // The number row does for the numpad on a laptop.
  const V = { yaw: 0.6, pitch: 0.42, dist: 8, target: [0, 0, 0], ortho: false, auto: false, fitted: false };
  const FOV = 40 * Math.PI / 180, NEAR = 0.05;
  const AXIS = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
  const AXIS_COLOUR = ['#FF3352', '#8BDC00', '#2890FF'], AXIS_NAME = ['X', 'Y', 'Z'];
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const add = (a, b, s = 1) => [a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s];
  const place = (p) => [p.x, p.z || 0, -p.y];
  const mouse = { x: 0, y: 0, over: false };
  // The room is on white: the panels are the dark things in it.
  const ROOM = { bg: '#FFFFFF', grid: '#DDD9D1', text: '#4E493F', line: '#6B655B', dark: '#1B1813' };

  // How a panel is turned: three angles in degrees about X, Y and Z, in that
  // order, as Blender keeps them (XYZ Euler). Matrices are rows.
  function matEuler(a, b, c) {
    const ca = Math.cos(a), sa = Math.sin(a), cb = Math.cos(b), sb = Math.sin(b), cc = Math.cos(c), sc = Math.sin(c);
    return [[cc * cb, cc * sb * sa - sc * ca, cc * sb * ca + sc * sa],
            [sc * cb, sc * sb * sa + cc * ca, sc * sb * ca - cc * sa],
            [-sb, cb * sa, cb * ca]];
  }
  function matAxis(axis, t) {    // a turn by t about a unit axis
    const [x, y, z] = axis, c = Math.cos(t), s = Math.sin(t), k = 1 - c;
    return [[c + x * x * k, x * y * k - z * s, x * z * k + y * s],
            [y * x * k + z * s, c + y * y * k, y * z * k - x * s],
            [z * x * k - y * s, z * y * k + x * s, c + z * z * k]];
  }
  const matMul = (A, M) => A.map((row) => [0, 1, 2].map((j) => row[0] * M[0][j] + row[1] * M[1][j] + row[2] * M[2][j]));
  const apply = (M, v) => [dot(M[0], v), dot(M[1], v), dot(M[2], v)];
  function eulerOf(M) {          // back to the three angles, to a tenth of a degree
    const sb = clamp(-M[2][0], -1, 1), b = Math.asin(sb);
    let a, c;
    if (Math.abs(sb) < 0.99999) { a = Math.atan2(M[2][1], M[2][2]); c = Math.atan2(M[1][0], M[0][0]); }
    else { a = Math.atan2(-M[1][2], M[1][1]); c = 0; }
    return [a, b, c].map((v) => Math.round(v * 1800 / Math.PI) / 10 || 0);
  }
  function rotOf(p) {
    const r = p.r || [0, 0, 0], d = Math.PI / 180;
    return matEuler(r[0] * d, r[1] * d, r[2] * d);
  }
  const turned3 = (p) => !!p.r && (p.r[0] || p.r[1] || p.r[2]);
  let nav = null;                // turning, sliding or zooming the view with the mouse
  let press = null;              // a press that may become a move of a tile, or of the view by its gizmo
  let grab = null;               // panels being moved or turned, Blender's G and R
  let box = null;                // a rectangle being dragged round the panels to choose, in either view

  // The eye looks along f; r is to its right, u is up. yaw 0, pitch 0 is the Front view.
  function basis() {
    const cy = Math.cos(V.yaw), sy = Math.sin(V.yaw), cp = Math.cos(V.pitch), sp = Math.sin(V.pitch);
    const r = [cy, sy, 0], f = [-sy * cp, cy * cp, -sp], u = [-sy * sp, cy * sp, cp];
    const focal = (H / 2) / Math.tan(FOV / 2);
    return { r, u, f, eye: add(V.target, f, -V.dist), focal, scale: focal / V.dist };
  }
  function toView(P, B) {
    const d = add(P, B.eye, -1);
    return [dot(d, B.r), dot(d, B.u), dot(d, B.f)];
  }
  function fromView(v, B) {      // -> [x, y, pixels a unit] on the canvas
    const s = V.ortho ? B.scale : B.focal / v[2];
    return [W / 2 + v[0] * s, H / 2 - v[1] * s, s];
  }
  function project(P, B) {       // -> [x, y, depth, pixels a unit], or null behind the eye
    const v = toView(P, B);
    if (!V.ortho && v[2] < NEAR) return null;
    const s = fromView(v, B);
    return [s[0], s[1], v[2], s[2]];
  }
  function line3(A, C, B) {      // a segment, cut where it passes behind the eye
    let a = toView(A, B), c = toView(C, B);
    if (!V.ortho) {
      if (a[2] < NEAR && c[2] < NEAR) return;
      if (a[2] < NEAR || c[2] < NEAR) {
        const t = (NEAR - a[2]) / (c[2] - a[2]);
        const m = [a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t, NEAR];
        if (a[2] < NEAR) a = m; else c = m;
      }
    }
    const p = fromView(a, B), q = fromView(c, B);
    ctx.beginPath();
    ctx.moveTo(p[0], p[1]);
    ctx.lineTo(q[0], q[1]);
    ctx.stroke();
  }

  const sameAngle = (a, b) => Math.abs(Math.atan2(Math.sin(a - b), Math.cos(a - b))) < 0.01;
  // Which axis the view looks along, as [axis, side it looks from], or null.
  function axisView() {
    if (Math.abs(V.pitch - Math.PI / 2) < 0.01) return [2, 1];
    if (Math.abs(V.pitch + Math.PI / 2) < 0.01) return [2, -1];
    if (Math.abs(V.pitch) >= 0.01) return null;
    if (sameAngle(V.yaw, 0)) return [1, -1];
    if (sameAngle(V.yaw, Math.PI)) return [1, 1];
    if (sameAngle(V.yaw, Math.PI / 2)) return [0, 1];
    if (sameAngle(V.yaw, -Math.PI / 2)) return [0, -1];
    return null;
  }
  function viewName() {
    const v = axisView();
    const side = !v ? 'User' : v[0] === 2 ? (v[1] > 0 ? 'Top' : 'Bottom') : v[0] === 1 ? (v[1] > 0 ? 'Back' : 'Front') : (v[1] > 0 ? 'Right' : 'Left');
    return `${side} ${V.ortho ? 'Orthographic' : 'Perspective'}`;
  }
  // Look from that end of an axis, flat, as the numpad does.
  function viewFrom(axis, side) {
    if (axis === 2) { V.yaw = 0; V.pitch = side * Math.PI / 2; }
    else { V.pitch = 0; V.yaw = axis === 0 ? side * Math.PI / 2 : side > 0 ? Math.PI : 0; }
    V.ortho = true;
    V.auto = true;               // turning the view by hand goes back to perspective, as Blender does
  }
  function turned() {
    if (V.auto) { V.ortho = false; V.auto = false; }
  }

  // Everything in view, or one panel.
  function frame3(only) {
    const ps = only && only.length ? only : [...S.panels.values()];
    if (!ps.length) return;
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (const p of ps) {
      const c = place(p);
      for (let a = 0; a < 3; a++) { lo[a] = Math.min(lo[a], c[a]); hi[a] = Math.max(hi[a], c[a]); }
    }
    V.target = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2];
    const radius = Math.hypot(hi[0] - lo[0] + TILE_W, hi[1] - lo[1], hi[2] - lo[2] + TILE_H) / 2 + (ps.length === 1 ? 0.9 : 0.4);
    const half = Math.min(FOV / 2, Math.atan(Math.tan(FOV / 2) * W / H));
    V.dist = clamp(radius / Math.sin(half), 1.5, 150);
    V.fitted = true;
  }

  // The ground, a line at every unit, with the axes in Blender's colours. An
  // axis view shows the plane it looks at instead.
  function drawGrid(B) {
    const v = V.ortho ? axisView() : null;
    const n = v && v[0] !== 2 ? v[0] : 2;
    const a = (n + 1) % 3, b = (n + 2) % 3;
    const ext = V.ortho ? Math.min(60, Math.ceil(Math.max(W, H) / B.scale / 2) + 1) : clamp(Math.ceil(V.dist * 2.5), 8, 40);
    ctx.lineWidth = 1;
    for (const [u, w] of [[a, b], [b, a]]) {
      const cu = Math.round(V.target[u]), cw = Math.round(V.target[w]);
      for (let k = -ext; k <= ext; k++) {
        const P = [0, 0, 0], Q = [0, 0, 0];
        P[u] = Q[u] = cu + k;
        P[w] = cw - ext;
        Q[w] = cw + ext;
        const isAxis = cu + k === 0;
        ctx.strokeStyle = isAxis ? AXIS_COLOUR[w] : ROOM.grid;
        ctx.globalAlpha = isAxis ? 0.8 : V.ortho ? 0.9 : clamp(1.15 - Math.abs(k) / ext, 0.12, 0.9);
        line3(P, Q, B);
      }
    }
    ctx.globalAlpha = 1;
  }

  // A tile's face is drawn flat, once, and laid on its four corners as two triangles.
  const FACE_W = 168, FACE_H = 84, FACE_SCALE = 2;
  const face = document.createElement('canvas');
  face.width = FACE_W * FACE_SCALE;
  face.height = FACE_H * FACE_SCALE;
  const faceCtx = face.getContext('2d');
  function texTriangle(p0, p1, p2, u0, v0, u1, v1, u2, v2) {
    const d = (u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0);
    if (!d) return;
    const a = ((p1[0] - p0[0]) * (v2 - v0) - (p2[0] - p0[0]) * (v1 - v0)) / d;
    const b = ((p1[1] - p0[1]) * (v2 - v0) - (p2[1] - p0[1]) * (v1 - v0)) / d;
    const c = ((p2[0] - p0[0]) * (u1 - u0) - (p1[0] - p0[0]) * (u2 - u0)) / d;
    const e = ((p2[1] - p0[1]) * (u1 - u0) - (p1[1] - p0[1]) * (u2 - u0)) / d;
    ctx.save();
    ctx.beginPath();
    ctx.moveTo(p0[0], p0[1]);
    ctx.lineTo(p1[0], p1[1]);
    ctx.lineTo(p2[0], p2[1]);
    ctx.closePath();
    ctx.clip();
    ctx.transform(a, b, c, e, p0[0] - a * u0 - c * v0, p0[1] - b * u0 - e * v0);
    ctx.drawImage(face, 0, 0);
    ctx.restore();
  }
  // The four corners of a panel on the canvas: top left, top right, bottom right, bottom left as its front shows them.
  function corners(p, B) {
    const c = place(p), M = rotOf(p), hw = TILE_W / 2, hh = TILE_H / 2;
    const q = [[-hw, 0, hh], [hw, 0, hh], [hw, 0, -hh], [-hw, 0, -hh]].map((v) => project(add(c, apply(M, v)), B));
    return q.every(Boolean) ? q : null;
  }

  // A panel seen edge-on (from above, from the side) is a line: its two ends, or null.
  function edgeOn(q) {
    const area = Math.abs((q[2][0] - q[0][0]) * (q[3][1] - q[1][1]) - (q[3][0] - q[1][0]) * (q[2][1] - q[0][1])) / 2;
    const mid = (a, b) => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
    const len = (e) => Math.hypot(e[1][0] - e[0][0], e[1][1] - e[0][1]);
    const across = [mid(q[0], q[3]), mid(q[1], q[2])], down = [mid(q[0], q[1]), mid(q[3], q[2])];
    const longer = len(across) >= len(down) ? across : down;
    return area > len(longer) * 5 ? null : longer;
  }

  function drawTile3(i, p, c, B, now) {
    const q = corners(p, B);
    if (!q) return;
    const edge = edgeOn(q);
    if (edge) {
      // Thick enough to see and to take hold of, with its number beside it.
      const flashedAt = S.flash.get(i);
      ctx.strokeStyle = S.picked.has(i) || (flashedAt && now - flashedAt < 450) ? colors.led : p.on ? ROOM.dark : '#B5AFA3';
      ctx.lineWidth = S.picked.has(i) ? 5 : 4;
      ctx.beginPath();
      ctx.moveTo(edge[0][0], edge[0][1]);
      ctx.lineTo(edge[1][0], edge[1][1]);
      ctx.stroke();
      ctx.fillStyle = S.picked.has(i) ? colors.led : ROOM.text;
      ctx.font = `700 11px ${colors.sans}`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'bottom';
      ctx.fillText(String(p.id), c[0], c[1] - 5);
      return;
    }
    const outline = () => {
      ctx.beginPath();
      ctx.moveTo(q[0][0], q[0][1]);
      for (let n = 1; n < 4; n++) ctx.lineTo(q[n][0], q[n][1]);
      ctx.closePath();
    };
    const st = tileState(p);
    // A panel faces whoever stands in front of the wall, towards -Y, until it is turned.
    const normal = apply(rotOf(p), [0, -1, 0]);
    const front = V.ortho ? dot(normal, B.f) < -1e-6 : dot(normal, add(B.eye, place(p), -1)) > 0;
    const wide = Math.hypot(q[1][0] - q[0][0], q[1][1] - q[0][1]);
    outline();
    ctx.fillStyle = st.lit ? '#1B1813' : '#0E0D0B';
    ctx.fill();
    if (front && wide >= 14) {
      pen = faceCtx;
      faceCtx.setTransform(FACE_SCALE, 0, 0, FACE_SCALE, 0, 0);
      tileFace(p, st, 0, 0, FACE_W, FACE_H);
      pen = ctx;
      const w = face.width, h = face.height;
      texTriangle(q[0], q[1], q[2], 0, 0, w, 0, w, h);
      texTriangle(q[0], q[2], q[3], 0, 0, w, h, 0, h);
    } else if (!front) {
      // From behind: the number, the right way round.
      ctx.fillStyle = colors.muted;
      ctx.font = `700 ${clamp(Math.round(wide * 0.16), 9, 22)}px ${colors.sans}`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(String(p.id), c[0], c[1]);
    }
    const flashed = S.flash.get(i);
    const hot = flashed && now - flashed < 450;
    outline();
    ctx.lineWidth = S.picked.has(i) ? (i === S.sel ? 3 : 2) : hot ? 2 : 1;   // the chosen ones in orange, as Blender marks them
    ctx.strokeStyle = hot || S.picked.has(i) ? colors.led : ROOM.line;
    ctx.stroke();
    if (wide >= 84) {
      ctx.textAlign = 'center';
      ctx.textBaseline = 'top';
      ctx.font = `11px ${colors.sans}`;
      ctx.fillStyle = p.on ? ROOM.text : colors.bad;
      ctx.fillText(subLabel(p, st.pat), (q[2][0] + q[3][0]) / 2, Math.max(q[2][1], q[3][1]) + 5, wide);
    }
  }

  // The little axes in the corner: which way is which, and a click on an end looks from there.
  const GIZMO_R = 30;
  const gizmoAt = () => [W - 52, 58];
  function gizmoEnds(B) {
    const [gx, gy] = gizmoAt(), out = [];
    for (let a = 0; a < 3; a++)
      for (const s of [1, -1]) {
        const d = AXIS[a].map((v) => v * s);
        out.push({ a, s, x: gx + dot(d, B.r) * GIZMO_R, y: gy - dot(d, B.u) * GIZMO_R, z: dot(d, B.f) });
      }
    return out.sort((p, q) => q.z - p.z);      // the far ones first
  }
  function drawGizmo(B) {
    const [gx, gy] = gizmoAt();
    for (const e of gizmoEnds(B)) {
      if (e.s > 0) {
        ctx.strokeStyle = AXIS_COLOUR[e.a];
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(gx, gy);
        ctx.lineTo(e.x, e.y);
        ctx.stroke();
      }
      ctx.beginPath();
      ctx.arc(e.x, e.y, e.s > 0 ? 9 : 6.5, 0, Math.PI * 2);
      ctx.fillStyle = AXIS_COLOUR[e.a];
      ctx.globalAlpha = e.s > 0 ? 1 : 0.4;
      ctx.fill();
      ctx.globalAlpha = 1;
      if (e.s > 0) {
        ctx.fillStyle = '#000';
        ctx.font = `700 10px ${colors.sans}`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(AXIS_NAME[e.a], e.x, e.y + 0.5);
      }
    }
  }
  function gizmoHit(sx, sy) {
    const ends = gizmoEnds(basis()).reverse();   // the near ones first
    for (const e of ends) if (Math.hypot(sx - e.x, sy - e.y) <= (e.s > 0 ? 10 : 8)) return e;
    const [gx, gy] = gizmoAt();
    return Math.hypot(sx - gx, sy - gy) <= GIZMO_R + 12 ? { area: true } : null;
  }

  function draw3d(now) {
    if (!V.fitted && S.panels.size) frame3();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = ROOM.bg;
    ctx.fillRect(0, 0, W, H);
    const B = basis();
    drawGrid(B);

    // Values on their way: a sphere from where the turn was made, seen as its outline.
    S.waves = S.waves.filter((w) => now - w.at < (w.now ? 500 : w.d + 500));
    for (const w of S.waves) {
      if (w.wr) continue;
      const c = project([w.x, w.z || 0, -w.y], B);
      if (!c) continue;
      const age = (now - w.at) / 1000;
      const radius = (w.now ? age * 14 : age * w.speed) * c[3];
      const fade = w.now ? 1 - age / 0.5 : clamp(1 - (age * 1000 - w.d) / 500, 0, 1);
      ctx.strokeStyle = w.c === 4 || w.c === 5 ? ROOM.dark : colors.led;
      ctx.globalAlpha = 0.5 * clamp(fade, 0, 1);
      ctx.lineWidth = w.c === 4 || w.c === 5 ? 2 : 1.25;
      ctx.beginPath();
      ctx.arc(c[0], c[1], Math.max(0, radius), 0, Math.PI * 2);
      ctx.stroke();
    }
    ctx.globalAlpha = 1;

    drawWires((i) => { const p = S.panels.get(i); return p ? project(place(p), B) : null; }, now);

    // The far panels first.
    const list = [];
    for (const [i, p] of S.panels) {
      const c = project(place(p), B);
      if (c) list.push([i, p, c]);
    }
    list.sort((p, q) => q[2][2] - p[2][2]);
    for (const [i, p, c] of list) drawTile3(i, p, c, B, now);

    // A grab along an axis or across one shows the axes it may move on.
    if (grab && grab.axis != null) {
      ctx.lineWidth = 1.25;
      for (let a = 0; a < 3; a++) {
        if (grab.plane ? a === grab.axis : a !== grab.axis) continue;
        ctx.strokeStyle = AXIS_COLOUR[a];
        line3(add(grab.start, AXIS[a], -60), add(grab.start, AXIS[a], 60), B);
      }
    }
    drawBox();
    drawGizmo(B);

    ctx.font = `12px ${colors.sans}`;
    ctx.textAlign = 'left';
    ctx.textBaseline = 'top';
    ctx.fillStyle = ROOM.text;
    ctx.fillText(viewName(), 12, 44);
    if (grab) {
      const d = grab.delta, f = (v) => (Math.round(v * 100) / 100 || 0).toFixed(2), len = Math.hypot(d[0], d[1], d[2]);
      const typed = grab.typed ? `[${grab.typed}|]` : null;
      let text;
      if (grab.mode === 'turn') text = `Rot: ${typed || f(grab.angle * 180 / Math.PI)}°  ${grab.axis == null ? 'about the view' : 'about ' + AXIS_NAME[grab.axis]}`;
      else if (grab.axis != null && !grab.plane) text = `D: ${typed || f(d[grab.axis])}  (${f(len)})  along ${AXIS_NAME[grab.axis]}`;
      else text = `Dx: ${f(d[0])}  Dy: ${f(d[1])}  Dz: ${f(d[2])}  (${f(len)})` + (grab.plane ? `  locking ${AXIS_NAME[grab.axis]}` : '');
      ctx.fillStyle = '#111';
      ctx.fillText(text + (grab.snap ? '  · snap' : ''), 12, 62);
    }
  }

  function draw(now) {
    if (S.view === '3d') draw3d(now);
    else draw2d(now);
    drawWireTool();
  }

  function tileAt3(sx, sy) {
    const B = basis();
    let best = null, depth = Infinity;
    for (const [i, p] of S.panels) {
      const q = corners(p, B), c = project(place(p), B);
      if (!q || !c || c[2] >= depth) continue;
      const edge = edgeOn(q);
      if (edge) {
        // Edge-on: near enough to the line it has become.
        const ex = edge[1][0] - edge[0][0], ey = edge[1][1] - edge[0][1];
        const t = clamp(((sx - edge[0][0]) * ex + (sy - edge[0][1]) * ey) / (ex * ex + ey * ey || 1), 0, 1);
        if (Math.hypot(sx - edge[0][0] - ex * t, sy - edge[0][1] - ey * t) <= 7) { best = i; depth = c[2]; }
        continue;
      }
      let sign = 0, inside = true;
      for (let n = 0; n < 4 && inside; n++) {
        const a = q[n], b = q[(n + 1) % 4];
        const cr = (b[0] - a[0]) * (sy - a[1]) - (b[1] - a[1]) * (sx - a[0]);
        if (Math.abs(cr) < 1e-9) continue;
        if (!sign) sign = Math.sign(cr);
        else if (Math.sign(cr) !== sign) inside = false;
      }
      if (inside) { best = i; depth = c[2]; }
    }
    return best;
  }

  // Where the line of sight through a point of the canvas meets a plane.
  function onPlane(sx, sy, P0, n, B) {
    const ax = sx - W / 2, ay = -(sy - H / 2);
    const o = V.ortho ? add(add(B.eye, B.r, ax / B.scale), B.u, ay / B.scale) : B.eye;
    const d = V.ortho ? B.f : add(add(B.f, B.r, ax / B.focal), B.u, ay / B.focal);
    const dn = dot(d, n);
    if (Math.abs(dn) < 1e-4) return null;
    return add(o, d, dot(add(P0, o, -1), n) / dn);
  }

  // The numbers of the panel the side shows, as they change under the pointer.
  function showPlace() {
    const p = S.sel != null ? S.panels.get(S.sel) : null;
    if (!p) return;
    const r = p.r || [0, 0, 0];
    for (const [id, v] of [['p-x', p.x], ['p-y', p.y], ['p-z', p.z || 0], ['p-rx', r[0]], ['p-ry', r[1]], ['p-rz', r[2]]])
      if (!busy($(id))) $(id).value = v;
  }

  // Blender's G and R on every chosen panel at once. A move is measured on
  // the panel the side shows; a turn goes round the middle of them all.
  function startGrab(mode, sx, sy, tweak) {
    const items = [];
    for (const i of S.picked) {
      const p = S.panels.get(i);
      if (p) items.push({ i, start: place(p), M0: rotOf(p), orig: { x: p.x, y: p.y, z: p.z || 0, r: (p.r || [0, 0, 0]).slice() } });
    }
    if (!items.length) return;
    const lead = items.find((it) => it.i === S.sel) || items[0];
    const middle = [0, 1, 2].map((a) => items.reduce((sum, it) => sum + it.start[a], 0) / items.length);
    grab = { mode, tweak, items, start: mode === 'turn' ? middle : lead.start, axis: null, plane: false, typed: '',
             mx0: sx, my0: sy, snap: false, delta: [0, 0, 0], angle: 0 };
    for (const it of items) held.set(it.i, { ...it.orig });
    canvas.style.cursor = mode === 'turn' ? 'crosshair' : 'move';
  }
  function moveGrab() {
    const g = grab, B = basis();
    const typed = parseFloat(g.typed), hasTyped = !!g.typed && !Number.isNaN(typed);
    if (g.mode === 'turn') {
      // About the line of sight, or about an axis; the pointer's angle
      // around the middle is the angle they turn by.
      const c = project(g.start, B);
      if (!c) return;
      const axis = g.axis == null ? B.f.map((v) => -v) : AXIS[g.axis];
      let t;
      if (hasTyped) t = typed * Math.PI / 180;
      else {
        const a0 = Math.atan2(-(g.my0 - c[1]), g.mx0 - c[0]), a1 = Math.atan2(-(mouse.y - c[1]), mouse.x - c[0]);
        t = Math.atan2(Math.sin(a1 - a0), Math.cos(a1 - a0));
        if (dot(axis, B.f) > 0) t = -t;        // an axis pointing away turns the other way on the screen
        if (g.snap) t = Math.round(t / (5 * Math.PI / 180)) * (5 * Math.PI / 180);
      }
      g.angle = t;
      const R = matAxis(axis, t);
      for (const it of g.items) {
        const p = S.panels.get(it.i);
        if (!p) continue;
        const P = add(g.start, apply(R, add(it.start, g.start, -1)));
        p.x = round2(P[0]);
        p.z = round2(P[1]);
        p.y = round2(-P[2]);
        p.r = eulerOf(matMul(R, it.M0));
        hold(it.i, p);
        throttled('move' + it.i, () => send({ c: 'panel', uid: p.uid, x: p.x, y: p.y, z: p.z, rx: p.r[0], ry: p.r[1], rz: p.r[2] }), 120);
      }
      showPlace();
      return;
    }
    let d = [0, 0, 0];
    if (hasTyped) {
      // A typed distance goes along the locked axis; with none, along X, as in Blender.
      d[g.axis == null ? 0 : g.plane ? (g.axis + 1) % 3 : g.axis] = typed;
    } else if (g.axis != null && !g.plane) {
      // Along one axis: how far the pointer went along that axis as the screen shows it.
      const a0 = project(g.start, B), a1 = project(add(g.start, AXIS[g.axis]), B);
      if (a0 && a1) {
        const ax = a1[0] - a0[0], ay = a1[1] - a0[1], l2 = ax * ax + ay * ay;
        if (l2 > 0.5) d[g.axis] = ((mouse.x - g.mx0) * ax + (mouse.y - g.my0) * ay) / l2;
      }
    } else {
      // On a plane: the view's own through the panel, or the one a locked axis stands on.
      const n = g.plane ? AXIS[g.axis] : B.f;
      const p0 = onPlane(g.mx0, g.my0, g.start, n, B), p1 = onPlane(mouse.x, mouse.y, g.start, n, B);
      if (p0 && p1) d = add(p1, p0, -1);
      if (g.plane) d[g.axis] = 0;
    }
    const step = g.snap && !g.typed ? 0.25 : 0.01;
    const P = g.start.map((v, a) => (Math.abs(d[a]) > 1e-9 ? Math.round((v + d[a]) / step) * step : v));
    g.delta = P.map((v, a) => v - g.start[a]);
    for (const it of g.items) {
      const p = S.panels.get(it.i);
      if (!p) continue;
      p.x = round2(it.start[0] + g.delta[0]);
      p.z = round2(it.start[1] + g.delta[1]);
      p.y = round2(-(it.start[2] + g.delta[2]));
      hold(it.i, p);
      throttled('move' + it.i, () => send({ c: 'panel', uid: p.uid, x: p.x, y: p.y, z: p.z }), 120);
    }
    showPlace();
  }
  function endGrab(keep) {
    const g = grab;
    grab = null;
    canvas.style.cursor = '';
    for (const it of g.items) {
      const p = S.panels.get(it.i);
      if (!p) continue;
      if (!keep) { p.x = it.orig.x; p.y = it.orig.y; p.z = it.orig.z; p.r = it.orig.r; }
      hold(it.i, p);
      const r = p.r || [0, 0, 0];
      send({ c: 'panel', uid: p.uid, x: p.x, y: p.y, z: p.z || 0, rx: r[0], ry: r[1], rz: r[2] });
    }
    renderInspector();
    letGo(g.items.map((it) => it.i));
  }

  const pointerAt = (ev) => {
    const r = canvas.getBoundingClientRect();
    return [ev.clientX - r.left, ev.clientY - r.top];
  };
  // The panels a dragged rectangle takes in.
  function inBox(b) {
    const x0 = Math.min(b.sx, b.x), x1 = Math.max(b.sx, b.x), y0 = Math.min(b.sy, b.y), y1 = Math.max(b.sy, b.y);
    const out = [];
    if (S.view === '3d') {
      const B = basis();
      for (const [i, p] of S.panels) {
        const pts = (corners(p, B) || []).concat([project(place(p), B)]).filter(Boolean);
        if (pts.some((q) => q[0] >= x0 && q[0] <= x1 && q[1] >= y0 && q[1] <= y1)) out.push(i);
      }
    } else {
      const tw = TILE_W * S.cam.z / 2, th = TILE_H * S.cam.z / 2;
      for (const [i, p] of S.panels) {
        const [cx, cy] = toScreen(p.x, p.y);
        if (cx + tw >= x0 && cx - tw <= x1 && cy + th >= y0 && cy - th <= y1) out.push(i);
      }
    }
    return out;
  }
  function drawBox() {
    if (!box) return;
    const x = Math.min(box.sx, box.x), y = Math.min(box.sy, box.y), w = Math.abs(box.x - box.sx), h = Math.abs(box.y - box.sy);
    ctx.fillStyle = 'rgba(255, 92, 46, 0.08)';
    ctx.fillRect(x, y, w, h);
    ctx.setLineDash([4, 3]);
    ctx.lineWidth = 1;
    ctx.strokeStyle = ROOM.text;
    ctx.strokeRect(x + 0.5, y + 0.5, w, h);
    ctx.setLineDash([]);
  }

  function startNav(kind, sx, sy) {
    nav = { kind, sx, sy, yaw: V.yaw, pitch: V.pitch, dist: V.dist, target: V.target.slice(), B: basis() };
    if (kind === 'orbit') turned();
  }
  function down3(ev) {
    const [sx, sy] = pointerAt(ev);
    mouse.x = sx;
    mouse.y = sy;
    canvas.focus({ preventScroll: true });
    if (grab) {
      if (ev.button === 0) endGrab(true);
      else if (ev.button === 2) endGrab(false);
      return;
    }
    if (ev.button === 1 || (ev.button === 0 && ev.altKey)) {
      ev.preventDefault();
      canvas.setPointerCapture(ev.pointerId);
      startNav(ev.ctrlKey ? 'zoom' : ev.shiftKey ? 'pan' : 'orbit', sx, sy);
      return;
    }
    if (ev.button !== 0) return;
    canvas.setPointerCapture(ev.pointerId);
    const gizmo = gizmoHit(sx, sy);
    if (gizmo) { press = { gizmo, sx, sy }; return; }
    const i = tileAt3(sx, sy);
    if (i != null && ev.shiftKey) { pick(i, true); return; }
    if (i != null) pick(i, false);
    press = { i, sx, sy, moved: false, add: ev.shiftKey };
  }
  function move3(ev) {
    const [sx, sy] = pointerAt(ev);
    mouse.x = sx;
    mouse.y = sy;
    if (grab) { grab.snap = ev.ctrlKey; moveGrab(); return; }
    if (nav) {
      const dx = sx - nav.sx, dy = sy - nav.sy;
      if (nav.kind === 'orbit') {
        // The room follows the pointer: right turns it right, down tips it to show its top.
        V.yaw = nav.yaw - dx * 0.008;
        V.pitch = clamp(nav.pitch + dy * 0.008, -Math.PI / 2, Math.PI / 2);
      } else if (nav.kind === 'pan') {
        const s = V.ortho ? nav.B.scale : nav.B.focal / nav.dist;
        V.target = add(add(nav.target, nav.B.r, -dx / s), nav.B.u, dy / s);
      } else {
        V.dist = clamp(nav.dist * Math.exp(dy * 0.01), 0.5, 200);
      }
      return;
    }
    if (!press || (!press.moved && Math.hypot(sx - press.sx, sy - press.sy) <= 4)) return;
    if (press.gizmo) {
      startNav('orbit', press.sx, press.sy);
      press = null;
    } else if (press.i != null) {
      startGrab('move', press.sx, press.sy, true);
      press = null;
      if (grab) moveGrab();
    } else {
      // A drag over nothing draws a box round the panels to choose.
      press.moved = true;
      box = { sx: press.sx, sy: press.sy, x: sx, y: sy };
    }
  }
  function up3() {
    if (grab && grab.tweak) endGrab(true);
    if (press) {
      if (press.gizmo) { if (press.gizmo.a != null) viewFrom(press.gizmo.a, press.gizmo.s); }
      else if (press.i == null) {
        if (press.moved && box) pickBox(inBox(box), press.add);
        else if (!press.add) pickNone();
      } else if (S.picked.size > 1) {
        pickBox([press.i], false);        // a plain click on one of several chosen: that one alone
      }
    }
    box = null;
    press = null;
    nav = null;
  }
  function wheel3(ev) {
    if (ev.ctrlKey || ev.shiftKey) {
      const B = basis(), s = V.ortho ? B.scale : B.focal / V.dist, d = (ev.deltaY || ev.deltaX) * 0.6 / s;
      V.target = ev.ctrlKey ? add(V.target, B.r, d) : add(V.target, B.u, -d);
      return;
    }
    V.dist = clamp(V.dist * Math.exp(ev.deltaY * 0.0012), 0.5, 200);
  }

  function mapKey(ev) {
    if (S.page !== 'stage' || $('help').open) return;
    const el = document.activeElement;
    const key = ev.key.toLowerCase();
    if (grab) {
      if (key === 'escape') endGrab(false);
      else if (key === 'enter' || key === ' ') endGrab(true);
      else if (key === 'x' || key === 'y' || key === 'z') {
        const a = 'xyz'.indexOf(key);
        const across = ev.shiftKey && grab.mode === 'move';
        if (grab.axis === a && grab.plane === across) { grab.axis = null; grab.plane = false; }
        else { grab.axis = a; grab.plane = across; }
        moveGrab();
      } else if (/^[0-9.]$/.test(ev.key)) { grab.typed += ev.key; moveGrab(); }
      else if (ev.key === '-') { grab.typed = grab.typed.startsWith('-') ? grab.typed.slice(1) : '-' + grab.typed; moveGrab(); }
      else if (key === 'backspace') { grab.typed = grab.typed.slice(0, -1); moveGrab(); }
      else if (key === 'control') { grab.snap = true; moveGrab(); }
      else return;
      ev.preventDefault();
      return;
    }
    const typing = el && el !== canvas && /^(INPUT|TEXTAREA|SELECT)$/.test(el.tagName);
    if (typing || !(mouse.over || el === canvas) || ev.metaKey) return;
    // Both views: A chooses every panel, Alt + A or Esc none.
    if (key === 'a' && !ev.ctrlKey) {
      if (ev.altKey) pickNone();
      else pickBox([...S.panels.keys()], false);
      ev.preventDefault();
      return;
    }
    if (key === 'escape') { pickNone(); return; }
    if (key === 'f' && !ev.ctrlKey && !ev.altKey) { wirePicked(); ev.preventDefault(); return; }
    if (S.view !== '3d') return;
    if (ev.altKey) {
      // Alt + R: no turn at all, as in Blender.
      if (key !== 'r' || !S.picked.size) return;
      for (const i of S.picked) {
        const p = S.panels.get(i);
        if (!p) continue;
        p.r = [0, 0, 0];
        send({ c: 'panel', uid: p.uid, rx: 0, ry: 0, rz: 0 });
      }
      renderInspector();
      ev.preventDefault();
      return;
    }
    const digit = /^(?:Numpad|Digit)(\d)$/.exec(ev.code);
    const side = ev.ctrlKey ? -1 : 1;
    if ((key === 'g' || key === 'r') && !ev.ctrlKey) {
      startGrab(key === 'r' ? 'turn' : 'move', mouse.x, mouse.y, false);
    } else if (digit) {
      const n = +digit[1], step = 15 * Math.PI / 180;
      if (n === 1) viewFrom(1, -side);
      else if (n === 3) viewFrom(0, side);
      else if (n === 7) viewFrom(2, side);
      else if (n === 9) { V.yaw += Math.PI; V.pitch = -V.pitch; }
      else if (n === 5) { V.ortho = !V.ortho; V.auto = false; }
      else if (n === 4 || n === 6) { turned(); V.yaw += n === 4 ? -step : step; }
      else if (n === 8 || n === 2) { turned(); V.pitch = clamp(V.pitch + (n === 8 ? step : -step), -Math.PI / 2, Math.PI / 2); }
      else return;
    } else if (key === 'home') frame3();
    else if (ev.code === 'NumpadDecimal' || key === '.') frame3([...S.picked].map((i) => S.panels.get(i)).filter(Boolean));
    else if (ev.code === 'NumpadAdd' || key === '+' || key === '=') V.dist = clamp(V.dist / 1.2, 0.5, 200);
    else if (ev.code === 'NumpadSubtract' || key === '-') V.dist = clamp(V.dist * 1.2, 0.5, 200);
    else return;
    ev.preventDefault();
  }
  document.addEventListener('keydown', mapKey);
  document.addEventListener('keyup', (ev) => {
    if (grab && ev.key === 'Control') { grab.snap = false; moveGrab(); }
  });
  canvas.addEventListener('pointerenter', () => { mouse.over = true; });
  canvas.addEventListener('pointerleave', () => { mouse.over = false; });
  // The middle button moves the view; the browser's own scrolling by it would take it away.
  canvas.addEventListener('mousedown', (ev) => { if (ev.button === 1) ev.preventDefault(); });
  canvas.addEventListener('auxclick', (ev) => ev.preventDefault());
  canvas.addEventListener('contextmenu', (ev) => ev.preventDefault());    // the right button pulls wires and cancels grabs

  function setView(view) {
    if (grab) endGrab(false);
    nav = null;
    press = null;
    drag = null;
    box = null;
    S.view = view === '3d' ? '3d' : '2d';
    $('view-2d').setAttribute('aria-pressed', String(S.view === '2d'));
    $('view-3d').setAttribute('aria-pressed', String(S.view === '3d'));
    $('stage').classList.toggle('three', S.view === '3d');
    try { localStorage.setItem('pfe-view', S.view); } catch (_) { /* no storage here */ }
  }

  // The flat map: a drag on a panel moves every chosen one, a drag on nothing
  // draws a box round the ones to choose, Shift adds to the choice. The
  // middle button (or Alt, or a finger) slides the map, the wheel zooms.
  let drag = null;
  canvas.addEventListener('pointerdown', (ev) => {
    // Wires, in either view: the right button from a panel to a panel, Ctrl
    // + right button to cut; with the Wire tool on, the left button does both.
    if (!grab && (ev.button === 2 || (S.wireMode && ev.button === 0 && !ev.altKey)) && startWire(ev)) return;
    if (S.view === '3d') { down3(ev); return; }
    const [sx, sy] = pointerAt(ev);
    canvas.focus({ preventScroll: true });
    const i = ev.button === 0 ? tileAt(sx, sy) : null;
    if (ev.button === 1 || (ev.button === 0 && i == null && (ev.altKey || ev.pointerType === 'touch'))) {
      ev.preventDefault();
      canvas.setPointerCapture(ev.pointerId);
      drag = { kind: 'pan', sx, sy, cx: S.cam.x, cy: S.cam.y, moved: false, touch: ev.pointerType === 'touch' };
      return;
    }
    if (ev.button !== 0) return;
    canvas.setPointerCapture(ev.pointerId);
    if (i == null) { drag = { kind: 'box', sx, sy, add: ev.shiftKey, moved: false }; return; }
    if (ev.shiftKey) { pick(i, true); return; }
    pick(i, false);
    const from = new Map();
    for (const j of S.picked) {
      const q = S.panels.get(j);
      if (q) from.set(j, { x: q.x, y: q.y });
    }
    drag = { kind: 'tile', i, sx, sy, from, moved: false };
  });
  canvas.addEventListener('pointermove', (ev) => {
    if (wireDrag) {
      const [sx, sy] = pointerAt(ev);
      if (wireDrag.kind === 'wire') { wireDrag.x = sx; wireDrag.y = sy; }
      else wireDrag.pts.push([sx, sy]);
      return;
    }
    if (S.view === '3d') { move3(ev); return; }
    if (!drag) return;
    const [sx, sy] = pointerAt(ev);
    if (!drag.moved && Math.hypot(sx - drag.sx, sy - drag.sy) < 5) return;
    drag.moved = true;
    const dx = (sx - drag.sx) / S.cam.z, dy = (sy - drag.sy) / S.cam.z;
    if (drag.kind === 'pan') { S.cam.x = drag.cx - dx; S.cam.y = drag.cy - dy; return; }
    if (drag.kind === 'box') { box = { sx: drag.sx, sy: drag.sy, x: sx, y: sy }; return; }
    const lead = drag.from.get(drag.i);
    if (!lead) return;
    // The panel under the pointer lands on the grid; the others keep their places beside it.
    const snap = ev.altKey ? 0.01 : 0.25;
    const mx = Math.round((lead.x + dx) / snap) * snap - lead.x, my = Math.round((lead.y + dy) / snap) * snap - lead.y;
    for (const [j, f] of drag.from) {
      const p = S.panels.get(j);
      if (!p) continue;
      p.x = round2(f.x + mx);
      p.y = round2(f.y + my);
      hold(j, p);
      throttled('move' + j, () => send({ c: 'panel', uid: p.uid, x: p.x, y: p.y }), 120);
    }
    showPlace();
  });
  const endDrag = () => {
    if (wireDrag) { endWire(); return; }
    if (S.view === '3d') { up3(); return; }
    if (drag && drag.kind === 'tile' && drag.moved) {
      for (const j of drag.from.keys()) {
        const p = S.panels.get(j);
        if (p) send({ c: 'panel', uid: p.uid, x: p.x, y: p.y });
      }
      letGo([...drag.from.keys()]);
    } else if (drag && drag.kind === 'tile' && S.picked.size > 1) {
      pickBox([drag.i], false);           // a plain click on one of several chosen: that one alone
    } else if (drag && drag.kind === 'box') {
      if (drag.moved && box) pickBox(inBox(box), drag.add);
      else if (!drag.add) pickNone();
    } else if (drag && drag.kind === 'pan' && !drag.moved && drag.touch) {
      pickNone();
    }
    box = null;
    drag = null;
  };
  canvas.addEventListener('pointerup', endDrag);
  canvas.addEventListener('pointercancel', endDrag);
  canvas.addEventListener('wheel', (ev) => {
    ev.preventDefault();
    if (S.view === '3d') { wheel3(ev); return; }
    const r = canvas.getBoundingClientRect();
    const sx = ev.clientX - r.left, sy = ev.clientY - r.top;
    const i = tileAt(sx, sy);
    if (i != null && i === S.sel && S.mode === 'demo') {
      const knob = ev.ctrlKey || ev.metaKey ? 3 : ev.altKey ? 2 : ev.shiftKey ? 1 : 0;
      virtualHand(knob, (ev.deltaY || ev.deltaX) < 0 ? 1 : -1);
      return;
    }
    const [wx, wy] = toWorld(sx, sy);
    S.cam.z = clamp(S.cam.z * Math.exp(-ev.deltaY * 0.0015), 18, 320);
    S.cam.x = wx - (sx - W / 2) / S.cam.z;
    S.cam.y = wy - (sy - H / 2) / S.cam.z;
  }, { passive: false });

  // Places for every panel at once, in the order of their numbers.
  function arrange(kind) {
    const ps = [...S.panels.values()].sort((a, b) => a.id - b.id);
    const n = ps.length;
    if (!n) return;
    ps.forEach((p, i) => {
      let x = i, y = 0;
      if (kind === 'grid') {
        const cols = Math.ceil(Math.sqrt(n * 1.6));
        x = i % cols;
        y = Math.floor(i / cols) * 0.75;
      } else if (kind === 'ring') {
        const r = Math.max(1, n / (2 * Math.PI) * 1.05);
        const a = (i / n) * Math.PI * 2 - Math.PI / 2;
        x = Math.round(Math.cos(a) * r * 100) / 100;
        y = Math.round(Math.sin(a) * r * 100) / 100;
      } else if (kind === 'stagger') {
        x = i * 0.5;
        y = (i % 2) * 0.75;
      }
      p.x = x;
      p.y = y;
      p.z = 0;                 // every arrangement here is flat on the wall
      send({ c: 'panel', uid: p.uid, x, y, z: 0 });
    });
    S.cam.fitted = false;
    V.fitted = false;
  }

  // ── A panel's own web server, through the hub ──────────────────────────
  // The console is on a cable to the hub and the panels are on the hub's
  // Wi-Fi, so HTTP for a panel goes as lines: the hub passes it on and sends
  // the answer back (core/pfe_relay.h). Up to four panels at once.
  const RELAY_SLOTS = 4;
  const relay = { next: 1, open: new Map(), busy: 0, waiting: [], again: 0, flying: 0, turns: [] };

  // Pieces of request bodies on their way to the hub at once, all requests
  // together. The hub reads its USB port into a buffer of 4096 bytes, and
  // while it is passing one piece on to a panel it reads nothing: with four
  // uploads each allowed a 1.5 KB line in flight, the buffer overflowed and
  // the hub got lines with holes in them ("not the bytes that were
  // promised"). Measured 2026-11-21 on five panels and a set of twenty
  // modules: 13 of 266 requests lost and four of five panels failed without
  // this; none of 225 with it, and no slower - the hub passes one piece on
  // at a time whatever is waiting.
  const BODY_LINES = 2;
  function bodyLineTurn() {
    return new Promise((go) => {
      if (relay.flying < BODY_LINES) { relay.flying++; go(); } else relay.turns.push(go);
    });
  }
  function bodyLineDone() {
    const next = relay.turns.shift();
    if (next) next(); else relay.flying = Math.max(0, relay.flying - 1);
  }

  function b64(bytes) {
    let s = '';
    for (let i = 0; i < bytes.length; i += 0x6000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x6000));
    return btoa(s);
  }
  function unb64(text) { return Uint8Array.from(atob(text), (c) => c.charCodeAt(0)); }
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

  function relayLine(obj) {       // not through send(): a file in the log is no use to anyone
    if (S.transport) S.transport.send(JSON.stringify(obj));
  }

  // -> { status, bytes, text, json }, or throws with what went wrong.
  //
  // A request that was lost on the way (not answered, cut off, damaged, the
  // hub's slots full) is asked again, twice, a little later each time.
  // Putting a set of twenty modules on a panel is forty-five requests, and
  // with one in twenty lost four panels of five failed and had to be done
  // over by hand (2026-11-21; what lost them is at BODY_LINES below).
  // GET, PUT and DELETE say the same thing however often they are said; a
  // POST is asked again only where the caller says it may be (opt.retry).
  const RELAY_AGAIN = /does not answer|closed the connection|busy with another request|reached the hub damaged/;
  async function panelHttp(uid, method, path, opt = {}) {
    const again = opt.retry != null ? opt.retry : (method === 'POST' ? 0 : 2);
    for (let n = 0; ; n++) {
      try {
        return await panelHttpOnce(uid, method, path, opt);
      } catch (e) {
        if (n >= again || !RELAY_AGAIN.test(e.message) || !S.connected) throw e;
        relay.again++;
        await sleep(600 * (n + 1));
      }
    }
  }
  async function panelHttpOnce(uid, method, path, opt = {}) {
    if (!S.transport || !S.connected) throw new Error('Not connected to a hub');
    if (relay.busy >= RELAY_SLOTS) await new Promise((go) => relay.waiting.push(go));
    relay.busy++;
    const id = relay.next++;
    const body = opt.body || new Uint8Array(0);
    const piece = S.transport.name === 'wifi' ? 720 : 1080;     // a WebSocket line has to fit one packet
    const tmo = opt.timeout || 15000;
    try {
      return await new Promise((resolve, reject) => {
        const r = { status: 0, parts: [], sent: 0, timer: 0, holding: false, asking: false, over: false };
        const letGo = () => { if (r.holding) { r.holding = false; bodyLineDone(); } };
        const fail = (why) => { r.over = true; letGo(); clearTimeout(r.timer); relay.open.delete(id); reject(new Error(why)); };
        const arm = () => { clearTimeout(r.timer); r.timer = setTimeout(() => fail('The hub does not answer'), tmo + 4000); };
        // The next piece of the body, when it is this request's turn to have one on its way (bodyLineTurn).
        const more = () => {
          if (r.sent >= body.length || r.asking || r.over) return;
          r.asking = true;
          bodyLineTurn().then(() => {
            r.asking = false;
            if (r.over) { bodyLineDone(); return; }
            r.holding = true;
            const part = body.subarray(r.sent, r.sent + piece);
            r.sent += part.length;
            relayLine({ c: 'hb', id, d: b64(part) });
            arm();
          });
        };
        r.on = (m) => {
          arm();
          if (m.t === 'hk') { letGo(); if (opt.progress) opt.progress(m.n, body.length); if (m.n === r.sent) more(); }
          else if (m.t === 'http') r.status = m.s;
          else if (m.t === 'hb') r.parts.push(unb64(m.d));
          else if (m.t === 'he') {
            if (m.err) { fail(RELAY_ERRORS[m.err] || m.err); return; }
            r.over = true;
            letGo();
            clearTimeout(r.timer);
            relay.open.delete(id);
            const bytes = new Uint8Array(r.parts.reduce((n, p) => n + p.length, 0));
            let at = 0;
            for (const p of r.parts) { bytes.set(p, at); at += p.length; }
            const text = new TextDecoder().decode(bytes);
            let json = null;
            try { json = JSON.parse(text); } catch (_) { /* not JSON */ }
            resolve({ status: r.status, bytes, text, json });
          }
        };
        relay.open.set(id, r);
        arm();
        // A panel by its uid; or, for a device on the hub's Wi-Fi that is no panel of the ensemble yet, { ip }.
        relayLine({ c: 'http', id, ...(uid && typeof uid === 'object' ? { ip: uid.ip } : { to: uid }), m: method, path, h: opt.headers || {}, len: body.length, tmo });
        more();
      });
    } finally {
      relay.busy--;
      const next = relay.waiting.shift();
      if (next) next();
    }
  }
  const RELAY_ERRORS = {
    'timeout': 'The panel does not answer',
    'the panel does not answer': 'The panel does not answer',
    'no such panel, or it is not connected': 'The panel is not connected',
    'the panel closed the connection': 'The panel closed the connection',
    'the panel closed the connection without answering': 'The panel closed the connection without answering',
    'busy with another request': 'The hub is busy with another request',
    'd: not the bytes that were promised': 'A piece of the file reached the hub damaged',
  };
  function relayDropped() {       // the link to the hub went: nothing under way will be answered
    for (const [, r] of relay.open) r.on({ t: 'he', err: 'The connection to the hub dropped' });
  }

  // The demo has no panels with web servers. These stand in for them: a
  // status, a pattern list, and a store that takes what is sent.
  const fakePanels = new Map();
  function fakePanel(uid) {
    if (!fakePanels.has(uid)) {
      const modules = ['ember_tide', 'paper_lanterns', 'slow_orbit'];
      fakePanels.set(uid, { modules, staged: new Map(), names: new Map(modules.map((s) => [s, s.replace(/_/g, ' ')])), born: performance.now(),
                            shows: new Map([['demo_loop', { title: 'Demo loop', length: 12, cues: 6 }]]), playing: '', loop: false, startedAt: 0 });
    }
    return fakePanels.get(uid);
  }
  function fakeHttp(m, body) {
    const p = fakePanel(m.to);
    const json = (status, obj) => ({ status, text: JSON.stringify(obj) });
    const path = m.path.split('?')[0];
    if (m.m === 'POST' && path === '/update') return json(200, { ok: true });
    if (m.m === 'GET' && path === '/api/status')
      return json(200, { uptime: Math.round((performance.now() - p.born) / 1000), heapLargest: 150000, variant: 'ensemble' });
    if (m.m === 'GET' && path === '/api/patterns') {
      const presets = ['Origin', 'Tide', 'Weave'];
      return json(200, { mounted: true, patterns: [
        ...presets.map((name, index) => ({ index, name })),
        ...p.modules.map((slug, n) => ({ index: presets.length + n, name: p.names.get(slug) || slug, module: slug })),
      ] });
    }
    if (m.m === 'POST' && path === '/api/patterns/delete') {
      const which = new TextDecoder().decode(body).split('\n').map((s) => s.trim()).filter(Boolean);
      p.modules = which.includes('*') ? [] : p.modules.filter((s) => !which.includes(s));
      return json(200, { ok: true });
    }
    if (m.m === 'PUT' && path === '/api/patterns') {
      const name = (m.h || {})['X-PF-Name'] || '';
      if (!/^[a-z0-9_]+\.(pfm|json)$|^catalog\.txt$/.test(name)) return json(400, { error: 'bad name' });
      p.staged.set(name, body);
      if (name.endsWith('.json')) {
        try { p.names.set(name.slice(0, -5), JSON.parse(new TextDecoder().decode(body)).name); } catch (_) { /* no name */ }
      }
      if (name === 'catalog.txt') {
        p.modules = new TextDecoder().decode(body).split('\n').map((s) => s.trim()).filter((s) => s && p.staged.has(s + '.pfm'));
        p.staged.clear();
      }
      return json(200, { ok: true });
    }
    if (m.m === 'POST' && path === '/api/wifi/reboot') { p.born = performance.now(); return json(200, { ok: true }); }
    // The player. What plays is only a pattern that changes every two seconds (fakeShowsTick).
    const showStatus = () => {
      const cur = p.shows.get(p.playing);
      return { playing: !!cur, paused: false, loop: p.loop, t: cur ? Math.floor((performance.now() - p.startedAt) / 1000) % Math.max(1, cur.length) : 0,
               length: cur ? cur.length : 0, cues: cur ? cur.cues : 0, slug: p.playing, title: cur ? cur.title : '', missing: [] };
    };
    const setPlaying = (slug) => {
      p.playing = slug;
      p.startedAt = performance.now();
      const sim = S.transport && S.transport.sim ? S.transport.sim(m.to) : -1;
      if (sim >= 0 && S.transport.x.pfe_solo) S.transport.x.pfe_solo(sim, slug ? 1 : 0);
    };
    if (m.m === 'GET' && path === '/api/shows/status') return json(200, showStatus());
    if (m.m === 'GET' && path === '/api/shows')
      return json(200, { ...showStatus(), shows: [...p.shows].map(([slug, s]) => ({ slug, title: s.title, length: s.length, cues: s.cues, loop: false })) });
    if (m.m === 'PUT' && path === '/api/shows') {
      const name = (m.h || {})['X-PF-Name'] || '';
      const info = pfsInfo(body);
      if (!/\.pfs$/i.test(name) || !info) return json(400, { ok: false, error: 'only .pfs tables accepted' });
      p.shows.set(showSlug(name), { title: info.title || showSlug(name), length: Math.round(info.seconds), cues: info.cues });
      return json(200, { ok: true });
    }
    if (m.m === 'DELETE' && path === '/api/shows') {
      const slug = new URLSearchParams(m.path.split('?')[1] || '').get('slug') || '';
      if (p.playing === slug) setPlaying('');
      return p.shows.delete(slug) ? json(200, { ok: true }) : json(404, { ok: false, error: 'no such sequence' });
    }
    if (m.m === 'POST' && path === '/api/shows/control') {
      const args = new URLSearchParams(new TextDecoder().decode(body));
      const op = args.get('op');
      if (op === 'stop') setPlaying('');
      else if (op === 'loop') p.loop = args.get('loop') === '1';
      else if (op === 'play') {
        if (!p.shows.has(args.get('slug'))) return json(400, { ok: false, error: 'no such sequence' });
        if (args.has('loop')) p.loop = args.get('loop') === '1';
        setPlaying(args.get('slug'));
      } else return json(400, { ok: false, error: 'op must be play, stop or loop' });
      return json(200, { ok: true, ...showStatus() });
    }
    return json(404, { error: 'not found' });
  }
  // The demo's shows: a pattern of their own every two seconds, and an end.
  function fakeShowsTick(demo) {
    const now = performance.now();
    for (const [uid, p] of fakePanels) {
      const cur = p.shows.get(p.playing);
      if (!cur) continue;
      const sim = demo.sim(uid);
      if (sim < 0) continue;
      const at = (now - p.startedAt) / 1000;
      if (!p.loop && at >= cur.length) {
        p.playing = '';
        demo.x.pfe_solo(sim, 0);
        continue;
      }
      const step = Math.floor(at / 2);
      if (step !== p.step) {
        p.step = step;
        demo.x.pfe_show_pattern(sim, 1 + (step % 5));
      }
    }
  }
  const fakeOpen = new Map();     // id -> { m, parts, got }
  // The stand-in's lines, in order and after the caller is done - and not by
  // timers, which a browser slows to one a second in a tab nobody is looking at.
  const fakeLines = [];
  const fakeChannel = new MessageChannel();
  fakeChannel.port1.onmessage = () => { const text = fakeLines.shift(); if (text) onLine(text); };
  // A line the demo's hub would have passed to a panel; true if it was one.
  function fakeRelay(text) {
    if (!text.startsWith('{"c":"h')) return false;
    let m;
    try { m = JSON.parse(text); } catch (_) { return false; }
    if (m.c !== 'http' && m.c !== 'hb') return false;
    const say = (obj) => { fakeLines.push(JSON.stringify(obj)); fakeChannel.port2.postMessage(0); };
    const answer = (req) => {
      const body = new Uint8Array(req.got);
      let at = 0;
      for (const p of req.parts) { body.set(p, at); at += p.length; }
      const known = [...S.panels.values()].some((p) => p.uid === req.m.to && p.on);
      if (!known) { say({ t: 'he', id: req.m.id, err: 'no such panel, or it is not connected' }); return; }
      const res = fakeHttp(req.m, body);
      const bytes = new TextEncoder().encode(res.text);
      say({ t: 'http', id: req.m.id, s: res.status, len: bytes.length });
      for (let i = 0; i < bytes.length; i += 360) say({ t: 'hb', id: req.m.id, d: b64(bytes.subarray(i, i + 360)) });
      say({ t: 'he', id: req.m.id, n: bytes.length });
    };
    if (m.c === 'http') {
      const req = { m, parts: [], got: 0 };
      if (m.len > 0) fakeOpen.set(m.id, req); else answer(req);
    } else {
      const req = fakeOpen.get(m.id);
      if (!req) return true;
      const part = unb64(m.d);
      req.parts.push(part);
      req.got += part.length;
      say({ t: 'hk', id: m.id, n: req.got });
      if (req.got >= req.m.len) { fakeOpen.delete(m.id); answer(req); }
    }
    return true;
  }

  // ── Patterns: a set from the community, onto every panel ───────────────
  // The set is a list of modules in the order the panels are to number them.
  // Each comes from the Patternflow community as two files, <slug>.pfm and
  // <slug>.json, through the routes it opens to any page:
  //   /api/community/patterns/<id>/zip   and   /api/community/decks/<id>/zip
  // with ?status=1, ?list=1 and ?file=<name>.
  const COMMUNITY = 'https://community.patternflow.work';
  const COMMUNITY_PAGE = 'https://patternflow.work/community';
  const SET_KEY = 'pfe-pattern-set';
  const P = {
    set: [],                 // { slug, name, author, size, from:{kind,id}|null, pfm, json, state, error }
    shows: [],               // { slug, title, seconds, size, from, data, state, error }: .pfs tables for the panels' players
    adding: 0,
    panels: new Map(),       // uid -> { checked, touched, have, state, text, done, total, tone }
    running: false,
    presets: null,           // the patterns built into the panels' firmware, in front of the set
  };

  function saveSet() {
    try {
      localStorage.setItem(SET_KEY, JSON.stringify({
        patterns: P.set.filter((e) => e.from).map((e) => ({ slug: e.slug, name: e.name, author: e.author, size: e.size, from: e.from })),
        shows: P.shows.filter((e) => e.from).map((e) => ({ slug: e.slug, title: e.title, seconds: e.seconds, size: e.size, from: e.from })),
      }));
    } catch (_) { /* no storage here */ }
  }
  function loadSet() {
    try {
      const saved = JSON.parse(localStorage.getItem(SET_KEY) || '[]');
      const patterns = Array.isArray(saved) ? saved : saved.patterns || [];       // a list alone: from before there were shows
      P.set = patterns.filter((e) => e && e.slug && e.from).map((e) => ({ ...e, pfm: null, json: null, state: 'idle', error: '' }));
      P.shows = (saved.shows || []).filter((e) => e && e.slug && e.from).map((e) => ({ ...e, data: null, state: 'idle', error: '' }));
    } catch (_) { P.set = []; P.shows = []; }
  }

  function communityUrl(from, query) {
    return `${COMMUNITY}/api/community/${from.kind === 'd' ? 'decks' : 'patterns'}/${encodeURIComponent(from.id)}/zip?${query}`;
  }
  async function communityJson(from, query) {
    let res;
    try { res = await fetch(communityUrl(from, query), { cache: 'no-store' }); }
    catch (_) { throw new Error('Cannot reach the community (check the internet connection)'); }
    let data = null;
    try { data = await res.json(); } catch (_) { /* no body */ }
    return { status: res.status, data: data || {} };
  }
  async function communityFile(from, name) {
    let res;
    try { res = await fetch(communityUrl(from, 'file=' + encodeURIComponent(name))); }
    catch (_) { throw new Error('Cannot reach the community (check the internet connection)'); }
    if (!res.ok) throw new Error(res.status === 404 ? 'The community does not have this file' : `The community answered ${res.status}`);
    return new Uint8Array(await res.arrayBuffer());
  }
  // The files of a pattern or a deck, once the community has them built.
  async function communityFiles(from) {
    for (let tries = 0; tries < 60; tries++) {
      const { status, data } = await communityJson(from, 'status=1');
      if (status === 404) throw new Error(from.kind === 'd' ? 'There is no such deck' : 'There is no such pattern');
      if (data.state === 'ready') break;
      if (data.state !== 'building' && status !== 202) throw new Error(data.error || 'This pattern has no file for a panel');
      setPatStatus('The community is building the panel file …');
      await sleep(2000);
    }
    const { status, data } = await communityJson(from, 'list=1');
    if (status !== 200 || !Array.isArray(data.files)) throw new Error(data.error || 'Could not get the list of files');
    return data.files;
  }

  async function mapLimit(items, limit, fn) {
    let next = 0;
    const run = async () => { while (next < items.length) { const i = next++; await fn(items[i], i); } };
    await Promise.all(Array.from({ length: Math.min(limit, items.length) }, run));
  }

  // Bring one entry's two files here (again, after a reload).
  async function fetchEntry(e) {
    if (e.pfm || !e.from) return;
    e.state = 'loading';
    e.error = '';
    renderSet();
    try {
      const [pfm, json] = await Promise.all([communityFile(e.from, e.slug + '.pfm'),
                                             communityFile(e.from, e.slug + '.json').catch(() => null)]);
      e.pfm = pfm;
      e.json = json;
      e.size = pfm.length;
      if (json) {
        try {
          const info = JSON.parse(new TextDecoder().decode(json));
          e.name = info.name || e.name;
          e.author = info.author || e.author;
        } catch (_) { /* a sidecar that is not JSON: the slug will do */ }
      }
      e.state = 'ready';
    } catch (err) {
      e.state = 'error';
      e.error = err.message;
    }
    renderSet();
  }

  function putEntry(e) {
    const at = P.set.findIndex((x) => x.slug === e.slug);
    if (at >= 0) P.set[at] = e; else P.set.push(e);
  }
  function putShow(e) {
    const at = P.shows.findIndex((x) => x.slug === e.slug);
    if (at >= 0) P.shows[at] = e; else P.shows.push(e);
  }
  // A show's table, here (again, after a reload).
  async function fetchShow(e) {
    if (e.data || !e.from) return;
    e.state = 'loading';
    e.error = '';
    renderSet();
    try {
      const data = await communityFile(e.from, e.file || e.slug + '.pfs');
      const info = pfsInfo(data);
      if (!info) throw new Error('Not a show file (.pfs)');
      e.data = data;
      e.size = data.length;
      e.title = info.title || e.title || e.slug;
      e.seconds = info.seconds;
      e.state = 'ready';
      libAdd(data, { title: e.title, slug: e.slug });
    } catch (err) {
      e.state = 'error';
      e.error = err.message;
    }
    renderSet();
  }

  // Every link to a deck or a pattern in a piece of text.
  function linksIn(text) {
    const out = [];
    const seen = new Set();
    const re = /\/community\/(p|d|patterns|decks)\/([A-Za-z0-9_-]{6,40})/g;
    let m;
    while ((m = re.exec(text))) {
      const from = { kind: m[1][0] === 'd' ? 'd' : 'p', id: m[2] };
      if (!seen.has(from.kind + from.id)) { seen.add(from.kind + from.id); out.push(from); }
    }
    if (!out.length) {
      for (const word of text.split(/[\s,]+/)) {
        if (/^[0-9a-f]{16}$/i.test(word) && !seen.has(word)) { seen.add(word); out.push({ kind: '?', id: word.toLowerCase() }); }
      }
    }
    return out;
  }

  async function addFrom(from) {
    let files;
    if (from.kind === '?') {            // a bare id: a pattern's, or else a deck's
      try { from = { kind: 'p', id: from.id }; files = await communityFiles(from); }
      catch (_) { from = { kind: 'd', id: from.id }; files = await communityFiles(from); }
    } else {
      files = await communityFiles(from);
    }
    let slugs = files.filter((f) => f.endsWith('.pfm')).map((f) => f.slice(0, -4));
    if (files.includes('catalog.txt')) {          // a deck: in the deck's order
      const order = new TextDecoder().decode(await communityFile(from, 'catalog.txt')).split('\n').map((s) => s.trim()).filter(Boolean);
      slugs = [...order.filter((s) => slugs.includes(s)), ...slugs.filter((s) => !order.includes(s))];
    }
    // A deck with a performance attached carries its show table too.
    const tables = files.filter((f) => /\.pfs$/i.test(f)).map((f) =>
      ({ slug: showSlug(f), file: f, title: showSlug(f), seconds: 0, size: 0, from, data: null, state: 'idle', error: '' }));
    if (!slugs.length && !tables.length) throw new Error('There are no files for a panel there');
    const fresh = slugs.map((slug) => ({ slug, name: slug, author: '', size: 0, from, pfm: null, json: null, state: 'idle', error: '' }));
    for (const e of fresh) putEntry(e);
    for (const e of tables) putShow(e);
    renderSet();
    await Promise.all([mapLimit(fresh, 4, fetchEntry), mapLimit(tables, 2, fetchShow)]);
    return fresh.length + tables.length;
  }

  async function addText(text) {
    const found = linksIn(text);
    if (!found.length) { toast('That is not a community pattern or deck link'); return; }
    P.adding++;
    let added = 0;
    const problems = [];
    setPatStatus('Fetching from the community …');
    try {
      for (const from of found) {
        try { added += await addFrom(from); } catch (e) { problems.push(e.message); }
      }
    } finally {
      P.adding--;
    }
    saveSet();
    renderSet();
    compareAll();
    setPatStatus(problems.length ? problems[0] : `Added ${added} to the set`, problems.length ? 'bad' : '');
  }

  // Files from this computer: <slug>.pfm, with its <slug>.json if it came along.
  async function addFiles(fileList) {
    const files = [...fileList];
    const mods = files.filter((f) => /\.pfm$/i.test(f.name));
    const tables = files.filter((f) => /\.pfs$/i.test(f.name));
    if (!mods.length && !tables.length) { toast('There are no .pfm or .pfs files there'); return; }
    for (const f of tables) {
      const data = new Uint8Array(await f.arrayBuffer());
      const info = pfsInfo(data);
      if (!info) { toast(`${f.name}: not a show file (.pfs)`); continue; }
      putShow({ slug: showSlug(f.name), title: info.title || showSlug(f.name), seconds: info.seconds, size: data.length, from: null, data, state: 'ready', error: '' });
      libAdd(data, { title: info.title || showSlug(f.name), slug: showSlug(f.name) });
    }
    for (const f of mods) {
      const slug = f.name.slice(0, -4).toLowerCase().replace(/[^a-z0-9_]/g, '_').slice(0, 40);
      const side = files.find((x) => x.name.toLowerCase() === f.name.slice(0, -4).toLowerCase() + '.json');
      const e = { slug, name: slug, author: '', size: f.size, from: null, pfm: new Uint8Array(await f.arrayBuffer()),
                  json: side ? new Uint8Array(await side.arrayBuffer()) : null, state: 'ready', error: '' };
      if (e.json) {
        try { const info = JSON.parse(new TextDecoder().decode(e.json)); e.name = info.name || slug; e.author = info.author || ''; } catch (_) { /* slug */ }
      }
      putEntry(e);
    }
    renderSet();
    compareAll();
  }

  function setPatStatus(text, tone) {
    const el = $('pat-status');
    el.textContent = text || '';
    el.style.color = tone === 'bad' ? 'var(--bad)' : '';
  }

  const kb = (n) => (n >= 1024 * 1024 ? (n / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB');

  let dragFrom = -1;
  function renderSet() {
    const list = $('pat-list');
    list.textContent = '';
    P.set.forEach((e, i) => {
      const li = document.createElement('li');
      li.draggable = !P.running;
      li.dataset.i = i;
      if (e.state === 'error') li.className = 'bad';
      const n = document.createElement('span');
      n.className = 'n num';
      n.textContent = P.presets != null ? P.presets + i : i + 1;
      const name = document.createElement('span');
      name.className = 'name';
      name.textContent = e.name;
      const by = document.createElement('span');
      by.className = 'by';
      by.textContent = e.state === 'error' ? e.error : e.state === 'loading' ? 'fetching …' : (e.author || (e.from ? '' : 'a file from this computer'));
      const size = document.createElement('span');
      size.className = 'size num';
      size.textContent = e.size ? kb(e.size) : '';
      const play = document.createElement('button');
      play.type = 'button';
      play.className = 'btn small quiet';
      play.textContent = '▶';
      play.title = 'Play this pattern on every panel';
      play.hidden = P.presets == null || !isEverywhere(e.slug);
      play.addEventListener('click', () => send({ c: 'set', to: 'all', pat: P.presets + P.set.indexOf(e) }));
      const drop = document.createElement('button');
      drop.type = 'button';
      drop.className = 'btn small quiet';
      drop.textContent = '✕';
      drop.title = 'Remove from the set';
      drop.disabled = P.running;
      drop.addEventListener('click', () => { P.set.splice(P.set.indexOf(e), 1); saveSet(); renderSet(); compareAll(); });
      li.append(n, name, by, size, play, drop);
      list.append(li);
    });
    const showList = $('pat-shows');
    showList.textContent = '';
    for (const e of P.shows) {
      const li = document.createElement('li');
      if (e.state === 'error') li.className = 'bad';
      const n = document.createElement('span');
      n.className = 'n';
      n.textContent = 'Show';
      const name = document.createElement('span');
      name.className = 'name';
      name.textContent = e.title || e.slug;
      const by = document.createElement('span');
      by.className = 'by';
      by.textContent = e.state === 'error' ? e.error : e.state === 'loading' ? 'fetching …' : (e.seconds ? `${Math.round(e.seconds)} s` : '');
      const size = document.createElement('span');
      size.className = 'size num';
      size.textContent = e.size ? kb(e.size) : '';
      const drop = document.createElement('button');
      drop.type = 'button';
      drop.className = 'btn small quiet';
      drop.textContent = '✕';
      drop.title = 'Remove from the set';
      drop.disabled = P.running;
      drop.addEventListener('click', () => { P.shows.splice(P.shows.indexOf(e), 1); saveSet(); renderSet(); });
      li.append(n, name, by, size, document.createElement('span'), drop);
      showList.append(li);
    }
    $('pat-shows-head').hidden = !P.shows.length;
    $('pat-empty').hidden = P.set.length > 0 || P.shows.length > 0;
    const total = P.set.reduce((n, e) => n + (e.size || 0), 0);
    $('pat-total').textContent = P.set.length ? `${nOf(P.set.length, 'pattern')} · ${kb(total)}` : '';
    $('pat-clear').hidden = !P.set.length && !P.shows.length;
    renderGo();
  }

  // ── The panels' side ───────────────────────────────────────────────────
  function patPanels() {
    return [...S.panels.values()].filter((p) => p.on).sort((a, b) => a.id - b.id);
  }
  function panelRow(uid) {
    if (!P.panels.has(uid)) P.panels.set(uid, { checked: true, touched: false, have: null, state: '', text: '', done: 0, total: 0, tone: '' });
    return P.panels.get(uid);
  }
  const setOrder = () => P.set.map((e) => e.slug);
  const sameList = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
  function isEverywhere(slug) {
    const rows = patPanels().map((p) => panelRow(p.uid));
    return rows.length > 0 && rows.every((r) => r.have && sameList(r.have, setOrder())) && setOrder().includes(slug);
  }

  async function panelList(uid) {
    const res = await panelHttp(uid, 'GET', '/api/patterns', { timeout: 8000 });
    if (res.status !== 200 || !res.json || !Array.isArray(res.json.patterns)) throw new Error('Could not get the pattern list');
    const entries = [...res.json.patterns].sort((a, b) => (a.index || 0) - (b.index || 0));
    return { mounted: res.json.mounted !== false, presets: entries.filter((p) => !p.module).map((p) => p.name || ''),
             modules: entries.filter((p) => p.module).map((p) => p.module) };
  }

  // What each panel holds now, next to the set.
  let comparing = 0;
  async function compareAll() {
    if (P.running || S.page !== 'patterns') return;
    const run = ++comparing;
    const panels = patPanels();
    await mapLimit(panels, RELAY_SLOTS, async (p) => {
      const row = panelRow(p.uid);
      try {
        const list = await panelList(p.uid);
        if (run !== comparing || P.running) return;
        row.have = list.modules;
        P.presets = list.presets.length;
        const same = P.set.length > 0 && sameList(list.modules, setOrder());
        row.text = same ? 'same as the set' : nOf(list.modules.length, 'module');
        row.tone = same ? 'ok' : '';
        if (!row.touched) row.checked = !same;
      } catch (e) {
        if (run !== comparing || P.running) return;
        row.have = null;
        row.text = e.message;
        row.tone = 'bad';
      }
      renderGo();
    });
    if (run === comparing) renderSet();
  }

  function renderGo() {
    const panels = patPanels();
    const ul = $('pat-panels');
    ul.textContent = '';
    for (const p of panels) {
      const row = panelRow(p.uid);
      const li = document.createElement('li');
      const label = document.createElement('label');
      const box = document.createElement('input');
      box.type = 'checkbox';
      box.checked = row.checked;
      box.disabled = P.running;
      box.addEventListener('change', () => { row.checked = box.checked; row.touched = true; renderGo(); });
      const id = document.createElement('b');
      id.className = 'num';
      id.textContent = p.id;
      const name = document.createElement('span');
      name.className = 'name';
      name.textContent = p.n || `Panel ${p.id}`;
      label.append(box, id, name);
      const state = document.createElement('span');
      state.className = 'state' + (row.tone ? ' ' + row.tone : '');
      state.textContent = row.text;
      li.append(label, state);
      if (row.total) {
        const bar = document.createElement('progress');
        bar.max = row.total;
        bar.value = row.done;
        li.append(bar);
      }
      ul.append(li);
    }
    $('pat-none').hidden = panels.length > 0;
    const chosen = panels.filter((p) => panelRow(p.uid).checked).length;
    const ready = (P.set.length > 0 || P.shows.length > 0) && !P.adding &&
                  P.set.every((e) => e.state !== 'loading') && P.shows.every((e) => e.state !== 'loading');
    const go = $('pat-install');
    go.textContent = P.running ? 'Installing …' : chosen ? `Install on ${nOf(chosen, 'panel')}` : 'Install';
    go.disabled = P.running || !chosen || !ready || !S.connected;
    $('pat-check-all').hidden = panels.length < 2;
    $('pat-check-all').disabled = P.running;
  }

  async function panelStatus(uid, timeout = 6000) {
    const res = await panelHttp(uid, 'GET', '/api/status', { timeout });
    if (res.status !== 200 || !res.json) throw new Error('Could not get the panel status');
    return res.json;
  }

  // A panel takes a module into one piece of its memory. One that has been
  // on for a while may have no piece that long left; a restart gives it back.
  async function makeRoom(p, row, biggest) {
    const before = await panelStatus(p.uid);
    if (!Number.isInteger(before.heapLargest) || before.heapLargest >= biggest + 2048) return;
    row.text = 'Restarting to free memory …';
    renderGo();
    await panelHttp(p.uid, 'POST', '/api/wifi/reboot', { headers: { 'Content-Type': 'application/x-www-form-urlencoded' } }).catch(() => null);
    await sleep(6000);
    const until = performance.now() + 75000;
    while (performance.now() < until) {
      await sleep(1500);
      const now = [...S.panels.values()].find((q) => q.uid === p.uid);
      if (!now || !now.on) continue;                  // the hub has not seen it back yet
      const st = await panelStatus(p.uid, 3000).catch(() => null);
      if (st && (st.uptime || 0) < (before.uptime || 1e9)) {
        if (Number.isInteger(st.heapLargest) && st.heapLargest < biggest) throw new Error('Still not enough memory after a restart');
        return;
      }
    }
    throw new Error('The panel did not come back after its restart');
  }

  async function putFile(uid, name, data, last) {
    for (let tries = 0; ; tries++) {
      const res = await panelHttp(uid, 'PUT', '/api/patterns', {
        headers: { 'Content-Type': 'application/octet-stream', 'X-PF-Name': name, 'X-PF-Last': last ? '1' : '0' },
        body: data, timeout: 30000 });
      if (res.status === 200) return;
      if (res.status === 503 && tries < 3) { await sleep(1500); continue; }      // "still loading; retry"
      throw new Error(`${name}: ${(res.json && res.json.error) || 'HTTP ' + res.status}`);
    }
  }

  // Make one panel's modules the set's: clear it, send everything, and wait
  // for the panel to have read it back in.
  async function installOn(p, row, order, files, tables) {
    row.total = (order.length ? files.length + 2 : 0) + tables.length;
    row.done = 0;
    row.tone = '';
    row.noPlayer = false;
    if (order.length) await installPatterns(p, row, order, files);
    // The shows, into the panel's player. An image without one keeps its patterns and says so.
    for (const t of tables) {
      row.text = 'Sending shows …';
      renderGo();
      const res = await panelHttp(p.uid, 'PUT', '/api/shows', {
        headers: { 'Content-Type': 'application/octet-stream', 'X-PF-Name': t.name }, body: t.data, timeout: 20000 });
      if (res.status === 404) { row.noPlayer = true; break; }
      if (res.status !== 200) throw new Error(`${t.name}: ${(res.json && res.json.error) || 'HTTP ' + res.status}`);
      row.done++;
    }
    shows.delete(p.uid);          // what the side column knows of this panel's shows is from before
    row.done = row.total;
  }

  async function installPatterns(p, row, order, files) {
    row.text = 'Checking …';
    renderGo();
    const have = await panelList(p.uid);
    if (!have.mounted) throw new Error('The pattern storage does not open (see the panel\'s own /patterns page)');
    await makeRoom(p, row, Math.max(...files.map((f) => f.data.length)));
    row.text = 'Clearing …';
    renderGo();
    for (let tries = 0; ; tries++) {
      const res = await panelHttp(p.uid, 'POST', '/api/patterns/delete', {       // clearing twice clears once
        headers: { 'Content-Type': 'text/plain' }, body: new TextEncoder().encode('*'), timeout: 30000, retry: 2 });
      if (res.status === 200) break;
      if (res.status === 503 && tries < 3) { await sleep(1500); continue; }
      throw new Error(`Could not clear it (HTTP ${res.status})`);
    }
    row.done = 1;
    for (let i = 0; i < files.length; i++) {
      row.text = `Sending ${i + 1}/${files.length}`;
      renderGo();
      await putFile(p.uid, files[i].name, files[i].data, i === files.length - 1);
      row.done = 2 + i;
    }
    row.text = 'The panel is loading them …';
    renderGo();
    // It reads them in one after another, and a panel busy with that answers
    // slowly or not at all. Forty seconds, and more for as long as its list
    // is still growing: a long set on a busy hub is not a failure.
    let until = performance.now() + 40000, seen = -1;
    for (;;) {
      await sleep(1200);
      const now = await panelList(p.uid).catch(() => null);
      if (now && sameList(now.modules, order)) { row.have = now.modules; P.presets = now.presets.length; break; }
      if (now && now.modules.length > seen) {
        seen = now.modules.length;
        until = Math.max(until, performance.now() + 25000);
        row.text = `The panel is loading them … ${seen}/${order.length}`;
        renderGo();
      }
      if (performance.now() > until) throw new Error(now ? `Sent, but the panel's list differs (${now.modules.length} of ${nOf(order.length, 'module')} loaded)` : 'No answer after sending');
    }
    row.done = files.length + 2;
  }

  async function installAll() {
    if (P.running) return;
    const panels = patPanels().filter((p) => panelRow(p.uid).checked);
    if (!panels.length || (!P.set.length && !P.shows.length)) return;
    P.running = true;
    comparing++;
    renderSet();
    setPatStatus('');
    try {
      // Everything here first: a set with a hole in it goes to no panel.
      await mapLimit(P.set.filter((e) => !e.pfm), 4, fetchEntry);
      const missing = P.set.filter((e) => !e.pfm);
      if (missing.length) throw new Error(`${missing[0].name}: ${missing[0].error || 'the file is missing'}`);
      await mapLimit(P.shows.filter((e) => !e.data), 2, fetchShow);
      const lost = P.shows.filter((e) => !e.data);
      if (lost.length) throw new Error(`${lost[0].title}: ${lost[0].error || 'the file is missing'}`);
      const tables = P.shows.map((e) => ({ name: e.slug + '.pfs', data: e.data }));
      const order = setOrder();
      // The largest first, while a panel's memory is at its least broken up;
      // the running order goes last and closes the batch.
      const files = [];
      for (const e of [...P.set].sort((a, b) => b.pfm.length - a.pfm.length)) {
        files.push({ name: e.slug + '.pfm', data: e.pfm });
        if (e.json) files.push({ name: e.slug + '.json', data: e.json });
      }
      if (order.length) files.push({ name: 'catalog.txt', data: new TextEncoder().encode(order.map((s) => s + '\n').join('')) });
      let failed = 0;
      const started = performance.now(), againBefore = relay.again;
      await mapLimit(panels, RELAY_SLOTS, async (p) => {
        const row = panelRow(p.uid);
        try {
          await installOn(p, row, order, files, tables);
          row.text = row.noPlayer ? 'Patterns only: this firmware has no show player' : 'Done';
          row.tone = 'ok';
          row.checked = false;
          row.touched = false;
        } catch (e) {
          failed++;
          row.text = e.message;
          row.tone = 'bad';
          log('err', `Panel ${p.id}: ${e.message}`);
        }
        row.total = 0;
        renderGo();
      });
      const secs = Math.round((performance.now() - started) / 1000);
      // The hub's list of names is from before: give it this one.
      if (!failed && order.length) sendNames();
      const what = [order.length ? nOf(order.length, 'pattern') : '', tables.length ? nOf(tables.length, 'show') : ''].filter(Boolean).join(' and ');
      setPatStatus(failed ? `${panels.length - failed} done, ${failed} failed` : `Put ${what} on ${nOf(panels.length, 'panel')} (${secs} s)`, failed ? 'bad' : '');
      const again = relay.again - againBefore;
      log(failed ? 'err' : '', `Install: ${panels.length - failed} done${failed ? `, ${failed} failed` : ''} (${secs} s${again ? `, ${nOf(again, 'request')} asked again` : ''})`);
    } catch (e) {
      setPatStatus(e.message, 'bad');
    } finally {
      P.running = false;
      renderSet();
    }
  }

  // The hub learns a pattern's name when a panel shows it. After a new set
  // the numbers mean other patterns: tell the hub the modules' names now.
  function sendNames() {
    if (P.presets == null) return;
    if (S.mode === 'demo') return;          // the demo's panels are the simulator's, with its own names
    send({ c: 'names', clear: true, from: P.presets });
    for (let i = 0; i < P.set.length; i += 6) {
      send({ c: 'names', at: P.presets + i, n: P.set.slice(i, i + 6).map((e) => String(e.name).slice(0, 24)) });
    }
  }

  function setPage(page) {
    if (S.page === 'nodes' && page !== 'nodes') ngWatch(false);
    S.page = page;
    setPressed(['page-stage', 'page-patterns', 'page-shows', 'page-nodes', 'page-setup'], 'page-' + page);
    $('stage-page').hidden = page !== 'stage';
    $('pat-page').hidden = page !== 'patterns';
    $('show-page').hidden = page !== 'shows';
    $('node-page').hidden = page !== 'nodes';
    $('setup-page').hidden = page !== 'setup';
    if (page === 'nodes') { ngOpen(); ngMini(); return; }
    if (page === 'setup') { suOpen(); return; }
    if (page === 'shows') {
      if (!L.sel && L.shows.length) L.sel = L.shows[0].id;
      L.touched = false;
      renderLib();
      if (performance.now() - L.knownAt > 15000 || !L.known.length) loadKnownPatterns();
      return;
    }
    if (page === 'patterns') {
      renderSet();
      mapLimit(P.set.filter((e) => !e.pfm && e.state !== 'loading'), 4, fetchEntry);
      mapLimit(P.shows.filter((e) => !e.data && e.state !== 'loading'), 2, fetchShow);
      compareAll();
    } else {
      resize();
      S.cam.fitted = false;
    }
  }


  // ── Setup: new panels brought in, and the panels' firmware ─────────────
  // A panel that knows no Wi-Fi sits on a hotspot of its own. The hub goes
  // onto that hotspot, gives the panel the hub's network and comes back
  // (hub/src/main.cpp, "Setup"); if the panel runs some other firmware, the
  // ensemble's image is then sent to it through the hub, as files for a
  // panel are. Nothing here needs a second computer or a script.
  const SU = { nets: [], rows: new Map(), busy: false, image: null, waitJoin: null, stations: [], looking: false, recalled: false,
               net: null, waitNet: null, lanExtra: [] };
  const suRow = (key, ticked = true) => {
    if (!SU.rows.has(key)) SU.rows.set(key, { checked: ticked, text: '', tone: '', done: 0, total: 0 });
    return SU.rows.get(key);
  };
  const suTail = (ssid) => ssid.slice(-4).toLowerCase();        // the four digits a hotspot's name ends in are the panel's address's
  const suPanel = (tail) => [...S.panels.values()].find((p) => p.on && p.uid.toLowerCase().endsWith(tail));

  // The image, as the browser keeps it between visits.
  function suDb() {
    return new Promise((res, rej) => {
      const r = indexedDB.open('pfe-setup', 1);
      r.onupgradeneeded = () => r.result.createObjectStore('files');
      r.onsuccess = () => res(r.result);
      r.onerror = () => rej(r.error);
    });
  }
  async function suKeep(image) {
    try { (await suDb()).transaction('files', 'readwrite').objectStore('files').put({ name: image.name, bytes: image.bytes }, 'firmware'); } catch (_) { /* then it is chosen again next time */ }
  }
  async function suRecall() {
    try {
      const db = await suDb();
      return await new Promise((res) => {
        const q = db.transaction('files').objectStore('files').get('firmware');
        q.onsuccess = () => res(q.result || null);
        q.onerror = () => res(null);
      });
    } catch (_) { return null; }
  }
  // Why a file may not go on a panel (tools/panels.py inspect_image has the same checks), or what it is.
  function suInspect(bytes, name) {
    if (bytes.length < 65536 || bytes[0] !== 0xE9 || bytes[0x20] !== 0x32 || bytes[0x21] !== 0x54 || bytes[0x22] !== 0xCD || bytes[0x23] !== 0xAB)
      throw new Error('Not an ESP32 app image. It has to be firmware.bin, not bootloader.bin or a .zip');
    if ((bytes[12] | (bytes[13] << 8)) !== 9) throw new Error('Not an image for the ESP32-S3');
    if (bytes.length > 3 * 1024 * 1024) throw new Error('Larger than a panel\'s app slot (3 MB)');
    const text = new TextDecoder('latin1').decode(bytes);
    if (!text.includes('/update/status')) throw new Error('This image has no /update: once on a panel it could not be replaced over Wi-Fi. Is it the hub\'s firmware?');
    const m = /ensemble v\d+\.\d+\.\d+/.exec(text);
    if (!m) throw new Error('Not the ensemble\'s panel image (it does not call itself "ensemble v…")');
    return { bytes, name, version: m[0], size: bytes.length };
  }
  async function suTake(file) {
    try {
      SU.image = suInspect(new Uint8Array(await file.arrayBuffer()), file.name);
      suKeep(SU.image);
      for (const p of patPanels()) suRow(p.uid).checked = p.var !== SU.image.version;
    } catch (e) {
      toast(e.message);
    }
    suRender();
  }

  function suSay(text, tone) {
    const el = $('su-status');
    el.textContent = text || '';
    el.style.color = tone === 'bad' ? 'var(--bad)' : tone === 'ok' ? 'var(--ok)' : '';
  }

  // The image onto one panel, through the hub. `target`: a panel's uid, or { ip } for one that is no panel of the ensemble yet.
  async function suFlash(target, row) {
    const bytes = SU.image.bytes;
    const boundary = '----pfe' + Math.random().toString(16).slice(2) + Date.now().toString(16);
    const enc = new TextEncoder();
    const head = enc.encode(`--${boundary}\r\nContent-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\nContent-Type: application/octet-stream\r\n\r\n`);
    const tail = enc.encode(`\r\n--${boundary}--\r\n`);
    const body = new Uint8Array(head.length + bytes.length + tail.length);
    body.set(head, 0);
    body.set(bytes, head.length);
    body.set(tail, head.length + bytes.length);
    row.total = body.length;
    row.done = 0;
    let pct = -1;
    const res = await panelHttp(target, 'POST', '/update?size=' + bytes.length, {
      headers: { 'Content-Type': `multipart/form-data; boundary=${boundary}` }, body, timeout: 90000, retry: 0,
      progress: (sent) => {
        row.done = sent;
        const now = Math.floor(sent * 100 / body.length);
        if (now !== pct) { pct = now; row.text = `Sending firmware ${now}%`; suRender(); }
      } });
    row.total = 0;
    if (res.status === 403) throw new Error('The panel refuses updates. On the panel: hold K2 for NETWORK, turn K4 to UPDATE, then try again');
    if (res.status !== 200) throw new Error(`The panel refused the image: ${(res.json && res.json.error) || 'HTTP ' + res.status}`);
  }
  // After an image, or after it was sent to the hub's Wi-Fi: the panel with these four digits, there and (if asked) on this version.
  async function suAwait(tail, version, secs) {
    const until = performance.now() + secs * 1000;
    while (performance.now() < until) {
      await sleep(1500);
      const p = suPanel(tail);
      if (p && (!version || p.var === version)) return p;
    }
    return null;
  }
  async function suStation(tail) {         // its address on the hub's network, if it is there
    send({ c: 'setup', op: 'stations' });
    await sleep(1200);
    const s = SU.stations.find((x) => x.mac.toLowerCase().endsWith(tail));
    return s ? s.ip : null;
  }

  function suJoin(ssid, row) {
    return new Promise((resolve) => {
      const timer = setTimeout(() => { SU.waitJoin = null; resolve({ step: 'failed', m: 'The hub does not answer' }); }, 45000);
      const words = { connecting: 'The hub is going onto its hotspot …', asking: 'Asking the panel what it is …', saving: 'Giving it the hub\'s Wi-Fi …' };
      SU.waitJoin = { ssid, resolve: (m) => { clearTimeout(timer); resolve(m); }, step: (s) => { row.text = words[s] || s; suRender(); } };
      send({ c: 'setup', op: 'join', ssid, pass: $('su-pass').value || 'patternflow' });
    });
  }

  // One new panel, from its hotspot to a panel of the ensemble.
  async function suBring(net) {
    const row = suRow(net.ssid), tail = suTail(net.ssid);
    row.tone = '';
    const said = await suJoin(net.ssid, row);
    if (said.step !== 'done') throw new Error(said.m || 'It did not work');
    if (S.mode === 'demo') return 'Done (the demo changes nothing)';
    row.text = 'Waiting for it on the hub\'s Wi-Fi …';
    suRender();
    const who = await suArrival(tail, new Set(), 75);
    if (!who) throw new Error('It was sent to the hub\'s Wi-Fi but has not shown up. Is the hub\'s Wi-Fi in its reach?');
    return suFirmware(who, row);
  }

  async function suBringAll() {
    if (SU.busy) return;
    const nets = SU.nets.filter((n) => suRow(n.ssid).checked);
    if (!nets.length) return;
    SU.busy = true;
    suSay('');
    let ok = 0;
    for (const net of nets) {
      const row = suRow(net.ssid);
      try {
        row.text = await suBring(net);
        row.tone = 'ok';
        row.checked = false;
        ok++;
      } catch (e) {
        row.text = e.message;
        row.tone = 'bad';
        log('err', `Setup, ${net.ssid}: ${e.message}`);
      }
      row.total = 0;
      suRender();
      await sleep(S.mode === 'demo' ? 200 : 2500);       // the ensemble finds the hub again before it leaves once more
    }
    SU.busy = false;
    suSay(`${ok} of ${nOf(nets.length, 'panel')} brought in` + (ok ? '. Place them on the Stage, and give them the set on Patterns' : ''), ok === nets.length ? 'ok' : 'bad');
    log(ok === nets.length ? '' : 'err', `Setup: ${ok} of ${nets.length} brought in`);
    suRender();
  }

  async function suUpdateAll() {
    if (SU.busy || !SU.image) return;
    const panels = patPanels().filter((p) => suRow(p.uid).checked);
    if (!panels.length) return;
    SU.busy = true;
    let ok = 0;
    for (const p of panels) {
      const row = suRow(p.uid);
      row.tone = '';
      try {
        await suFlash(p.uid, row);
        row.text = 'Restarting with the new firmware …';
        suRender();
        if (S.mode !== 'demo' && !(await suAwait(p.uid.slice(-4).toLowerCase(), SU.image.version, 120))) throw new Error('The image was sent, but the panel has not come back with it');
        row.text = S.mode === 'demo' ? 'Done (the demo changes nothing)' : 'Updated';
        row.tone = 'ok';
        row.checked = false;
        ok++;
      } catch (e) {
        row.text = e.message;
        row.tone = 'bad';
        log('err', `Setup, panel ${p.id}: ${e.message}`);
      }
      row.total = 0;
      suRender();
    }
    SU.busy = false;
    log(ok === panels.length ? '' : 'err', `Firmware: ${ok} of ${panels.length} updated`);
    suRender();
  }


  // ── Setup: panels on this computer's Wi-Fi ─────────────────────────────
  // A panel that knows another Wi-Fi it can reach (the room's) joins that
  // and starts no hotspot, so the hub never sees it. This computer does, if
  // it is on that Wi-Fi too: the page tells the panel the hub's network
  // itself. A panel's Wi-Fi settings answer no page but its own, so nothing
  // can be read back - the request is sent blind, and the rest (making the
  // hub's network the one the panel starts from, its firmware) is done
  // through the hub once the panel has arrived there.
  const SU_LAN_KEY = 'pfe-setup-lan';
  const suHost = (text) => {
    let h = String(text || '').trim().toLowerCase().replace(/^https?:\/\//, '').replace(/\/.*$/, '');
    if (/^[0-9a-f]{4}$/.test(h)) h = 'patternflow-' + h;
    if (/^patternflow-[0-9a-f]{4}$/.test(h)) h += '.local';
    return /^[a-z0-9.-]+$/.test(h) && h.length > 3 ? h : '';
  };
  const suHostTail = (host) => { const m = /^patternflow-([0-9a-f]{4})\.local$/.exec(host); return m ? m[1] : ''; };
  function suLanList() {
    const known = [...S.panels.values()].filter((p) => !p.on).sort((a, b) => a.id - b.id)
      .map((p) => ({ host: `patternflow-${p.uid.slice(-4).toLowerCase()}.local`, label: `Panel ${p.id} (not here now)` }));
    const extra = SU.lanExtra.filter((h) => !known.some((k) => k.host === h)).map((h) => ({ host: h, label: 'Added here' }));
    return [...known, ...extra];
  }
  // The hub's own network, from the hub (over the cable only).
  function suNet() {
    if (SU.net) return Promise.resolve(SU.net);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { SU.waitNet = null; reject(new Error('The hub did not say what its Wi-Fi is. This needs the USB cable to the hub')); }, 5000);
      SU.waitNet = (m) => {
        clearTimeout(timer);
        SU.waitNet = null;
        if (!m.ssid) { reject(new Error('The hub says its Wi-Fi ' + (m.m || 'to nobody') + '. Connect with the USB cable')); return; }
        SU.net = { ssid: m.ssid, pass: m.pass || '' };
        resolve(SU.net);
      };
      send({ c: 'setup', op: 'net' });
    });
  }
  // Blind requests to a panel on this computer's network: whether it is there, and a form for it.
  async function suReach(host) {
    try { await fetch(`http://${host}/api/status`, { mode: 'no-cors', cache: 'no-store', signal: AbortSignal.timeout(6000) }); return true; } catch (_) { return false; }
  }
  function suPost(host, path, fields) {
    return fetch(`http://${host}${path}`, { method: 'POST', mode: 'no-cors', cache: 'no-store', body: new URLSearchParams(fields), signal: AbortSignal.timeout(8000) });
  }
  // Who is on the hub's Wi-Fi now: the ensemble's panels and everything else, by the twelve digits of their addresses.
  async function suPresent() {
    send({ c: 'setup', op: 'stations' });
    await sleep(1200);
    return new Set([...[...S.panels.values()].filter((p) => p.on).map((p) => p.uid.toLowerCase()), ...SU.stations.map((x) => x.mac.toLowerCase())]);
  }
  // A panel that was sent to the hub's Wi-Fi: there as one of the ensemble's, or (after a while with no sign of that) as a device with an address.
  async function suArrival(tail, before, secs) {
    const until = performance.now() + secs * 1000;
    let station = null, since = 0;
    while (performance.now() < until) {
      await sleep(1500);
      const p = tail ? suPanel(tail) : [...S.panels.values()].find((q) => q.on && !before.has(q.uid.toLowerCase()));
      if (p) return { panel: p };
      send({ c: 'setup', op: 'stations' });
      await sleep(900);
      const s = SU.stations.find((x) => (tail ? x.mac.toLowerCase().endsWith(tail) : !before.has(x.mac.toLowerCase())));
      if (!s) continue;
      if (!station) { station = s; since = performance.now(); }
      if (performance.now() - since > 12000) return { ip: s.ip, mac: s.mac };       // here for a while and no panel of the ensemble: another firmware
    }
    return station ? { ip: station.ip, mac: station.mac } : null;
  }
  // On the hub's Wi-Fi: the ensemble's firmware if it runs another, and it is a panel of the ensemble.
  async function suFirmware(who, row) {
    const tail = (who.panel ? who.panel.uid : who.mac).slice(-4).toLowerCase();
    if (who.panel && (!SU.image || who.panel.var === SU.image.version)) return `Panel ${who.panel.id} · ${who.panel.var}`;
    if (!SU.image) throw new Error(`It is on the hub's Wi-Fi (${who.ip}) but runs another firmware. Choose firmware.bin on the right and try again`);
    row.text = who.panel ? `Panel ${who.panel.id}: updating ${who.panel.var} to ${SU.image.version}` : 'Sending the ensemble\'s firmware …';
    suRender();
    if (!who.panel) await sleep(1500);
    await suFlash(who.panel ? who.panel.uid : { ip: who.ip }, row);
    row.text = 'Restarting with the new firmware …';
    suRender();
    const p = await suAwait(tail, SU.image.version, 120);
    if (!p) throw new Error('The image was sent, but the panel has not come back with it');
    return `Panel ${p.id} · ${p.var}`;
  }

  async function suLanOne(item) {
    const row = suRow('lan:' + item.host);
    row.tone = '';
    row.text = 'Looking for it on this computer\'s Wi-Fi …';
    suRender();
    if (S.mode === 'demo') { await sleep(600); return 'Done (the demo changes nothing)'; }
    const net = await suNet();
    if (!(await suReach(item.host))) throw new Error(`Not found on this computer's Wi-Fi (${item.host}). Is it switched on, and is this computer on the same Wi-Fi?`);
    const before = await suPresent();
    row.text = 'Giving it the hub\'s Wi-Fi …';
    suRender();
    await suPost(item.host, '/api/wifi', { ssid: net.ssid, pass: net.pass });
    await sleep(700);
    await suPost(item.host, '/api/wifi', { ssid: net.ssid, pass: net.pass, connect: '1' }).catch(() => {});      // it leaves this Wi-Fi to do so: the answer may not come
    row.text = 'Waiting for it on the hub\'s Wi-Fi …';
    suRender();
    const who = await suArrival(suHostTail(item.host), before, 75);
    if (!who) throw new Error('It was told the hub\'s Wi-Fi but has not shown up there. Is the hub in its reach?');
    // The hub's network is to be the one it starts from, or the next restart takes it back to the room's Wi-Fi.
    const target = who.panel ? who.panel.uid : { ip: who.ip };
    const list = await panelHttp(target, 'GET', '/api/wifi', { timeout: 8000 });
    const names = ((list.json && list.json.networks) || []).map((n) => n && n.ssid);
    const at = names.indexOf(net.ssid);
    if (at < 0) throw new Error('It is on the hub\'s Wi-Fi, but the network is not in its list: after a restart it may go back');
    const boot = await panelHttp(target, 'POST', '/api/wifi/boot', {
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: new TextEncoder().encode('bootIdx=' + at), timeout: 8000, retry: 2 });
    if (boot.status !== 200) throw new Error('It is on the hub\'s Wi-Fi, but could not be told to start from it (HTTP ' + boot.status + ')');
    return suFirmware(who, row);
  }

  async function suLanAll() {
    if (SU.busy) return;
    const items = suLanList().filter((it) => suRow('lan:' + it.host).checked);
    if (!items.length) return;
    SU.busy = true;
    let ok = 0;
    for (const item of items) {
      const row = suRow('lan:' + item.host);
      try {
        row.text = await suLanOne(item);
        row.tone = 'ok';
        row.checked = false;
        ok++;
      } catch (e) {
        row.text = e.message;
        row.tone = 'bad';
        log('err', `Setup, ${item.host}: ${e.message}`);
      }
      row.total = 0;
      suRender();
    }
    SU.busy = false;
    log(ok === items.length ? '' : 'err', `Setup: ${ok} of ${items.length} sent to the hub from this computer's Wi-Fi`);
    suRender();
  }

  // The hub's lines about Setup.
  function suLine(m) {
    if (m.op === 'net') { if (SU.waitNet) SU.waitNet(m); return; }
    if (m.op === 'scan') {
      SU.looking = false;
      SU.nets = (m.nets || []).filter((n) => n && typeof n.ssid === 'string').sort((a, b) => b.rssi - a.rssi);
      // A panel that lost the hub for a while (the hub restarted) starts its hotspot too, and keeps it a
      // little after it is back: one that is here already is not a new panel.
      for (const n of SU.nets) {
        const here = suPanel(suTail(n.ssid)), row = suRow(n.ssid);
        if (here) { row.checked = false; row.text = `Already here: panel ${here.id}`; row.tone = 'ok'; }
        else if (row.tone === 'ok') { row.checked = true; row.text = ''; row.tone = ''; }
      }
      if (m.step === 'failed' || m.m) suSay(m.m || 'The scan failed', 'bad');
      else if (m.seen === 0) suSay('The hub heard no network at all. Look again', 'bad');
      else suSay(SU.nets.length ? '' : 'No new panels found. Look again if one was just switched on: its hotspot takes about half a minute to appear. A panel that already knows a Wi-Fi it can reach does not start one');
      suRender();
    } else if (m.op === 'stations') {
      SU.stations = m.list || [];
    } else if (m.op === 'join') {
      const w = SU.waitJoin;
      if (!w || w.ssid !== m.ssid) return;
      if (m.step === 'done' || m.step === 'failed') { SU.waitJoin = null; w.resolve(m); } else w.step(m.step);
    } else if (m.op === 'busy') {
      toast('The hub is busy: ' + (m.m || ''));
      SU.looking = false;
      if (SU.waitJoin) { const w = SU.waitJoin; SU.waitJoin = null; w.resolve({ step: 'failed', m: m.m }); }
      suRender();
    }
  }

  function suRender() {
    if (S.page !== 'setup') return;
    const fill = (ul, items, key, label) => {
      ul.textContent = '';
      for (const it of items) {
        const row = suRow(key(it), !String(key(it)).startsWith('lan:'));      // one on the room's Wi-Fi is ticked by whoever knows it is there
        const li = document.createElement('li');
        const lab = document.createElement('label');
        const box = document.createElement('input');
        box.type = 'checkbox';
        box.checked = row.checked;
        box.disabled = SU.busy;
        box.addEventListener('change', () => { row.checked = box.checked; suRender(); });
        lab.append(box);
        lab.insertAdjacentHTML('beforeend', label(it));
        const state = document.createElement('span');
        state.className = 'state ' + (row.tone || '');
        state.textContent = row.text;
        li.append(lab, state);
        if (row.total) {
          const bar = document.createElement('progress');
          bar.max = row.total;
          bar.value = row.done;
          li.append(bar);
        }
        ul.append(li);
      }
    };
    const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
    fill($('su-new'), SU.nets, (n) => n.ssid, (n) => `<b class="mono">${esc(n.ssid)}</b><span class="name">signal ${n.rssi}</span>`);
    $('su-new-none').hidden = SU.nets.length > 0;
    $('su-new-none').textContent = SU.looking ? 'Looking … (a few seconds)' : 'Nothing looked for yet.';
    const ticked = SU.nets.filter((n) => suRow(n.ssid).checked).length;
    $('su-go').disabled = SU.busy || !ticked || !S.connected;
    $('su-go').textContent = SU.busy ? 'Working …' : ticked ? `Bring ${nOf(ticked, 'panel')} in` : 'Bring them in';
    $('su-scan').disabled = SU.busy || SU.looking || !S.connected;

    const lan = suLanList();
    fill($('su-lan'), lan, (it) => 'lan:' + it.host, (it) => `<b class="mono">${esc(it.host.replace(/\.local$/, ''))}</b><span class="name">${esc(it.label)}</span>`);
    const sending = lan.filter((it) => suRow('lan:' + it.host).checked).length;
    $('su-lan-go').disabled = SU.busy || !sending || !S.connected;
    $('su-lan-go').textContent = sending ? `Send ${nOf(sending, 'panel')} to the hub` : 'Send them to the hub';

    const panels = patPanels();
    for (const p of panels) {
      const row = suRow(p.uid);
      if (!row.text || row.tone === '') { if (!row.total && !SU.busy) row.text = p.var || 'firmware not known yet'; }
      if (!SU.image) row.checked = false;
    }
    fill($('su-panels'), panels, (p) => p.uid, (p) => `<b>${p.id}</b><span class="name">${esc(p.n || 'Panel ' + p.id)}</span>`);
    $('su-panels-none').hidden = panels.length > 0;
    $('su-file-info').textContent = SU.image ? `${SU.image.version} · ${(SU.image.size / 1024).toFixed(0)} KB · ${SU.image.name}` : 'None chosen';
    const due = panels.filter((p) => suRow(p.uid).checked).length;
    $('su-update').disabled = SU.busy || !SU.image || !due || !S.connected;
    $('su-update').textContent = !SU.image ? 'Choose firmware.bin first' : due ? `Put ${SU.image.version} on ${nOf(due, 'panel')}` : 'Update the ticked panels';
  }

  async function suOpen() {
    if (!SU.recalled) {
      SU.recalled = true;
      const kept = await suRecall();
      if (kept && kept.bytes) {
        try { SU.image = suInspect(new Uint8Array(kept.bytes), kept.name || 'firmware.bin'); } catch (_) { /* what was kept is no image any more */ }
        if (SU.image) for (const p of patPanels()) suRow(p.uid).checked = p.var !== SU.image.version;
      }
    }
    suRender();
  }

  // The demo has no radio: two hotspots that are not there, and a hub that says it went.
  function fakeSetup(text) {
    if (!text.startsWith('{"c":"setup"')) return false;
    let m;
    try { m = JSON.parse(text); } catch (_) { return true; }
    const later = (ms, obj) => setTimeout(() => onLine(JSON.stringify(obj)), ms);
    if (m.op === 'scan') later(900, { t: 'setup', op: 'scan', nets: [{ ssid: 'patternflow-a1b2', rssi: -48, ch: 11 }, { ssid: 'patternflow-c3d4', rssi: -63, ch: 1 }] });
    else if (m.op === 'stations') later(100, { t: 'setup', op: 'stations', list: [] });
    else if (m.op === 'net') later(50, { t: 'setup', op: 'net', ssid: 'PatternflowEnsemble', pass: '' });
    else if (m.op === 'join') {
      later(100, { t: 'setup', op: 'join', ssid: m.ssid, step: 'connecting' });
      later(700, { t: 'setup', op: 'join', ssid: m.ssid, step: 'asking' });
      later(1100, { t: 'setup', op: 'join', ssid: m.ssid, step: 'saving' });
      later(1600, { t: 'setup', op: 'join', ssid: m.ssid, step: 'done', host: m.ssid, variant: 'ensemble', ver: 'v0.2.0', fw: '3.12.0' });
    }
    return true;
  }

  function wireSetup() {
    $('page-setup').addEventListener('click', () => setPage('setup'));
    $('su-scan').addEventListener('click', () => {
      if (SU.busy || SU.looking) return;
      SU.looking = true;
      SU.nets = [];
      suSay('');
      suRender();
      send({ c: 'setup', op: 'scan' });
      setTimeout(() => { if (SU.looking) { SU.looking = false; suSay('The hub did not answer. Is its firmware new enough for Setup?', 'bad'); suRender(); } }, 16000);
    });
    $('su-go').addEventListener('click', suBringAll);
    try { SU.lanExtra = (JSON.parse(localStorage.getItem(SU_LAN_KEY) || '[]') || []).map(suHost).filter(Boolean); } catch (_) { SU.lanExtra = []; }
    $('su-lan-add').addEventListener('submit', (ev) => {
      ev.preventDefault();
      const host = suHost($('su-lan-name').value);
      if (!host) { toast('A panel\'s name (patternflow-1a2b, or just the four digits) or its address'); return; }
      if (!SU.lanExtra.includes(host)) SU.lanExtra.push(host);
      try { localStorage.setItem(SU_LAN_KEY, JSON.stringify(SU.lanExtra)); } catch (_) { /* no storage here */ }
      suRow('lan:' + host, true).checked = true;
      $('su-lan-name').value = '';
      suRender();
    });
    $('su-lan-go').addEventListener('click', suLanAll);
    $('su-update').addEventListener('click', suUpdateAll);
    $('su-file-pick').addEventListener('click', () => $('su-file').click());
    $('su-file').addEventListener('change', (ev) => { if (ev.target.files[0]) suTake(ev.target.files[0]); ev.target.value = ''; });
  }

  function wirePatterns() {
    loadSet();
    $('page-stage').addEventListener('click', () => setPage('stage'));
    $('page-patterns').addEventListener('click', () => setPage('patterns'));
    $('pat-open').href = COMMUNITY_PAGE;
    $('pat-add').addEventListener('submit', (ev) => {
      ev.preventDefault();
      const text = $('pat-link').value.trim();
      if (!text) return;
      $('pat-link').value = '';
      addText(text);
    });
    // A link pasted anywhere on the page, not only in the box.
    document.addEventListener('paste', (ev) => {
      if (S.page !== 'patterns' || P.running) return;
      const el = document.activeElement;
      if (el && el !== $('pat-link') && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA')) return;
      const text = (ev.clipboardData && ev.clipboardData.getData('text')) || '';
      if (!linksIn(text).length) return;
      ev.preventDefault();
      addText(text);
    });
    // A pattern dragged over from the community's page, or files from a folder.
    const page = $('pat-page');
    page.addEventListener('dragover', (ev) => {
      if (dragFrom >= 0) return;
      ev.preventDefault();
      page.classList.add('over');
    });
    page.addEventListener('dragleave', (ev) => { if (ev.target === page) page.classList.remove('over'); });
    page.addEventListener('drop', (ev) => {
      page.classList.remove('over');
      if (dragFrom >= 0) return;
      ev.preventDefault();
      if (P.running) return;
      const dt = ev.dataTransfer;
      if (dt.files && dt.files.length) { addFiles(dt.files); return; }
      const text = dt.getData('text/uri-list') || dt.getData('text/plain') || '';
      if (text) addText(text);
    });
    // The order: drag a row.
    const list = $('pat-list');
    list.addEventListener('dragstart', (ev) => {
      const li = ev.target.closest('li');
      if (!li || P.running) { ev.preventDefault(); return; }
      dragFrom = +li.dataset.i;
      ev.dataTransfer.effectAllowed = 'move';
      ev.dataTransfer.setData('text/plain', '');
    });
    list.addEventListener('dragover', (ev) => {
      if (dragFrom < 0) return;
      ev.preventDefault();
      const li = ev.target.closest('li');
      for (const x of list.children) x.classList.toggle('under', x === li);
    });
    list.addEventListener('drop', (ev) => {
      if (dragFrom < 0) return;
      ev.preventDefault();
      ev.stopPropagation();
      const li = ev.target.closest('li');
      const to = li ? +li.dataset.i : P.set.length - 1;
      const [moved] = P.set.splice(dragFrom, 1);
      P.set.splice(to, 0, moved);
      dragFrom = -1;
      saveSet();
      renderSet();
      compareAll();
    });
    list.addEventListener('dragend', () => { dragFrom = -1; for (const x of list.children) x.classList.remove('under'); });

    $('pat-clear').addEventListener('click', () => {
      if (P.running) return;
      P.set = [];
      P.shows = [];
      saveSet();
      renderSet();
      compareAll();
    });
    $('pat-check-all').addEventListener('click', () => {
      const rows = patPanels().map((p) => panelRow(p.uid));
      const on = !rows.every((r) => r.checked);
      for (const r of rows) { r.checked = on; r.touched = true; }
      renderGo();
    });
    $('pat-install').addEventListener('click', installAll);
  }

  // ── Shows: a panel playing on its own ──────────────────────────────────
  // A show is a .pfs table kept on a panel (the Performance edition's
  // player, in the ensemble's image from v0.2.0): a timeline of patterns
  // and knob values that the panel plays, and loops, by itself. While it
  // does, the panel stands outside the ensemble - the hub's report says so
  // (flag 16) - and it is the ensemble's again when the show stops.
  const RF_SOLO = 16;
  const isSolo = (p) => !!(p && p.on && (p.fl & RF_SOLO));
  const shows = new Map();        // uid -> { loading, none, error, data }

  // The head of a .pfs table: its title and how long it runs. null: not one.
  function pfsInfo(bytes) {
    if (!bytes || bytes.length < 76 || String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]) !== 'PFST') return null;
    const version = bytes[4];
    if (version !== 1 && version !== 2) return null;
    const u16 = (at) => bytes[at] | (bytes[at + 1] << 8);
    let end = 12;
    while (end < 44 && bytes[end]) end++;
    return { version, loop: !!(bytes[5] & 1), seconds: version === 2 ? u16(6) / 10 : u16(6), cues: u16(8),
             title: new TextDecoder().decode(bytes.subarray(12, end)) };
  }
  const showSlug = (name) => name.replace(/\.pfs$/i, '').toLowerCase().replace(/[^a-z0-9_-]/g, '_').slice(0, 39) || 'show';

  async function loadShows(uid) {
    const st = shows.get(uid) || {};
    if (st.loading) return;
    st.loading = true;
    shows.set(uid, st);
    try {
      const res = await panelHttp(uid, 'GET', '/api/shows', { timeout: 8000 });
      st.none = res.status === 404;                 // an image without the player
      st.error = res.status === 200 || st.none ? '' : `HTTP ${res.status}`;
      st.data = res.status === 200 && res.json ? res.json : null;
    } catch (e) {
      st.error = e.message;
      st.data = null;
    }
    st.loading = false;
    st.at = performance.now();
    renderShow();
  }

  function renderShow() {
    const p = S.sel != null ? S.panels.get(S.sel) : null;
    const box = $('insp-show');
    box.hidden = !p || !p.on;
    if (box.hidden) return;
    const st = shows.get(p.uid);
    if (!st) { loadShows(p.uid); }
    const data = st && st.data;
    const list = (data && data.shows) || [];
    const sel = $('p-show');
    const key = p.uid + '|' + list.map((s) => s.slug).join('|');
    if (sel.dataset.key !== key && !busy(sel)) {
      sel.dataset.key = key;
      sel.textContent = '';
      for (const s of list) {
        const o = document.createElement('option');
        o.value = s.slug;
        o.textContent = `${s.title || s.slug} · ${s.length} s`;
        sel.append(o);
      }
      if (data && data.slug && list.some((s) => s.slug === data.slug)) sel.value = data.slug;
    }
    const playing = !!(data && data.playing) || isSolo(p);
    const can = S.connected && list.length > 0;
    sel.disabled = !can;
    $('p-show-play').disabled = !can;
    $('p-show-stop').disabled = !S.connected || !data;
    $('p-show-play').setAttribute('aria-pressed', String(playing));
    $('p-show-delete').disabled = !can || REC.on || REC.busy;
    const rec = $('p-show-rec');
    rec.disabled = !S.connected || REC.busy || (!REC.on && (!st || !!st.none || !st.data));
    rec.setAttribute('aria-pressed', String(REC.on));
    rec.textContent = REC.busy ? (REC.on ? 'Getting ready …' : 'Sending to the panels …')
      : REC.on ? `■ Stop · ${((performance.now() - REC.startedAt) / 1000).toFixed(1)} s · ${nOf(REC.panels.size, 'panel')}` : '● Record';
    if (data && !busy($('p-show-loop')) && playing) $('p-show-loop').checked = !!data.loop;
    const note = $('p-show-state');
    note.style.color = '';
    if (!st || (st.loading && !data && !st.none)) note.textContent = 'Checking …';
    else if (st.none) note.textContent = 'This panel\'s firmware has no show player (it comes with ensemble v0.2.0).';
    else if (st.error) { note.textContent = st.error; note.style.color = 'var(--bad)'; }
    else if (playing && data) {
      const missing = (data.missing || []).length ? ` · ${nOf(data.missing.length, 'pattern')} missing` : '';
      note.textContent = `Playing · ${data.title || data.slug} · ${data.t}/${data.length} s${data.loop ? ' · loop' : ''}${missing}`;
    } else if (!list.length) note.textContent = 'No shows on this panel. Record one here, or put one on from Patterns or Shows.';
    else note.textContent = '';
  }

  // One order to every chosen panel's player. The answer is the player's state.
  async function showControl(fields) {
    const panels = pickedPanels().filter((p) => p.on);
    const body = new TextEncoder().encode(new URLSearchParams(fields).toString());
    await mapLimit(panels, RELAY_SLOTS, async (p) => {
      try {
        const res = await panelHttp(p.uid, 'POST', '/api/shows/control', {
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body, timeout: 10000 });
        const st = shows.get(p.uid) || {};
        shows.set(p.uid, st);
        if (res.status === 200 && res.json) {
          st.data = { ...(st.data || {}), ...res.json };
          st.error = '';
        } else {
          const why = (res.json && res.json.error) || (res.status === 404 ? 'This firmware has no show player' : `HTTP ${res.status}`);
          toast(`Panel ${p.id}: ${why}`);
          log('err', `Panel ${p.id}, show: ${why}`);
        }
      } catch (e) {
        toast(`Panel ${p.id}: ${e.message}`);
      }
    });
    renderShow();
  }

  // ── Recording: what the chosen panels do, looped on them at once ───────
  // While it records, every report of a chosen panel is a sample: its four
  // knob positions (console/pfs.js). At the end each panel gets its own
  // table - what that panel itself did, so a wave is recorded as the wave
  // it was - and plays it in a loop.
  //
  // The pattern a panel is on, and its changes, go into the table only when
  // "With patterns" is ticked. Without it a recording is knob moves and
  // nothing else, to be put on any panel showing any pattern - which is what
  // recordings are for at this stage (2026-11-21). One made with patterns
  // can still be played without them: the Shows page's "Leave the pattern
  // alone".
  const REC = { on: false, startedAt: 0, panels: new Map(), timer: 0, busy: false, patterns: false };
  const REC_KEEP = 4;             // recordings a panel keeps; older ones are deleted when a new one arrives

  function recSample(m) {
    const r = REC.panels.get(m.uid);
    if (!REC.on || !r || !Array.isArray(m.k)) return;
    r.rec.sample((performance.now() - REC.startedAt) / 100, m.k, REC.patterns ? (r.names[m.pat] || '') : '');
  }

  // What a panel calls each of its patterns, for a player to find it by: a
  // module's slug, a built-in pattern's name.
  async function patternKeys(uid) {
    const res = await panelHttp(uid, 'GET', '/api/patterns', { timeout: 8000 });
    const names = [];
    if (res.status === 200 && res.json && Array.isArray(res.json.patterns))
      for (const p of res.json.patterns) names[p.index] = p.module || p.name || '';
    return names;
  }

  async function startRecording() {
    if (REC.on || REC.busy) return;
    const panels = pickedPanels().filter((p) => p.on);
    if (!panels.length) return;
    REC.busy = true;
    renderShow();
    try {
      // A panel in a show reports the ensemble's values, not what it shows: out of the show first.
      if (panels.some(isSolo)) {
        await showControl({ op: 'stop' });
        await sleep(700);
      }
      REC.panels.clear();
      REC.patterns = $('p-show-rec-pat').checked;       // as it stood when the recording began
      await mapLimit(panels, RELAY_SLOTS, async (p) => {
        let names = [];
        if (REC.patterns) {
          try { names = await patternKeys(p.uid); } catch (_) { /* then the table says nothing about patterns */ }
        }
        REC.panels.set(p.uid, { id: p.id, rec: PFS.recorder(), names });
      });
      REC.startedAt = performance.now();
      REC.on = true;
      for (const p of panels) {
        const now = [...S.panels.values()].find((q) => q.uid === p.uid);
        if (now) recSample(now);
      }
      REC.timer = setInterval(() => {
        if ((performance.now() - REC.startedAt) / 100 >= PFS.MAX_TICKS) stopRecording();
        else renderShow();
      }, 200);
      log('', `Recording panel ${panels.map((p) => p.id).join(', ')} (${REC.patterns ? 'knobs and patterns' : 'knobs only'})`);
    } finally {
      REC.busy = false;
      renderShow();
    }
  }

  async function stopRecording() {
    if (!REC.on || REC.busy) return;
    REC.on = false;
    clearInterval(REC.timer);
    const length = Math.round((performance.now() - REC.startedAt) / 100);
    if (length < 10) { toast('A recording under a second was discarded'); REC.panels.clear(); renderShow(); return; }
    REC.busy = true;
    renderShow();
    const d = new Date();
    const two = (n) => String(n).padStart(2, '0');
    const stamp = `${two(d.getMonth() + 1)}${two(d.getDate())}_${two(d.getHours())}${two(d.getMinutes())}${two(d.getSeconds())}`;
    const slug = 'rec_' + stamp;
    const title = `REC ${two(d.getMonth() + 1)}-${two(d.getDate())} ${two(d.getHours())}:${two(d.getMinutes())}:${two(d.getSeconds())}`;
    let played = 0, cues = 0;
    const problems = [];
    const tables = new Map();
    try {
      await mapLimit([...REC.panels], RELAY_SLOTS, async ([uid, r]) => {
        try {
          const made = PFS.build({ title, id: slug, length, tracks: r.rec.tracks, patterns: r.rec.patterns });
          // Kept, whatever happens to it on the panel: the same table from several panels once.
          const key = b64(made.bytes);
          if (!tables.has(key)) tables.set(key, { bytes: made.bytes, ids: [] });
          tables.get(key).ids.push(r.id);
          await putAndPlay(uid, slug, made.bytes, true);
          played++;
          cues = Math.max(cues, made.cues);
          await tidyRecordings(uid).catch(() => {});      // older recordings make room: the newest few stay
        } catch (e) {
          problems.push(`Panel ${r.id}: ${e.message}`);
        }
        shows.delete(uid);
      });
    } finally {
      REC.busy = false;
      REC.panels.clear();
    }
    // Into the library (the show page), one entry a different table.
    let n = 0;
    for (const t of [...tables.values()].sort((a, b) => Math.min(...a.ids) - Math.min(...b.ids))) {
      const entry = libAdd(t.bytes, { title: tables.size > 1 ? `${title} · panel ${t.ids.sort((a, b) => a - b).join(', ')}` : title,
                                      slug: slug + (n ? String.fromCharCode(97 + n) : '') });
      if (entry && !n) L.sel = entry.id;
      n++;
    }
    for (const p of problems) log('err', 'Recording, ' + p);
    const secs = (length / 10).toFixed(1);
    if (played) log('', `${secs} s recorded, looping on ${nOf(played, 'panel')} (${title}, ${nOf(cues, 'cue')})`);
    toast(problems.length ? problems[0] : `${secs} s recorded and looping on ${nOf(played, 'panel')}. Kept on the Shows page`);
    renderShow();
  }

  // The show chosen in the list, off every chosen panel.
  async function deleteShow() {
    const slug = $('p-show').value;
    if (!slug) return;
    await mapLimit(pickedPanels().filter((p) => p.on), RELAY_SLOTS, async (p) => {
      try {
        const res = await panelHttp(p.uid, 'DELETE', '/api/shows?slug=' + encodeURIComponent(slug), { timeout: 8000 });
        if (res.status !== 200 && res.status !== 404) toast(`Panel ${p.id}: ${(res.json && res.json.error) || 'HTTP ' + res.status}`);
      } catch (e) {
        toast(`Panel ${p.id}: ${e.message}`);
      }
      shows.delete(p.uid);
    });
    renderShow();
  }

  function wireShows() {
    $('p-show-play').addEventListener('click', () => {
      const slug = $('p-show').value;
      if (slug) showControl({ op: 'play', slug, loop: $('p-show-loop').checked ? '1' : '0' });
    });
    $('p-show-stop').addEventListener('click', () => showControl({ op: 'stop' }));
    $('p-show-delete').addEventListener('click', deleteShow);
    $('p-show-rec').addEventListener('click', () => (REC.on ? stopRecording() : startRecording()));
    try { $('p-show-rec-pat').checked = localStorage.getItem('pfe-rec-patterns') === '1'; } catch (_) { /* no storage here */ }
    $('p-show-rec-pat').addEventListener('change', (ev) => {
      try { localStorage.setItem('pfe-rec-patterns', ev.target.checked ? '1' : '0'); } catch (_) { /* no storage here */ }
    });
    $('p-show-loop').addEventListener('change', (ev) => {
      const p = S.sel != null ? S.panels.get(S.sel) : null;
      if (isSolo(p)) showControl({ op: 'loop', loop: ev.target.checked ? '1' : '0' });
    });
    // The playhead of the panel in the side column, while it plays.
    setInterval(() => {
      const p = S.sel != null ? S.panels.get(S.sel) : null;
      if (S.connected && S.page === 'stage' && isSolo(p) && !document.hidden) loadShows(p.uid);
    }, 2500);
  }

  // ── The show page: a library of shows, and one of them on the bench ────
  // Every recording is kept here (in this browser), as it was made. What is
  // done to it - a piece cut out, slower or faster, knobs crossed or turned
  // over, another pattern - is kept beside it and applied when it is drawn
  // and when it goes to a panel (console/pfs.js, PFS.edit), so nothing done
  // is lost and "as it was" is one button.
  const LIB_KEY = 'pfe-show-library';
  const KNOB_COLOURS = ['#E0481C', '#2B7A4B', '#2B6CB0', '#9A6A00'];
  const L = { shows: [], sel: null, targets: new Map(), touched: false, busy: false, known: [], knownAt: 0 };

  // Text cut to fit a field of `max` bytes, never inside a character.
  function utf8Cut(text, max) {
    const enc = new TextEncoder();
    let out = '';
    for (const ch of String(text)) {
      if (enc.encode(out + ch).length > max) break;
      out += ch;
    }
    return out;
  }
  const freshEdit = (show) => ({ start: 0, end: show.length, length: show.length, map: [0, 1, 2, 3],
                                 invert: [false, false, false, false], pattern: null, loop: true });

  function libLoad() {
    try {
      const saved = JSON.parse(localStorage.getItem(LIB_KEY) || '[]');
      L.shows = saved.map((e) => ({ ...e, show: PFS.read(unb64(e.src)) })).filter((e) => e.show && e.edit);
    } catch (_) { L.shows = []; }
  }
  function libSave() {
    try { localStorage.setItem(LIB_KEY, JSON.stringify(L.shows.map(({ show, ...rest }) => rest))); }
    catch (_) { toast('The browser is out of storage: the show was not saved'); }
  }
  // A table into the library (once: the same table again is the one already there).
  function libAdd(bytes, { title, slug }) {
    const src = b64(bytes);
    const have = L.shows.find((e) => e.src === src);
    if (have) return have;
    const show = PFS.read(bytes);
    if (!show) return null;
    const base = (slug || 'show').toLowerCase().replace(/[^a-z0-9_-]/g, '_').slice(0, 36) || 'show';
    let id = base;
    for (let n = 2; L.shows.some((e) => e.id === id); n++) id = `${base}_${n}`;
    const entry = { id, title: title || show.title || id, src, created: Date.now(), edit: freshEdit(show), show };
    L.shows.unshift(entry);
    libSave();
    if (S.page === 'shows') renderLib();
    return entry;
  }
  const libSel = () => L.shows.find((e) => e.id === L.sel) || null;
  // The show as it goes out: the recording with what was done to it.
  function effective(e) {
    return PFS.edit(e.show, { ...e.edit, title: utf8Cut(e.title, 31), id: e.id });
  }

  // The patterns a show can be put on: what the panels hold, by the name a player finds them by.
  async function loadKnownPatterns() {
    const known = new Map();
    for (const e of P.set) known.set(e.slug, e.name || e.slug);
    const p = patPanels()[0];
    if (p && S.connected) {
      try {
        const res = await panelHttp(p.uid, 'GET', '/api/patterns', { timeout: 8000 });
        if (res.status === 200 && res.json && Array.isArray(res.json.patterns))
          for (const q of [...res.json.patterns].sort((a, b) => (a.index || 0) - (b.index || 0))) known.set(q.module || q.name, q.name || q.module);
      } catch (_) { /* the list from the set will do */ }
    }
    L.known = [...known].map(([key, name]) => ({ key, name }));
    L.knownAt = performance.now();
    if (S.page === 'shows') renderLib();
  }

  function drawShowGraph(show) {
    const cv = $('sh-graph');
    const dpr = window.devicePixelRatio || 1;
    const W = cv.clientWidth, H = cv.clientHeight;
    if (!W || !H) return;
    cv.width = Math.round(W * dpr);
    cv.height = Math.round(H * dpr);
    const g = cv.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, W, H);
    const padL = 34, padR = 10, padT = 18, padB = 20;
    const x = (t) => padL + (W - padL - padR) * t / show.length;
    const y = (v) => padT + (H - padT - padB) * (1 - v / 1000);
    g.font = '11px system-ui, sans-serif';
    g.textBaseline = 'middle';
    // The frame: quarters of a knob's travel, and the seconds.
    g.strokeStyle = '#E3DCCE';
    g.fillStyle = '#7D7668';
    g.lineWidth = 1;
    for (const v of [0, 250, 500, 750, 1000]) {
      g.beginPath(); g.moveTo(padL, y(v) + 0.5); g.lineTo(W - padR, y(v) + 0.5); g.stroke();
      g.textAlign = 'right';
      g.fillText(String(v), padL - 6, y(v));
    }
    const secs = show.length / 10;
    const step = [0.5, 1, 2, 5, 10, 20, 30, 60, 120].find((s) => secs / s <= 10) || 300;
    g.textAlign = 'center';
    for (let s = 0; s <= secs + 1e-6; s += step) g.fillText(`${Math.round(s * 10) / 10}s`, x(s * 10), H - padB / 2);
    // Where the pattern changes.
    g.textAlign = 'left';
    for (const e of show.events) {
      if (!e.pattern) continue;
      g.strokeStyle = '#B9B2A4';
      g.setLineDash([3, 3]);
      g.beginPath(); g.moveTo(x(e.t) + 0.5, padT); g.lineTo(x(e.t) + 0.5, H - padB); g.stroke();
      g.setLineDash([]);
      g.fillStyle = '#4E493F';
      const name = (L.known.find((k) => k.key === e.pattern) || {}).name || e.pattern;
      g.fillText(name, Math.min(x(e.t) + 4, W - padR - g.measureText(name).width), padT / 2);
    }
    // The four knobs: a line where a point eases to the next, a step where it does not.
    show.tracks.forEach((track, k) => {
      if (!track.length) return;
      g.strokeStyle = KNOB_COLOURS[k];
      g.lineWidth = 1.8;
      g.beginPath();
      g.moveTo(x(track[0].t), y(track[0].v));
      track.forEach((p, i) => {
        const next = track[i + 1];
        if (!next) { g.lineTo(x(show.length), y(p.v)); return; }
        if (!p.ease) g.lineTo(x(next.t), y(p.v));
        g.lineTo(x(next.t), y(next.v));
      });
      g.stroke();
    });
  }

  function setShStatus(text, tone) {
    const el = $('sh-status');
    el.textContent = text || '';
    el.style.color = tone === 'bad' ? 'var(--bad)' : '';
  }

  function renderLib() {
    const list = $('lib-list');
    list.textContent = '';
    for (const e of L.shows) {
      const li = document.createElement('li');
      li.setAttribute('role', 'option');
      li.setAttribute('aria-selected', String(e.id === L.sel));
      li.tabIndex = 0;
      const name = document.createElement('span');
      name.className = 'name';
      name.textContent = e.title;
      const meta = document.createElement('span');
      meta.className = 'meta num';
      const d = new Date(e.created);
      meta.textContent = `${(e.edit.length / 10).toFixed(1)} s · ${d.getMonth() + 1}/${d.getDate()} ${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
      li.append(name, meta);
      const choose = () => { L.sel = e.id; setShStatus(''); renderLib(); };
      li.addEventListener('click', choose);
      li.addEventListener('keydown', (ev) => { if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); choose(); } });
      list.append(li);
    }
    $('lib-empty').hidden = L.shows.length > 0;
    const e = libSel();
    $('sh-none').hidden = !!e;
    $('sh-body').hidden = !e;
    if (!e) return;

    const c = e.edit;
    const set = (id, v) => { if (!busy($(id))) $(id).value = v; };
    set('sh-title', e.title);
    set('sh-start', (c.start / 10).toFixed(1));
    set('sh-end', (c.end / 10).toFixed(1));
    set('sh-length', (c.length / 10).toFixed(1));
    $('sh-end').max = (e.show.length / 10).toFixed(1);
    const speed = (c.end - c.start) / c.length;
    $('sh-speed').textContent = Math.abs(speed - 1) < 0.005 ? 'as recorded' : `×${speed.toFixed(2)}`;
    $('sh-speed-reset').disabled = Math.abs(speed - 1) < 0.005;
    for (let k = 0; k < KNOBS; k++) {
      set(`sh-map-${k}`, String(c.map[k]));
      $(`sh-inv-${k}`).checked = !!c.invert[k];
    }
    $('sh-loop').checked = c.loop !== false;

    // The pattern: as the recording says, none, or one of those the panels hold.
    const written = PFS.patternsOf(e.show);
    const sel = $('sh-pattern');
    const nameOf = (key) => (L.known.find((k) => k.key === key) || {}).name || key;
    const options = [['', written.length ? `As recorded (${written.map(nameOf).join(' → ')})` : 'No pattern in the recording'],
                     ['-', 'Leave the pattern alone (play on whatever the panel is showing)'],
                     ...L.known.map((k) => [k.key, k.name])];
    if (c.pattern && !options.some(([key]) => key === c.pattern)) options.push([c.pattern, c.pattern]);
    const key = options.map((o) => o.join('=')).join('|');
    if (sel.dataset.key !== key && !busy(sel)) {
      sel.dataset.key = key;
      sel.textContent = '';
      for (const [value, label] of options) {
        const o = document.createElement('option');
        o.value = value;
        o.textContent = label;
        sel.append(o);
      }
    }
    if (!busy(sel)) sel.value = c.pattern == null ? '' : c.pattern === '' ? '-' : c.pattern;

    let show = null, problem = '';
    try { show = effective(e); PFS.write(show); } catch (err) { problem = err.message; }
    if (show) {
      drawShowGraph(show);
      $('sh-info').textContent = `${PFS.cueCount(show)} of ${PFS.MAX_CUES} cues · ${(show.length / 10).toFixed(1)} s` +
        (problem ? ` · ${problem}` : '');
    } else {
      $('sh-info').textContent = problem;
    }
    $('sh-info').style.color = problem ? 'var(--bad)' : '';

    // The panels it goes to: the ones chosen on the map, until somebody ticks here.
    const panels = patPanels();
    const ul = $('sh-panels');
    ul.textContent = '';
    for (const p of panels) {
      if (!L.touched || !L.targets.has(p.uid)) L.targets.set(p.uid, L.touched ? true : (S.picked.size ? S.picked.has(p.i) : true));
      const li = document.createElement('li');
      const label = document.createElement('label');
      const box = document.createElement('input');
      box.type = 'checkbox';
      box.checked = L.targets.get(p.uid);
      box.disabled = L.busy;
      box.addEventListener('change', () => { L.targets.set(p.uid, box.checked); L.touched = true; renderLib(); });
      const id = document.createElement('b');
      id.className = 'num';
      id.textContent = p.id;
      const name = document.createElement('span');
      name.className = 'name';
      name.textContent = p.n || `Panel ${p.id}`;
      label.append(box, id, name);
      const state = document.createElement('span');
      state.className = 'state' + (isSolo(p) ? ' ok' : '');
      state.textContent = isSolo(p) ? 'Playing a show' : '';
      li.append(label, state);
      ul.append(li);
    }
    const chosen = panels.filter((p) => L.targets.get(p.uid)).length;
    $('sh-send').textContent = L.busy ? 'Sending …' : chosen ? `Put on ${nOf(chosen, 'panel')} and play` : 'Put on and play';
    $('sh-send').disabled = L.busy || !chosen || !!problem || !S.connected;
    $('sh-stop').disabled = L.busy || !chosen || !S.connected;
  }

  // A table onto a panel and playing. Returns the patterns it names that the panel does not hold.
  async function putAndPlay(uid, slug, bytes, loop) {
    const put = await panelHttp(uid, 'PUT', '/api/shows', {
      headers: { 'Content-Type': 'application/octet-stream', 'X-PF-Name': slug + '.pfs' }, body: bytes, timeout: 20000 });
    if (put.status === 404) throw new Error('This firmware has no show player');
    if (put.status !== 200) throw new Error((put.json && put.json.error) || `HTTP ${put.status}`);
    const res = await panelHttp(uid, 'POST', '/api/shows/control', {
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new TextEncoder().encode(new URLSearchParams({ op: 'play', slug, loop: loop ? '1' : '0' }).toString()), timeout: 10000 });
    if (res.status !== 200) throw new Error((res.json && res.json.error) || `Play: HTTP ${res.status}`);
    shows.delete(uid);
    return (res.json && res.json.missing) || [];
  }
  // A panel keeps the newest few recordings; the library here has them all.
  async function tidyRecordings(uid) {
    const list = await panelHttp(uid, 'GET', '/api/shows', { timeout: 8000 });
    const old = ((list.json && list.json.shows) || []).map((s) => s.slug).filter((s) => /^rec_\d{4}_\d{6}/.test(s)).sort().slice(0, -REC_KEEP);
    for (const s of old) await panelHttp(uid, 'DELETE', '/api/shows?slug=' + encodeURIComponent(s), { timeout: 8000 });
  }

  async function sendShow() {
    const e = libSel();
    if (!e || L.busy) return;
    let bytes;
    try { bytes = PFS.write(effective(e)); } catch (err) { setShStatus(err.message, 'bad'); return; }
    const panels = patPanels().filter((p) => L.targets.get(p.uid));
    L.busy = true;
    setShStatus('');
    renderLib();
    let ok = 0;
    const problems = [], missing = new Set();
    await mapLimit(panels, RELAY_SLOTS, async (p) => {
      try {
        for (const name of await putAndPlay(p.uid, e.id, bytes, e.edit.loop !== false)) missing.add(name);
        ok++;
        await tidyRecordings(p.uid).catch(() => {});
      } catch (err) {
        problems.push(`Panel ${p.id}: ${err.message}`);
      }
    });
    L.busy = false;
    for (const p of problems) log('err', 'Show, ' + p);
    setShStatus(problems.length ? problems[0] : `Playing on ${nOf(ok, 'panel')}` + (missing.size ? ` · patterns the panel does not hold: ${[...missing].join(', ')}` : ''),
                problems.length || missing.size ? 'bad' : '');
    renderLib();
  }

  async function stopShows() {
    const panels = patPanels().filter((p) => L.targets.get(p.uid));
    const body = new TextEncoder().encode('op=stop');
    await mapLimit(panels, RELAY_SLOTS, async (p) => {
      try {
        await panelHttp(p.uid, 'POST', '/api/shows/control', { headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body, timeout: 10000 });
      } catch (_) { /* it says so itself on the map */ }
      shows.delete(p.uid);
    });
    setShStatus('');
  }

  function wireShowPage() {
    libLoad();
    $('page-shows').addEventListener('click', () => setPage('shows'));
    // The four knobs' rows.
    const knobs = $('sh-knobs');
    for (let k = 0; k < KNOBS; k++) {
      const field = document.createElement('div');
      field.className = 'field';
      const label = document.createElement('span');
      label.innerHTML = `<i style="background:${KNOB_COLOURS[k]}"></i>K${k + 1} follows`;
      const sel = document.createElement('select');
      sel.id = `sh-map-${k}`;
      sel.setAttribute('aria-label', `What K${k + 1} follows`);
      for (let from = 0; from < KNOBS; from++) {
        const o = document.createElement('option');
        o.value = String(from);
        o.textContent = `Recorded K${from + 1}`;
        sel.append(o);
      }
      const none = document.createElement('option');
      none.value = '-1';
      none.textContent = 'Nothing (the show leaves it alone)';
      sel.append(none);
      const inv = document.createElement('label');
      inv.className = 'check';
      inv.innerHTML = `<input type="checkbox" id="sh-inv-${k}"> Invert`;
      field.append(label, sel, inv);
      knobs.append(field);
      sel.addEventListener('change', () => change((c) => { c.map[k] = +sel.value; }));
      inv.querySelector('input').addEventListener('change', (ev) => change((c) => { c.invert[k] = ev.target.checked; }));
    }
    function change(fn) {
      const e = libSel();
      if (!e) return;
      fn(e.edit, e);
      libSave();
      renderLib();
    }
    const ticks = (el) => Math.round((parseFloat(el.value) || 0) * 10);
    $('sh-title').addEventListener('input', (ev) => change((c, e) => { e.title = ev.target.value.trim() || e.id; }));
    $('sh-start').addEventListener('change', (ev) => change((c, e) => {
      const speed = (c.end - c.start) / c.length;
      c.start = clamp(ticks(ev.target), 0, c.end - 1);
      c.length = Math.max(1, Math.round((c.end - c.start) / speed));        // the same speed, a shorter piece
    }));
    $('sh-end').addEventListener('change', (ev) => change((c, e) => {
      const speed = (c.end - c.start) / c.length;
      c.end = clamp(ticks(ev.target), c.start + 1, e.show.length);
      c.length = Math.max(1, Math.round((c.end - c.start) / speed));
    }));
    $('sh-length').addEventListener('change', (ev) => change((c) => { c.length = clamp(ticks(ev.target), 1, 60000); }));
    $('sh-speed-reset').addEventListener('click', () => change((c) => { c.length = c.end - c.start; }));
    $('sh-pattern').addEventListener('change', (ev) => change((c) => { c.pattern = ev.target.value === '' ? null : ev.target.value === '-' ? '' : ev.target.value; }));
    $('sh-loop').addEventListener('change', (ev) => change((c) => { c.loop = ev.target.checked; }));
    $('sh-reset').addEventListener('click', () => change((c, e) => { e.edit = freshEdit(e.show); }));
    $('sh-dup').addEventListener('click', () => {
      const e = libSel();
      if (!e) return;
      const base = e.id.replace(/_\d+$/, '');
      let id = base;
      for (let n = 2; L.shows.some((x) => x.id === id); n++) id = `${base}_${n}`;
      const copy = { ...e, id, title: e.title + ' copy', created: Date.now(), edit: JSON.parse(JSON.stringify(e.edit)) };
      L.shows.splice(L.shows.indexOf(e), 0, copy);
      L.sel = id;
      libSave();
      renderLib();
    });
    $('sh-delete').addEventListener('click', () => {
      const e = libSel();
      if (!e) return;
      L.shows.splice(L.shows.indexOf(e), 1);
      L.sel = L.shows.length ? L.shows[0].id : null;
      libSave();
      renderLib();
    });
    $('sh-export').addEventListener('click', () => {
      const e = libSel();
      if (!e) return;
      try {
        const url = URL.createObjectURL(new Blob([PFS.write(effective(e))], { type: 'application/octet-stream' }));
        const a = document.createElement('a');
        a.href = url;
        a.download = e.id + '.pfs';
        a.click();
        setTimeout(() => URL.revokeObjectURL(url), 2000);
      } catch (err) { setShStatus(err.message, 'bad'); }
    });
    $('sh-send').addEventListener('click', sendShow);
    $('sh-stop').addEventListener('click', stopShows);

    // Tables from this computer: the button, or dropped anywhere on the page.
    const takeFiles = async (files) => {
      let last = null;
      for (const f of files) {
        if (!/\.pfs$/i.test(f.name)) continue;
        const entry = libAdd(new Uint8Array(await f.arrayBuffer()), { slug: showSlug(f.name) });
        if (entry) last = entry; else toast(`${f.name}: not a show file (.pfs)`);
      }
      if (last) { L.sel = last.id; renderLib(); }
    };
    $('sh-file').addEventListener('change', (ev) => { takeFiles([...ev.target.files]); ev.target.value = ''; });
    const page = $('show-page');
    page.addEventListener('dragover', (ev) => { ev.preventDefault(); });
    page.addEventListener('drop', (ev) => {
      ev.preventDefault();
      if (ev.dataTransfer.files && ev.dataTransfer.files.length) takeFiles([...ev.dataTransfer.files]);
    });
    new ResizeObserver(() => { if (S.page === 'shows' && libSel()) renderLib(); }).observe($('sh-graph'));
  }

  // ── The node page: MIDI in, arithmetic, panels out ─────────────────────
  // A graph of nodes, wired by hand (console/graph.js says what the nodes
  // are and turns the graph into the hub's program). Every change is
  // compiled and sent to the hub at once, which runs it from then on with
  // or without this page; the hub says back what comes in over MIDI and
  // what each node's value is for one panel, and that is drawn on the nodes.
  const NG_KEY = 'pfe-graph';
  const NG = {
    graph: { nodes: [], links: [] },
    view: { x: 60, y: 40, z: 1 },
    sel: new Set(),
    els: new Map(),            // node id -> its element
    drag: null,                // what the pointer is doing: { kind: 'pan' | 'node' | 'wire', ... }
    made: null,                // the last compile
    live: null,                // the registers of the watched panel, as the hub last said
    seq: 0, pending: 0,
    hub: null,                 // what the hub says it is running
    learn: null,               // the controller node waiting for a controller to move
    wiresDirty: true, liveDirty: false,
    mouse: { x: 0, y: 0, over: false },
    timer: 0, said: '',
    on: false,                 // the hub's switch for the graph, as it last said
    open: null,                // the Panel Output whose list of panels is open
    clip: null,                // nodes copied
  };
  const NG_COLOUR = { Input: '#A8452B', Converter: '#2B6CB0', Output: '#1B1813' };

  function ngLoad() {
    try {
      const saved = JSON.parse(localStorage.getItem(NG_KEY) || 'null');
      if (saved && Array.isArray(saved.nodes)) {
        NG.graph = { nodes: saved.nodes.filter((n) => PFG.TYPES[n.type] && n.p), links: Array.isArray(saved.links) ? saved.links : [] };
        if (saved.view && saved.view.z > 0) NG.view = saved.view;
      }
    } catch (_) { /* start empty */ }
  }
  function ngSave() {
    try { localStorage.setItem(NG_KEY, JSON.stringify({ nodes: NG.graph.nodes, links: NG.graph.links, view: NG.view })); } catch (_) { /* no storage */ }
  }
  const ngNode = (id) => NG.graph.nodes.find((n) => n.id === id);
  function ngNewId() {
    for (let n = NG.graph.nodes.length + 1; ; n++) if (!ngNode('n' + n)) return 'n' + n;
  }
  function ngToWorld(cx, cy) {
    const r = $('ng-stage').getBoundingClientRect();
    return [(cx - r.left - NG.view.x) / NG.view.z, (cy - r.top - NG.view.y) / NG.view.z];
  }
  function ngApplyView() {
    $('ng-world').style.transform = `translate(${NG.view.x}px, ${NG.view.y}px) scale(${NG.view.z})`;
    NG.wiresDirty = true;
  }

  // ── Compiling and sending ──────────────────────────────────────────────
  function ngStatus(text, tone) {
    const el = $('ng-status');
    el.textContent = text;
    el.style.color = tone === 'bad' ? 'var(--bad)' : tone === 'ok' ? 'var(--ok)' : '';
  }
  function ngChanged(structure) {
    ngSave();
    if (structure) ngRender();
    clearTimeout(NG.timer);
    NG.timer = setTimeout(ngApply, 180);
  }
  function ngApply(force) {
    const made = PFG.compile(NG.graph);
    NG.made = made;
    for (const [id, el] of NG.els) {
      const err = made.errors.find((e) => e.node === id);
      el.classList.toggle('bad', !!err);
      el.title = err ? err.text : '';
    }
    $('ng-hint').hidden = NG.graph.nodes.length > 0;
    if (made.errors.length) { ngStatus(made.errors[0].text, 'bad'); return; }
    const key = made.bytes ? b64(made.bytes) : '';
    if (!S.connected) { ngStatus('Not connected to a hub. The graph is sent when one is'); return; }
    if (!force && key === NG.said) { ngSaid(made); return; }
    NG.said = key;
    const r = ++NG.seq;
    NG.pending = r;
    if (!made.bytes) send({ c: 'graph', op: 'clear', r });
    else {
      send({ c: 'graph', op: 'begin', len: made.bytes.length });
      for (let at = 0; at < made.bytes.length; at += 240) send({ c: 'graph', op: 'data', d: b64(made.bytes.subarray(at, at + 240)) });
      send({ c: 'graph', op: 'end', r });
    }
    ngStatus('Sending to the hub …');
  }
  function ngSaid(made) {
    const power = $('ng-power');
    power.setAttribute('aria-pressed', String(NG.on));
    power.textContent = NG.on ? 'Nodes: On' : 'Nodes: Off';
    if (!made.bytes) {
      // On with nothing wired is a state MIDI does nothing in, and it should say so.
      if (NG.on) ngStatus('Nodes is on and nothing is wired to a Panel Output: MIDI moves nothing. Off brings the plain MIDI map back');
      else ngStatus(NG.graph.nodes.length ? 'Nothing is wired to a Panel Output yet' : '');
    } else if (!NG.on) ngStatus(`Off: the hub reads MIDI the plain way. Turn Nodes on to run this graph (${made.size} bytes)`);
    else ngStatus(`Running on the hub · ${made.size} bytes · ${made.controllers.length} MIDI controller${made.controllers.length === 1 ? '' : 's'} · plain MIDI map off`, 'ok');
  }
  // The switch: off, the hub keeps the graph and reads MIDI the plain way; on, MIDI goes through the wires only.
  function ngPower(on) {
    if (!S.connected) { toast('Not connected to a hub'); return; }
    NG.on = on;
    send({ c: 'graph', op: on ? 'on' : 'off' });
    send({ c: 'graph', op: 'get' });
    if (NG.made) ngSaid(NG.made);
  }

  // ── Copies ─────────────────────────────────────────────────────────────
  // The chosen nodes and the wires among them, to put down again.
  function ngCopy(ids) {
    const nodes = NG.graph.nodes.filter((n) => ids.has(n.id)).map((n) => JSON.parse(JSON.stringify(n)));
    const links = NG.graph.links.filter((l) => ids.has(l.a) && ids.has(l.b)).map((l) => ({ ...l }));
    return nodes.length ? { nodes, links } : null;
  }
  // Put a copy down: with its corner at (wx, wy), or a little off where it came from.
  function ngPaste(clip, wx, wy) {
    if (!clip || !clip.nodes.length) return;
    const x0 = Math.min(...clip.nodes.map((n) => n.x)), y0 = Math.min(...clip.nodes.map((n) => n.y));
    const dx = wx == null ? 30 : Math.round(wx - x0), dy = wy == null ? 30 : Math.round(wy - y0);
    const renamed = new Map();
    for (const n of clip.nodes) {
      const copy = { ...JSON.parse(JSON.stringify(n)), id: ngNewId(), x: n.x + dx, y: n.y + dy };
      NG.graph.nodes.push(copy);
      renamed.set(n.id, copy.id);
    }
    for (const l of clip.links) NG.graph.links.push({ a: renamed.get(l.a), o: l.o, b: renamed.get(l.b), i: l.i });
    NG.sel = new Set(renamed.values());
    ngChanged(true);
  }
  // A line from the hub that is the node page's.
  function ngLine(m) {
    if (m.t === 'ack' && m.c === 'graph' && m.r === NG.pending && NG.made) { ngSaid(NG.made); return; }
    if (m.t === 'err' && m.c === 'graph') { NG.said = ''; ngStatus('The hub refused it: ' + m.m, 'bad'); return; }
    if (m.t === 'graph') {
      NG.hub = m;
      NG.on = !!m.en;
      // The hub holds something this page did not send (another browser's graph, or none): say so.
      const mine = NG.made && NG.made.bytes ? NG.made.size : 0;
      if (NG.made) ngSaid(NG.made);
      if (!NG.pending && (m.on ? m.len : 0) !== mine)
        ngStatus(m.on ? `The hub holds another graph (${m.len} bytes). "Send to hub" replaces it with this one`
                      : (mine ? 'The hub does not have this graph yet. Press "Send to hub"' : ''));
      return;
    }
    if (m.t === 'nv') { NG.live = m.v; NG.liveDirty = true; return; }
    if (m.t === 'mi') {
      const learner = NG.learn && ngNode(NG.learn);
      if (learner) {
        learner.p.ch = m.ch;
        learner.p.cc = m.cc;
        learner.p.v = m.v;
        NG.learn = null;
        toast(`Learned: channel ${m.ch}, CC ${m.cc}`);
        ngChanged(true);
        return;
      }
      // A real controller moved: its fader here follows.
      for (const n of NG.graph.nodes) {
        if (n.type !== 'cc' || +n.p.ch !== m.ch || +n.p.cc !== m.cc) continue;
        n.p.v = m.v;
        const el = NG.els.get(n.id);
        const fader = el && el.querySelector('input[type="range"]');
        if (fader && document.activeElement !== fader) fader.value = m.v;
        if (el) { el.querySelector('.gn-cc-v').textContent = m.v; el.classList.add('heard'); setTimeout(() => el.classList.remove('heard'), 250); }
      }
    }
  }
  // The stage, small, in the corner: where the panels are and their
  // numbers, for choosing them in a Panel Output without going back to look.
  function ngMini() {
    const cv = $('ng-mini');
    const ps = [...S.panels.values()];
    cv.hidden = !ps.length;
    if (!ps.length) return;
    const w = cv.clientWidth || 320, h = cv.clientHeight || 190, r = Math.min(window.devicePixelRatio || 1, 2);
    if (cv.width !== Math.round(w * r) || cv.height !== Math.round(h * r)) { cv.width = Math.round(w * r); cv.height = Math.round(h * r); }
    const c = cv.getContext('2d');
    c.setTransform(r, 0, 0, r, 0, 0);
    c.clearRect(0, 0, w, h);
    let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
    for (const p of ps) { x0 = Math.min(x0, p.x); x1 = Math.max(x1, p.x); y0 = Math.min(y0, p.y); y1 = Math.max(y1, p.y); }
    const pad = 28;
    const z = Math.min((w - pad * 2) / Math.max(x1 - x0, 0.01), (h - pad * 2) / Math.max(y1 - y0, 0.01), 90);
    const ox = w / 2 - (x0 + x1) / 2 * z, oy = h / 2 - (y0 + y1) / 2 * z;
    // Each panel as its number on a dark chip, as large as its neighbours leave room for: the numbers are what this is for.
    const at = ps.map((p) => [ox + p.x * z, oy + p.y * z]);
    let near = Infinity;
    for (let a = 0; a < at.length; a++)
      for (let b = a + 1; b < at.length; b++) near = Math.min(near, Math.hypot(at[a][0] - at[b][0], (at[a][1] - at[b][1]) * 1.6));
    const size = clamp(Math.round((Number.isFinite(near) ? near : 60) * 0.5), 12, 30);
    c.lineCap = 'round';
    c.strokeStyle = '#8F877A';
    c.lineWidth = 1.5;
    for (const [i, j] of wireList()) {
      const a = S.panels.get(i), b = S.panels.get(j);
      c.beginPath();
      c.moveTo(ox + a.x * z, oy + a.y * z);
      c.lineTo(ox + b.x * z, oy + b.y * z);
      c.stroke();
    }
    c.textAlign = 'center';
    c.textBaseline = 'middle';
    c.font = `700 ${size}px ${css('--sans') || 'sans-serif'}`;
    ps.forEach((p, n) => {
      const [cx, cy] = at[n];
      const text = String(p.id), cw = Math.max(c.measureText(text).width + size * 0.7, size * 1.5), ch = size * 1.35;
      c.fillStyle = p.on ? '#1B1813' : '#B9B3A7';
      c.beginPath();
      if (c.roundRect) c.roundRect(cx - cw / 2, cy - ch / 2, cw, ch, 4); else c.rect(cx - cw / 2, cy - ch / 2, cw, ch);
      c.fill();
      c.fillStyle = '#EDE7DB';
      c.fillText(text, cx, cy + 1);
    });
  }

  function ngWatch(on) {
    if (!S.connected) return;
    const id = +$('ng-preview').value || 0;
    send(on ? { c: 'mon', on: true, ...(id ? { panel: '#' + id } : {}) } : { c: 'mon', on: false });
  }

  // ── Drawing ────────────────────────────────────────────────────────────
  const ngWired = (id, i) => NG.graph.links.find((l) => l.b === id && l.i === i);

  function ngNodeEl(node) {
    const T = PFG.TYPES[node.type];
    const el = document.createElement('div');
    el.className = 'gn' + (NG.sel.has(node.id) ? ' sel' : '');
    el.dataset.id = node.id;
    el.style.left = node.x + 'px';
    el.style.top = node.y + 'px';
    const head = document.createElement('div');
    head.className = 'gn-head';
    head.style.background = NG_COLOUR[T.group];
    head.textContent = node.type === 'cc' && node.p.label ? node.p.label : T.title;
    el.append(head);
    const body = document.createElement('div');
    body.className = 'gn-body';
    el.append(body);

    T.outs.forEach((name, o) => {
      const row = document.createElement('div');
      row.className = 'gn-row out';
      const val = document.createElement('span');
      val.className = 'gn-val num';
      val.dataset.o = o;
      const label = document.createElement('span');
      label.textContent = name;
      const sock = document.createElement('i');
      sock.className = 'sock out';
      sock.dataset.o = o;
      row.append(val, label, sock);
      body.append(row);
    });

    for (const q of T.params) {
      const row = document.createElement('div');
      row.className = 'gn-row param';
      const set = (v) => { node.p[q.key] = v; ngChanged(false); };
      if (q.kind === 'select') {
        const sel = document.createElement('select');
        for (const [value, text] of q.options) {
          const o = document.createElement('option');
          o.value = value;
          o.textContent = text;
          sel.append(o);
        }
        sel.value = node.p[q.key];
        sel.addEventListener('change', () => { node.p[q.key] = sel.value; ngChanged(true); });      // which inputs count may change
        row.append(sel);
      } else if (q.kind === 'check') {
        const label = document.createElement('label');
        label.className = 'check';
        const box = document.createElement('input');
        box.type = 'checkbox';
        box.checked = node.p[q.key] !== false;
        box.addEventListener('change', () => set(box.checked));
        label.append(box, ' ' + q.label);
        row.append(label);
      } else if (q.kind === 'cc') {
        const fader = document.createElement('input');
        fader.type = 'range';
        fader.min = 0;
        fader.max = 127;
        fader.value = node.p.v || 0;
        fader.title = 'Move it here to try the graph without the controller';
        const v = document.createElement('span');
        v.className = 'gn-cc-v num';
        v.textContent = node.p.v || 0;
        fader.addEventListener('input', () => {
          node.p.v = +fader.value;
          v.textContent = fader.value;
          send({ c: 'midi', ch: +node.p.ch, cc: +node.p.cc, v: +fader.value });
          ngSave();
        });
        const learn = document.createElement('button');
        learn.type = 'button';
        learn.className = 'btn small';
        learn.textContent = NG.learn === node.id ? 'Move it …' : 'Learn';
        learn.title = 'Press, then move a MIDI controller: its channel and number are taken';
        learn.addEventListener('click', () => { NG.learn = NG.learn === node.id ? null : node.id; ngRender(); });
        row.classList.add('cc');
        row.append(fader, v, learn);
      } else if (q.kind === 'panels') {
        // Who it is for: one line, and the list only when it is asked for.
        row.classList.add('targets');
        const to = Array.isArray(node.p.to) ? node.p.to : [];
        const pick = document.createElement('button');
        pick.type = 'button';
        pick.className = 'gn-pick';
        pick.setAttribute('aria-expanded', String(NG.open === node.id));
        pick.textContent = !to.length ? 'All panels' : to.length === 1 ? `Panel ${to[0]}` : `Panels ${to.join(', ')}`;
        pick.addEventListener('click', () => { NG.open = NG.open === node.id ? null : node.id; ngRender(); });
        row.append(pick);
        if (NG.open === node.id) {
          el.classList.add('open');
          const list = document.createElement('div');
          list.className = 'gn-pop';
          const item = (text, on, click) => {
            const label = document.createElement('label');
            label.className = 'check';
            const box = document.createElement('input');
            box.type = 'checkbox';
            box.checked = on;
            box.addEventListener('change', click);
            label.append(box, ' ' + text);
            list.append(label);
          };
          item('All panels', !to.length, () => { node.p.to = []; ngChanged(true); });
          const names = new Map(patPanels().map((p) => [p.id, p.n]));
          const ids = [...new Set([...names.keys(), ...to])].sort((a, b) => a - b);
          for (const id of ids) {
            item(`Panel ${id}${names.get(id) ? ' · ' + names.get(id) : ''}`, to.includes(id), () => {
              node.p.to = to.includes(id) ? to.filter((x) => x !== id) : [...to, id].sort((a, b) => a - b);
              ngChanged(true);
            });
          }
          row.append(list);
        }
      } else {
        if (q.label) {
          const label = document.createElement('span');
          label.textContent = q.label;
          row.append(label);
        }
        const input = document.createElement('input');
        input.type = q.kind === 'number' ? 'number' : 'text';
        if (q.kind === 'number') {
          if (q.min != null) input.min = q.min;
          if (q.max != null) input.max = q.max;
          input.step = q.step || 'any';
        }
        if (q.kind === 'formula') { input.className = 'mono'; input.spellcheck = false; input.placeholder = 'a * 0.5 + sin(t)'; }
        if (q.key === 'label') input.placeholder = 'e.g. Filter';
        input.value = node.p[q.key] == null ? '' : node.p[q.key];
        input.addEventListener('input', () => {
          set(q.kind === 'number' ? (input.value === '' ? q.def : +input.value) : input.value);
          if (q.key === 'label') head.textContent = input.value || T.title;
        });
        row.append(input);
      }
      body.append(row);
    }

    const uses = T.uses ? T.uses(node) : T.ins.length;
    T.ins.forEach((s, i) => {
      const row = document.createElement('div');
      row.className = 'gn-row in' + (i >= uses ? ' unused' : '');
      const sock = document.createElement('i');
      sock.className = 'sock in' + (ngWired(node.id, i) ? ' wired' : '');
      sock.dataset.i = i;
      const label = document.createElement('span');
      label.textContent = s.name;
      row.append(sock, label);
      if (T.sink || ngWired(node.id, i)) {
        const val = document.createElement('span');
        val.className = 'gn-val num';
        val.dataset.i = i;
        row.append(val);
      } else {
        const input = document.createElement('input');
        input.type = 'number';
        input.step = s.step || 'any';
        input.value = PFG.inputValue(node, i);
        input.addEventListener('input', () => { node.p['in' + i] = input.value === '' ? s.def : +input.value; ngChanged(false); });
        row.append(input);
      }
      body.append(row);
    });
    return el;
  }

  function ngRender() {
    const world = $('ng-world');
    world.textContent = '';
    NG.els.clear();
    for (const n of NG.graph.nodes) {
      const el = ngNodeEl(n);
      NG.els.set(n.id, el);
      world.append(el);
    }
    NG.wiresDirty = true;
    NG.liveDirty = true;
    // Which panel's values the nodes show.
    const sel = $('ng-preview');
    const panels = patPanels();
    const key = panels.map((p) => p.id).join(',');
    if (sel.dataset.key !== key) {
      const was = sel.value;
      sel.dataset.key = key;
      sel.textContent = '';
      for (const p of panels) {
        const o = document.createElement('option');
        o.value = p.id;
        o.textContent = `Values for panel ${p.id}`;
        sel.append(o);
      }
      if ([...sel.options].some((o) => o.value === was)) sel.value = was;
    }
  }

  function ngSockAt(el) {            // the centre of a socket, in the stage's own pixels
    const r = el.getBoundingClientRect(), s = $('ng-stage').getBoundingClientRect();
    return [r.left + r.width / 2 - s.left, r.top + r.height / 2 - s.top];
  }
  function ngPath(a, b) {
    const reach = Math.max(40, Math.abs(b[0] - a[0]) * 0.5);
    return `M${a[0]},${a[1]} C${a[0] + reach},${a[1]} ${b[0] - reach},${b[1]} ${b[0]},${b[1]}`;
  }
  // A wire as a run of points (the same curve ngPath draws).
  function ngCurve(a, b, steps = 24) {
    const reach = Math.max(40, Math.abs(b[0] - a[0]) * 0.5);
    const p1 = [a[0] + reach, a[1]], p2 = [b[0] - reach, b[1]];
    const pts = [];
    for (let i = 0; i <= steps; i++) {
      const t = i / steps, u = 1 - t;
      pts.push([u * u * u * a[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * b[0],
                u * u * u * a[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * b[1]]);
    }
    return pts;
  }
  function ngCross(p, q, r, s) {      // do the segments p-q and r-s cross?
    const side = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    const d1 = side(r, s, p), d2 = side(r, s, q), d3 = side(p, q, r), d4 = side(p, q, s);
    // Touching counts (a stroke through the very point where two pieces of a wire meet has to cut it);
    // lying along each other does not.
    if (d1 === 0 && d2 === 0 && d3 === 0 && d4 === 0) return false;
    return d1 * d2 <= 0 && d3 * d4 <= 0;
  }
  // The wires a stroke (points in the stage's pixels) passes over.
  function ngCutBy(stroke) {
    const hit = [];
    for (const l of NG.graph.links) {
      const from = NG.els.get(l.a), to = NG.els.get(l.b);
      const o = from && from.querySelector(`.sock.out[data-o="${l.o}"]`), i = to && to.querySelector(`.sock.in[data-i="${l.i}"]`);
      if (!o || !i) continue;
      const curve = ngCurve(ngSockAt(o), ngSockAt(i));
      let crossed = false;
      for (let a = 1; a < stroke.length && !crossed; a++)
        for (let b = 1; b < curve.length && !crossed; b++) crossed = ngCross(stroke[a - 1], stroke[a], curve[b - 1], curve[b]);
      if (crossed) hit.push(l);
    }
    return hit;
  }

  function ngDrawWires() {
    const svg = $('ng-wires');
    let d = '';
    for (const l of NG.graph.links) {
      const from = NG.els.get(l.a), to = NG.els.get(l.b);
      const o = from && from.querySelector(`.sock.out[data-o="${l.o}"]`), i = to && to.querySelector(`.sock.in[data-i="${l.i}"]`);
      if (o && i) d += ngPath(ngSockAt(o), ngSockAt(i)) + ' ';
    }
    svg.querySelector('.wires').setAttribute('d', d);
    const w = NG.drag && NG.drag.kind === 'wire' ? NG.drag : null;
    let loose = '';
    if (w) {
      const s = $('ng-stage').getBoundingClientRect();
      const end = [w.x - s.left, w.y - s.top];
      const fixed = NG.els.get(w.node) && NG.els.get(w.node).querySelector(w.out ? `.sock.out[data-o="${w.n}"]` : `.sock.in[data-i="${w.n}"]`);
      if (fixed) loose = w.out ? ngPath(ngSockAt(fixed), end) : ngPath(end, ngSockAt(fixed));
    }
    svg.querySelector('.loose').setAttribute('d', loose);
    const cut = NG.drag && NG.drag.kind === 'cut' ? NG.drag.pts : null;
    svg.querySelector('.cut').setAttribute('d', cut && cut.length > 1 ? 'M' + cut.map((p) => p.join(',')).join(' L') : '');
  }
  function ngShowLive() {
    const made = NG.made, live = NG.live;
    const text = (r) => (live && r >= 0 && r < live.length ? (Math.round(live[r] * 100) / 100).toString() : '');
    for (const [id, el] of NG.els) {
      const outs = made && made.regs[id], ins = made && made.ins[id];
      for (const span of el.querySelectorAll('.gn-val')) {
        if (span.dataset.o != null) span.textContent = outs ? text(outs[+span.dataset.o]) : '';
        else span.textContent = ins ? text(ins[+span.dataset.i]) : '';
      }
    }
  }
  // Once a frame, while the page is up.
  function ngFrame() {
    if (NG.wiresDirty) { NG.wiresDirty = false; ngDrawWires(); }
    if (NG.liveDirty) { NG.liveDirty = false; ngShowLive(); }
  }

  // ── Adding, removing, wiring ───────────────────────────────────────────
  function ngAdd(type, wx, wy) {
    const node = PFG.create(type, ngNewId(), Math.round(wx), Math.round(wy));
    if (!node) return null;
    NG.graph.nodes.push(node);
    NG.sel = new Set([node.id]);
    ngChanged(true);
    return node;
  }
  function ngRemove(ids) {
    NG.graph.nodes = NG.graph.nodes.filter((n) => !ids.has(n.id));
    NG.graph.links = NG.graph.links.filter((l) => !ids.has(l.a) && !ids.has(l.b));
    NG.sel.clear();
    if (NG.learn && ids.has(NG.learn)) NG.learn = null;
    ngChanged(true);
  }
  function ngLink(a, o, b, i) {
    if (a === b) return;
    NG.graph.links = NG.graph.links.filter((l) => !(l.b === b && l.i === i));       // an input takes one wire
    NG.graph.links.push({ a, o, b, i });
    ngChanged(true);
  }
  function ngMenu(cx, cy) {
    const menu = $('ng-menu');
    const [wx, wy] = ngToWorld(cx, cy);
    menu.textContent = '';
    let group = '';
    for (const [type, T] of Object.entries(PFG.TYPES)) {
      if (T.group !== group) {
        group = T.group;
        const h = document.createElement('h3');
        h.textContent = group;
        menu.append(h);
      }
      const b = document.createElement('button');
      b.type = 'button';
      b.textContent = T.title;
      b.addEventListener('click', () => { menu.hidden = true; ngAdd(type, wx - 20, wy - 14); });
      menu.append(b);
    }
    menu.hidden = false;
    const s = $('ng-stage').getBoundingClientRect();
    menu.style.left = clamp(cx - s.left, 4, Math.max(4, s.width - menu.offsetWidth - 4)) + 'px';
    menu.style.top = clamp(cy - s.top, 4, Math.max(4, s.height - menu.offsetHeight - 4)) + 'px';
  }
  function ngFit() {
    if (!NG.graph.nodes.length) { NG.view = { x: 60, y: 40, z: 1 }; ngApplyView(); return; }
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    for (const n of NG.graph.nodes) {
      const el = NG.els.get(n.id);
      x0 = Math.min(x0, n.x); y0 = Math.min(y0, n.y);
      x1 = Math.max(x1, n.x + (el ? el.offsetWidth : 180)); y1 = Math.max(y1, n.y + (el ? el.offsetHeight : 120));
    }
    const s = $('ng-stage').getBoundingClientRect();
    const z = clamp(Math.min((s.width - 60) / (x1 - x0), (s.height - 60) / (y1 - y0)), 0.3, 1);
    NG.view = { z, x: (s.width - (x1 - x0) * z) / 2 - x0 * z, y: (s.height - (y1 - y0) * z) / 2 - y0 * z };
    ngApplyView();
    ngSave();
  }
  // Something to start from: a controller to every panel, another shaped on its way, a wave across the room.
  function ngExample() {
    const G = { nodes: [], links: [] };
    const add = (type, x, y, p) => { const n = PFG.create(type, 'n' + (G.nodes.length + 1), x, y); Object.assign(n.p, p || {}); G.nodes.push(n); return n; };
    const wire = (a, o, b, i) => G.links.push({ a: a.id, o, b: b.id, i });
    const c1 = add('cc', 0, 0, { label: 'Controller 1', ch: 1, cc: 21 });
    const c2 = add('cc', 0, 225, { label: 'Controller 2', ch: 1, cc: 22 });
    const map = add('map', 260, 215, { in3: 0.2, in4: 0.8 });
    const smooth = add('smooth', 520, 250, { in1: 0.5 });
    const panel = add('panel', 0, 450);
    const lfo = add('lfo', 260, 480, { wave: 'sine', in0: 0.25 });
    const out = add('out', 780, 110);
    wire(c1, 0, out, 0);
    wire(c2, 0, map, 0);
    wire(map, 0, smooth, 0);
    wire(smooth, 0, out, 1);
    wire(panel, 1, lfo, 1);
    wire(lfo, 0, out, 2);
    NG.graph = G;
    NG.sel.clear();
    ngChanged(true);
    requestAnimationFrame(ngFit);
  }

  function ngOpen() {
    $('ng-hint').hidden = NG.graph.nodes.length > 0;
    ngRender();
    ngApplyView();
    NG.pending = 0;
    NG.made = PFG.compile(NG.graph);
    if (S.connected) { send({ c: 'graph', op: 'get' }); ngWatch(true); }
    if (S.connected && S.mode === 'demo') { NG.said = ''; ngApply(true); }      // the demo's hub starts with none
    if (NG.made.errors.length) ngStatus(NG.made.errors[0].text, 'bad');
  }

  function wireNodes() {
    ngLoad();
    const stage = $('ng-stage');
    const ngCapture = (ev) => { try { stage.setPointerCapture(ev.pointerId); } catch (_) { /* not every pointer can be held */ } };
    $('page-nodes').addEventListener('click', () => setPage('nodes'));
    $('ng-add').addEventListener('click', (ev) => { const r = ev.target.getBoundingClientRect(); ngMenu(r.left, r.bottom + 6); });
    $('ng-fit').addEventListener('click', ngFit);
    $('ng-send').addEventListener('click', () => ngApply(true));
    $('ng-power').addEventListener('click', () => ngPower(!NG.on));
    $('ng-example').addEventListener('click', ngExample);
    $('ng-hint-example').addEventListener('click', ngExample);
    $('ng-clear').addEventListener('click', () => { NG.graph = { nodes: [], links: [] }; NG.sel.clear(); ngChanged(true); });
    $('ng-preview').addEventListener('change', () => ngWatch(true));
    $('ng-export').addEventListener('click', () => {
      const url = URL.createObjectURL(new Blob([JSON.stringify({ nodes: NG.graph.nodes, links: NG.graph.links }, null, 1)], { type: 'application/json' }));
      const a = document.createElement('a');
      a.href = url;
      a.download = 'patternflow-graph.json';
      a.click();
      setTimeout(() => URL.revokeObjectURL(url), 2000);
    });
    $('ng-import').addEventListener('change', async (ev) => {
      const f = ev.target.files[0];
      ev.target.value = '';
      if (!f) return;
      try {
        const g = JSON.parse(await f.text());
        if (!Array.isArray(g.nodes)) throw new Error('nodes');
        NG.graph = { nodes: g.nodes.filter((n) => PFG.TYPES[n.type] && n.p), links: Array.isArray(g.links) ? g.links : [] };
        NG.sel.clear();
        ngChanged(true);
        requestAnimationFrame(ngFit);
      } catch (_) { toast('That is not a graph file'); }
    });

    stage.addEventListener('pointerenter', () => { NG.mouse.over = true; });
    stage.addEventListener('pointerleave', () => { NG.mouse.over = false; });
    stage.addEventListener('pointerdown', (ev) => {
      NG.mouse.x = ev.clientX; NG.mouse.y = ev.clientY;
      if (ev.target.closest('#ng-menu')) return;
      $('ng-menu').hidden = true;
      if (NG.open && !ev.target.closest('.gn-row.targets')) { NG.open = null; ngRender(); return; }
      if (ev.target.closest('.gn-row.targets')) return;
      // Cut Links, as in Blender: hold Ctrl and drag the right button across wires
      // (or the left button, starting on empty space).
      if (ev.ctrlKey && (ev.button === 2 || (ev.button === 0 && !ev.target.closest('.gn')))) {
        const r = stage.getBoundingClientRect();
        NG.drag = { kind: 'cut', pts: [[ev.clientX - r.left, ev.clientY - r.top]] };
        stage.classList.add('cutting');
        ngCapture(ev);
        ev.preventDefault();
        return;
      }
      const sock = ev.target.closest('.sock');
      const nodeEl = ev.target.closest('.gn');
      if (sock && nodeEl && ev.button === 0) {
        const id = nodeEl.dataset.id;
        if (sock.classList.contains('out')) NG.drag = { kind: 'wire', node: id, n: +sock.dataset.o, out: true, x: ev.clientX, y: ev.clientY };
        else {
          const was = ngWired(id, +sock.dataset.i);
          if (was) {                                 // pulled off: the wire comes along from its source
            NG.graph.links = NG.graph.links.filter((l) => l !== was);
            ngChanged(true);
            NG.drag = { kind: 'wire', node: was.a, n: was.o, out: true, x: ev.clientX, y: ev.clientY };
          } else NG.drag = { kind: 'wire', node: id, n: +sock.dataset.i, out: false, x: ev.clientX, y: ev.clientY };
        }
        ngCapture(ev);
        ev.preventDefault();
        NG.wiresDirty = true;
        return;
      }
      if (nodeEl) {
        const id = nodeEl.dataset.id;
        if (ev.shiftKey) { if (NG.sel.has(id)) NG.sel.delete(id); else NG.sel.add(id); }
        else if (!NG.sel.has(id)) NG.sel = new Set([id]);
        for (const [nid, el] of NG.els) el.classList.toggle('sel', NG.sel.has(nid));
        if (ev.target.closest('.gn-head') && ev.button === 0) {
          const [wx, wy] = ngToWorld(ev.clientX, ev.clientY);
          NG.drag = { kind: 'node', wx, wy, start: [...NG.sel].map((nid) => { const n = ngNode(nid); return [n, n.x, n.y]; }) };
          ngCapture(ev);
          ev.preventDefault();
        }
        return;
      }
      if (ev.button === 0 || ev.button === 1) {
        NG.drag = { kind: 'pan', sx: ev.clientX, sy: ev.clientY, vx: NG.view.x, vy: NG.view.y, moved: false };
        ngCapture(ev);
        if (ev.button === 1) ev.preventDefault();
      }
    });
    stage.addEventListener('pointermove', (ev) => {
      NG.mouse.x = ev.clientX; NG.mouse.y = ev.clientY;
      const d = NG.drag;
      if (!d) return;
      if (d.kind === 'pan') {
        if (Math.abs(ev.clientX - d.sx) + Math.abs(ev.clientY - d.sy) > 3) d.moved = true;
        NG.view.x = d.vx + ev.clientX - d.sx;
        NG.view.y = d.vy + ev.clientY - d.sy;
        ngApplyView();
      } else if (d.kind === 'node') {
        const [wx, wy] = ngToWorld(ev.clientX, ev.clientY);
        for (const [n, x, y] of d.start) {
          n.x = Math.round(x + wx - d.wx);
          n.y = Math.round(y + wy - d.wy);
          const el = NG.els.get(n.id);
          if (el) { el.style.left = n.x + 'px'; el.style.top = n.y + 'px'; }
        }
        NG.wiresDirty = true;
      } else if (d.kind === 'wire') {
        d.x = ev.clientX; d.y = ev.clientY;
        NG.wiresDirty = true;
      } else if (d.kind === 'cut') {
        const r = stage.getBoundingClientRect();
        const p = [ev.clientX - r.left, ev.clientY - r.top], last = d.pts[d.pts.length - 1];
        if (Math.abs(p[0] - last[0]) + Math.abs(p[1] - last[1]) > 3) { d.pts.push(p); NG.wiresDirty = true; }
      }
    });
    const drop = (ev) => {
      const d = NG.drag;
      NG.drag = null;
      NG.wiresDirty = true;
      if (!d) return;
      if (d.kind === 'cut') {
        stage.classList.remove('cutting');
        NG.cutAt = performance.now();              // the right button's menu is not wanted after this
        const gone = ev.type === 'pointerup' ? ngCutBy(d.pts) : [];
        if (gone.length) {
          NG.graph.links = NG.graph.links.filter((l) => !gone.includes(l));
          ngChanged(true);
        }
      } else if (d.kind === 'pan') {
        if (!d.moved && NG.sel.size) { NG.sel.clear(); for (const el of NG.els.values()) el.classList.remove('sel'); }
        ngSave();
      } else if (d.kind === 'node') ngSave();
      else if (d.kind === 'wire' && ev.type === 'pointerup') {
        const hit = document.elementFromPoint(ev.clientX, ev.clientY);
        const sock = hit && hit.closest('.sock');
        const el = hit && hit.closest('.gn');
        if (sock && el) {
          if (d.out && sock.classList.contains('in')) ngLink(d.node, d.n, el.dataset.id, +sock.dataset.i);
          else if (!d.out && sock.classList.contains('out')) ngLink(el.dataset.id, +sock.dataset.o, d.node, d.n);
        } else if (el && d.out) {
          // Dropped on a node, not on a socket: its first free input.
          const T = PFG.TYPES[ngNode(el.dataset.id).type];
          const free = T.ins.findIndex((_, i) => !ngWired(el.dataset.id, i));
          if (free >= 0) ngLink(d.node, d.n, el.dataset.id, free);
        }
      }
    };
    stage.addEventListener('pointerup', drop);
    stage.addEventListener('pointercancel', drop);
    stage.addEventListener('contextmenu', (ev) => {
      ev.preventDefault();
      if (ev.ctrlKey || (NG.drag && NG.drag.kind === 'cut') || performance.now() - (NG.cutAt || 0) < 400) return;      // that was a cut
      if (!ev.target.closest('.gn')) ngMenu(ev.clientX, ev.clientY);
    });
    stage.addEventListener('dblclick', (ev) => { if (!ev.target.closest('.gn') && !ev.target.closest('#ng-menu')) ngMenu(ev.clientX, ev.clientY); });
    stage.addEventListener('wheel', (ev) => {
      if (ev.target.closest('#ng-menu')) return;
      ev.preventDefault();
      const s = stage.getBoundingClientRect();
      const mx = ev.clientX - s.left, my = ev.clientY - s.top;
      const z = clamp(NG.view.z * Math.exp(-ev.deltaY * 0.0012), 0.3, 1.6);
      NG.view.x = mx - (mx - NG.view.x) * z / NG.view.z;
      NG.view.y = my - (my - NG.view.y) * z / NG.view.z;
      NG.view.z = z;
      ngApplyView();
      throttled('ngSave', ngSave, 400);
    }, { passive: false });
    document.addEventListener('keydown', (ev) => {
      if (S.page !== 'nodes' || $('help').open) return;
      const el = document.activeElement;
      if (el && /^(INPUT|TEXTAREA|SELECT)$/.test(el.tagName)) { if (ev.key === 'Escape') el.blur(); return; }
      const key = ev.key.toLowerCase();
      if (key === 'escape') { $('ng-menu').hidden = true; NG.learn = null; if (NG.open) { NG.open = null; ngRender(); } return; }
      if (!NG.mouse.over && document.activeElement !== stage) return;
      const cmdKey = ev.ctrlKey || ev.metaKey;
      const under = NG.mouse.over ? ngToWorld(NG.mouse.x, NG.mouse.y) : [null, null];
      if (key === 'a' && ev.shiftKey) { ev.preventDefault(); ngMenu(NG.mouse.x, NG.mouse.y); }
      else if (key === 'c' && cmdKey) { if (NG.sel.size) { NG.clip = ngCopy(NG.sel); toast(`Copied ${NG.clip.nodes.length} node${NG.clip.nodes.length === 1 ? '' : 's'}`); } }
      else if (key === 'x' && cmdKey) { if (NG.sel.size) { ev.preventDefault(); NG.clip = ngCopy(NG.sel); ngRemove(new Set(NG.sel)); } }
      else if (key === 'v' && cmdKey) { if (NG.clip) { ev.preventDefault(); ngPaste(NG.clip, under[0], under[1]); } }
      else if (key === 'd' && ev.shiftKey) { if (NG.sel.size) { ev.preventDefault(); ngPaste(ngCopy(NG.sel)); } }
      else if ((key === 'x' || key === 'delete' || key === 'backspace') && NG.sel.size) { ev.preventDefault(); ngRemove(new Set(NG.sel)); }
      else if (key === 'home') ngFit();
      else if (key === 'a' && !cmdKey) { NG.sel = new Set(ev.altKey ? [] : NG.graph.nodes.map((n) => n.id)); for (const [id, e2] of NG.els) e2.classList.toggle('sel', NG.sel.has(id)); }
    });
    new ResizeObserver(() => { NG.wiresDirty = true; }).observe(stage);
  }

  // ── Wiring the controls ────────────────────────────────────────────────
  function wire() {
    $('conn-demo').addEventListener('click', () => connect('demo'));
    $('conn-usb').addEventListener('click', () => connect('usb'));
    $('conn-wifi').addEventListener('click', () => {
      let host = '192.168.66.1';
      try { host = localStorage.getItem('pfe-host') || host; } catch (_) { /* no storage here */ }
      const row = document.createElement('form');
      row.className = 'conn';
      row.innerHTML = `<input type="text" id="wifi-host" value="${host}" aria-label="Hub address" style="width:150px">` +
        `<button type="submit" class="btn small primary">Connect</button>`;
      const old = $('wifi-host');
      if (old) old.closest('form').remove();
      $('conn-status').after(row);
      row.querySelector('input').focus();
      row.addEventListener('submit', (ev) => {
        ev.preventDefault();
        const h = row.querySelector('input').value.trim();
        row.remove();
        try { localStorage.setItem('pfe-host', h); } catch (_) { /* no storage here */ }
        connect('wifi', h);
      });
    });
    if (!('serial' in navigator)) {
      $('conn-usb').title = 'Needs a browser with Web Serial (Chrome, Edge), with this file opened directly';
    }

    $('cfg-speed').addEventListener('input', (ev) => {
      const v = +ev.target.value;
      $('cfg-speed-v').textContent = `${v} units/s`;
      throttled('speed', () => send({ c: 'cfg', speed: v }));
    });
    $('cfg-glide').addEventListener('input', (ev) => {
      const v = +ev.target.value;
      $('cfg-glide-v').textContent = `${v} ms`;
      throttled('glide', () => send({ c: 'cfg', glide: v }));
    });
    $('cfg-k4-pattern').addEventListener('click', () => send({ c: 'cfg', k4: true }));
    $('cfg-k4-knob').addEventListener('click', () => send({ c: 'cfg', k4: false }));
    for (const [id, key] of [['cfg-pmin', 'pmin'], ['cfg-pmax', 'pmax'], ['cfg-k4s', 'k4s']])
      $(id).addEventListener('change', (ev) => send({ c: 'cfg', [key]: +ev.target.value }));
    $('cfg-lock').addEventListener('change', (ev) => send({ c: 'cfg', lock: ev.target.checked }));

    $('demo-count').addEventListener('input', (ev) => { $('demo-count-v').textContent = `${ev.target.value}`; });
    $('demo-count').addEventListener('change', (ev) => {
      if (S.mode !== 'demo' || !S.transport) return;
      resetState();
      S.transport.boot(+ev.target.value);
    });
    for (const id of ['demo-lat', 'demo-loss'])
      $(id).addEventListener('input', () => {
        $('demo-lat-v').textContent = `${$('demo-lat').value} ms`;
        $('demo-loss-v').textContent = `${$('demo-loss').value} %`;
        if (S.mode === 'demo' && S.transport && S.transport.x) S.transport.applyNet();
      });
    $('demo-hub-restart').addEventListener('click', () => {
      if (S.mode !== 'demo' || !S.transport || !S.transport.x) return;
      S.transport.x.pfe_restart_hub();
      S.transport.send('{"c":"hello"}');
      log('', 'The hub was switched off and on. The layout and the rules come back from what was saved, and the panels go on showing what they showed.');
    });

    $('map-fit').addEventListener('click', () => (S.view === '3d' ? frame3() : fit()));
    $('view-2d').addEventListener('click', () => setView('2d'));
    $('view-3d').addEventListener('click', () => setView('3d'));
    $('map-arrange').addEventListener('change', (ev) => {
      if (ev.target.value) arrange(ev.target.value);
      ev.target.value = '';
    });
    // One button for all of them, on and off. What it says follows the
    // panels (any of them showing its colour = "off" is the next press), so
    // it is right after a reload, from a second console, or in the demo.
    $('map-identify').addEventListener('click', () => {
      send({ c: 'identify', uid: 'all', on: !anyIdentified() });
    });

    // Every connected panel's address, one a line, in the order of their
    // numbers: what a loop that pushes firmware to each of them reads.
    $('map-addresses').addEventListener('click', async () => {
      const list = [...S.panels.values()].filter((p) => p.on).sort((a, b) => a.id - b.id);
      if (!list.length) { toast('No panels are connected.'); return; }
      const text = list.map((p) => p.ip).join('\n');
      for (const p of list) log('', `Panel ${p.id}  ${p.ip}  ${p.uid}  ${p.fw || '?'} ${p.var || ''}`);
      try {
        await navigator.clipboard.writeText(text + '\n');
        toast(`Copied the address of ${nOf(list.length, 'panel')}.`);
      } catch (_) {
        toast('Could not copy. The addresses are in the log below.');
      }
    });

    for (const sc of ['one', 'turn', 'wave', 'all'])
      $('scope-' + sc).addEventListener('click', () => {
        S.scope = sc;
        setPressed(['scope-one', 'scope-turn', 'scope-wave', 'scope-all'], 'scope-' + sc);
      });
    // Wires switched on is somebody about to play along them: the side column then acts as a turn.
    $('wired-on').addEventListener('click', () => $('scope-turn').click());
    $('p-pattern').addEventListener('change', (ev) => { stepHeld.delete('p-pattern'); setFromConsole({ pat: +ev.target.value }); });
    $('all-pattern').addEventListener('change', (ev) => { stepHeld.delete('all-pattern'); send({ c: 'set', to: 'all', pat: +ev.target.value }); });
    for (const [id, by] of [['prev', -1], ['next', 1]]) {
      $(`p-pattern-${id}`).addEventListener('click', () => stepPattern($('p-pattern'), by, (pat) => setFromConsole({ pat })));
      $(`all-pattern-${id}`).addEventListener('click', () => stepPattern($('all-pattern'), by, (pat) => send({ c: 'set', to: 'all', pat })));
    }
    $('all-bright').addEventListener('input', (ev) => {
      const v = +ev.target.value;
      $('all-bright-v').textContent = v;
      throttled('allBright', () => send({ c: 'set', to: 'all', br: v }), 120);
    });
    const edit = (what) => { const uid = selUid(); if (uid) send({ c: 'panel', uid, ...what }); };
    $('p-id').addEventListener('change', (ev) => edit({ id: clamp(Math.round(+ev.target.value) || 1, 1, 250) }));
    $('p-name').addEventListener('change', (ev) => edit({ name: ev.target.value }));
    $('p-x').addEventListener('change', (ev) => edit({ x: +ev.target.value || 0 }));
    $('p-y').addEventListener('change', (ev) => edit({ y: +ev.target.value || 0 }));
    $('p-z').addEventListener('change', (ev) => edit({ z: +ev.target.value || 0 }));
    for (const a of ['rx', 'ry', 'rz']) $('p-' + a).addEventListener('change', (ev) => edit({ [a]: +ev.target.value || 0 }));
    $('p-linked').addEventListener('change', (ev) => edit({ linked: ev.target.checked }));
    $('p-unwire').addEventListener('click', () => { const uid = selUid(); if (uid) send({ c: 'wire', clear: true, a: uid }); });
    // Number, name, place and turn: seldom looked at, so folded away, and as they were left.
    try { $('insp-more').open = localStorage.getItem('pfe-insp-more') === '1'; } catch (_) { /* no storage here */ }
    $('insp-more').addEventListener('toggle', () => {
      try { localStorage.setItem('pfe-insp-more', $('insp-more').open ? '1' : '0'); } catch (_) { /* no storage here */ }
    });
    $('map-wire').addEventListener('click', () => {
      S.wireMode = !S.wireMode;
      $('map-wire').setAttribute('aria-pressed', String(S.wireMode));
      canvas.style.cursor = S.wireMode ? 'crosshair' : '';
    });
    // What is asked of the chosen panel is asked of every chosen one; forgetting is for one at a time.
    $('p-identify').addEventListener('click', () => {
      const p = S.panels.get(S.sel);
      if (p) for (const q of pickedPanels()) send({ c: 'identify', uid: q.uid, on: !p.idf });
    });
    $('p-sleep').addEventListener('click', () => {
      const p = S.panels.get(S.sel);
      if (p) for (const q of pickedPanels()) send({ c: 'set', to: q.uid, sleep: !p.sl });
    });
    $('all-sleep').addEventListener('click', () => {
      const ps = [...S.panels.values()].filter((p) => p.on);
      send({ c: 'set', to: 'all', sleep: !(ps.length > 0 && ps.every((p) => p.sl)) });
    });
    $('p-reboot').addEventListener('click', () => { for (const q of pickedPanels()) send({ c: 'reboot', uid: q.uid }); });
    $('p-forget').addEventListener('click', () => { const uid = selUid(); if (uid) send({ c: 'forget', uid }); });
    $('p-power').addEventListener('click', () => {
      const uid = selUid();
      const t = S.transport;
      if (!uid || !t || t.name !== 'demo') return;
      const sim = t.sim(uid);
      if (sim < 0) return;
      t.x.pfe_power(sim, t.x.pfe_powered(sim) ? 0 : 1);
      renderInspector();
    });

    $('log-clear').addEventListener('click', () => { logEl.textContent = ''; });
    $('log-form').addEventListener('submit', (ev) => {
      ev.preventDefault();
      const text = $('log-input').value.trim();
      if (!text) return;
      try { JSON.parse(text); } catch (_) { toast('It has to be one line of JSON, for example {"c":"get"}'); return; }
      log('me', '→ ' + text);
      if (S.transport) S.transport.send(text);
      $('log-input').value = '';
    });

    wirePatterns();
    wireSetup();
    wireShows();
    wireShowPage();
    wireNodes();

    // Help: what is easy to forget (the MIDI numbers, the map's keys).
    const help = $('help');
    const helpTabs = ['midi', 'map', 'patterns', 'nodes'];
    const showHelp = (tab) => {
      setPressed(helpTabs.map((t) => 'help-tab-' + t), 'help-tab-' + tab);
      for (const t of helpTabs) $('help-' + t).hidden = t !== tab;
      help.scrollTop = 0;
    };
    for (const t of helpTabs) $('help-tab-' + t).addEventListener('click', () => showHelp(t));
    $('help-open').addEventListener('click', () => {
      showHelp(S.page === 'nodes' ? 'nodes' : S.page === 'patterns' || S.page === 'shows' ? 'patterns' : help.dataset.last || 'midi');
      help.showModal();
    });
    $('help-close').addEventListener('click', () => help.close());
    help.addEventListener('click', (ev) => { if (ev.target === help) help.close(); });      // beside the window
    help.addEventListener('close', () => { help.dataset.last = helpTabs.find((t) => !$('help-' + t).hidden) || 'midi'; });

    new ResizeObserver(() => { resize(); S.cam.fitted = false; }).observe(canvas);
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', readColors);
    new MutationObserver(readColors).observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });
  }

  function frame(now) {
    if (S.transport && S.transport.tick) S.transport.tick();
    if (S.page === 'nodes') ngFrame();
    draw(now);
    // A hub that has gone quiet (unplugged, out of range) should not look alive.
    if (S.connected && S.mode !== 'demo' && S.hub && !S.quiet && now - S.lastHubLineAt > 4000) {
      S.quiet = true;
      status('The hub has gone quiet', 'warn');
    }
    requestAnimationFrame(frame);
  }

  // ── Start ──────────────────────────────────────────────────────────────
  buildRules();
  buildInspector();
  wire();
  resize();
  let lastView = '2d';
  try { lastView = localStorage.getItem('pfe-view') || '2d'; } catch (_) { /* no storage here */ }
  setView(lastView);
  if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) $('demo-auto').checked = false;
  // Served by the hub itself: talk to it. Opened from a file or anywhere
  // else: start in the demo.
  const fromHub = location.protocol === 'http:' && location.hostname && !/^(localhost|127\.|\[::1\])/.test(location.hostname) &&
                  document.querySelector('meta[name="pfe-hub"]');
  connect(fromHub ? 'wifi' : 'demo', fromHub ? location.host : undefined);
  requestAnimationFrame(frame);
})();

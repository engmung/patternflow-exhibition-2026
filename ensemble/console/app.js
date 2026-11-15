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
  const LINK_LABEL = { solo: '단독', sync: '동시', ripple: '물결' };
  const CHANNEL_LABEL = ['K1', 'K2', 'K3', 'K4', '패턴'];
  const TILE_W = 0.84, TILE_H = 0.42;   // a panel on the map, in map units (128 x 64 LEDs)
  // The mark a panel shows while it is identified, by its number: a shape and
  // a colour, both stepping with the number (1 red circle, 2 green square,
  // 3 blue triangle, 4 yellow circle, ...; twelve different marks). The same
  // lists as core/pfe_proto.h identifyMark (tools/build_console.py --check
  // holds them together).
  const IDENTIFY_COLOURS = [[255, 0, 0], [0, 220, 0], [0, 80, 255], [255, 210, 0]];
  const IDENTIFY_COLOUR_NAMES = ['빨강', '초록', '파랑', '노랑'];
  const IDENTIFY_SHAPES = ['circle', 'square', 'triangle'];
  const IDENTIFY_SHAPE_NAMES = ['원', '사각형', '삼각형'];
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
    if (i == null || i < 0) return '모름';
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
        if (!window.PFE_SIM_WASM) throw new Error('pfe_sim.wasm.js가 없습니다');
        const bin = Uint8Array.from(atob(window.PFE_SIM_WASM), (c) => c.charCodeAt(0));
        const { instance } = await WebAssembly.instantiate(bin, {});
        this.x = instance.exports;
      } catch (e) {
        // Some frames refuse WebAssembly. A page may carry the same module
        // as plain JavaScript (binaryen's wasm2js) for those; the copy in
        // the repository does not, and says so instead.
        if (typeof window.PFE_SIM_JS !== 'function') {
          throw new Error('이 화면에서는 데모를 돌릴 수 없습니다 (WebAssembly가 막혀 있습니다). ensemble/console/index.html을 브라우저에서 직접 여세요.');
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
      if (!('serial' in navigator)) throw new Error('이 브라우저에는 Web Serial이 없습니다. Chrome이나 Edge에서 이 파일을 여세요.');
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
        if (!this.closing) dropped('USB 연결이 끊겼습니다: ' + e.message);
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
        ws.onerror = () => { if (!opened) reject(new Error(`${this.host}에 연결하지 못했습니다`)); };
        ws.onclose = () => { if (opened && !this.closing) dropped('Wi-Fi 연결이 끊겼습니다'); };
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
    status('끊김', 'bad');
    log('err', why);
    toast(why);
  }

  async function connect(mode, arg) {
    if (S.transport) { try { await S.transport.stop(); } catch (_) { /* gone */ } }
    S.transport = null;
    S.connected = false;
    relayDropped();
    shows.clear();
    if (REC.on) { REC.on = false; clearInterval(REC.timer); REC.panels.clear(); }
    P.panels.clear();
    P.presets = null;
    resetState();
    S.mode = mode;
    setPressed(['conn-demo', 'conn-usb', 'conn-wifi'], 'conn-' + mode);
    $('demo-box').hidden = mode !== 'demo';
    status('연결하는 중', 'warn');
    const t = mode === 'demo' ? new DemoTransport() : mode === 'usb' ? new SerialTransport() : new SocketTransport(arg);
    if (mode === 'demo') t.count = +$('demo-count').value;
    try {
      S.transport = t;
      await t.start();
      S.connected = true;
      status(mode === 'demo' ? '데모: 가상 패널' : mode === 'usb' ? 'USB로 허브에 연결됨' : `Wi-Fi: ${arg}`, 'ok');
      log('', mode === 'demo' ? '데모를 시작했습니다. 하드웨어 없이 허브와 패널의 코드를 그대로 돌립니다.' : '허브에 연결했습니다.');
    } catch (e) {
      S.transport = null;
      status('연결 안 됨', 'bad');
      // A page inside a preview frame is refused the serial port outright;
      // a person who closes the port chooser picked nothing.
      const why = e.name === 'NotFoundError' ? '포트를 고르지 않았습니다.'
        : e.name === 'SecurityError' || e.name === 'NotAllowedError'
          ? '이 화면에서는 USB 포트를 열 수 없습니다. 저장소의 ensemble/console/index.html을 Chrome이나 Edge에서 직접 여세요.'
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
    if ($('log-raw').checked && m.t !== 'p' && m.t !== 'hub' && m.t !== 'hb' && m.t !== 'hk') log('raw', '← ' + text);
    switch (m.t) {
      case 'hello':
        log('', `허브: 프로토콜 ${m.proto}${m.fw ? ', 펌웨어 ' + m.fw : ''}${m.ssid ? ', Wi-Fi ' + m.ssid : ''}`);
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
      case 'hub':
        S.hub = m;
        S.lastHubLineAt = performance.now();
        if (S.quiet) { S.quiet = false; status(S.mode === 'usb' ? 'USB로 허브에 연결됨' : 'Wi-Fi로 허브에 연결됨', 'ok'); }
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
        if (S.page === 'shows' && (!prev || prev.on !== m.on || prev.n !== m.n || ((prev.fl ^ m.fl) & RF_SOLO))) throttled('libPanels', renderLib, 300);
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
        S.waves.push({ x: m.x, y: m.y, z: m.z || 0, c: m.c, at, now: !!m.now, d: m.d, speed: S.cfg ? S.cfg.speed : 2, o: m.o });
        if (S.waves.length > 60) S.waves.shift();
        break;
      }
      case 'log': log('', m.m); break;
      case 'err': log('err', `${m.c}: ${m.m}`); toast(m.m); break;
      case 'ack': break;
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
      seg.setAttribute('aria-label', CHANNEL_LABEL[c] + '의 연결');
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
    $(`link-3-solo`).closest('.row').hidden = !!cfg.k4;   // K4 has no value to share while it switches patterns
    if (!busy($('cfg-speed'))) $('cfg-speed').value = cfg.speed;
    $('cfg-speed-v').textContent = `${cfg.speed} 칸/초`;
    if (!busy($('cfg-glide'))) $('cfg-glide').value = cfg.glide;
    $('cfg-glide-v').textContent = `${cfg.glide} ms`;
    setPressed(['cfg-k4-pattern', 'cfg-k4-knob'], cfg.k4 ? 'cfg-k4-pattern' : 'cfg-k4-knob');
    $('k4-range').hidden = !cfg.k4;
    // While K4 switches patterns, the pattern row is K4's row: say so.
    $('rule-lab-4').textContent = cfg.k4 ? 'K4 패턴' : '패턴';
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
      `<span>패널 <b>${h.online}/${h.panels}</b></span>` +
      `<span>이동 중인 값 <b>${h.fly}</b></span>` +
      `<span>받음 <b>${h.rx}</b> · 보냄 <b>${h.tx}</b></span>` +
      `<span>깨진 패킷 <b>${h.bad}</b> · 늦게 온 보고 <b>${h.stale}</b></span>` +
      `<span>허브 가동 <b>${time}</b></span>`;
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
        `<button type="button" class="btn small" id="p-hand-${k}-down" aria-label="K${k + 1} 왼쪽으로 한 클릭">◀ 왼쪽</button>` +
        `<button type="button" class="btn small" id="p-hand-${k}-up" aria-label="K${k + 1} 오른쪽으로 한 클릭">오른쪽 ▶</button>`;
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
    br.innerHTML = '<label class="lab" for="p-br">밝기</label>' +
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
    k4.innerHTML = '<span class="lab">K4</span><span class="note" style="margin:0">패턴을 넘기는 중 (위의 패턴에서 고름)</span>' +
      '<button type="button" class="btn small" id="p-k4-as-knob">노브로 쓰기</button>';
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
    $('insp-title').textContent = (p.n || `패널 ${p.id}`) + (S.picked.size > 1 ? ` 외 ${S.picked.size - 1}대` : '');
    $('insp-uid').textContent = p.uid;
    const st = $('insp-state');
    if (!p.on) { st.textContent = '연결 안 됨'; st.className = 'pill bad'; }
    else if (isSolo(p)) { st.textContent = '쇼 재생 중'; st.className = 'pill ok'; }
    else if (!p.ok) { st.textContent = '따라오는 중'; st.className = 'pill warn'; }
    else { st.textContent = '맞춰짐'; st.className = 'pill ok'; }
    const turn = p.r || [0, 0, 0];
    for (const [id, v] of [['p-id', p.id], ['p-name', p.n], ['p-x', p.x], ['p-y', p.y], ['p-z', p.z || 0],
                           ['p-rx', turn[0]], ['p-ry', turn[1]], ['p-rz', turn[2]]])
      if (!busy($(id))) $(id).value = v;
    $('p-linked').checked = !!p.l;
    $('p-sleep').textContent = p.sl ? '깨우기' : '절전';

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
    $('p-hand-3-down').textContent = k4 ? '◀ 이전 패턴' : '◀ 왼쪽';
    $('p-hand-3-up').textContent = k4 ? '다음 패턴 ▶' : '오른쪽 ▶';
    if (demo && S.transport && S.transport.x) {
      const sim = S.transport.sim(p.uid);
      $('p-power').textContent = sim >= 0 && S.transport.x.pfe_powered(sim) ? '전원 끄기' : '전원 켜기';
    }
    $('p-identify').textContent = p.idf ? '표시 끄기' : `표시 (${idMark(p.id).name})`;
    $('p-forget').disabled = !!p.on;
    $('p-forget').title = p.on ? '연결이 끊긴 패널만 지울 수 있습니다' : '';
    $('p-reboot').disabled = !p.on;

    const flags = [];
    if (p.fl & 1) flags.push('막 켜짐');
    if (p.fl & 2) flags.push('절전');
    if (p.fl & 4) flags.push('패널 메뉴 열림');
    if (p.fl & 8) flags.push('패턴 고르는 중');
    const up = p.up >= 3600 ? `${Math.floor(p.up / 3600)}시간 ${Math.floor(p.up / 60) % 60}분` : `${Math.floor(p.up / 60)}분 ${p.up % 60}초`;
    const rows = [
      ['주소', p.ip], ['신호', `${p.rssi} dBm`], ['화면', `${p.fps} fps`], ['켜진 지', up],
      ['펌웨어', `${p.fw || '?'}${p.var ? ' · ' + p.var : ''}`],
      ['지금 패턴', `${patternName(p.pat)}${p.pn && !S.names[p.pat] ? ' · ' + p.pn : ''}`],
      ['지금 노브', p.k.join(' / ')], ['목표 노브', p.tk.join(' / ')],
      ['마지막 보고', `${p.age} ms 전`], ['보고 / 잃은 보고', `${p.rep} / ${p.lost}`],
      ['손으로 돌린 횟수', p.hands], ['다시 켜진 횟수', p.rst], ['상태', flags.join(', ') || '보통'],
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
    $('all-sleep').textContent = allAsleep ? '깨우기' : '절전';
    $('all-sleep').setAttribute('aria-pressed', String(allAsleep));
    $('all-sleep').disabled = !ps.length;
    $('map-identify').textContent = anyIdentified() ? '표시 끄기' : '전체 모양·색 표시';
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
      warn.textContent = `패널마다 패턴 수가 다릅니다 (${counts.map((p) => `${p.id}번 ${p.pc}개`).join(', ')}). ` +
        '같은 번호가 다른 패턴이 됩니다. 맞추려면 ensemble\\tools\\sync-patterns.cmd 를 두 번 누르세요.';
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
    pen.fillText(`패턴 ${pat}`, x + tw - pad, y + head / 2 + 1);

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
      pen.fillText('절전', x + tw / 2, y + th / 2);
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

  const subLabel = (p, pat) => (p.n ? p.n + ' · ' : '') + (!p.on ? '연결 안 됨' : isSolo(p) ? '쇼 재생 중' : (S.names[pat] || `패턴 ${pat}`)) + (p.l ? '' : ' · 따로');

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
        ctx.fillText(subLabel(p, st.pat) + (p.z ? ` · 깊이 ${p.z}` : '') + (turned3(p) ? ' · 돌려 놓음' : ''), x, y + th + 5, tw);
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
    const side = !v ? '사용자' : v[0] === 2 ? (v[1] > 0 ? '위' : '아래') : v[0] === 1 ? (v[1] > 0 ? '뒤' : '앞') : (v[1] > 0 ? '오른쪽' : '왼쪽');
    return `${side} ${V.ortho ? '정사' : '원근'}`;
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
      if (grab.mode === 'turn') text = `회전: ${typed || f(grab.angle * 180 / Math.PI)}°  ${grab.axis == null ? '보는 방향을 축으로' : AXIS_NAME[grab.axis] + ' 축'}`;
      else if (grab.axis != null && !grab.plane) text = `D: ${typed || f(d[grab.axis])}  (${f(len)})  ${AXIS_NAME[grab.axis]} 축을 따라`;
      else text = `Dx: ${f(d[0])}  Dy: ${f(d[1])}  Dz: ${f(d[2])}  (${f(len)})` + (grab.plane ? `  ${AXIS_NAME[grab.axis]} 축 고정` : '');
      ctx.fillStyle = '#111';
      ctx.fillText(text + (grab.snap ? '  · 스냅' : ''), 12, 62);
    }
  }

  function draw(now) {
    if (S.view === '3d') draw3d(now);
    else draw2d(now);
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
  canvas.addEventListener('contextmenu', (ev) => { if (S.view === '3d') ev.preventDefault(); });

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
  const relay = { next: 1, open: new Map(), busy: 0, waiting: [] };

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
  async function panelHttp(uid, method, path, opt = {}) {
    if (!S.transport || !S.connected) throw new Error('허브에 연결되어 있지 않습니다');
    if (relay.busy >= RELAY_SLOTS) await new Promise((go) => relay.waiting.push(go));
    relay.busy++;
    const id = relay.next++;
    const body = opt.body || new Uint8Array(0);
    const piece = S.transport.name === 'wifi' ? 720 : 1080;     // a WebSocket line has to fit one packet
    const tmo = opt.timeout || 15000;
    try {
      return await new Promise((resolve, reject) => {
        const r = { status: 0, parts: [], sent: 0, timer: 0 };
        const fail = (why) => { clearTimeout(r.timer); relay.open.delete(id); reject(new Error(why)); };
        const arm = () => { clearTimeout(r.timer); r.timer = setTimeout(() => fail('허브가 답하지 않습니다'), tmo + 4000); };
        const more = () => {
          if (r.sent >= body.length) return;
          const part = body.subarray(r.sent, r.sent + piece);
          r.sent += part.length;
          relayLine({ c: 'hb', id, d: b64(part) });
        };
        r.on = (m) => {
          arm();
          if (m.t === 'hk') { if (m.n === r.sent) more(); }
          else if (m.t === 'http') r.status = m.s;
          else if (m.t === 'hb') r.parts.push(unb64(m.d));
          else if (m.t === 'he') {
            if (m.err) { fail(RELAY_ERRORS[m.err] || m.err); return; }
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
        relayLine({ c: 'http', id, to: uid, m: method, path, h: opt.headers || {}, len: body.length, tmo });
        more();
      });
    } finally {
      relay.busy--;
      const next = relay.waiting.shift();
      if (next) next();
    }
  }
  const RELAY_ERRORS = {
    'timeout': '패널이 답하지 않습니다',
    'the panel does not answer': '패널이 답하지 않습니다',
    'no such panel, or it is not connected': '패널이 연결되어 있지 않습니다',
    'the panel closed the connection': '패널이 연결을 끊었습니다',
    'the panel closed the connection without answering': '패널이 답 없이 연결을 끊었습니다',
    'busy with another request': '허브가 다른 요청을 처리하는 중입니다',
  };
  function relayDropped() {       // the link to the hub went: nothing under way will be answered
    for (const [, r] of relay.open) r.on({ t: 'he', err: '허브와 연결이 끊겼습니다' });
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
    catch (_) { throw new Error('커뮤니티에 닿지 않습니다 (인터넷 연결을 확인하세요)'); }
    let data = null;
    try { data = await res.json(); } catch (_) { /* no body */ }
    return { status: res.status, data: data || {} };
  }
  async function communityFile(from, name) {
    let res;
    try { res = await fetch(communityUrl(from, 'file=' + encodeURIComponent(name))); }
    catch (_) { throw new Error('커뮤니티에 닿지 않습니다 (인터넷 연결을 확인하세요)'); }
    if (!res.ok) throw new Error(res.status === 404 ? '커뮤니티에 이 파일이 없습니다' : `커뮤니티가 ${res.status}로 답했습니다`);
    return new Uint8Array(await res.arrayBuffer());
  }
  // The files of a pattern or a deck, once the community has them built.
  async function communityFiles(from) {
    for (let tries = 0; tries < 60; tries++) {
      const { status, data } = await communityJson(from, 'status=1');
      if (status === 404) throw new Error(from.kind === 'd' ? '그런 덱이 없습니다' : '그런 패턴이 없습니다');
      if (data.state === 'ready') break;
      if (data.state !== 'building' && status !== 202) throw new Error(data.error || '패널용 파일이 없는 패턴입니다');
      setPatStatus('커뮤니티가 패널용 파일을 만드는 중 …');
      await sleep(2000);
    }
    const { status, data } = await communityJson(from, 'list=1');
    if (status !== 200 || !Array.isArray(data.files)) throw new Error(data.error || '파일 목록을 받지 못했습니다');
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
      if (!info) throw new Error('쇼 파일(.pfs)이 아닙니다');
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
    if (!slugs.length && !tables.length) throw new Error('패널용 파일이 없습니다');
    const fresh = slugs.map((slug) => ({ slug, name: slug, author: '', size: 0, from, pfm: null, json: null, state: 'idle', error: '' }));
    for (const e of fresh) putEntry(e);
    for (const e of tables) putShow(e);
    renderSet();
    await Promise.all([mapLimit(fresh, 4, fetchEntry), mapLimit(tables, 2, fetchShow)]);
    return fresh.length + tables.length;
  }

  async function addText(text) {
    const found = linksIn(text);
    if (!found.length) { toast('커뮤니티의 패턴이나 덱 링크가 아닙니다'); return; }
    P.adding++;
    let added = 0;
    const problems = [];
    setPatStatus('커뮤니티에서 가져오는 중 …');
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
    setPatStatus(problems.length ? problems[0] : `${added}개를 모음에 넣었습니다`, problems.length ? 'bad' : '');
  }

  // Files from this computer: <slug>.pfm, with its <slug>.json if it came along.
  async function addFiles(fileList) {
    const files = [...fileList];
    const mods = files.filter((f) => /\.pfm$/i.test(f.name));
    const tables = files.filter((f) => /\.pfs$/i.test(f.name));
    if (!mods.length && !tables.length) { toast('.pfm이나 .pfs 파일이 없습니다'); return; }
    for (const f of tables) {
      const data = new Uint8Array(await f.arrayBuffer());
      const info = pfsInfo(data);
      if (!info) { toast(`${f.name}: 쇼 파일(.pfs)이 아닙니다`); continue; }
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
      by.textContent = e.state === 'error' ? e.error : e.state === 'loading' ? '받는 중 …' : (e.author || (e.from ? '' : '이 컴퓨터 파일'));
      const size = document.createElement('span');
      size.className = 'size num';
      size.textContent = e.size ? kb(e.size) : '';
      const play = document.createElement('button');
      play.type = 'button';
      play.className = 'btn small quiet';
      play.textContent = '▶';
      play.title = '모든 패널에서 이 패턴 틀기';
      play.hidden = P.presets == null || !isEverywhere(e.slug);
      play.addEventListener('click', () => send({ c: 'set', to: 'all', pat: P.presets + P.set.indexOf(e) }));
      const drop = document.createElement('button');
      drop.type = 'button';
      drop.className = 'btn small quiet';
      drop.textContent = '✕';
      drop.title = '모음에서 빼기';
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
      n.textContent = '쇼';
      const name = document.createElement('span');
      name.className = 'name';
      name.textContent = e.title || e.slug;
      const by = document.createElement('span');
      by.className = 'by';
      by.textContent = e.state === 'error' ? e.error : e.state === 'loading' ? '받는 중 …' : (e.seconds ? `${Math.round(e.seconds)}초` : '');
      const size = document.createElement('span');
      size.className = 'size num';
      size.textContent = e.size ? kb(e.size) : '';
      const drop = document.createElement('button');
      drop.type = 'button';
      drop.className = 'btn small quiet';
      drop.textContent = '✕';
      drop.title = '모음에서 빼기';
      drop.disabled = P.running;
      drop.addEventListener('click', () => { P.shows.splice(P.shows.indexOf(e), 1); saveSet(); renderSet(); });
      li.append(n, name, by, size, document.createElement('span'), drop);
      showList.append(li);
    }
    $('pat-shows-head').hidden = !P.shows.length;
    $('pat-empty').hidden = P.set.length > 0 || P.shows.length > 0;
    const total = P.set.reduce((n, e) => n + (e.size || 0), 0);
    $('pat-total').textContent = P.set.length ? `${P.set.length}개 · ${kb(total)}` : '';
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
    if (res.status !== 200 || !res.json || !Array.isArray(res.json.patterns)) throw new Error('패턴 목록을 받지 못했습니다');
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
        row.text = same ? '모음과 같음' : `${list.modules.length}개`;
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
      name.textContent = p.n || `패널 ${p.id}`;
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
    go.textContent = P.running ? '넣는 중 …' : chosen ? `패널 ${chosen}대에 넣기` : '넣기';
    go.disabled = P.running || !chosen || !ready || !S.connected;
    $('pat-check-all').hidden = panels.length < 2;
    $('pat-check-all').disabled = P.running;
  }

  async function panelStatus(uid, timeout = 6000) {
    const res = await panelHttp(uid, 'GET', '/api/status', { timeout });
    if (res.status !== 200 || !res.json) throw new Error('패널 상태를 받지 못했습니다');
    return res.json;
  }

  // A panel takes a module into one piece of its memory. One that has been
  // on for a while may have no piece that long left; a restart gives it back.
  async function makeRoom(p, row, biggest) {
    const before = await panelStatus(p.uid);
    if (!Number.isInteger(before.heapLargest) || before.heapLargest >= biggest + 2048) return;
    row.text = '메모리 정리하려고 다시 켜는 중 …';
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
        if (Number.isInteger(st.heapLargest) && st.heapLargest < biggest) throw new Error('다시 켠 뒤에도 메모리가 모자랍니다');
        return;
      }
    }
    throw new Error('다시 켠 패널이 돌아오지 않았습니다');
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
      row.text = '쇼 보내는 중 …';
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
    row.text = '확인하는 중 …';
    renderGo();
    const have = await panelList(p.uid);
    if (!have.mounted) throw new Error('패턴 저장소가 열리지 않습니다 (패널의 /patterns 화면을 보세요)');
    await makeRoom(p, row, Math.max(...files.map((f) => f.data.length)));
    row.text = '비우는 중 …';
    renderGo();
    for (let tries = 0; ; tries++) {
      const res = await panelHttp(p.uid, 'POST', '/api/patterns/delete', {
        headers: { 'Content-Type': 'text/plain' }, body: new TextEncoder().encode('*'), timeout: 30000 });
      if (res.status === 200) break;
      if (res.status === 503 && tries < 3) { await sleep(1500); continue; }
      throw new Error(`비우지 못했습니다 (HTTP ${res.status})`);
    }
    row.done = 1;
    for (let i = 0; i < files.length; i++) {
      row.text = `보내는 중 ${i + 1}/${files.length}`;
      renderGo();
      await putFile(p.uid, files[i].name, files[i].data, i === files.length - 1);
      row.done = 2 + i;
    }
    row.text = '패널이 읽어 들이는 중 …';
    renderGo();
    const until = performance.now() + 40000;
    for (;;) {
      await sleep(1200);
      const now = await panelList(p.uid).catch(() => null);
      if (now && sameList(now.modules, order)) { row.have = now.modules; P.presets = now.presets.length; break; }
      if (performance.now() > until) throw new Error(now ? `보냈지만 목록이 다릅니다 (${now.modules.length}개)` : '보낸 뒤 답이 없습니다');
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
      if (missing.length) throw new Error(`${missing[0].name}: ${missing[0].error || '파일이 없습니다'}`);
      await mapLimit(P.shows.filter((e) => !e.data), 2, fetchShow);
      const lost = P.shows.filter((e) => !e.data);
      if (lost.length) throw new Error(`${lost[0].title}: ${lost[0].error || '파일이 없습니다'}`);
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
      const started = performance.now();
      await mapLimit(panels, RELAY_SLOTS, async (p) => {
        const row = panelRow(p.uid);
        try {
          await installOn(p, row, order, files, tables);
          row.text = row.noPlayer ? '쇼는 못 넣었습니다 (재생기 없는 펌웨어)' : '넣었습니다';
          row.tone = 'ok';
          row.checked = false;
          row.touched = false;
        } catch (e) {
          failed++;
          row.text = e.message;
          row.tone = 'bad';
          log('err', `패널 ${p.id}: ${e.message}`);
        }
        row.total = 0;
        renderGo();
      });
      const secs = Math.round((performance.now() - started) / 1000);
      // The hub's list of names is from before: give it this one.
      if (!failed && order.length) sendNames();
      const what = [order.length ? `패턴 ${order.length}개` : '', tables.length ? `쇼 ${tables.length}개` : ''].filter(Boolean).join(', ');
      setPatStatus(failed ? `${panels.length - failed}대 완료, ${failed}대 실패` : `패널 ${panels.length}대에 ${what}를 넣었습니다 (${secs}초)`, failed ? 'bad' : '');
      log(failed ? 'err' : '', `패턴 넣기: ${panels.length - failed}대 완료${failed ? `, ${failed}대 실패` : ''} (${secs}초)`);
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
    S.page = page;
    setPressed(['page-stage', 'page-patterns', 'page-shows'], 'page-' + page);
    $('stage-page').hidden = page !== 'stage';
    $('pat-page').hidden = page !== 'patterns';
    $('show-page').hidden = page !== 'shows';
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
        o.textContent = `${s.title || s.slug} · ${s.length}초`;
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
    rec.textContent = REC.busy ? (REC.on ? '준비하는 중 …' : '패널에 넣는 중 …')
      : REC.on ? `■ 녹화 끝 · ${((performance.now() - REC.startedAt) / 1000).toFixed(1)}초 · 패널 ${REC.panels.size}대` : '● 녹화';
    if (data && !busy($('p-show-loop')) && playing) $('p-show-loop').checked = !!data.loop;
    const note = $('p-show-state');
    note.style.color = '';
    if (!st || (st.loading && !data && !st.none)) note.textContent = '확인하는 중 …';
    else if (st.none) note.textContent = '이 패널의 펌웨어에는 쇼 재생기가 없습니다 (ensemble v0.2.0부터).';
    else if (st.error) { note.textContent = st.error; note.style.color = 'var(--bad)'; }
    else if (playing && data) {
      const missing = (data.missing || []).length ? ` · 없는 패턴 ${data.missing.length}개` : '';
      note.textContent = `재생 중 · ${data.title || data.slug} · ${data.t}/${data.length}초${data.loop ? ' · 반복' : ''}${missing}`;
    } else if (!list.length) note.textContent = '이 패널에 쇼가 없습니다. 패턴 화면에서 넣습니다.';
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
          const why = (res.json && res.json.error) || (res.status === 404 ? '쇼 재생기가 없는 펌웨어입니다' : `HTTP ${res.status}`);
          toast(`패널 ${p.id}: ${why}`);
          log('err', `패널 ${p.id} 쇼: ${why}`);
        }
      } catch (e) {
        toast(`패널 ${p.id}: ${e.message}`);
      }
    });
    renderShow();
  }

  // ── Recording: what the chosen panels do, looped on them at once ───────
  // While it records, every report of a chosen panel is a sample: its four
  // knob positions and its pattern (console/pfs.js). At the end each panel
  // gets its own table - what that panel itself did, so a wave is recorded
  // as the wave it was - and plays it in a loop.
  const REC = { on: false, startedAt: 0, panels: new Map(), timer: 0, busy: false };
  const REC_KEEP = 4;             // recordings a panel keeps; older ones are deleted when a new one arrives

  function recSample(m) {
    const r = REC.panels.get(m.uid);
    if (!REC.on || !r || !Array.isArray(m.k)) return;
    r.rec.sample((performance.now() - REC.startedAt) / 100, m.k, r.names[m.pat] || '');
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
      await mapLimit(panels, RELAY_SLOTS, async (p) => {
        let names = [];
        try { names = await patternKeys(p.uid); } catch (_) { /* then the table says nothing about patterns */ }
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
      log('', `녹화 시작: 패널 ${panels.map((p) => p.id).join(', ')}`);
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
    if (length < 10) { toast('1초도 안 되는 녹화는 버렸습니다'); REC.panels.clear(); renderShow(); return; }
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
          problems.push(`패널 ${r.id}: ${e.message}`);
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
      const entry = libAdd(t.bytes, { title: tables.size > 1 ? `${title} · 패널 ${t.ids.sort((a, b) => a - b).join(', ')}` : title,
                                      slug: slug + (n ? String.fromCharCode(97 + n) : '') });
      if (entry && !n) L.sel = entry.id;
      n++;
    }
    for (const p of problems) log('err', '녹화 ' + p);
    const secs = (length / 10).toFixed(1);
    if (played) log('', `녹화 ${secs}초를 패널 ${played}대가 반복합니다 (${title}, 큐 ${cues}개)`);
    toast(problems.length ? problems[0] : `${secs}초 녹화를 패널 ${played}대가 반복합니다. 쇼 화면에 저장했습니다`);
    renderShow();
  }

  // The show chosen in the list, off every chosen panel.
  async function deleteShow() {
    const slug = $('p-show').value;
    if (!slug) return;
    await mapLimit(pickedPanels().filter((p) => p.on), RELAY_SLOTS, async (p) => {
      try {
        const res = await panelHttp(p.uid, 'DELETE', '/api/shows?slug=' + encodeURIComponent(slug), { timeout: 8000 });
        if (res.status !== 200 && res.status !== 404) toast(`패널 ${p.id}: ${(res.json && res.json.error) || 'HTTP ' + res.status}`);
      } catch (e) {
        toast(`패널 ${p.id}: ${e.message}`);
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
    catch (_) { toast('브라우저의 저장 공간이 모자라 쇼를 저장하지 못했습니다'); }
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
      meta.textContent = `${(e.edit.length / 10).toFixed(1)}초 · ${d.getMonth() + 1}/${d.getDate()} ${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
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
    $('sh-speed').textContent = Math.abs(speed - 1) < 0.005 ? '원래 속도' : `×${speed.toFixed(2)}`;
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
    const options = [['', written.length ? `녹화된 대로 (${written.map(nameOf).join(' → ')})` : '녹화에 패턴 없음'],
                     ['-', '패턴은 바꾸지 않기 (패널에 지금 있는 패턴 위에서)'],
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
      $('sh-info').textContent = `큐 ${PFS.cueCount(show)}개 / ${PFS.MAX_CUES} · ${(show.length / 10).toFixed(1)}초` +
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
      name.textContent = p.n || `패널 ${p.id}`;
      label.append(box, id, name);
      const state = document.createElement('span');
      state.className = 'state' + (isSolo(p) ? ' ok' : '');
      state.textContent = isSolo(p) ? '쇼 재생 중' : '';
      li.append(label, state);
      ul.append(li);
    }
    const chosen = panels.filter((p) => L.targets.get(p.uid)).length;
    $('sh-send').textContent = L.busy ? '넣는 중 …' : chosen ? `패널 ${chosen}대에 넣고 재생` : '넣고 재생';
    $('sh-send').disabled = L.busy || !chosen || !!problem || !S.connected;
    $('sh-stop').disabled = L.busy || !chosen || !S.connected;
  }

  // A table onto a panel and playing. Returns the patterns it names that the panel does not hold.
  async function putAndPlay(uid, slug, bytes, loop) {
    const put = await panelHttp(uid, 'PUT', '/api/shows', {
      headers: { 'Content-Type': 'application/octet-stream', 'X-PF-Name': slug + '.pfs' }, body: bytes, timeout: 20000 });
    if (put.status === 404) throw new Error('쇼 재생기가 없는 펌웨어입니다');
    if (put.status !== 200) throw new Error((put.json && put.json.error) || `HTTP ${put.status}`);
    const res = await panelHttp(uid, 'POST', '/api/shows/control', {
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new TextEncoder().encode(new URLSearchParams({ op: 'play', slug, loop: loop ? '1' : '0' }).toString()), timeout: 10000 });
    if (res.status !== 200) throw new Error((res.json && res.json.error) || `재생 HTTP ${res.status}`);
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
        problems.push(`패널 ${p.id}: ${err.message}`);
      }
    });
    L.busy = false;
    for (const p of problems) log('err', '쇼 ' + p);
    setShStatus(problems.length ? problems[0] : `패널 ${ok}대에서 재생 중` + (missing.size ? ` · 패널에 없는 패턴: ${[...missing].join(', ')}` : ''),
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
      label.innerHTML = `<i style="background:${KNOB_COLOURS[k]}"></i>K${k + 1}이 따를 값`;
      const sel = document.createElement('select');
      sel.id = `sh-map-${k}`;
      sel.setAttribute('aria-label', `K${k + 1}이 따를 값`);
      for (let from = 0; from < KNOBS; from++) {
        const o = document.createElement('option');
        o.value = String(from);
        o.textContent = `녹화의 K${from + 1}`;
        sel.append(o);
      }
      const none = document.createElement('option');
      none.value = '-1';
      none.textContent = '쇼가 건드리지 않음';
      sel.append(none);
      const inv = document.createElement('label');
      inv.className = 'check';
      inv.innerHTML = `<input type="checkbox" id="sh-inv-${k}"> 뒤집기`;
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
      const copy = { ...e, id, title: e.title + ' 사본', created: Date.now(), edit: JSON.parse(JSON.stringify(e.edit)) };
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
        if (entry) last = entry; else toast(`${f.name}: 쇼 파일(.pfs)이 아닙니다`);
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

  // ── Wiring the controls ────────────────────────────────────────────────
  function wire() {
    $('conn-demo').addEventListener('click', () => connect('demo'));
    $('conn-usb').addEventListener('click', () => connect('usb'));
    $('conn-wifi').addEventListener('click', () => {
      let host = '192.168.66.1';
      try { host = localStorage.getItem('pfe-host') || host; } catch (_) { /* no storage here */ }
      const row = document.createElement('form');
      row.className = 'conn';
      row.innerHTML = `<input type="text" id="wifi-host" value="${host}" aria-label="허브 주소" style="width:150px">` +
        `<button type="submit" class="btn small primary">연결</button>`;
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
      $('conn-usb').title = 'Web Serial이 있는 브라우저(Chrome, Edge)에서 이 파일을 직접 열어야 합니다';
    }

    $('cfg-speed').addEventListener('input', (ev) => {
      const v = +ev.target.value;
      $('cfg-speed-v').textContent = `${v} 칸/초`;
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

    $('demo-count').addEventListener('input', (ev) => { $('demo-count-v').textContent = `${ev.target.value}대`; });
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
      log('', '허브를 껐다 켰습니다. 배치와 규칙은 저장된 것에서 돌아오고, 패널은 보던 것을 그대로 보여 줍니다.');
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
      if (!list.length) { toast('연결된 패널이 없습니다.'); return; }
      const text = list.map((p) => p.ip).join('\n');
      for (const p of list) log('', `패널 ${p.id}  ${p.ip}  ${p.uid}  ${p.fw || '?'} ${p.var || ''}`);
      try {
        await navigator.clipboard.writeText(text + '\n');
        toast(`패널 ${list.length}대의 주소를 복사했습니다.`);
      } catch (_) {
        toast('복사하지 못했습니다. 아래 로그에 주소를 적어 두었습니다.');
      }
    });

    for (const sc of ['one', 'wave', 'all'])
      $('scope-' + sc).addEventListener('click', () => {
        S.scope = sc;
        setPressed(['scope-one', 'scope-wave', 'scope-all'], 'scope-' + sc);
      });
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
      try { JSON.parse(text); } catch (_) { toast('JSON 한 줄이어야 합니다. 예: {"c":"get"}'); return; }
      log('me', '→ ' + text);
      if (S.transport) S.transport.send(text);
      $('log-input').value = '';
    });

    wirePatterns();
    wireShows();
    wireShowPage();

    // Help: what is easy to forget (the MIDI numbers, the map's keys).
    const help = $('help');
    const helpTabs = ['midi', 'map', 'patterns'];
    const showHelp = (tab) => {
      setPressed(helpTabs.map((t) => 'help-tab-' + t), 'help-tab-' + tab);
      for (const t of helpTabs) $('help-' + t).hidden = t !== tab;
      help.scrollTop = 0;
    };
    for (const t of helpTabs) $('help-tab-' + t).addEventListener('click', () => showHelp(t));
    $('help-open').addEventListener('click', () => {
      showHelp(S.page === 'patterns' ? 'patterns' : help.dataset.last || 'midi');
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
    draw(now);
    // A hub that has gone quiet (unplugged, out of range) should not look alive.
    if (S.connected && S.mode !== 'demo' && S.hub && !S.quiet && now - S.lastHubLineAt > 4000) {
      S.quiet = true;
      status('허브가 조용합니다', 'warn');
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

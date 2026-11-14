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
    sel: null,               // hub index of the chosen panel
    scope: 'one',
    sceneOp: 'recall',
    cam: { x: 0, y: 0, z: 90, fitted: false },
    flash: new Map(),        // hub index -> time a hand was last seen there
    phase: new Map(),        // hub index -> animation phase of its preview
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
      const bytes = this.enc.encode(text);
      if (bytes.length >= this.x.pfe_io_size()) return;
      new Uint8Array(this.x.memory.buffer, this.x.pfe_io(), bytes.length).set(bytes);
      this.x.pfe_command(bytes.length);
    }
    tick() {
      const x = this.x;
      if (!x) return;
      this.autoplay();
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
      // is up, which opening the port already did. On the COM jack - a
      // USB-serial chip - DTR and RTS are the board's reset lines: let go of
      // both, or the hub restarts and takes its hotspot down with it.
      const native = this.port.getInfo().usbVendorId === 0x303a;
      if (!native) {
        try { await this.port.setSignals({ dataTerminalReady: false, requestToSend: false }); } catch (_) { /* not every port has them */ }
      }
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
    S.flash.clear();
    S.cam.fitted = false;
    renderAll();
  }

  function status(text, tone) {
    const el = $('conn-status');
    el.textContent = text;
    el.className = 'pill' + (tone ? ' ' + tone : '');
  }

  function dropped(why) {
    S.connected = false;
    status('끊김', 'bad');
    log('err', why);
    toast(why);
  }

  async function connect(mode, arg) {
    if (S.transport) { try { await S.transport.stop(); } catch (_) { /* gone */ } }
    S.transport = null;
    S.connected = false;
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
    if ($('log-raw').checked && m.t !== 'p' && m.t !== 'hub') log('raw', '← ' + text);
    switch (m.t) {
      case 'hello':
        log('', `허브: 프로토콜 ${m.proto}${m.fw ? ', 펌웨어 ' + m.fw : ''}${m.ssid ? ', Wi-Fi ' + m.ssid : ''}`);
        break;
      case 'cfg': S.cfg = m; renderRules(); renderInspector(); break;
      case 'names': S.names = m.n || []; renderInspector(); break;
      case 'scenes': S.scenes = m.u || []; renderScenes(); break;
      case 'hub':
        S.hub = m;
        S.lastHubLineAt = performance.now();
        if (S.quiet) { S.quiet = false; status(S.mode === 'usb' ? 'USB로 허브에 연결됨' : 'Wi-Fi로 허브에 연결됨', 'ok'); }
        renderStats();
        break;
      case 'p': {
        const prev = S.panels.get(m.i);
        if (prev && m.hands !== prev.hands) S.flash.set(m.i, performance.now());
        S.panels.set(m.i, m);
        if (S.sel === m.i) renderInspector();
        $('stage-empty').hidden = true;
        break;
      }
      case 'gone':
        S.panels.delete(m.i);
        if (S.sel === m.i) { S.sel = null; renderInspector(); }
        break;
      case 'w': {
        // A turning knob sends a value every few frames. One ring per
        // quarter second from the same place says the same thing.
        const at = performance.now();
        const last = S.waves[S.waves.length - 1];
        if (last && last.o === m.o && last.c === m.c && at - last.at < 250) break;
        S.waves.push({ x: m.x, y: m.y, c: m.c, at, now: !!m.now, d: m.d, speed: S.cfg ? S.cfg.speed : 2, o: m.o });
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
    const sc = $('scenes');
    for (let i = 0; i < 8; i++) {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'scene';
      b.id = `scene-${i}`;
      b.innerHTML = `<b class="num">${i + 1}</b><span>비어 있음</span>`;
      b.addEventListener('click', () => sceneClick(i));
      sc.append(b);
    }
  }

  function busy(el) { return document.activeElement === el; }

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
    $('k4-note').hidden = !cfg.k4;
    for (const [id, v] of [['cfg-pmin', cfg.pmin], ['cfg-pmax', cfg.pmax], ['cfg-k4s', cfg.k4s]])
      if (!busy($(id))) $(id).value = v;
    const last = cfg.pmax === 255 ? '끝' : cfg.pmax;
    $('k4-note').textContent = `패턴 ${cfg.pmin}번부터 ${last}까지, ${cfg.k4s} 클릭에 한 패턴. 마지막 칸의 255는 "끝까지"입니다.`;
    $('cfg-lock').checked = !!cfg.lock;
  }

  function renderScenes() {
    for (let i = 0; i < 8; i++) {
      const b = $(`scene-${i}`);
      const n = S.scenes[i] || 0;
      b.classList.toggle('used', n > 0);
      b.lastElementChild.textContent = n > 0 ? `패널 ${n}대` : '비어 있음';
    }
  }

  function selUid() {
    const p = S.sel != null ? S.panels.get(S.sel) : null;
    return p ? p.uid : null;
  }

  function sceneClick(i) {
    if (S.sceneOp === 'save') { send({ c: 'scene', op: 'save', slot: i }); toast(`장면 ${i + 1}에 지금 상태를 저장했습니다`); return; }
    if (S.sceneOp === 'clear') { send({ c: 'scene', op: 'clear', slot: i }); return; }
    if (!(S.scenes[i] > 0)) { toast(`장면 ${i + 1}은 비어 있습니다. "저장"으로 바꾸고 누르면 지금 상태가 들어갑니다.`); return; }
    const from = selUid();
    const ripple = $('scene-ripple').checked && from;
    send({ c: 'scene', op: 'recall', slot: i, ...(ripple ? { mode: 'ripple', from } : {}), glide: 400 });
  }

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
    if (S.scope === 'one') send({ c: 'set', to: uid, ...what });
    else if (S.scope === 'wave') send({ c: 'set', to: 'all', mode: 'ripple', from: uid, ...what });
    else send({ c: 'set', to: 'all', ...what });
  }

  function renderInspector() {
    const p = S.sel != null ? S.panels.get(S.sel) : null;
    const demo = S.mode === 'demo';
    $('insp-none').hidden = !!p;
    for (const id of ['insp', 'insp-set', 'insp-act', 'insp-diag']) $(id).hidden = !p;
    $('insp-hand').hidden = !p || !demo;
    if (!p) return;
    const k4 = S.cfg && S.cfg.k4;
    $('insp-id').textContent = p.id;
    $('insp-title').textContent = p.n || `패널 ${p.id}`;
    $('insp-uid').textContent = p.uid;
    const st = $('insp-state');
    if (!p.on) { st.textContent = '연결 안 됨'; st.className = 'pill bad'; }
    else if (!p.ok) { st.textContent = '따라오는 중'; st.className = 'pill warn'; }
    else { st.textContent = '맞춰짐'; st.className = 'pill ok'; }
    for (const [id, v] of [['p-id', p.id], ['p-name', p.n], ['p-x', p.x], ['p-y', p.y]])
      if (!busy($(id))) $(id).value = v;
    $('p-linked').checked = !!p.l;

    // Patterns: as many as this panel says it has, named where a name is known.
    const sel = $('p-pattern');
    const count = Math.max(p.pc || 0, S.names.length, (p.tp >= 0 ? p.tp : 0) + 1, 1);
    if (sel.options.length !== count || sel.dataset.names !== S.names.join('|')) {
      sel.dataset.names = S.names.join('|');
      sel.textContent = '';
      for (let i = 0; i < count; i++) sel.append(new Option(patternName(i), String(i)));
    }
    if (!busy(sel)) sel.value = String(p.tp >= 0 ? p.tp : p.pat);
    for (let k = 0; k < KNOBS; k++) {
      $(`p-knob-row-${k}`).hidden = k === 3 && k4;
      $(`p-hand-row-${k}`).hidden = false;
      const input = $(`p-k${k}`);
      if (!busy(input)) { input.value = p.tk[k]; $(`p-k${k}-v`).textContent = p.tk[k]; }
    }
    $('p-hand-3-down').textContent = k4 ? '◀ 이전 패턴' : '◀ 왼쪽';
    $('p-hand-3-up').textContent = k4 ? '다음 패턴 ▶' : '오른쪽 ▶';
    if (demo && S.transport && S.transport.x) {
      const sim = S.transport.sim(p.uid);
      $('p-power').textContent = sim >= 0 && S.transport.x.pfe_powered(sim) ? '전원 끄기' : '전원 켜기';
    }
    $('p-identify').textContent = p.idf ? '번호 표시 끄기' : '번호 표시';
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

  function renderAll() {
    renderRules();
    renderScenes();
    renderStats();
    renderInspector();
    $('stage-empty').hidden = S.panels.size > 0 || S.mode === 'demo';
  }

  // ── The map ────────────────────────────────────────────────────────────
  const canvas = $('map');
  const ctx = canvas.getContext('2d');
  const led = document.createElement('canvas');
  const LW = 24, LH = 12;
  led.width = LW;
  led.height = LH;
  const lctx = led.getContext('2d');
  const limg = lctx.createImageData(LW, LH);
  let W = 0, H = 0, dpr = 1;
  const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  let colors = {};
  function readColors() {
    colors = { stage: css('--stage'), grid: css('--stage-grid'), ink: css('--stage-ink'), muted: css('--stage-muted'),
               led: css('--led'), ok: css('--ok'), warn: css('--warn'), bad: css('--bad') };
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

  function hsv(h, s, v) {
    h = ((h % 360) + 360) % 360;
    const c = v * s, x = c * (1 - Math.abs(((h / 60) % 2) - 1)), m = v - c;
    const [r, g, b] = h < 60 ? [c, x, 0] : h < 120 ? [x, c, 0] : h < 180 ? [0, c, x] : h < 240 ? [0, x, c] : h < 300 ? [x, 0, c] : [c, 0, x];
    return [(r + m) * 255, (g + m) * 255, (b + m) * 255];
  }
  const BASE_HUE = [16, 196, 268, 322, 132, 214, 38, 60];
  const fract = (v) => v - Math.floor(v);

  // What a panel "shows" here: not its pattern, but a picture in which the
  // pattern number and every knob value can be seen moving across the room.
  // K1 how much, K2 colour, K3 speed (in the phase), K4 brightness.
  function paintLeds(pat, k, phase, on) {
    const d = limg.data;
    const shape = ((pat % 8) + 8) % 8;
    const hue0 = BASE_HUE[shape] + (k[1] - 0.5) * 150;
    const amount = k[0];
    const bright = on ? 0.35 + 0.65 * k[3] : 0;
    for (let y = 0; y < LH; y++) {
      for (let x = 0; x < LW; x++) {
        const u = (x + 0.5) / LW, v = (y + 0.5) / LH;
        const cx = (u - 0.5) * 2, cy = v - 0.5;
        let b = 0, hue = hue0;
        switch (shape) {
          case 0: { const r = Math.hypot(cx, cy); b = 0.5 + 0.5 * Math.sin(r * (4 + 18 * amount) - phase * 3); hue += r * 40; break; }
          case 1: b = fract(u * (1.5 + 7 * amount) + v * 0.6 - phase * 0.5); break;
          case 2: b = Math.abs(Math.sin(u * Math.PI * (2 + 9 * amount)) * Math.sin(v * Math.PI * (1 + 4 * amount) + phase)); break;
          case 3: { const a = Math.atan2(cy, cx), r = Math.hypot(cx, cy); b = 0.5 + 0.5 * Math.cos(a * (2 + Math.round(6 * amount)) + phase) - r * 0.5; hue += r * 60; break; }
          case 4: b = 0.5 + 0.25 * Math.sin(u * (3 + 10 * amount) + phase) + 0.25 * Math.sin(v * 7 - phase * 1.3 + u * 4); break;
          case 5: b = 0.5 + 0.5 * Math.sin(v * (3 + 14 * amount) + Math.sin(u * 5 + phase) * 1.2 - phase * 1.5); hue += v * 30; break;
          case 6: { const n = fract(Math.sin(x * 12.9898 + y * 78.233 + Math.floor(phase * 3) * 37.719) * 43758.5453); b = n < 0.12 + 0.6 * amount ? 0.4 + 0.6 * n : 0; hue += n * 40; break; }
          default: b = ((x >> 1) + (y >> 1) + Math.floor(phase * 2)) % 2 ? 0.25 + 0.75 * amount : 0.08; break;
        }
        const [r, g, bl] = hsv(hue, shape === 7 ? 0.15 : 0.85, clamp(b, 0, 1) * bright);
        const o = (y * LW + x) * 4;
        d[o] = r; d[o + 1] = g; d[o + 2] = bl; d[o + 3] = 255;
      }
    }
    lctx.putImageData(limg, 0, 0);
  }

  let lastFrame = performance.now();
  function draw(now) {
    const dt = Math.min(0.1, (now - lastFrame) / 1000);
    lastFrame = now;
    if (!S.cam.fitted && S.panels.size) fit();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = colors.stage;
    ctx.fillRect(0, 0, W, H);

    // A dot at every whole unit: the map's ruler.
    const z = S.cam.z;
    if (z > 26) {
      ctx.fillStyle = colors.grid;
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
      ctx.strokeStyle = w.c === 4 || w.c === 5 ? colors.ink : colors.led;
      ctx.globalAlpha = 0.5 * clamp(fade, 0, 1);
      ctx.lineWidth = w.c === 4 || w.c === 5 ? 2 : 1.25;
      ctx.beginPath();
      ctx.arc(sx, sy, Math.max(0, radius), 0, Math.PI * 2);
      ctx.stroke();
    }
    ctx.globalAlpha = 1;

    const demo = S.transport && S.transport.name === 'demo' && S.transport.x ? S.transport : null;
    const tw = TILE_W * z, th = TILE_H * z;
    ctx.imageSmoothingEnabled = false;
    for (const [i, p] of S.panels) {
      const [cx, cy] = toScreen(p.x, p.y);
      const x = cx - tw / 2, y = cy - th / 2;
      if (x > W || y > H || x + tw < 0 || y + th + 12 < 0) continue;

      // In the demo the tile shows the virtual panel itself; with hardware,
      // what the panel last reported.
      let k = p.k.map((v) => v / 1000), pat = p.pat, lit = !!p.on, idf = !!p.idf;
      if (demo) {
        const sim = demo.sim(p.uid);
        if (sim >= 0) {
          lit = !!demo.x.pfe_powered(sim);
          pat = demo.x.pfe_pattern(sim);
          k = [0, 1, 2, 3].map((n) => demo.x.pfe_lane(sim, n));
          idf = !!demo.x.pfe_identify(sim);
        }
      }
      const phase = (S.phase.get(i) || 0) + dt * (0.25 + 3.2 * k[2]);
      S.phase.set(i, phase);
      paintLeds(pat, k, phase, lit);
      ctx.drawImage(led, x, y, tw, th);

      if (idf && lit) {
        ctx.fillStyle = 'rgba(0,0,0,0.72)';
        ctx.fillRect(x, y, tw, th);
        ctx.fillStyle = colors.ink;
        ctx.font = `700 ${Math.round(th * 0.7)}px ${css('--sans')}`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(String(p.id), cx, cy + 1);
      }

      const flashed = S.flash.get(i);
      const hot = flashed && now - flashed < 450;
      ctx.lineWidth = i === S.sel ? 2 : 1;
      ctx.strokeStyle = hot ? colors.led : i === S.sel ? colors.ink : colors.grid;
      ctx.strokeRect(x - 1.5, y - 1.5, tw + 3, th + 3);

      // The number rides on the tile's corner, with the state as a dot
      // beside it; the name and the pattern go underneath when there is room.
      const chip = String(p.id);
      ctx.font = `700 11px ${css('--sans')}`;
      ctx.textAlign = 'left';
      ctx.textBaseline = 'middle';
      const cw = ctx.measureText(chip).width + 17;
      ctx.fillStyle = 'rgba(12,11,9,0.78)';
      ctx.fillRect(x, y, cw, 15);
      ctx.fillStyle = '#EDE7DB';
      ctx.fillText(chip, x + 4, y + 8);
      ctx.fillStyle = !p.on ? colors.bad : p.ok ? colors.ok : colors.warn;
      ctx.beginPath();
      ctx.arc(x + cw - 6, y + 7.5, 2.5, 0, Math.PI * 2);
      ctx.fill();
      if (z >= 84) {
        ctx.textBaseline = 'top';
        ctx.font = `11px ${css('--sans')}`;
        ctx.fillStyle = p.on ? colors.muted : colors.bad;
        const sub = (p.n ? p.n + ' · ' : '') + (!p.on ? '연결 안 됨' : (S.names[pat] || `패턴 ${pat}`)) + (p.l ? '' : ' · 따로');
        ctx.fillText(sub, x, y + th + 5, tw);
      }
    }
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

  let drag = null;
  canvas.addEventListener('pointerdown', (ev) => {
    const r = canvas.getBoundingClientRect();
    const sx = ev.clientX - r.left, sy = ev.clientY - r.top;
    const i = tileAt(sx, sy);
    canvas.setPointerCapture(ev.pointerId);
    if (i != null) {
      const p = S.panels.get(i);
      drag = { kind: 'tile', i, sx, sy, px: p.x, py: p.y, moved: false };
      if (S.sel !== i) { S.sel = i; renderInspector(); }
    } else {
      drag = { kind: 'pan', sx, sy, cx: S.cam.x, cy: S.cam.y, moved: false };
    }
  });
  canvas.addEventListener('pointermove', (ev) => {
    if (!drag) return;
    const r = canvas.getBoundingClientRect();
    const dx = (ev.clientX - r.left - drag.sx) / S.cam.z, dy = (ev.clientY - r.top - drag.sy) / S.cam.z;
    if (!drag.moved && Math.hypot(dx, dy) * S.cam.z < 5) return;
    drag.moved = true;
    if (drag.kind === 'pan') { S.cam.x = drag.cx - dx; S.cam.y = drag.cy - dy; return; }
    const p = S.panels.get(drag.i);
    if (!p) return;
    const snap = ev.altKey ? 0.01 : 0.25;
    p.x = Math.round((drag.px + dx) / snap) * snap;
    p.y = Math.round((drag.py + dy) / snap) * snap;
    throttled('move', () => send({ c: 'panel', uid: p.uid, x: p.x, y: p.y }), 120);
    if (S.sel === drag.i) { $('p-x').value = p.x; $('p-y').value = p.y; }
  });
  const endDrag = () => {
    if (drag && drag.kind === 'tile' && drag.moved) {
      const p = S.panels.get(drag.i);
      if (p) send({ c: 'panel', uid: p.uid, x: p.x, y: p.y });
    } else if (drag && drag.kind === 'pan' && !drag.moved && S.sel != null) {
      S.sel = null;
      renderInspector();
    }
    drag = null;
  };
  canvas.addEventListener('pointerup', endDrag);
  canvas.addEventListener('pointercancel', endDrag);
  canvas.addEventListener('wheel', (ev) => {
    ev.preventDefault();
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
      send({ c: 'panel', uid: p.uid, x, y });
    });
    S.cam.fitted = false;
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

    for (const op of ['recall', 'save', 'clear'])
      $('scene-' + op).addEventListener('click', () => {
        S.sceneOp = op;
        setPressed(['scene-recall', 'scene-save', 'scene-clear'], 'scene-' + op);
      });

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

    $('map-fit').addEventListener('click', fit);
    $('map-arrange').addEventListener('change', (ev) => {
      if (ev.target.value) arrange(ev.target.value);
      ev.target.value = '';
    });
    let allIdentify = false;
    $('map-identify').addEventListener('click', () => {
      allIdentify = !allIdentify;
      send({ c: 'identify', uid: 'all', on: allIdentify });
      $('map-identify').textContent = allIdentify ? '번호 표시 끄기' : '전체 번호 표시';
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
    $('p-pattern').addEventListener('change', (ev) => setFromConsole({ pat: +ev.target.value }));
    const edit = (what) => { const uid = selUid(); if (uid) send({ c: 'panel', uid, ...what }); };
    $('p-id').addEventListener('change', (ev) => edit({ id: clamp(Math.round(+ev.target.value) || 1, 1, 250) }));
    $('p-name').addEventListener('change', (ev) => edit({ name: ev.target.value }));
    $('p-x').addEventListener('change', (ev) => edit({ x: +ev.target.value || 0 }));
    $('p-y').addEventListener('change', (ev) => edit({ y: +ev.target.value || 0 }));
    $('p-linked').addEventListener('change', (ev) => edit({ linked: ev.target.checked }));
    $('p-identify').addEventListener('click', () => {
      const p = S.panels.get(S.sel);
      if (p) send({ c: 'identify', uid: p.uid, on: !p.idf });
    });
    $('p-reboot').addEventListener('click', () => { const uid = selUid(); if (uid) send({ c: 'reboot', uid }); });
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
  if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) $('demo-auto').checked = false;
  // Served by the hub itself: talk to it. Opened from a file or anywhere
  // else: start in the demo.
  const fromHub = location.protocol === 'http:' && location.hostname && !/^(localhost|127\.|\[::1\])/.test(location.hostname) &&
                  document.querySelector('meta[name="pfe-hub"]');
  connect(fromHub ? 'wifi' : 'demo', fromHub ? location.host : undefined);
  requestAnimationFrame(frame);
})();

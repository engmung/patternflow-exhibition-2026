/**
 * MQTT publish helpers for Patternflow Director (mqtt.js global `mqtt`).
 * Wire protocol matches P1 firmware.
 */
export function createMqttController() {
  let client = null;
  let prefix = "patternflow";
  let statusCb = () => {};
  let lastSnapshotMs = 0;
  const SNAPSHOT_MS = 8000;
  let lastParamPub = [0, 0, 0, 0];
  let lastParamAt = 0;
  const PARAM_MIN_MS = 50; // ~20 Hz cap
  let selectAckCb = null;

  function onSelectAck(handler) {
    selectAckCb = handler || null;
  }

  function setStatus(handler) {
    statusCb = handler || (() => {});
  }

  function emit(kind, message) {
    statusCb({ kind, message, connected: !!(client && client.connected) });
  }

  function topic(leaf) {
    return `${prefix}/${leaf}`;
  }

  function connect({ url, prefix: pfx, username, password }) {
    disconnect();
    prefix = (pfx && pfx.trim()) || "patternflow";
    const opts = {
      reconnectPeriod: 3000,
      connectTimeout: 8000,
      clean: true,
      clientId: "pfdir_" + Math.random().toString(16).slice(2, 10)
    };
    if (username) {
      opts.username = username;
      opts.password = password || "";
    }
    emit("warn", "connecting…");
    try {
      client = window.mqtt.connect(url, opts);
    } catch (err) {
      emit("err", String(err.message || err));
      return;
    }
    client.on("connect", () => {
      try {
        client.subscribe(topic("inventory/#"), { qos: 0 });
        client.subscribe(topic("inventory"), { qos: 0 });
        client.subscribe(topic("select/ack"), { qos: 0 });
      } catch (_) {
        /* ignore */
      }
      emit("ok", "connected");
    });
    client.on("message", (t, payload) => {
      if (t !== topic("select/ack") || !selectAckCb) return;
      try {
        selectAckCb(JSON.parse(payload ? payload.toString() : "{}"));
      } catch (_) {
        /* ignore */
      }
    });
    client.on("reconnect", () => emit("warn", "reconnecting…"));
    client.on("close", () => emit("err", "disconnected"));
    client.on("error", (err) => emit("err", err && err.message ? err.message : "mqtt error"));
  }

  function disconnect() {
    if (client) {
      try {
        client.end(true);
      } catch (_) { /* ignore */ }
      client = null;
    }
    emit("err", "disconnected");
  }

  function isConnected() {
    return !!(client && client.connected);
  }

  function publish(leaf, payload, retain) {
    if (!isConnected()) return false;
    client.publish(topic(leaf), payload == null ? "" : String(payload), {
      qos: 0,
      retain: !!retain
    });
    return true;
  }

  function publishPattern(name) {
    return publish("pattern", name || "", false);
  }

  function publishParam(index1to4, value0to1000, force) {
    const i = index1to4 - 1;
    if (i < 0 || i > 3) return false;
    let v = Math.round(Number(value0to1000));
    if (!Number.isFinite(v)) return false;
    if (v < 0) v = 0;
    if (v > 1000) v = 1000;
    const now = performance.now();
    if (!force && v === lastParamPub[i] && now - lastParamAt < PARAM_MIN_MS) {
      return false;
    }
    if (!force && now - lastParamAt < PARAM_MIN_MS && v === lastParamPub[i]) {
      return false;
    }
    // Rate-limit bursts across all params
    if (!force && now - lastParamAt < PARAM_MIN_MS) {
      // still publish if value changed meaningfully — allow slightly denser
      if (Math.abs(v - lastParamPub[i]) < 1) return false;
    }
    lastParamPub[i] = v;
    lastParamAt = now;
    return publish(`param/${index1to4}`, String(v), false);
  }

  function publishParams(arr, force) {
    if (!arr || arr.length < 4) return;
    for (let i = 0; i < 4; i++) publishParam(i + 1, arr[i], force);
  }

  function publishMessage(text) {
    return publish("message", text == null ? "" : text, false);
  }

  function publishSnapshot(state, force) {
    if (!isConnected()) return false;
    const now = Date.now();
    if (!force && lastSnapshotMs && now - lastSnapshotMs < SNAPSHOT_MS) return false;
    lastSnapshotMs = now;
    const body = JSON.stringify({
      pattern: state.pattern || "",
      param: (state.param || [500, 500, 500, 500]).map((n) => {
        let v = Math.round(Number(n));
        if (v < 0) v = 0;
        if (v > 1000) v = 1000;
        return v;
      }),
      message: state.message || "",
      t: state.t != null ? state.t : 0,
      id: state.id || "",
      title: state.title || ""
    });
    return publish("snapshot", body, true);
  }

  function queryPatterns(timeoutMs) {
    const wait = timeoutMs || 8000;
    return new Promise((resolve, reject) => {
      if (!isConnected()) {
        reject(new Error("not connected"));
        return;
      }
      const items = [];
      let finished = false;
      let expectedCount = -1;
      const pfx = topic("inventory");
      let timer = setTimeout(() => done(), wait);

      function done() {
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        if (client) client.removeListener("message", onMsg);
        resolve(items.filter((x) => x && x.name));
      }

      function bumpTimeout() {
        clearTimeout(timer);
        timer = setTimeout(() => done(), wait);
      }

      function onMsg(t, payload) {
        const text = payload ? payload.toString() : "";
        if (t === pfx) {
          try {
            const meta = JSON.parse(text);
            if (meta && Number.isFinite(Number(meta.count))) {
              expectedCount = Math.max(0, Number(meta.count));
              if (expectedCount === 0) done();
            }
          } catch (_) {
            /* ignore */
          }
          bumpTimeout();
          return;
        }
        if (t === pfx + "/end") {
          done();
          return;
        }
        const m = t && t.indexOf(pfx + "/") === 0 ? t.slice((pfx + "/").length) : "";
        if (!m || m === "end") return;
        if (!/^\d+$/.test(m)) return;
        const i = Number(m);
        try {
          items[i] = JSON.parse(text);
          if (expectedCount > 0) {
            let received = 0;
            for (let k = 0; k < expectedCount; k++) {
              if (items[k] && items[k].name) received++;
            }
            if (received >= expectedCount) {
              done();
              return;
            }
          }
          bumpTimeout();
        } catch (_) {
          /* ignore */
        }
      }

      client.on("message", onMsg);
      publish("query", "patterns", false);
    });
  }

  // Newline-separated names/slugs. Chunks if over the panel's 512-byte MQTT buffer.
  function publishSelect(ids) {
    const unique = [];
    const seen = Object.create(null);
    (ids || []).forEach((raw) => {
      const id = String(raw || "").trim();
      if (!id) return;
      const key = id.toLowerCase();
      if (seen[key]) return;
      seen[key] = true;
      unique.push(id);
    });
    if (!unique.length) {
      return publish("select", "clear", false);
    }
    const joined = unique.join("\n");
    if (joined.length <= 400) {
      return publish("select", joined, false);
    }
    let chunk = [];
    let size = 0;
    let first = true;
    unique.forEach((id) => {
      if (chunk.length && size + id.length + 1 > 400) {
        publish("select", (first ? "" : "+") + chunk.join("\n"), false);
        first = false;
        chunk = [];
        size = 0;
      }
      chunk.push(id);
      size += id.length + 1;
    });
    if (chunk.length) publish("select", (first ? "" : "+") + chunk.join("\n"), false);
    return true;
  }

  function getPrefix() {
    return prefix;
  }

  return {
    setStatus,
    connect,
    disconnect,
    isConnected,
    publishPattern,
    publishParam,
    publishParams,
    publishMessage,
    publishSnapshot,
    queryPatterns,
    publishSelect,
    onSelectAck,
    getPrefix
  };
}

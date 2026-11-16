/**
 * Setup tab: bundled Python MQTT broker + live LAN IP for the panel.
 */

const LAN_HTTP = "http://127.0.0.1:19001/lan";
const POLL_WAITING_MS = 3000;
const POLL_OK_MS = 15000;

export function initBrokerGuide(root) {
  const copyLanBig = root.querySelector("#copyLanBig");
  const hint = root.querySelector("#lanHint");
  const brokerState = root.querySelector("#brokerState");
  let lastIp = "";
  let brokerOnline = false;
  let timer = 0;

  function setBrokerState(online, extra) {
    brokerOnline = online;
    if (!brokerState) return;
    brokerState.className = "broker-state " + (online ? "on" : "off");
    brokerState.textContent = online
      ? "broker: running" + (extra ? " · " + extra : "")
      : "broker: not running — start run_local_broker.py";
  }

  function setIp(ip) {
    const value = (ip || "").trim();
    if (!value) return;
    lastIp = value;
    if (copyLanBig) copyLanBig.textContent = value;
    try {
      localStorage.setItem("pf-director-lan", value);
    } catch (_) {
      /* ignore */
    }
  }

  root.querySelectorAll("[data-copy]").forEach((btn) => {
    btn.addEventListener("click", async () => {
      const sel = btn.getAttribute("data-copy");
      const el = root.querySelector(sel);
      const text = el ? el.textContent.trim() : "";
      try {
        await navigator.clipboard.writeText(text);
        btn.textContent = "copied";
        setTimeout(() => {
          btn.textContent = "copy";
        }, 1200);
      } catch (_) {
        btn.textContent = "fail";
      }
    });
  });

  async function poll() {
    try {
      const r = await fetch(LAN_HTTP, { cache: "no-store" });
      const d = await r.json();
      if (d && d.ok && d.ip) {
        const extras =
          Array.isArray(d.ips) && d.ips.length > 1
            ? d.ips.filter((x) => x !== d.ip).join(", ")
            : "";
        setIp(d.ip);
        setBrokerState(true, extras ? "also " + extras : "");
        if (hint) {
          hint.textContent =
            "Live from this PC. Paste it on the panel MQTT page (Director mode).";
        }
        return;
      }
    } catch (_) {
      /* helper not up */
    }
    setBrokerState(false);
    if (!lastIp) {
      try {
        lastIp = localStorage.getItem("pf-director-lan") || "";
      } catch (_) {
        lastIp = "";
      }
      if (lastIp && copyLanBig) copyLanBig.textContent = lastIp + " (last seen)";
    }
    if (hint) {
      hint.textContent = lastIp
        ? "Broker offline — showing the last IP. Start run_local_broker.py to refresh."
        : "Start run_local_broker.py on this PC. The IP appears here automatically.";
    }
  }

  function setupVisible() {
    return root.classList.contains("on") && document.visibilityState === "visible";
  }

  function schedule() {
    const ms = brokerOnline && lastIp ? POLL_OK_MS : POLL_WAITING_MS;
    timer = window.setTimeout(async () => {
      if (setupVisible()) await poll();
      schedule();
    }, ms);
  }

  poll().then(schedule);
}

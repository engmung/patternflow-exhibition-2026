/**
 * FlowLocal content library client (patterns / shows / launchpads).
 * Used when Director runs on the FlowLocal box (same origin).
 */

export function libraryBase() {
  if (typeof window === "undefined") return "";
  if (window.PF_IS_FLOWLOCAL) return "";
  const rt = window.PF_RUNTIME || {};
  if (rt.flowlocal) return "";
  return "";
}

export function isFlowLocalHost() {
  if (typeof window === "undefined") return false;
  if (window.PF_IS_FLOWLOCAL) return true;
  const rt = window.PF_RUNTIME || {};
  if (rt.flowlocal) return true;
  const h = location.hostname;
  // Multi-island AP IPs are not always .66.1
  if (h === "flowlocal.local") return true;
  if (/^192\.168\.\d+\.\d+$/.test(h)) return true;
  return false;
}

function url(path) {
  return libraryBase() + path;
}

function libraryError(d, fallback) {
  const msg = (d && d.error) || fallback || "library error";
  if (msg === "media not mounted") {
    return "USB/SD not mounted — decks live in launchpads/ on the stick (OTG jumper on, stick in USB jack)";
  }
  return msg;
}

export async function libraryStatus() {
  const r = await fetch(url("/api/library"), { cache: "no-store" });
  if (!r.ok) throw new Error("library unavailable");
  return r.json();
}

export async function listLibrary(kind) {
  const r = await fetch(url("/api/library/" + kind), { cache: "no-store" });
  const d = await r.json().catch(() => ({}));
  if (!r.ok || d.ok === false) throw new Error(libraryError(d, "list failed"));
  return d.files || [];
}

export async function getLibraryFile(kind, name) {
  const r = await fetch(
    url("/api/library/" + kind + "/file?name=" + encodeURIComponent(name)),
    { cache: "no-store" }
  );
  if (!r.ok) {
    let msg = "download failed";
    try {
      const d = await r.json();
      msg = libraryError(d, msg);
    } catch (_) {}
    throw new Error(msg);
  }
  return r;
}

export async function putLibraryFile(kind, name, body, contentType) {
  const r = await fetch(
    url("/api/library/" + kind + "/file?name=" + encodeURIComponent(name)),
    {
      method: "PUT",
      headers: { "Content-Type": contentType || "application/octet-stream" },
      body
    }
  );
  const d = await r.json().catch(() => ({}));
  if (!r.ok || d.ok === false) throw new Error(libraryError(d, "upload failed"));
  return d;
}

export async function deleteLibraryFile(kind, name) {
  const r = await fetch(
    url("/api/library/" + kind + "/file?name=" + encodeURIComponent(name)),
    { method: "DELETE" }
  );
  const d = await r.json().catch(() => ({}));
  if (!r.ok || d.ok === false) throw new Error(libraryError(d, "delete failed"));
  return d;
}

export function slugifyName(raw, fallback) {
  const slug = String(raw || fallback || "untitled")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_|_$/g, "")
    .slice(0, 32);
  return slug || fallback || "untitled";
}

/**
 * Pick a library file. Uses an on-page dialog (window.prompt fails on many phones).
 */
export async function pickLibraryFile(kind, extHint) {
  const files = await listLibrary(kind);
  if (!files.length) {
    throw new Error(
      "no " + kind + " on FlowLocal yet — copy them into " + kind + "/ on the USB stick"
    );
  }
  return new Promise((resolve, reject) => {
    const prev = document.querySelector("dialog.pf-lib-pick");
    if (prev) prev.remove();

    const dlg = document.createElement("dialog");
    dlg.className = "pf-lib-pick";
    const title = document.createElement("h3");
    title.textContent = "Open from FlowLocal — " + kind;
    const list = document.createElement("div");
    list.className = "pf-lib-pick-list";
    files.forEach((f) => {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "btn pf-lib-pick-item";
      btn.textContent = f.name + (f.size != null ? "  (" + f.size + " B)" : "");
      btn.addEventListener("click", () => {
        dlg.close();
        dlg.remove();
        resolve(f);
      });
      list.appendChild(btn);
    });
    const cancel = document.createElement("button");
    cancel.type = "button";
    cancel.className = "btn";
    cancel.textContent = "Cancel";
    cancel.addEventListener("click", () => {
      dlg.close();
      dlg.remove();
      reject(new Error("cancelled"));
    });
    const actions = document.createElement("div");
    actions.className = "actions";
    actions.appendChild(cancel);
    dlg.appendChild(title);
    dlg.appendChild(list);
    dlg.appendChild(actions);
    dlg.addEventListener("cancel", (ev) => {
      ev.preventDefault();
      dlg.close();
      dlg.remove();
      reject(new Error("cancelled"));
    });
    document.body.appendChild(dlg);
    if (typeof dlg.showModal === "function") dlg.showModal();
    else {
      // Very old WebViews: fall back to numbered prompt
      const labels = files.map((f, i) => i + 1 + ". " + f.name);
      const choice = window.prompt(
        "Open from FlowLocal (" + kind + "):\n" + labels.join("\n") + "\n\nEnter number:",
        "1"
      );
      dlg.remove();
      if (choice == null) {
        reject(new Error("cancelled"));
        return;
      }
      const idx = parseInt(choice, 10) - 1;
      if (!Number.isFinite(idx) || idx < 0 || idx >= files.length) {
        reject(new Error("invalid selection"));
        return;
      }
      resolve(files[idx]);
    }
  });
}

const PANEL_HTTP_KEY = "pf-director-panel-http";

export function getPanelHttpBase() {
  const el = typeof document !== "undefined" ? document.getElementById("panelHttp") : null;
  let v = el && el.value ? el.value.trim() : "";
  if (!v) {
    try {
      v = localStorage.getItem(PANEL_HTTP_KEY) || "";
    } catch (_) {}
  }
  if (!v) v = "http://patternflow.local";
  v = v.replace(/\/+$/, "");
  if (!/^https?:\/\//i.test(v)) v = "http://" + v;
  return v;
}

export function setPanelHttpBase(value) {
  const v = String(value || "").trim();
  try {
    localStorage.setItem(PANEL_HTTP_KEY, v);
  } catch (_) {}
  const el = typeof document !== "undefined" ? document.getElementById("panelHttp") : null;
  if (el && v) el.value = v;
}

/** Ask the panel to pull files from a FlowLocal library host. */
export async function panelPullLibrary(kind, names, opts) {
  const panel = (opts && opts.panelBase) || getPanelHttpBase();
  const host =
    (opts && opts.libraryHost) ||
    (typeof location !== "undefined" && location.hostname) ||
    "192.168.66.1";
  const list = (names || []).map((n) => String(n || "").trim()).filter(Boolean);
  if (!list.length) throw new Error("nothing to pull");
  const body = new URLSearchParams();
  body.set("host", host);
  body.set("kind", kind);
  body.set("names", list.join(","));
  const r = await fetch(panel + "/api/library/pull", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body
  });
  const d = await r.json().catch(() => ({}));
  if (!r.ok || d.ok === false) {
    throw new Error(d.error || "panel pull failed (" + r.status + ")");
  }
  return d;
}

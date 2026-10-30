/* Patternflow Director — minimal offline shell */
const CACHE = "pf-director-v29";
const ASSETS = [
  "./",
  "./index.html",
  "./manifest.webmanifest",
  "./css/director.css",
  "./js/main.js",
  "./js/mqtt.js",
  "./js/timeline.js",
  "./js/performance.js",
  "./js/show-table.js",
  "./js/deck.js",
  "./js/launchpad.js",
  "./js/broker-guide.js",
  "./js/patterns-meta.js",
  "./js/patterns-catalog.js",
  "./js/patterns-catalog.json",
  "./vendor/mqtt.min.js",
  "./examples/director-demo.pfs",
  "./examples/simone_performative_deck.json",
  "./icons/icon-192.png",
  "./icons/icon-512.png"
];

self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(CACHE).then((cache) => cache.addAll(ASSETS)).then(() => self.skipWaiting())
  );
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k)))
    ).then(() => self.clients.claim())
  );
});

self.addEventListener("fetch", (event) => {
  const req = event.request;
  if (req.method !== "GET") return;
  event.respondWith(
    caches.match(req).then((cached) => cached || fetch(req).catch(() => cached))
  );
});

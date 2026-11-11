// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - the simulator, as a WebAssembly module
//
// The console's demo mode: the hub's Conductor and up to 24 panel Followers,
// the same source the firmware compiles, running in the browser with no
// hardware. Built by ensemble/tools/build_wasm.py into
// ensemble/console/pfe_sim.wasm (committed, so nobody needs a compiler to
// open the console).
//
// Freestanding: no libc, no imports. The page calls the exports below,
// writes console lines into the buffer at pfe_io() and reads the hub's
// lines back from pfe_out().
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#include "pfe_sim.h"

// The compiler lowers struct copies and __builtin_mem* to these, and with
// -nostdlib nobody else provides them. Plain loops; this file is built with
// -fno-builtin so they are not turned back into calls to themselves.
extern "C" {
void* memcpy(void* dst, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  for (size_t i = 0; i < n; i++) d[i] = s[i];
  return dst;
}
void* memmove(void* dst, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  if (d < s) for (size_t i = 0; i < n; i++) d[i] = s[i];
  else for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
  return dst;
}
void* memset(void* dst, int c, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  for (size_t i = 0; i < n; i++) d[i] = (unsigned char)c;
  return dst;
}
int memcmp(const void* a, const void* b, size_t n) {
  const unsigned char* x = (const unsigned char*)a;
  const unsigned char* y = (const unsigned char*)b;
  for (size_t i = 0; i < n; i++)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}
size_t strlen(const char* s) {
  size_t n = 0;
  while (s[n]) n++;
  return n;
}
}

// Placement new, without <new>.
inline void* operator new(size_t, void* p) noexcept { return p; }

#define PFE_EXPORT(name) __attribute__((export_name(#name))) name

namespace {

constexpr size_t IO_SIZE = 8192;
alignas(16) unsigned char g_store[sizeof(pfe::Sim)];
pfe::Sim* g_sim = nullptr;
char g_io[IO_SIZE];

bool ok(int panel) { return g_sim && panel >= 0 && panel < g_sim->count(); }

}  // namespace

extern "C" {

// Start an installation of `panels` virtual panels. Calling it again starts over.
void PFE_EXPORT(pfe_init)(int panels, uint32_t seed, uint32_t nowMs) {
  g_sim = new (g_store) pfe::Sim();
  g_sim->begin(panels, seed, nowMs);
}

void PFE_EXPORT(pfe_tick)(uint32_t nowMs) {
  if (g_sim) g_sim->tick(nowMs);
}

// Text in: one console line at pfe_io(), `len` bytes.
char* PFE_EXPORT(pfe_io)() { return g_io; }
int PFE_EXPORT(pfe_io_size)() { return (int)IO_SIZE; }
void PFE_EXPORT(pfe_command)(int len) {
  if (g_sim && len > 0 && (size_t)len < IO_SIZE) g_sim->command(g_io, (size_t)len);
}

// Text out: the hub's lines, newline-separated.
const char* PFE_EXPORT(pfe_out)() { return g_sim ? g_sim->out() : g_io; }
int PFE_EXPORT(pfe_out_len)() { return g_sim ? (int)g_sim->outLen() : 0; }
void PFE_EXPORT(pfe_out_clear)() {
  if (g_sim) g_sim->outClear();
}

// A hand on a virtual panel, the venue's power, the air.
void PFE_EXPORT(pfe_hand)(int panel, int knob, int detents) {
  if (g_sim) g_sim->hand(panel, knob, detents);
}
void PFE_EXPORT(pfe_pick)(int panel, int pattern) {
  if (g_sim) g_sim->pick(panel, pattern);
}
void PFE_EXPORT(pfe_power)(int panel, int on) {
  if (g_sim) g_sim->power(panel, on != 0);
}
void PFE_EXPORT(pfe_net)(uint32_t latencyMs, uint32_t jitterMs, uint32_t lossPermille) {
  if (g_sim) g_sim->network(latencyMs, jitterMs, lossPermille);
}
void PFE_EXPORT(pfe_restart_hub)() {
  if (g_sim) g_sim->restartHub();
}

// What a virtual panel's LEDs would show - not what the hub believes.
int PFE_EXPORT(pfe_count)() { return g_sim ? g_sim->count() : 0; }
float PFE_EXPORT(pfe_lane)(int panel, int knob) {
  return ok(panel) && knob >= 0 && knob < pfe::KNOBS ? g_sim->panel(panel).f.lane(knob) : 0.0f;
}
int PFE_EXPORT(pfe_pattern)(int panel) { return ok(panel) ? g_sim->panel(panel).actual : -1; }
int PFE_EXPORT(pfe_powered)(int panel) { return ok(panel) && g_sim->panel(panel).powered ? 1 : 0; }
int PFE_EXPORT(pfe_linked)(int panel) {
  return ok(panel) && g_sim->panel(panel).powered && g_sim->panel(panel).f.linked(g_sim->now()) ? 1 : 0;
}
int PFE_EXPORT(pfe_identify)(int panel) { return ok(panel) && g_sim->panel(panel).out.identify ? 1 : 0; }
int PFE_EXPORT(pfe_panel_id)(int panel) { return ok(panel) ? g_sim->panel(panel).f.panelId() : 0; }
// The panel's uid as 12 hex digits at pfe_io().
void PFE_EXPORT(pfe_uid)(int panel) {
  pfe::Out o(g_io, IO_SIZE);
  if (ok(panel))
    for (int b = 0; b < 6; b++) o.hex8(g_sim->panel(panel).uid[b]);
}

// The hub's setup as bytes at pfe_io(), for the page to keep and give back.
int PFE_EXPORT(pfe_save)() {
  return g_sim ? (int)g_sim->conductor().save((uint8_t*)g_io, IO_SIZE) : 0;
}
int PFE_EXPORT(pfe_load)(int len) {
  if (!g_sim || len <= 0 || (size_t)len > IO_SIZE) return 0;
  return g_sim->conductor().load((const uint8_t*)g_io, (size_t)len) ? 1 : 0;
}

}  // extern "C"

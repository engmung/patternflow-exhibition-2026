# Patternflow Ensemble protocol

**PFE/1** (2026-11-11). Two links: panels and hub talk in datagrams, hub and console talk in lines of JSON. The code is [`core/pfe_proto.h`](core/pfe_proto.h) and [`core/pfe_console.h`](core/pfe_console.h); where this file and the code disagree, the tests decide ([`tests/test_core.cpp`](tests/test_core.cpp) holds a REPORT as the bytes it must be).

Not verified on hardware yet. See [README.md §8](README.md#8-아직-실기로-확인-안-된-것).

## The panel link

| | |
|---|---|
| Transport | UDP. The hub listens on **7400**. A panel sends from any port (7401 by default) and the hub answers the address and port a report came from. |
| Finding the hub | A panel that knows no hub sends its reports to `255.255.255.255:7400`. The sender of the first STATE it receives is its hub, and it talks to that address alone from then on. After 5 s without a STATE it broadcasts again. `PF_ENSEMBLE_HOST` fixes the address instead. |
| Byte order | Little-endian. |
| Header | Every message starts `'P' 'F' 'E'`, version `1`, then the type. A reader ignores anything else, and anything shorter than its type needs. Longer is accepted: a later version may append fields. |
| Identity | A panel is its Wi-Fi station MAC, six bytes, called `uid`. Addresses can change; the uid does not. |

Both directions carry whole state, never a change. A lost datagram is repaired by the next one.

### REPORT, panel to hub, type `0x01`, 36 bytes

Sent every 500 ms, within 33 ms of a hand moving a knob, and within 100 ms of applying a new STATE.

| Offset | Size | Field | |
|---|---|---|---|
| 0 | 5 | header | `50 46 45 01 01` |
| 5 | 1 | `flags` | bit 0 `BOOT`: no STATE applied since this panel started · bit 1 asleep · bit 2 a device screen has the knobs · bit 3 not in RUNNING mode |
| 6 | 6 | `uid` | |
| 12 | 2 | `seq` | counts this panel's reports |
| 14 | 2 | `ack` | `seq` of the last STATE applied, 0 for none |
| 16 | 1 | `pattern` | registry index the panel is on or switching to, `FF` unknown |
| 17 | 1 | `patTouch` | moves when a person picks a pattern at the panel |
| 18 | 8 | `knob[4]` | u16 each, 0 to 1000 |
| 26 | 4 | `touch[4]` | each moves when a hand turns that knob |
| 30 | 1 | `rssi` | dBm, signed |
| 31 | 1 | `fps` | |
| 32 | 4 | `uptimeS` | seconds since the panel started |

### INFO, panel to hub, type `0x02`, 72 bytes

Sent at start, when the pattern or its name changes, when a STATE asks, and every 10 s.

| Offset | Size | Field | |
|---|---|---|---|
| 0 | 5 | header | `50 46 45 01 02` |
| 5 | 1 | reserved | 0 |
| 6 | 6 | `uid` | |
| 12 | 1 | `pattern` | the pattern running now |
| 13 | 1 | `patternCount` | 0 when unknown |
| 14 | 1 | `detents` | detents from one end of a knob to the other |
| 15 | 1 | `k4Steps` | K4 detents per pattern step |
| 16 | 16 | `fw` | zero-padded text, e.g. `3.12.0` |
| 32 | 16 | `variant` | e.g. `ensemble v0.1.0` |
| 48 | 24 | `patternName` | the running pattern's display name |

The hub keeps one table of pattern names by index for the whole ensemble, filled from these as panels visit patterns. It assumes every panel has the same patterns installed in the same order.

### STATE, hub to panel, type `0x10`, 32 bytes

Sent when its content changes (at most every 30 ms), again after 200 ms while the panel has not acknowledged it, and once a second regardless.

| Offset | Size | Field | |
|---|---|---|---|
| 0 | 5 | header | `50 46 45 01 10` |
| 5 | 1 | `flags` | bit 0 K4 steps the pattern (else it is a knob) · bit 1 lock the long-press screens · bit 2 show the panel's number · bit 3 send an INFO |
| 6 | 2 | `seq` | never 0 |
| 8 | 1 | `panelId` | the number the console shows |
| 9 | 1 | `pattern` | target, `FF` for none |
| 10 | 1 | `patAck` | the `patTouch` this target accounts for |
| 11 | 1 | `patMin` | K4's range, first pattern |
| 12 | 1 | `patMax` | last pattern, `FF` for the panel's last |
| 13 | 1 | `k4Steps` | 0 for the panel's own default |
| 14 | 8 | `knob[4]` | targets, u16 each, 0 to 1000 |
| 22 | 4 | `ack[4]` | the `touch` each target accounts for |
| 26 | 2 | `glideMs` | time to ease into a new target, 0 for at once |
| 28 | 4 | `hubMs` | the hub's clock, for logs |

### CMD, hub to panel, type `0x11`, 8 bytes

`50 46 45 01 11 <cmd> 50 46`. The two bytes after the command are a guard. Command `01` restarts the panel 300 ms later.

### The rules

**Hands win.** A panel obeys `knob[k]` only when `ack[k]` equals its own `touch[k]`, and `pattern` only when `patAck` equals its `patTouch`. A target the hub sent before it heard of the last turn is ignored, so a knob being turned is never pulled back.

**A hand is a counter that moved.** The hub compares each report's `touch` and `patTouch` with the previous report's. A counter that moved means a hand, and the reported value is where the hand left it. Following a target does not move a counter, so nothing echoes.

**What does not count as a hand.** A report with `BOOT` set, or whose `uptimeS` went back by more than 2 s, comes from a panel that started over: its counters began again and the hub's target stands. The same holds for the first report after more than 10 s of silence. A report whose `seq` is not newer than the last one's, within 2 s, arrived late and is dropped.

**Newest emitted, once arrived.** On a ripple channel a hand's value leaves its panel and reaches each other panel after `distance / speed`. Each panel takes, per channel, the most recently emitted value that has arrived. No value takes longer than 30 s.

**Joining.** A panel the hub holds no target for takes, on each linked channel, the value the ensemble last emitted there. With none emitted yet, its own value becomes that. A panel the hub already knows is told the hub's target.

**Old STATEs.** A panel ignores a STATE whose `seq` is not newer than the one it holds, unless it has heard nothing for 1.5 s (the hub restarted).

**A pattern that will not load.** A panel asks the device for a target pattern at most once a second, five times, then gives up on that target and reports the pattern it has.

## The console link

One JSON object per line, UTF-8, `\n` at the end. The same lines go over the hub's USB serial port, its UART (115200), and its WebSocket at `/ws`. On a serial port the hub sends lines only while a console is there: one that sent a JSON line in the last 10 s. A line that does not start with `{` is not protocol (the UART also carries the boot log).

### Console to hub

Every command has `"c"`. An optional `"r"` (integer) comes back in the reply. Panels are named by `uid` (12 hex digits) or `"#<number>"`.

| `c` | Fields | |
|---|---|---|
| `hello` | | The hub says who it is, then sends everything |
| `get` | | Everything again: cfg, names, scenes, every panel |
| `ping` | | Keeps a serial port listed as a console. No reply |
| `cfg` | `link` (four of `"solo"`, `"sync"`, `"ripple"`, or `null` to keep), `plink`, `speed` (units per second), `glide` (ms), `k4` (true: K4 steps patterns), `lock`, `pmin`, `pmax` (255: to the last), `k4s` | Any subset |
| `panel` | `uid`, and any of `x`, `y`, `id` (1 to 250), `name`, `linked` | Two panels never share a number: the other one takes this panel's old one |
| `forget` | `uid` | Drops a panel from the table |
| `set` | `to` (a panel or `"all"`), and any of `pat`, `k` (four numbers or `null`), `kn` + `v` (one knob), `glide`. With `to: "all"`: `mode: "ripple"` and `from` (a panel) make it a wave | To one panel it stays on that panel. This is how each panel gets a pattern of its own |
| `scene` | `op` (`save`, `recall`, `clear`), `slot` (0 to 7), and for recall `mode`, `from`, `glide` | A scene is every panel's pattern and knobs |
| `identify` | `uid` (or `"all"`), `on` | Switches itself off after 30 s |
| `reboot` | `uid` (a panel, or `"hub"`) | |

Replies: `{"t":"ack","c":"cfg","r":12}` or `{"t":"err","c":"set","m":"no such panel"}`.

### Hub to console

| `t` | When | Fields |
|---|---|---|
| `hello` | answer to `hello` | `proto`, `port`, `fw`, `hub`, `mode` (`hotspot` or `router`), `ssid` |
| `cfg` | on change | `link`, `plink`, `speed`, `glide`, `k4`, `lock`, `pmin`, `pmax`, `k4s`, `maxPanels`, `scenes` |
| `names` | on change | `n`: pattern names by index, `""` where unknown |
| `scenes` | on change | `u`: panels stored in each slot, 0 for an empty one |
| `p` | a panel changed (at most every 100 ms; 500 ms when the UART is the only console), and every 2 s | below |
| `w` | a value started travelling | `q` sequence, `c` channel (0 to 3 knobs, 4 pattern, 5 scene), `o` panel index it started at (-1 for the console), `x`, `y`, `v` value, `now` (1: everywhere at once), `d` ms until it has landed everywhere |
| `hub` | every second | `ms` uptime, `proto`, `panels`, `online`, `rx`, `tx`, `bad`, `ev` values emitted, `fly` values in flight, `drop`, `full`, `stale` |
| `gone` | a panel was forgotten | `i` |
| `log` | something happened | `ms`, `m` |

A `p` line:

| Field | |
|---|---|
| `i` | the hub's index for this panel; `p`, `w` and `gone` use it |
| `uid`, `id`, `n`, `x`, `y`, `l` | identity, number, name, place, linked |
| `on`, `age`, `ip` | online, ms since the last report, address |
| `rssi`, `fps`, `up`, `fl` | from the last report: dBm, fps, uptime in seconds, flags |
| `pat`, `pn`, `pc` | reported pattern, its name, how many patterns the panel has |
| `k`, `tp`, `tk` | reported knobs, target pattern, target knobs |
| `ok` | 1 when the panel reports what the hub wants |
| `idf` | 1 while identify is on |
| `fw`, `var` | firmware and variant |
| `rst`, `lost`, `hands`, `rep` | restarts seen, reports lost, hand moves, reports received |

## The hub over HTTP

For scripts, on the hub's port 80 beside the console page and `/ws`. Read-only.

| Route | |
|---|---|
| `GET /api/status` | The hub itself: `fw`, `proto`, `uptime_s`, `mode`, `panels`, `online`, `stations`, `heap`, `rx`, `tx`, `bad`, `lines_dropped`, `usb_stalls` |
| `GET /api/panels` | `{"panels":[{"id":1,"uid":"246f28010203","n":"door","on":1,"ip":"192.168.66.2","fw":"3.12.0","var":"ensemble v0.1.0"}]}`: every panel the hub knows, refreshed once a second. `ip` is where the panel's last report came from, which is also where its own web console is. [`tools/panels.py`](tools/panels.py) `--hub` reads this |

A panel says its side in its own `/api/status`, in an `ensemble` object: `uid` (the same twelve hex digits), `linked`, `id`, `hub`, `states`, `sent`, `detents`, `knob`.

## What the hub keeps

Across restarts, in flash: the config, each panel's uid, number, place, name and linked flag, and the scenes. Not the live targets: after a restart the panels report where they are and the hub takes that.

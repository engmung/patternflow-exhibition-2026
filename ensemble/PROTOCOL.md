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

### REPORT, panel to hub, type `0x01`, 37 bytes

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
| 36 | 1 | `brightness` | the panel's brightness now, 5 to 255. Appended 2026-11-14: a panel from before sends 36 bytes, read as `0` (does not say) |

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

### STATE, hub to panel, type `0x10`, 33 bytes

Sent when its content changes (at most every 30 ms), again after 200 ms while the panel has not acknowledged it, and once a second regardless.

| Offset | Size | Field | |
|---|---|---|---|
| 0 | 5 | header | `50 46 45 01 10` |
| 5 | 1 | `flags` | bit 0 K4 steps the pattern (else it is a knob) · bit 1 lock the long-press screens · bit 2 show the panel's mark (shape and colour) · bit 3 send an INFO |
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
| 32 | 1 | `brightness` | target, 5 to 255; `0` leaves the panel's own. Appended 2026-11-14: a hub from before sends 32 bytes, read as `0` |
| 33 | 1 | `sleep` | an order to sleep or to wake; `0` none. Low two bits: `1` wake, `2` sleep; the rest count the orders, so the same order given again is a new one. Appended 2026-11-15: a hub from before sends 33 bytes, read as `0` |

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

**Brightness is set, not held.** It is not a channel: it does not travel as a wave, and a hand at a panel does not send it on. A panel takes a `brightness` target when it differs from the last one it took since it started, the way `GET /api/display?brightness=` sets it (the device stores it once it has been still). A brightness set at the panel afterwards (K1's long press) stays until the console sends a new one. The hub holds targets in RAM only; a panel it meets for the first time since it started takes the brightness the console last gave every panel, if any.

**Sleep is an order, not a state.** A panel carries an order out once, when its `sleep` byte differs from the last one it acted on. A hand on a knob wakes a sleeping panel (the panel's own rule), and the hub does not put it back: it stays awake until the console orders sleep again. A panel that joins while the console's last order to everyone was sleep is given that order. The panel says whether it is asleep in its report's flags (`RF_SLEEPING`), at once when it changes.

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
| `panel` | `uid`, and any of `x`, `y`, `z`, `rx`, `ry`, `rz`, `id` (1 to 250), `name`, `linked` | Two panels never share a number: the other one takes this panel's old one |
| `forget` | `uid` | Drops a panel from the table |
| `set` | `to` (a panel or `"all"`), and any of `pat`, `k` (four numbers or `null`), `kn` + `v` (one knob), `glide`, `br` (brightness, 5 to 255), `sleep` (`true`: go to sleep, `false`: wake). With `to: "all"`: `mode: "ripple"` and `from` (a panel) make it a wave | To one panel it stays on that panel. This is how each panel gets a pattern of its own. `br` alone leaves the pattern and knobs as they are; to `"all"` it is also given to panels that join later |
| `scene` | `op` (`save`, `recall`, `clear`), `slot` (0 to 7), and for recall `mode`, `from`, `glide` | A scene is every panel's pattern and knobs |
| `identify` | `uid` (or `"all"`), `on` | The panel shows its mark: a big shape in a colour on black, both from its number (`identifyMark` in `core/pfe_proto.h`: circle, square, triangle and red, green, blue, yellow, stepping together, twelve marks). One panel switches itself off after 30 s. `"all"` stays on until it is switched off, or for 30 minutes, and a panel that joins meanwhile shows its mark too |
| `reboot` | `uid` (a panel, or `"hub"`) | |
| `names` | `at` + `n` (names for the patterns from that number on), and/or `clear: true` with `from` | What the console knows after giving the panels another set of patterns. `clear` forgets the names from `from` on |
| `graph` | `op`: `begin` + `len`, `data` + `d` (base64, up to 240 bytes), `end`; or `clear`; or `on` / `off`; or `get` | The node graph's program (`core/pfe_graph.h`), in pieces. `end` answers `err` with what is wrong, or `ack` and the program runs (and is kept in the hub's flash). `on` and `off` are the graph's switch (off, which is how a hub starts: the program is kept, drives nothing and takes no controller). `get` answers with a `graph` line |
| `midi` | `ch` (1 to 16), `cc`, `v` (0 to 127) | A MIDI controller moved from the console instead of a MIDI port: for the graph |
| `mon` | `on`, `panel` | While on: a `mi` line for each controller that comes in (25 a second of each at most) and, five times a second, an `nv` line with the graph's registers as computed for that panel |
| `http` | `id`, `to` (a panel), `m` (`GET`, `POST`, `PUT`, `DELETE`), `path`, `h` (headers), `len` (bytes of body), `tmo` (ms, 1000 to 120000) | A request to that panel's own web server, passed on by the hub. See below. No `ack` |
| `hb` | `id`, `d` | The next piece of that request's body: base64 of up to 1080 bytes (720 over the WebSocket, where a line has to fit one packet) |

Replies: `{"t":"ack","c":"cfg","r":12}` or `{"t":"err","c":"set","m":"no such panel"}`.

A panel's `fl` in its `p` line is the flags of its last report: 1 no STATE applied yet, 2 asleep, 4 one of its own screens has the knobs, 8 not in RUNNING mode, **16 solo** - another feature has the panel (a show is playing, `features/show`), and for that long it is not one of the ensemble: its hands are not counted, the hub's knob values and pattern wait, and a pattern the show switches to is not passed on. The hub needs to do nothing for this; a panel that is solo takes what the ensemble shows 0.4 s after the show ends.

### A panel's web server, through the hub

A panel has a web server (its patterns, its status), and a console on the USB cable is not on the panels' network. So the hub passes HTTP along (`core/pfe_relay.h`):

```
-> {"c":"http","id":7,"to":"<uid>","m":"PUT","path":"/api/patterns","h":{"X-PF-Name":"a.pfm","X-PF-Last":"0"},"len":2500}
-> {"c":"hb","id":7,"d":"<base64>"}        the first piece
<- {"t":"hk","id":7,"n":1080}              it went: send the next
   ...
<- {"t":"http","id":7,"s":200,"len":11}
<- {"t":"hb","id":7,"d":"<base64>"}
<- {"t":"he","id":7,"n":11}
```

One piece at a time: the next `hb` after its `hk`. A request ends with `he`, with `err` if it failed anywhere (`timeout`, `the panel does not answer`, `busy with another request`, ...). The hub carries four requests at once, so four panels can be worked on together; a request to an offline panel is refused at once. The hub closes the connection as soon as the panel's answer is whole, because a panel serves one connection at a time.

### Hub to console

| `t` | When | Fields |
|---|---|---|
| `hello` | answer to `hello` | `proto`, `port`, `fw`, `hub`, `mode` (`hotspot` or `router`), `ssid` |
| `cfg` | on change | `link`, `plink`, `speed`, `glide`, `k4`, `lock`, `pmin`, `pmax`, `k4s`, `maxPanels`, `scenes` |
| `names` | on change | `n`: pattern names by index, `""` where unknown. In pieces of 24: `at` is the index of this line's first name, `of` how many there are in all; `at: 0` starts the list afresh |
| `scenes` | on change | `u`: panels stored in each slot, 0 for an empty one |
| `p` | a panel changed (at most every 100 ms; 500 ms when the UART is the only console), and every 2 s | below |
| `w` | a value started travelling | `q` sequence, `c` channel (0 to 3 knobs, 4 pattern, 5 scene), `o` panel index it started at (-1 for the console), `x`, `y`, `z`, `v` value, `now` (1: everywhere at once), `d` ms until it has landed everywhere |
| `hub` | every second | `ms` uptime, `proto`, `panels`, `online`, `rx`, `tx`, `bad`, `ev` values emitted, `fly` values in flight, `drop`, `full`, `stale` |
| `gone` | a panel was forgotten | `i` |
| `log` | something happened | `ms`, `m` |
| `graph` | answer to `graph` `get` | `on` (1: there is a program), `en` (1: the switch is on), `len` its bytes, `cc`: the controllers it names, `[channel, number]` each |
| `mi` | a MIDI controller came in (while `mon` is on) | `ch`, `cc`, `v` |
| `nv` | five times a second (while `mon` is on) | `p` the panel's number, `v`: the program's registers for that panel |
| `hk` | a piece of a request's body went to the panel | `id`, `n` bytes passed on so far |
| `http` | the panel answered | `id`, `s` status, `len` its Content-Length (-1: not said) |
| `hb` | the answer's body, in order | `id`, `d` base64 of up to 360 bytes |
| `he` | the request is over | `id`, and `n` (bytes of body in all) or `err` |

A `p` line:

| Field | |
|---|---|
| `i` | the hub's index for this panel; `p`, `w` and `gone` use it |
| `uid`, `id`, `n`, `x`, `y`, `z`, `r`, `l` | identity, number, name, place, turn, linked. `r` is `[rx, ry, rz]`, how the panel is turned in degrees about the 3D view's X, Y and Z in that order (Blender's XYZ Euler); the hub only keeps it for whoever draws the room, a wave does not care which way a panel faces. The place is in map units, seen from the front of the wall: `x` across, `y` down, `z` depth away from the viewer (0 on a flat wall). A wave travels the straight line between two places. The console's 3D view shows the point as Blender would, (x, z, -y) with Z up |
| `on`, `age`, `ip` | online, ms since the last report, address |
| `rssi`, `fps`, `up`, `fl` | from the last report: dBm, fps, uptime in seconds, flags |
| `pat`, `pn`, `pc` | reported pattern, its name, how many patterns the panel has |
| `k`, `tp`, `tk` | reported knobs, target pattern, target knobs |
| `br`, `tb` | reported brightness (`0`: the panel does not say), target brightness (`0`: none) |
| `sl` | `1` while the panel is asleep |
| `ok` | 1 when the panel reports what the hub wants |
| `idf` | 1 while identify is on |
| `fw`, `var` | firmware and variant |
| `rst`, `lost`, `hands`, `rep` | restarts seen, reports lost, hand moves, reports received |

## The hub over HTTP

For scripts, on the hub's port 80 beside the console page and `/ws`. Read-only.

| Route | |
|---|---|
| `GET /api/status` | The hub itself: `fw`, `proto`, `uptime_s`, `mode`, `panels`, `online`, `stations`, `heap`, `rx`, `tx`, `bad`, `lines_dropped`, `usb_stalls` (times the USB console stopped taking lines for half a second: a console that is not reading), and two objects about the USB jack. `usb`: `ready` (the computer has configured the device), `open` (a program has the serial port open), `space` (bytes of room in the port's buffer), `busy` (a packet is waiting for the computer), `sent` (bytes handed to the port), `rearmed` and `unstuck` (transfers the hub had to start again, see `hub/src/usb_s3.cpp`; both stay 0). `midi`: `mounted`, `busy`, `sent` (messages the port took), `pending` (values not said yet: the port is full, nobody is listening) |
| `GET /api/panels` | `{"panels":[{"id":1,"uid":"246f28010203","n":"door","on":1,"ip":"192.168.66.2","fw":"3.12.0","var":"ensemble v0.1.0"}]}`: every panel the hub knows, refreshed once a second. `ip` is where the panel's last report came from, which is also where its own web console is. [`tools/panels.py`](tools/panels.py) `--hub` reads this |

A panel says its side in its own `/api/status`, in an `ensemble` object: `uid` (the same twelve hex digits), `linked`, `id`, `hub`, `states`, `sent`, `detents`, `knob`.

## What the hub keeps

Across restarts, in flash: the config, each panel's uid, number, place, name and linked flag, and the scenes. Not the live targets: after a restart the panels report where they are and the hub takes that.

## MIDI (the hub's USB jack)

The hub is a USB-MIDI device named `Patternflow Ensemble` beside its serial console. It speaks Patternflow's MIDI mapping (`docs/midi-spec.md` in Patternflow, the Audio edition's) once per panel: a panel's channel is its number. [`core/pfe_midi.h`](core/pfe_midi.h) is the code; the PC tests hold it.

| Direction | Message | |
|---|---|---|
| ensemble to host | CC 24-27 on channel n | knob 1-4 of panel n, 0..127, whenever its target changes: a hand on it, another panel's value arriving (at once, or when a wave reaches it), a scene, the console |
| | Program Change on channel n | the pattern of panel n |
| | channel 16 | the value that just left a hand (or the console), once, as it starts to travel |
| host to ensemble | channel n (1-15): panel n · channel 16: every panel | |
| | CC 20-23 | knob 1-4, absolute, 0..127 onto 0..1000 |
| | CC 24-27 | knob 1-4, relative, 64 = no motion, binary offset in detents of 1/32 of the range |
| | CC 28 | brightness, 0..127 onto 5..255 (the ensemble's own) |
| | Program Change | pattern |

A knob value the host set is not sent back to it, but remembered as sent, so a hand bringing the knob back is told. A Program Change comes back, as in the panel's own mapping. A value is not sent twice in a row on one channel.

What goes to the host is state, not a queue: per channel and control the hub keeps the value to say and the value the port last took. The USB-MIDI port takes sixteen messages at a time; a change of more than that (a scene) goes out over the next passes of the hub's loop, a millisecond or two each, and none is lost. While no program is listening nothing piles up: when one starts, it hears each control once, where it is now.

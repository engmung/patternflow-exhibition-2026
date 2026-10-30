# Ableton setup for the exhibition — PF Exhibit Scenes

> **English translation.** The Korean original is [`README.md`](README.md). It was written for the crew of the 2026 exhibition *Anbu* in Seoul and describes that installation as it was — the exhibition laptop's paths, COM ports and dates are kept as facts. In this public copy the hotspot password reads `YOUR_HOTSPOT_PASSWORD`; the prebuilt firmware images, `flasher.html`, `backups/` and the project's copies of the audio samples are not included — build from source as the [front page](../README.md) shows. The original's `patternflow/` (the panel firmware clone) is what you get by applying [`firmware/panel-exhibit-2026.patch`](../firmware/panel-exhibit-2026.patch) to Patternflow `4d264d5`, inside this repo; its `flowlocal/` (the FlowLocal firmware clone) is [`firmware/flowlocal/`](../firmware/flowlocal/) here, so `flowlocal/EXHIBIT.md` means `firmware/flowlocal/EXHIBIT.md`. Where the original says "the root `README.md`", it means the Korean notes now in [`README.ko.md`](../README.ko.md), translated in [`docs/setup-notes.en.md`](../docs/setup-notes.en.md).

When you change the pattern on the panel with K4, the scene in Live follows. K1–K3 move Live parameters.

```
Panel --(Wi-Fi, RTP-MIDI)--> FlowLocal --(USB, the DevKit's "USB" port)--> laptop --> Live 12
                                       <------- only one thing comes back: the resync request (sent by this device)
                                                          MIDI port name: "Patternflow Exhibit"
Panel sends (channel 1):   Program Change n = pattern n  ·  CC 24/25/26 = K1–K3 position (0–127 absolute, the same value the picture uses)  ·  notes 60–62 = K1–K3 press
Panel sends (channel 10):  notes 36/38 = one for every frame in which a foot of HEOEUNBIN (no. 4, the lizard) touches the ground (§4 "Footstep notes")
FlowLocal adds:            right after every Program Change, CC 118 = n (channel 16, bytes BF 76 n) = program mirror
                           on a re-sent PC, after the mirror: CC 24/25/26 = the values the panel sent last (channel 1)
This device sends:         resync request CC 119 = 127 (channel 16, bytes BF 77 7F)
Pattern numbers:  0 = Origin   ·   1–6 = the six artists (K4 order)   ·   boot / idle return = 1
```

- K4 sends nothing over MIDI (no CC 27, no note 63). Only the Program Change that says the pattern changed goes out.
- The only thing that goes from the laptop to FlowLocal is this device's **resync request**. FlowLocal throws away everything else that comes from the laptop, and passes nothing on to the panel. The laptop needs neither an rtpMIDI driver nor Wi-Fi.
- When the session reconnects, the panel sends the current PC once more, and right after it the current CC 24/25/26 values too (§4). FlowLocal re-sends the last PC (and the CC 118 mirror, followed by the CC 24/25/26 the panel sent last) in the cases below, and at no other time. A knob the panel has not sent even once since FlowLocal was switched on is left out (from flowlocal `0c05e79` on).
  1. When it could not deliver that PC over USB (buffer full, no host)
  2. When USB reconnects or the laptop wakes from sleep
  3. When it receives a resync request. This device sends one 0.5 s after it loads, and one each time you press the device's **Resync** button.
  4. When you press **Send current pattern to Live** at `http://192.168.66.1/`
- When you restart only Live, case 3 brings back the current pattern and knob positions (end of §3).
- Inviting FlowLocal and having MIDI on are the panel firmware's defaults. If anything was ever saved on this panel's MIDI page, the saved value wins. Check it once before the show (root `README.md` [§7 "Pre-show checklist"](../docs/setup-notes.en.md#pre-show-checklist)). In this build K1–K3 send the knob position as it is, so the MIDI page's Knob value / Relative steps and Sensitivity do not matter.

```
ableton/
├── README.md                         ← this document (Korean original)
├── README.en.md                      ← this English translation
└── pf-exhibit-scenes/
    ├── pf_exhibit_scenes.maxpat      ← device patch (open it in Max, save it as .amxd)
    └── pf_exhibit_scenes.js          ← all of the device logic (loaded by the patch)
tools/
├── midi_monitor.py                   ← watch incoming MIDI (on-site check)
├── send_test.py                      ← send fake PC/CC, resync request, CC 118 (test without hardware)
├── test_pf_exhibit_scenes.js         ← offline test of the js (Node)
└── check_maxpat.py                   ← structure check of the .maxpat
```

---

## 1. Making the .amxd (once)

*(Translator's note: skip this chapter if you use the shared set. `set Project/set.als` already loads a built, frozen `PF Exhibit Scenes.amxd` from `set Project/Presets/MIDI Effects/Max MIDI Effect/`, with the js inside. You need §1 only if you change `pf_exhibit_scenes.js` or the `.maxpat`.)*

This works the same way as the earlier Patternflow Bridge. The repo keeps only the text `.maxpat`; you make the `.amxd` once and then use it. The step where the order matters is step 3.

1. Pick a folder for the device and copy `pf_exhibit_scenes.js` into it. Example: `Documents\Ableton\User Library\Presets\MIDI Effects\Max MIDI Effect\` (it shows up right away under User Library in Live's browser).
2. In Live, create a MIDI track. In the browser, under Max for Live, drag **Max MIDI Effect** onto that track. Press the **Edit** button in the device's title bar, and Max opens.
3. **Save before you paste.** In Max, **File → Save As** → `PF Exhibit Scenes.amxd` in the folder from step 1. The device must be in the same folder as the js, so that it finds the js the moment you paste. If you swap the order, the js stays in a failed-to-load state.
4. **File → Open** → `ableton/pf-exhibit-scenes/pf_exhibit_scenes.maxpat`. Select all (Ctrl-A), copy (Ctrl-C), then close that window.
5. Go back to the device patcher, **delete all of the default contents (midiin → midiout and the explanatory comment)**, then paste (Ctrl-V). This patch already contains midiin/midiout. If you leave the default midiin → midiout in place, all of the panel's MIDI goes back out to the track's MIDI To (FlowLocal), even with Thru off.
6. **View → Patcher Inspector** (or right-click the empty canvas) → tick **Open in Presentation**. Pasting does not carry this setting over.
7. Save (Ctrl-S). It worked if the device's bottom status line changes to `ready - scenes 0-…` and the Max menu **Window → Max Console** shows no `js: can't find file`. If not, check where the js is, delete the device from the track and drag it in again from the browser.
8. (Recommended) When it checks out, press **Freeze Device** (snowflake icon) in the editor toolbar and save again. The js goes inside the `.amxd`, so the device moves as one file. Then do **File → Collect All and Save** in the exhibition set, and the device is copied into the project folder.

After you change the js, delete the device and drag it in again (if you froze it: Unfreeze → edit → Freeze again). Automatic js reload is turned off on purpose: a reload resets the state, but live.thisdevice does not bang again, so the device would stay stuck. The `set Project/Presets/MIDI Effects/Max MIDI Effect/PF Exhibit Scenes.amxd` that the exhibition set opens is frozen, so it carries its own copy of the js inside (checked on 2026-09-25: identical to `ableton/pf-exhibit-scenes/pf_exhibit_scenes.js` except for line endings). If you change only the js in the repo, Live keeps running the old copy. So after a change, freeze that `.amxd` again and also replace the js copy next to it.

## 2. Live settings — Settings → Link, Tempo & MIDI

| Port | Track | Remote | Why |
|---|---|---|---|
| **In: Patternflow Exhibit** | **ON** | **ON** | Track: Program Change reaches the device on the track. Remote: you can map K1–K3 with Ctrl-M. |
| **Out: Patternflow Exhibit** | **ON** | **OFF** | Track: this port appears in the control track's MIDI To list, and the device's resync request goes out to FlowLocal. Remote: see below |

Turn **Sync** (MIDI clock) off for both ports. If it is on, clock bytes flow to FlowLocal non-stop and are thrown away.

**Always turn Remote off on the output.** If it is on, Live sends the values of mapped parameters back out of the port on the same CC numbers. The panel reads incoming CC 24–27 as relative turns, so if those values reach the panel, the knob gets stuck at one value and jumps every time you turn it. This is a bug we actually hit on 2026-09-15 (also written up in `docs/director-midi.md`, Route D). Right now FlowLocal throws away everything from the laptop except the resync request and passes nothing on to the panel, so it will not blow up right away. But if you leave it on, useless CC goes back and forth over USB, and it will blow up the moment the path becomes two-way later.

In the same tab, set **Takeover Mode → None** (changed on 2026-09-19; before that it was Value Scaling). The CC 24–26 the panel sends are always the knob position itself (§4), and the panel sends no CC when K4 switches the pattern. So as long as a mapped parameter has only ever moved by the values the panel sent, None and Value Scaling behave exactly the same.

They differ when the parameter has drifted away from the panel position: the saved value comes back when the set is reopened; the mouse; scene tempo; clip automation; the panel was moved while Live was off or the USB cable was out, so the CC never arrived.

- **None**: on the next CC the parameter goes straight back to the panel position. The same knob position always gives the same sound. It jumps once at that moment.
- **Value Scaling**: no jump. Instead, until the knob reaches an end, the parameter stays somewhere other than the panel position, so the same knob position gives a different sound. The CC that FlowLocal re-sends (end of §3) may also fail to move the parameter to the panel position.

None is the one that fits the exhibition's goal (everyone sees the same scene and hears the same sound). Whether the jump sounds bad has not been checked on real hardware yet (§9 item 11). To get fewer jumps, do not let anything inside Live move a mapped parameter (scene tempo, clip automation).

The port name is set by the FlowLocal firmware (`FL_EXHIBIT_USB_PRODUCT`). If it shows up under a different name in the list, check with `python tools/midi_monitor.py --list`. If you **plug FlowLocal into a different USB port**, Windows may see it as a new device, and the name may change to something like `2- Patternflow Exhibit`. Then you have to turn the switches above on again. So pick one USB port on the laptop and mark it with tape.

## 3. Set layout (recommended)

**Control track** — one MIDI track, named for example `PF Control`.

- MIDI From: **Patternflow Exhibit**, **All Channels** (PC arrives on channel 1, the CC 118 mirror on channel 16; with Ch. 1 the track filters the mirror out). Live is said to merge the messages that pass through a track onto channel 1 before handing them to the device (Cycling '74 forum; not checked on real hardware). That is why the device ignores the channel.
- MIDI To: **Patternflow Exhibit**, **Ch. 16**. The device's resync request travels to FlowLocal this way. Live may change the channel of messages a track sends to an external port to the channel chosen here (not checked on real hardware). The request is on channel 16 to begin with, so Ch. 16 is right either way.
- Monitor: **In** (the input always passes through, even without Arm)
- Device: **PF Exhibit Scenes**, alone. No instrument, no other MIDI effect, no clips. If you put an instrument after the device, the request goes into that instrument and never reaches FlowLocal.
- Put **only one** of this device in the set. With two, every scene fires twice and every request goes out twice.
*(Translator's note: in the shared set, scene track `4.도마뱀` plays the footstep notes itself (MIDI From All Ins / **Ch. 10**, Monitor **In**); leave it on that input. The rule below is for the other instrument tracks.)*
- Set MIDI From to **No Input** on the other instrument tracks. If one of them is on All Ins with Arm on, the panel's notes go into that instrument too. For a track that turns knob presses into sound, see §4 "Press sounds".

**Why no feedback loop can form.** The device sends a request only once after loading and when you press the Resync button. It never sends anything in reply to a message it received. FlowLocal throws away every message from the laptop except the request, and passes nothing on to the panel. The device never reads the reply to a request (PC, CC 118, CC 24–26) as a request. Even with Thru on, the panel's MIDI just goes back to FlowLocal and is thrown away. The only setup that forms a loop is MIDI From and MIDI To on **the same loopMIDI port** with Thru on (§5).

**Status replies to the laptop monitor (SysEx).** FlowLocal answers the status request that the laptop monitor tool (Patternflow Monitor, [`tools/monitor/`](../tools/monitor/README.en.md)) sends every 2 seconds with SysEx `F0 7D 50 46 02 <text> F7` (text up to 240 bytes). This reply comes in on the same USB input, so Live receives it too. From Live 10.0.1 on, SysEx passes through a MIDI track's device chain (Live 10 release notes), so it also reaches this device's `midiin`. The device throws away everything from `F0` to `F7` and also breaks running status (real-time bytes in between do not end the SysEx). So the text bytes are never read as PC or CC 118. The first character of the text, `v`, is 0x76 = 118: had running status survived, it would have been read as CC 118 right there. Node test 26 checks this with a 128-scene set. Live's built-in instruments do not react to SysEx, and Live does not pass SysEx to VSTs. Ctrl-M mappings take only notes, CCs and pitch bend, so SysEx moves nothing, even with Remote on for the In port. While the monitor runs, `bytes` in `dump` grows by the reply length every 2 seconds. With Thru on, the reply goes back to FlowLocal and is thrown away (`usb_in_discarded` goes up).

**Scenes are counted the same way as Program Change. The top scene is number 0.** PC n fires `live_set scenes n`, that is, scene n when you count 0, 1, 2… from the top. So that you do not mix this up with the numbers on Live's screen, name the scenes after their content (`Origin`, `ARTIST A / Title` …). The device's status line shows the number and that name together.

| Scene (= PC) | Panel pattern | When it arrives | Content |
|---|---|---|---|
| **0** | Origin | Never from K4. Only when Origin is picked in the web console | A quiet sound, or an empty scene (stop) |
| **1** | Artist 1 = **default pattern** | At boot, when nobody has touched the panel for a while (3 minutes by default), K4 | Artist 1 |
| **2–6** | Artists 2–6 | K4 | Artists 2–6 |

K4 cycles 1 → 2 → … → 6 → 1. The default pattern is set by the panel firmware's `PF_EXHIBIT_DEFAULT_SLOT` (0 = artist 1 = PC 1). If you change it, also change the device's **Start PC** to that PC number. If you would rather not move the scenes around, you can shift them with **Offset** instead (with Offset 1, PC 1 fires scene 2). Offset is added to Start PC in the same way, so leave Start PC as it is.

- **Global Launch Quantization** (Control Bar): for the show, **None** (the scene changes the instant K4 is turned; the creator's decision on 2026-09-19). If that cuts off too abruptly, use **1/16** or **1/8** (almost instant, but on the beat). Each clip's Launch Quantization must be set to **Global** for this setting to apply. To make the new scene pick up the beat position of the old one, turn on the clip's **Legato** switch (Legato Mode, in the clip's launch settings next to Launch Mode).
- In every scene, the track slots it does not use need a **Clip Stop Button**, so that the previous scene's sound stops. Empty slots have one by default, but in a slot where someone used "Remove Stop Button", the previous clip keeps playing.
- If you give a scene a tempo, the base tempo changes with every scene. **If you mapped K1 to tempo, do not use scene tempos.** If a scene tempo changes the tempo on each K4, the same K1 position gives a different tempo (Value Scaling), and with None the tempo jumps on the next K1 CC. If you want a different tempo for each scene, map K1 to something other than tempo.

### Device options

| Option | Default | Meaning |
|---|---|---|
| Enable | on | When off, the device ignores Program Change and does not do Autostart or the resync request after loading. The Resync button still sends a request. |
| Autostart | off | When the set opens, fires the Start PC scene after 1.5 s, and starts the transport if it is stopped. If a PC has already arrived by then (including FlowLocal's reply to the request sent after loading), it keeps that scene. **Turn it on in the exhibition set and save.** While editing, it is easier to leave it off. |
| Start PC | **1** | Autostart fires as if this PC had come from the panel. Scene = Start PC + Offset. 1 = the pattern the panel starts with at boot, so if you power everything up together, sound and light match from the start. |
| Offset | 0 | Scene = PC + Offset (Start PC included) |
| Thru | **off** | When on, incoming MIDI goes straight to the device output. On the control track the output goes to FlowLocal, so there is no reason to turn it on (the panel's MIDI would just go back and be thrown away). Use it only when this device sits on a track that is set up differently. |
| Alt CC | **off** | For emergencies. When on, the value of a CC (number in the box below, default 118) is treated as a Program Change. FlowLocal sends the same number as CC 118 (channel 16) right after every PC, so if Live does not pass PC to the device, **just turn this on**. No firmware change is needed (§9 item 1). **It ignores the channel.** Live merges a track's input onto channel 1 before passing it to the device, so filtering for channel 16 would read none of the mirror. The only CCs the exhibition panel sends are 24–26 (K1–K3), so they never clash with 118 (the midi feature's range is 24–27). |
| Alt CC number | 118 | The CC number Alt CC reads (1–118, any channel). Must match `FL_EXHIBIT_PROGRAM_CC` in the FlowLocal build. Do not change it to 24–27 (the panel knobs would be read as programs). |
| **Resync** (button) | — | Press it to send the resync request (`BF 77 7F`). A live.text in button mode outputs a bang when pressed, and the patch passes it to the js as `resync bang`. FlowLocal re-sends the last PC, CC 118 and the last CC 24–26 values, and the device sets that scene. If that scene is already playing, it does not fire it again (`already on`). |

Every value except Resync is saved with the set and kept out of automation (Stored Only). The Resync button is neither saved nor automatable (Hidden). So a saved button press is never replayed when the set opens.

**Reading the status line**

- `waiting for Live` / `no program yet` — every time the device loads, the patch resets the two lines to this text (this works even without the js). If it does not change to `ready` a few seconds after the set opens, the js was not found (§1 step 7, Max Console).
- `ready - scenes 0-6` — ready; the set has 7 scenes (0–6). *(Translator's note: the shared set has 8 scenes, so it reads `ready - scenes 0-7`.)* The request sent automatically after loading does not change this line.
- `fired 12:34:56` — the bottom line changes to this every time the device fires a scene. With Autostart on, this happens 1.5 s after loading, so it is normal that `ready` shows only briefly.
- `PC 3 ch1 -> 3: ARTIST C / Title` — the last PC and the scene it fired (number: scene name)
- `CC118=3 ch1 -> 3: ARTIST C / Title` — a scene fired by Alt CC from FlowLocal's CC 118 mirror. The number after `ch` is the channel the device received. FlowLocal sends on channel 16, but if Live merges it onto channel 1 it shows as `ch1`. If Live does pass PC and Alt CC is on anyway, on every K4 the PC fires first and this line ends with `(already on)`. That is normal.
- `autostart PC 1 -> 1: ARTIST A / Title` — a scene fired by Autostart with Start PC
- `resync request sent 12:34:56` — a request was sent with the Resync button. When FlowLocal replies, the top line changes to `PC n ch1 -> …` (`(already on)` if it is already on that scene).
- `... (already on)` — that scene is already playing, so it was not fired again (see below)
- `... scene 7 missing (scenes 0-6)` — there is no scene with that number. Check the number of scenes or the Offset.
- `... ignored (Enable off)`

**When the same PC comes again** — the same program can arrive again because of a reconnect, a reply to a resync request, or (with Alt CC on) the CC 118 mirror that follows the PC. The device then skips firing again only when all of the following are true. This is so that the music does not restart from the beginning.

1. It is the same scene this device fired last
2. The transport is running
3. At least one track that has a clip in that scene's row is still playing that clip or waiting to launch it. A row with no clips at all (an empty scene used to stop) skips condition 3.

So if someone started a different scene with the mouse or pressed Stop All Clips, the same PC fires the scene again to match the panel.
One exception: if a scene with no clips arrives while the transport is stopped, condition 2 is not met, so with Alt CC on, the PC and CC 118 each fire that scene once. Firing an empty scene once more sounds the same.

**When only Live is restarted** — when you close and reopen Live, the USB connection stays up, so FlowLocal does not re-send the PC by itself, and a panel nobody touches has nothing to send. So the device asks for it. When the set opens and the device is ready (live.thisdevice), it waits 0.5 s and sends one resync request through the control track's MIDI To. FlowLocal re-sends the last PC and CC 118, the device fires that scene, and Autostart, which runs at 1.5 s, sees that a PC has already arrived and keeps that scene. The CC 24–26 that follow (the values the panel sent last) move the mapped parameters to the panel's knob positions (with Takeover None, §2). Whatever value was saved in the set, and whatever pattern and knob positions the panel is on, the sound matches from the start.

If no reply comes, Autostart plays the scene for Start PC 1. If the panel was on a different pattern, the two stay out of step until someone turns K4 or the idle return happens (3 minutes, pattern 1). There are two reasons a reply does not come.

- The panel has not sent a PC even once since FlowLocal was switched on. The status page's `Current pattern (program)` reads `none yet`, and pressing **Send current pattern to Live** shows `No pattern yet: …`. The panel sends the current PC when the session connects, so if the panel is connected, there is usually one already.
- The request did not reach FlowLocal. Check the control track's MIDI To (Patternflow Exhibit, Ch. 16), the Out port's Track switch in Live's settings, and the device's Enable. Every time the set opens, `midi.resync_requests` in `http://192.168.66.1/api/status` should go up by 1. Once it is fixed, press **Resync**, or press **Send current pattern to Live** on the status page from a phone, and it matches right away.

Live opens the ports before it loads the set and its devices. So a PC that FlowLocal sent for reason 1 or 2 (list at the top) can arrive before the device exists and be lost. Because the device asks by itself after loading, this case also gets matched as described above.

## 4. Mapping K1–K3 (Ctrl-M)

The panel sends K1/K2/K3 as **CC 24/25/26**, 0–127 absolute. This value is **the knob position itself**. The panel keeps one position (0–1) per knob, draws the pattern currently on screen with that position (parameter = the low end of that pattern's range + position × range), and sends the same position as round(position × 127). So what visitors see and what they hear come from one number (root `README.md` [§1](../docs/setup-notes.en.md#1-at-a-glance), "Knob position").

- **Start and idle return** — when the panel powers up, the three knobs start in the middle (0.5 = **CC 64**), and when the session connects, three CC 64s go out right after the PC. If nobody touches the panel for 3 minutes, the three knobs go back to the middle, and only the knobs that were not in the middle send **CC 64** (a knob already in the middle has nothing to send). If the panel was on a pattern other than 1, PC 1 goes out on the next frame. So the middle of the mapped range is "the opening sound". This holds only when nothing inside Live moves that parameter (§2 Takeover, §3 scene tempo).
- **Range** — **32 steps** (detents) from end to end (about one and a half turns, about 4 CC per step). When the knob reaches an end, picture and sound stop together. The number of steps changes only through the panel firmware's `PF_EXHIBIT_KNOB_DETENTS` (rebuild), and changing it changes picture and sound together. **The panel MIDI page's Sensitivity (×4 etc.) and Knob value / Relative steps have no effect on K1–K3 in this build.**
- **When K4 changes the pattern** — the knob positions stay where they are. Not a single CC 24–26 goes out (the values did not change); only the PC goes out. The new pattern appears as it looks at the same knob positions. Even when the scene changes, mapped parameters stay put (unless scene tempo or clip automation moves them; §3).
- **When the session reconnects** (panel reboot, FlowLocal restart) — the panel sends the current values of CC 24, 25 and 26 right after the PC. **When only Live restarts or the USB reconnects**, FlowLocal re-sends the CC 24–26 the panel sent last, after the PC (and CC 118) (list at the top). Either way, with Takeover None (§2) the Live parameters are set to the panel's knob positions. With Value Scaling, parameters that changed on the Live side in the meantime may not be matched.
- **Direction** — flip it in the Live mapping with **Min > Max**. The panel has no per-knob direction setting (left out on purpose: a direction that differed per pattern would make the CC jump on a K4 switch). The direction of the picture is set by the artist inside the pattern.

1. **Ctrl-M** (MIDI Map Mode)
2. In Live, click a parameter → turn a panel knob a little → next parameter …
   Do not turn K4 while mapping. The CC 118 (channel 16) that comes right after the PC can get mapped to the parameter you clicked. A CC mapped with Ctrl-M no longer goes to the track, so Alt CC can no longer read it either.
3. In the mapping list on the left (Mapping Browser), narrow the range the parameter moves through with **Min / Max** (Min > Max reverses the direction).
4. Leave with **Ctrl-M**

Examples:

- **K1 → tempo** (the Tempo field in the Control Bar). Keep it narrow, like Min 100 / Max 130 BPM. The full range is too much.
- **K2 → the Frequency of an Auto Filter on the Main track.** Example: Min 300 Hz / Max 18 kHz
- **K3 → a layer-fade Macro.** Put the layer tracks as chains of one Rack (Instrument/Audio Effect Rack), and map the chain volumes to one Macro. If you give each layer the opposite Macro mapping range, you get a crossfade. Map that Macro to K3 with Ctrl-M.

**Set the ranges so that the middle is the opening sound and either end is still worth hearing.** The idle return (3 minutes) brings the CC back to 64, but until then the Live parameter stays where the last visitor left it. If someone closes the filter all the way so that almost no sound comes out and walks away, it stays like that for 3 minutes. The Min 300 Hz in the example is the safety net for that.

There is one set of mappings for the whole Live set. Even when the scene changes, K1–K3 move the same targets. How far you have to turn a knob to go from end to end is decided by the panel, not by Live (32 steps, "Range" above).

**Press sounds** — pressing K1–K3 sends notes 60–62. Do not put an instrument on the control track (the device output goes to FlowLocal, and Thru is off). To turn presses into sound, make another MIDI track with MIDI From **Patternflow Exhibit**, **Ch. 1**, Monitor **In**, and put an instrument on it. With Ch. 1, FlowLocal's CC 118 (channel 16) does not reach the instrument. Use Live's built-in instruments (Simpler, Drum Rack etc.). A VST instrument may switch presets when it receives the Program Change that also arrives on channel 1.

### Footstep notes (HEOEUNBIN)

*(Translator's note: the shared set does not use a separate footstep track: scene track `4.도마뱀` (MIDI From All Ins / Ch. 10, Monitor In, a Drum Rack with Collision on C1 and D1) plays them, and its scene-4 clip is empty. The steps below describe the separate-track version.)*

When the fourth pattern (the lizard) walks, the panel sends one note **on the very frame a foot touches the ground**. Live is not counting beats here: the panel reports it together with the picture, so wherever a visitor has turned K3 (speed), the sample stays on the feet. The other five patterns do not send these notes.

| | |
|---|---|
| Channel | **10** (drum channel). Knob CC 24–26, press notes 60–62 and Program Change stay on channel 1 |
| Notes | **36** (Live name **C1**) = near front foot + far hind foot, **38** (**D1**) = near hind foot + far front foot. The two alternate. You can put different samples on the two pads, or the same one |
| Velocity | Fixed at 100. A footstep is an event, not a performance, so its strength does not change |
| Length | Note-off 40 ms after note-on. With one-shot samples you can ignore it (it is there only for use with synth pads) |
| Rate | **3 × Speed** per second, alternating 36, 38, 36, 38… At one end of K3 0.6 per second, in the middle (at power-up and idle return) 2.1 per second, at the other end 7.2 per second. Numbers and BPM conversion: HEOEUNBIN in the root `PATTERN_KNOBS.md` (English: [`PATTERN_KNOBS.en.md`](../PATTERN_KNOBS.en.md)) |

**Making the track**

1. Make another MIDI track (name for example `PF Footsteps`). Do not put this on the control track (§3: that track's output goes to FlowLocal).
2. MIDI From: **Patternflow Exhibit**, **Ch. 10**. MIDI To: **No Output**.
3. Monitor: **In** (the input reaches the instrument even without Arm).
4. Instrument: **Drum Rack**. Put footstep samples on the C1 and D1 pads. To use one sound for both, copy the C1 pad's chain to D1 — pads do not transpose, so both notes give the same sound. If you map them onto a single Simpler instead, D1 plays two semitones higher.
5. No clips. Leave this track alone when the scene changes (K4). When the panel is on another pattern no notes arrive at all, so it stays quiet even if it is always on.

**Keeping it apart from the scene device** — one channel setting closes both directions.

- If this track is on **All Channels**, the Program Change on channel 1 reaches the instrument and changes a VST instrument's preset, and the press notes 60–62 sound as well. Set it to **Ch. 10**.
- The other direction needs no worry. The control track is on All Channels, so the footsteps get in there too, but the `PF Exhibit Scenes` device does not read notes (only Program Change and Alt CC). Footsteps never fire scenes. The rule of no instrument on the control track (§3) still stands.
- Other instrument tracks' MIDI From: **No Input** (§3). With All Ins + Arm, the footsteps sound on that instrument too.
- If you turn MIDI off on the panel's `/midi` page, the footsteps stop together with the knobs and the pattern. Same RTP session, same USB port, same tx counter.

**Lining up a few ms (Track Delay)**

The mixer's **Track Delay** field shifts a track's output earlier or later in ms (turn on **Track Delay** among the show/hide switches at the lower right of Session View, and every track gets one; if you cannot see them, use the **View** menu). Panel → FlowLocal → USB → Live takes a few ms (measured: the resync request round trip is under 10 ms, see the measurement log in the root `README.md`, translated in [`docs/setup-notes.en.md`](../docs/setup-notes.en.md)). At 45 fps one frame is 22 ms, so the link delay is less than one frame.

- **A negative delay cannot undo the link delay.** Live cannot know in advance an input that arrives in real time. What you can actually shorten is the buffer size in Live's audio settings and the silence before the sample (in the Simpler on the Drum Rack pad, pull the start point to just before the footstep).
- Use Track Delay **to line the footsteps up with the rest of the set's sound**. If the footsteps sound ahead of the music, give this track a positive delay. If they sound behind it, give the other tracks the same amount of positive delay.
- Start at 0, then move it in 5 ms steps on real hardware while watching the panel. The value is saved with the set and cannot be automated.

**Check** — `python tools/midi_monitor.py` prints `NoteOn   ch10  36 C2   vel 100`. The monitor names notes with C4 = 60, and Live names them with C3 = 60, so the same note is `C2` in the monitor and `C1` in Live. Go by the numbers 36/38 and nothing gets mixed up. To fake it without hardware: `python tools/send_test.py --port "PF Test" --channel 10 note 36 --velocity 100` (§5).

### Once before the show — checks on the panel side

The exhibition firmware's MIDI defaults (MIDI on, invite FlowLocal at `192.168.66.1`) are used **only when nothing is saved on the panel** (sending K1–K3 as knob positions is not a setting: it is built into the exhibition image, so saved values do not affect it; `midi.outLaneAbs: true` in `/api/status`). A panel that saved anything on the MIDI page with older firmware (the Audio·MIDI edition etc.) keeps using that value even after the exhibition firmware is flashed. If a different host address is left there, or it was saved with the address cleared, the panel does not invite FlowLocal and nothing reaches Live. The same goes for knob direction / click size and for the Wi-Fi saved on the panel.

The check items (panel MIDI page, knob page, saved Wi-Fi, captions, brightness) and how to open the panel pages are written down in one place only: **root `README.md` [§7 "Pre-show checklist"](../docs/setup-notes.en.md#pre-show-checklist)**, and the panel address in [§3](../docs/setup-notes.en.md#3-network) of the same document, "Opening the panel page from a phone" (the `Panel: …` link on the FlowLocal status page).

## 4.5 Continuing work on another computer (laptop)

The set lives inside the repo (`set Project/`), and the only sounds it uses are Live's built-in instruments (Collision, Drift, Wavetable, Dream Keys) and the M4L device in this folder. It refers to no outside samples, so it opens as it is once you have the repo. *(Translator's note: this describes an earlier version of the set. The shared `set.als` also uses Meld, Corpus, Granulator III and three samples from the Mood Reel and Drone Lab packs; see [The Live set, track by track](../README.md#the-live-set-track-by-track) on the front page.)*

What the receiving computer needs:

- **Live 12 Suite** and the Core Library. Collision and Wavetable are Suite devices, so in Intro/Standard those tracks open empty. Max for Live is also included in Suite (the device is frozen, so there is no need to open Max separately).
- git. It is a private repo, so you need to sign in. *(Translator's note: the public copy is `https://github.com/Patternflow-lab/patternflow-exhibition-2026`; no sign-in needed.)*

```bash
git clone https://github.com/Patternflow-lab/patternflow-exhibition-2026.git   # the original named the private repo engmung/patternflow-exhibition-2026
```

Then open `set Project/set.als`. Without the panel and FlowLocal, Live reports that `Patternflow Exhibit` is missing from the MIDI tracks' inputs, but the set still opens and all sound work can be done. On the day you connect the hardware, do the Live settings from §2 (input: Track and Remote on; output: Track only) once on that computer. **Live settings are saved on the computer, not in the set.** *(Translator's note: and per Live version: each update starts a new `%APPDATA%\Ableton\Live 12.x.y\Preferences` folder, so check the MIDI port switches and Takeover Mode again after updating Live.)*

After working:

```bash
git add "set Project" && git commit -m "ableton: ..." && git push
```

- **If you brought in new samples**, do File → **Collect All and Save** before committing. That copies the samples into `set Project/Samples/`, so they also play on the other computer.
- `set Project/Backup/` is not uploaded to the repo (it is a folder Live fills automatically).
- If you switch back and forth between two computers, always start with `git pull`. The `.als` is a compressed file, so it does not merge: whichever version is pushed later overwrites the whole file.

## 5. Testing without hardware (loopMIDI)

Check the device and the mappings first with the laptop alone, without the panel and FlowLocal.

1. Install **loopMIDI** (Tobias Erichsen, free) and add a port called `PF Test`.
2. Live settings: **In: PF Test** → Track ON, Remote ON. **Out: PF Test** → all OFF (prevents a loop).
3. For now, switch the control track's MIDI From to `PF Test` and its MIDI To to **No Output**. A loopMIDI port is a loop: whatever you send into it comes straight back in. If MIDI From and MIDI To are the same loopMIDI port, the device's request comes back, and if Thru is on as well, messages go round forever.
4. In a terminal (in the `exhibition_2026` folder — the repo root; needs `pip install python-rtmidi`):

```bash
python tools/send_test.py --list
python tools/send_test.py --port "PF Test" note 60                     # one note first (to compare in dump, §9 item 1)
python tools/send_test.py --port "PF Test" pc 1                        # scene 1 (the first thing to check, §9 item 1)
python tools/send_test.py --port "PF Test" pc 1 2 3 4 5 6 --interval 4 # one round of the artist scenes
python tools/send_test.py --port "PF Test" pc 3 3                      # if the clip is playing, the second one gives (already on)
python tools/send_test.py --port "PF Test" altcc 2                     # CC 118 = 2, channel 16. Alt CC on: scene 2 / off: nothing happens. Write down ch1/ch16 from the status line (§9 item 1)
python tools/send_test.py --port "PF Test" pc 4 --mirror               # CC 118 after the PC, like FlowLocal. Even with Alt CC on, it fires once
python tools/send_test.py --port "PF Test" demo --hold 8               # imitates the panel: boot 1 → K4 to 2…6 → 1, K1–K3 sweep
```

5. Mapping practice: Ctrl-M → click a parameter → `python tools/send_test.py --port "PF Test" cc 24 64` → leave Ctrl-M. Then watch it move with `python tools/send_test.py --port "PF Test" sweep --cc 24`. Move the parameter with the mouse and send `cc 24 64` again: with Takeover None the parameter goes straight to that value.
6. Check that the resync request leaves the device. Make one more loopMIDI port, `PF Resync`. In Live settings, **Out: PF Resync** → Track ON (In: PF Resync all OFF). Change the control track's MIDI To to `PF Resync`, Ch. 16. Start the monitor in a terminal (several programs can open a loopMIDI port at the same time).
   ```bash
   python tools/midi_monitor.py --port "PF Resync" --raw
   ```
   Press the device's **Resync** and one line prints: `CC ch16 #119 = 127 (resync request to FlowLocal) | BF 77 7F`. Delete the device from the track and add it again, and the same line prints once, about 0.5 s later. If the channel is not 16 or nothing prints, see §9 item 2.
7. When you are done, set the control track back to MIDI From **Patternflow Exhibit** (All Channels) and MIDI To **Patternflow Exhibit** (Ch. 16).

`send_test.py` will not send to a port that can reach hardware (a name containing patternflow/flowlocal, or an rtpMIDI session named after a computer) unless you add `--allow-device`. `resync` and `altcc` send on channel 16 whatever `--channel` says.

## 6. On-site check — watching incoming MIDI

```bash
python tools/midi_monitor.py            # picks "Patternflow Exhibit" automatically
python tools/midi_monitor.py --raw      # shows the bytes too
```

Turn K4 and it prints `PROGRAM`, right after it `CC ch16 #118 = n (program mirror from FlowLocal)`; turn K1–K3 and it prints `CC #24–26` — each line with the time, the interval and the count per second. Ctrl-C prints a summary. Unplug FlowLocal's USB and `port disappeared` appears; plug it back in and `port back` appears.

To see whether FlowLocal answers a resync request without Live, keep the monitor running and send a request from another terminal. It works if `PROGRAM` and `CC ch16 #118` print once more, followed by `CC #24–26` (only for the knobs the panel has sent). FlowLocal answers only if the panel has sent a PC since FlowLocal was switched on. Whether two different programs can open the input and the output of the same port at the same time has not been checked on Windows.

```bash
python tools/send_test.py --port "Patternflow Exhibit" --allow-device resync
```

*(Translator's note: the show laptop had Windows MIDI Services, which let Live and the monitor share the port; this paragraph applies to Windows without it.)* **On Windows, a USB MIDI port can usually be opened by only one program at a time.** When you use the monitor, close Live (or turn that port off in Live's settings). When you are done, close the monitor first, then start Live.

## 7. Windows kiosk checklist

The goal: switch the power on, and sound comes out without anyone touching anything. At the end, always do **a full rehearsal that starts with the power unplugged**.

**Automatic sign-in**
- [ ] A local account if possible. Settings → Accounts → Sign-in options → turn off "For improved security, only allow Windows Hello sign-in …"
- [ ] `Win+R` → `netplwiz` → untick "Users must enter a user name and password to use this computer". If you do not see the checkbox, use Sysinternals **Autologon**
- [ ] Sign-in options → "If you've been away, when should Windows require you to sign in again?" → **Never**. Turn off the screen saver

**Open the set at boot**
- [ ] `Win+R` → `shell:startup` → create a shortcut there. Do not rely on the program associated with `.als`; write out the Live executable and the set explicitly. Change the paths to the real install location.
  `"C:\ProgramData\Ableton\Live 12 Suite\Program\Ableton Live 12 Suite.exe" "D:\Exhibit\PF Exhibit Project\PF Exhibit.als"`
- [ ] If the audio device is detected late and Live starts on the wrong output, open the set from Task Scheduler with "At log on + 30-second delay" instead of the shortcut
- [ ] Live settings → Audio: fix the output device. Check that it does not change when you plug in HDMI etc.
- [ ] If Live's settings have an automatic update option, turn it off. Start Live once while offline and check that no authorization window appears

**Turn off sleep** (administrator terminal — PowerShell or cmd, either works)
- [ ] Settings → System → Power: when plugged in, screen and sleep **Never**
- [ ] Run the lines below one at a time. They turn off sleep and hibernation, keep the laptop running with the lid closed (in case it sits closed under a desk), turn off USB selective suspend, and then apply the changes. All of them apply when plugged in (AC).
```
powercfg /change standby-timeout-ac 0
powercfg /change hibernate-timeout-ac 0
powercfg /hibernate off
powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0
powercfg /setacvalueindex SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0
powercfg /setactive SCHEME_CURRENT
```
- [ ] Device Manager → Universal Serial Bus controllers → each **USB Root Hub** → Power Management → untick "Allow the computer to turn off this device to save power". On recent laptops the powercfg setting sometimes does not take effect, so do this as well.

**Updates and notifications**
- [ ] Settings → Windows Update → **Pause updates** (up to 5 weeks; check that it covers the whole exhibition). Set the active hours to the exhibition hours too
- [ ] Settings → System → Notifications → turn on **Do not disturb**
- [ ] In this setup the laptop does not need the internet. At the venue, keeping it off Wi-Fi means fewer worries about updates.

**Power**
- [ ] Always plugged in. Power mode "Best performance"; turn off battery saver turning on automatically
- [ ] (Optional) If the BIOS has a "power on when AC is connected" option, turn it on. It is for recovery after a power cut

**Crash recovery test**
- [ ] While Live is running, **force-quit** it from Task Manager → open it again with the shortcut (or a reboot) → see which window appears. The recovery dialog does not go away until someone clicks it. Find out which button takes you back to the exhibition state, and write it **in the blank on the staff card (`STAFF_CARD.md`)**.
- [ ] After recovery, check that the device's bottom line shows `ready …` or `fired hh:mm:ss` and that there is sound
- [ ] With the panel on pattern 3 (via K4), quit only Live → open it again → check that it starts on scene 3 (the resync request, end of §3). If it starts on scene 1, check whether `midi.resync_requests` in FlowLocal's `/api/status` went up
- [ ] While Live is running, unplug FlowLocal's USB and plug it back in → check that Live picks up the port again and that K4 changes scenes again

**Staff card** — the one-page card is [`STAFF_CARD.en.md`](../STAFF_CARD.en.md), the English version of the root `STAFF_CARD.md` (order for switching on and off, when there is no sound, when the screen freezes, what not to do). After confirming things with the tests above, fill in the blanks (the button in Live's recovery window, contacts etc.) and print it.

## 8. Troubleshooting

| Symptom | Check |
|---|---|
| The set is open, but the bottom line stays at `waiting for Live` and never changes to `ready …`/`fired …` | The js was not found → check the Max Console. Put the js next to the `.amxd`, or Freeze it (§1) |
| The port is in the list but the monitor shows nothing / the FlowLocal page says `Panel not connected` | The panel is not inviting FlowLocal → panel MIDI page: MIDI on, Computer to invite `192.168.66.1` (root `README.md` [§7 checklist](../docs/setup-notes.en.md#pre-show-checklist)). If the FlowLocal page says `Panel: unknown`, open the panel page with the **MIDI** link in the list below it. Is the panel connected to the PatternflowLocal Wi-Fi? |
| The monitor shows PROGRAM but the scene does not change | Live In port **Track** ON? Control track MIDI From / Monitor **In**? Device Enable? If the status line says `missing`, check the scene count and Offset. If the editor's `dump` shows `programs=0` while `altccIn` goes up, Live is not passing PC → turn **Alt CC** on (§9 item 1) |
| After restarting only Live, the scene does not match the panel | The request did not reach FlowLocal: control track MIDI To = Patternflow Exhibit **Ch. 16**, Live Out port **Track** ON, Enable. Does `midi.resync_requests` in FlowLocal's `/api/status` go up every time the set opens? If it goes up and the scene still does not match, does the status page's `Current pattern (program)` say `none yet` (turn K4 once)? For right now: the device's **Resync**, or **Send current pattern to Live** on the status page (end of §3) |
| Pressing Resync seems to do nothing | Check the same things as in the row above. Even when the reply arrives, the top line may read exactly as before (if it already said `(already on)`). In the editor's `dump`, check that `programs` (or `altccIn` if you use Alt CC) goes up with each press. If `resync_requests` goes up but this number does not, FlowLocal's reply is not reaching the device |
| Alt CC is on but K4 does nothing | Is Alt CC number 118? Did you accidentally map CC 118 with Ctrl-M (check the mapping list, §4)? Is the control track's MIDI From **All Channels** (with Ch. 1 the track filters out the mirror on channel 16)? Is `Program mirror to Live` on FlowLocal's status page anything other than `off`? With Live closed, does `midi_monitor.py` show `CC ch16 #118` after `PROGRAM` (if not, the FlowLocal build has the mirror turned off)? |
| `midi.usb_in_discarded` in FlowLocal's `/api/status` keeps going up | The laptop is sending FlowLocal something other than the request: device **Thru** on, the default midiin/midiout not deleted in §1 step 5, Live Out port **Remote** or **Sync** on, another track's MIDI To set to Patternflow Exhibit. It is thrown away, so it is not a fault, but turn it off |
| Scenes are one off | The top scene is number 0 (§3). Check the scene order or Offset |
| Scenes change, but late | Is Global Launch Quantization set to None (§3)? Is each clip's Launch Quantization set to Global? |
| A knob will not map | Live In port **Remote** ON? Did you actually turn the panel knob during Ctrl-M? |
| Turning a knob does not move the parameter, or it only twitches around 64 | Is Takeover Mode **None**? (With Pick-Up the knob value has to pass the parameter value before it moves; with Value Scaling a parameter changed on the Live side only meets the panel position at the end of the knob's travel.) Is the panel running the exhibition image (`midi.outLaneAbs: true` in `/api/status`; the exhibition image sends the position whatever the MIDI page's Relative steps say)? Is another mapping or automation acting on the same parameter? The Out port's Remote stays off (§2), but since FlowLocal does not pass CC from the laptop on to the panel, it is not the cause in the current setup |
| Parameters jump when K4 changes the pattern | The panel does not send CC 24–26 on a K4 switch (§4). If `midi_monitor.py` shows `CC #24–26` at the moment of the switch, the panel is not running the exhibition image. If nothing shows and it still jumps, the cause is on the scene side (scene tempo, clip automation, a different chain per scene) |
| The picture has stopped at the end but the sound keeps moving (or the other way round) | The panel is not running the exhibition image (`midi.outLaneAbs` missing from `/api/status`). The exhibition image stops picture and sound together on the same step. If someone moved a knob slider on the console home page, turning that knob one step brings them back together (if the slider was moved within the last 5 seconds, only after those 5 seconds; root `README.md` [§8](../docs/setup-notes.en.md#8-not-yet-confirmed-on-hardware), "Known and left as is"). On the Live side: is Takeover None, and are scene tempo and clip automation leaving that parameter alone? |
| A mapped parameter suddenly jumps | With Takeover None, a parameter changed on the Live side (set reopened, mouse, scene tempo, clip automation) jumps back to the panel position on the next CC. This is intended (§2). If it jumps often, remove the scene tempo or automation that moves that parameter. If not jumping matters more to you, use Value Scaling (but then the same knob position may give a different sound) |
| The monitor cannot open the port | Live (or another program) is holding the port → close Live |
| The port is not in the list | Is FlowLocal plugged into the DevKit's **USB** port (not COM)? Was it plugged into a different USB port on the laptop, so the name changed (§2)? |
| Press notes are not heard | Is the instrument on a separate track (MIDI From Patternflow Exhibit **Ch. 1**, Monitor In) and not on the control track (§4 "Press sounds")? |
| Footsteps are not heard | Is the panel on **HEOEUNBIN (pattern 4)** (the other patterns do not send them)? Is the footstep track's MIDI From **Ch. 10** and Monitor **In**? Are there samples on the Drum Rack's **C1 / D1** pads (36 / 38)? Does `midi_monitor.py` show `NoteOn ch10 36`? If not, the panel's `/midi` is off or the session has dropped (then the knob CCs will not arrive either) |
| On the footstep track a VST preset changes, or the press sounds play too | That track's MIDI From is All Channels → set it to **Ch. 10** (§4 "Footstep notes") |
| The sound is out of step with the foot touching the ground | A few ms are link delay, which Live cannot undo. First look at the silence before the sample and the audio buffer size. If it is far off, the cause is in the set, not in the footstep notes (Track Delay, §4) |
| Notes are heard twice | There are two tracks for the press notes, or that track is also receiving through All Ins + Arm |
| Pressing K1–K3 changes the VST instrument's preset | The Program Change that came along with it → use Live's built-in instruments (§4) |

## 9. Not yet checked (built without hardware or Live)

*(Translator's note: this chapter was written before the hardware tests. On 2026-09-19, with Live 12 Suite, items 1–3 were confirmed: Live passes Program Change to the device (status line `PC 6 ch1 -> 6: …`), so Alt CC stays off; Resync → FlowLocal's answer → scene fired works, so the MIDI To path works. See [setup notes, 'Live check (2026-09-19)'](../docs/setup-notes.en.md#live-check-2026-09-19-live-12-suite-on-that-desktop). The other items stay as written.)*

This device was built without Live or Max, using the format of the earlier Bridge patch and its docs as a reference. What was verified: `.maxpat` JSON parsing and a structure check (patch cords, inlet/outlet numbers, parameter names, presentation size), the js syntax (`node --check`, acorn ES5), and Node tests that imitate Max/Live. These tests cover the byte parser (running status, real-time bytes, sysex, FlowLocal's status reply SysEx), holding messages back until initialised, range checks, Offset (including Start PC), duplicate-fire prevention, Autostart, Alt CC ignoring the channel (both when Live merges the input onto channel 1 and when it keeps the channel), the dump counters, the resync request (once 0.5 s after loading, the `resync bang` sent by a live.text in button mode, Enable), the Thru gate, firing only once when PC and the CC 118 mirror arrive together, a Live-only restart imitated with a fake FlowLocal that answers requests, and the patch wiring (`midiin → deferlow → js`, `midiin → gate → midiout`, `js → midiout`, resetting the status lines on load, setting names, defaults and message pairs). That the tests can actually fail was checked by breaking the code on purpose. In real Live, check the following first.

1. **Whether Live passes a Program Change on a MIDI track's input through to the `midiin` of an M4L MIDI Effect.** This is the most important one. In the loopMIDI test in §5, before mapping anything, send `note 60`, then `pc 1`, and press `dump` in the editor (the patching view): one line prints in the Max Console.
   - `bytes=8 programs=1` but the scene does not change → look at the status line (`missing`, `Enable off` etc.).
   - `bytes=6 programs=0` → notes arrive but PC does not. Live is not passing PC. **Turn on the device's Alt CC.** Do not change the firmware. FlowLocal sends the same number as CC 118 (channel 16) right after every PC, and Alt CC reads that CC by default. Check that `python tools/send_test.py --port "PF Test" altcc 1` fires scene 1. With the real FlowLocal, `altccIn` in `dump` goes up by 1 each time K4 is turned (it counts even with Alt CC off).
   - For the same `altcc 1`, write down whether the status line says `CC118=1 ch1` or `ch16`. `ch1` means Live merged the channel (the behaviour known from the forum); the device reads either.
   - `bytes=0` → nothing reaches the device. Check the track's MIDI From, Monitor In, and the In port's Track switch in Live's settings. Live does not pass messages mapped with Ctrl-M on to the track, so use an unmapped note for this check.

   `midiin` outputs bytes one at a time, and the Max documentation does not say whether `[js]` keeps their order when it moves those calls to the low-priority queue. So the patch first passes the bytes through `deferlow` (documented to always add to the end of the queue, which keeps the order) and only then to the js. This ordering guarantee has not been run in Max either.
2. **Whether the device's resync request reaches FlowLocal.** Does Live send a CC that the js outputs through [midiout] out of the track's MIDI To (an external port)? Does it change the channel to MIDI To's Ch. 16 or leave it? Is the output ready 0.5 s after loading? First §5 step 6 (the `PF Resync` monitor), then with the real FlowLocal check that `midi.resync_requests` in `/api/status` goes up by 1 every time the set opens and every time Resync is pressed. If it does not work, after a Live-only restart, match things up with **Send current pattern to Live** on the status page.
3. Whether the **Resync** button sends exactly one request per press. According to the Max reference, a live.text in button mode outputs a bang from its left outlet when pressed (the `transition` default), `[prepend resync]` turns it into `resync bang`, and the js reads that message as a request. It works if the §5 step 6 monitor prints one `BF 77 7F` line per press. Also check whether the button outputs a bang when the set opens (if it does, only one extra request goes out). Check whether `[gate 1]` opens and closes with the 0/1 the js sends (turn Thru on and see whether `send_test.py`'s messages appear in the §5 step 6 monitor, and disappear when you turn it off).
4. Whether the live.toggle/live.numbox values are saved and restored with the set after pasting, and whether the comment status lines update
5. Whether the track properties used for the "already on" check (`playing_slot_index`, `fired_slot_index`, the clip slot's `has_clip`) and `is_playing` give values as documented — especially whether reading them in the same flow right after `fire` already returns the changed values. Check with §5's `pc 3 3`, with `pc 3` after starting a different scene with the mouse, and with Alt CC on, `pc 4 --mirror` (one fire, top line `CC118=4 … (already on)`, also with the transport stopped). If the values change late, the mirror fires the same scene once more. While quantizing, the same scene just gets queued again, but with quantization None the clip restarts from the beginning.
6. Whether the frozen `.amxd` runs in another folder without the js
7. Whether Autostart (1.5 s) and the request after loading (0.5 s) leave enough time for the exhibition set to load (with many samples). If the reply comes later than Autostart, scene 1 plays first and then switches straight to the panel's scene.
8. The real port name and how it changes when Windows re-enumerates; whether a Live-only restart starts on the panel's scene thanks to the reply to the request (end of §3, §7 crash recovery test)
9. What the crash recovery dialog actually looks like (§7)
10. Whether, on load, the js changes the status line to `ready` after the patch has reset it to `waiting for Live`. Also open, once, the `.amxd` saved after testing (before Freeze) from a folder without the js, and check that it stays at `waiting for Live`.
11. **CC 24–26 = knob position** (panel firmware, 2026-09-19; only host tests so far; the checklist on the panel side is root `README.md` [§8](../docs/setup-notes.en.md#8-not-yet-confirmed-on-hardware) item 15) — whether the Live parameters are set to the panel position by the 3 CCs right after the PC when the session connects, and by the 3 CCs that FlowLocal re-sends after the PC when only Live restarts. With **Takeover None** (§2), whether a parameter changed on the Live side (mouse, value saved in the set) goes to the panel position in one step on the next CC, whether that jump is acceptable to the ear, and whether Live applies a CC that arrives again with the same value. Whether the return to CC 64 at idle return sounds all right, and whether the middle of the mapped range is acceptable as the opening sound.
12. **Footstep notes** (panel firmware, 2026-09-20; only host tests and a build; not flashed to the panel) — on HEOEUNBIN, whether `midi_monitor.py` shows `NoteOn ch10 36` and `38` alternating; whether the rate follows 3 × Speed per second when K3 turns (end 0.6 / middle 2.1 / end 7.2); whether the Drum Rack's C1 and D1 sound; whether the sound comes together with the moment the foot touches down on the LEDs (if it is a few ms off, §4 Track Delay). Whether the notes stop, with no note left hanging, when K4 goes to another pattern and when the panel goes to sleep. Whether footsteps that enter the control track (All Channels) do not fire scenes.

Commands for checking during development (in the `exhibition_2026` folder — the repo root):

```bash
python -c "import json; json.load(open('ableton/pf-exhibit-scenes/pf_exhibit_scenes.maxpat', encoding='utf-8'))"
python tools/check_maxpat.py ableton/pf-exhibit-scenes/pf_exhibit_scenes.maxpat
node --check ableton/pf-exhibit-scenes/pf_exhibit_scenes.js
node tools/test_pf_exhibit_scenes.js      # with ACORN_PATH=<acorn package folder> it also checks ES5 syntax
```

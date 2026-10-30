<!-- Printable, one page. The operator fills in the blanks (______) after the rehearsal. Write the same values in tools/monitor/config.json and they also show on the monitor screen. Do not write the hotspot password on this card (save it only on the check phone). Operator notes: docs/setup-notes.en.md (Korean original: README.ko.md); the monitor: tools/monitor/README.en.md. -->

# Patternflow exhibition — staff guide

> **English translation.** The Korean original is [`STAFF_CARD.md`](STAFF_CARD.md). It was written for the crew of the 2026 exhibition *Anbu* in Seoul and describes that installation as it was — the exhibition laptop's paths, COM ports and dates are kept as facts. In this public copy the hotspot password reads `YOUR_HOTSPOT_PASSWORD`; the prebuilt firmware images, `flasher.html`, `backups/`, the `patternflow/` and `flowlocal/` clones and the audio samples are not included, and the FlowLocal firmware source is not public — the [front page](README.md) says what to do instead. Where the original says "the root `README.md`", it means the Korean notes now in [`README.ko.md`](README.ko.md), translated in [`docs/setup-notes.en.md`](docs/setup-notes.en.md).

When visitors turn the knobs on the LED panel, the picture changes and the laptop under the desk makes the sound. A **small board (label: FlowLocal)** plugged into the laptop links the panel and the laptop wirelessly. The **패턴플로우 모니터** ("Patternflow Monitor") on the laptop screen tells you where the link is broken.

| Technician | ______________ phone ______________ | Backup | ______________ phone ______________ |
|---|---|---|---|
| **Where to switch on the panel power** | ______________ | **Check phone** | ______________ |
| **Knob that changes the picture** | ______________ (3 clicks = next picture) | **Button to press in the music program's recovery window** | ______________ |

## Switching on

1. Switch on the speakers and set the volume.
2. Check that the laptop charger is plugged in, and that the small board is plugged into the laptop at the **spot marked with tape**.
3. Switch on the laptop. Press nothing and wait: the music program (Ableton Live) and the **패턴플로우 모니터** ("Patternflow Monitor") window open by themselves and the sound starts. If the monitor window is not there, double-click **패턴플로우 모니터** on the desktop.
4. Switch on the panel power. A picture appears.
5. When the top of the monitor turns green with **모두 정상이에요** ("All good"), the panel and the sound are linked. This usually happens within 10 seconds of switching on the panel. If it is not green after 1 minute, see **Step 2** under "If there is no sound" below.
6. Turn the knob that changes the picture by 3 clicks. If the picture and the sound change together, you are ready.

## Switching off

1. Switch off the panel power.
2. Shut down the laptop (Start → Power → Shut down). If the music program asks whether to save, choose **No** (do not save). Do not close the music program on its own (the monitor reopens it after 8 seconds). The small board and the monitor switch off with the laptop. Do not unplug the board.
3. Switch off the speakers. If the panel runs from a power bank, put it on charge.

## If there is no sound — go in order, from Step 1

**Step 1 · Sound equipment** — Check the speakers' power, volume and cables, and that the laptop is not muted. Check that the music program is showing on the laptop screen. If a recovery window is showing, press the button written in the table above.

**Step 2 · 패턴플로우 모니터 ("Patternflow Monitor") on the laptop screen**
1. Look at the colour and the words at the very top.
   - Green **정상** ("OK") → the connection is fine. Turn a panel knob and check that the bars on the screen move. If the sound does not match the picture on the panel, press the **한 번에 다시 연결하기** ("Reconnect everything") button once.
   - Yellow **기다리는 중** ("Waiting") → press nothing and wait.
   - Red **문제** ("Problem") or yellow **확인 필요** ("Needs checking") → press **Reconnect everything** once. The monitor reconnects the small board, the panel and the music program in turn and puts a ✓ by each step (about 30 seconds). If there is something you must do by hand, exactly one thing appears, large, in a yellow box. Do just that, and the monitor carries on by itself. Wait until green **다 됐어요** ("All done") appears.
2. If it asks you to press the **RST** button on the small board, press RST once and nothing else (do not press **BOOT** next to it). Unplug and replug the board only when it tells you to: unplug, wait 5 seconds, and plug it back into **the same taped spot**. If it says **음악 프로그램이 꺼져서 다시 켜는 중** ("The music program closed — restarting it"), wait; if the recovery window appears, press the button written in the table above.
3. If the monitor window is missing, or it shows **모니터 프로그램이 꺼졌어요** ("The monitor program has stopped"), double-click **패턴플로우 모니터** on the desktop. The sound keeps playing even while the monitor is off.
4. **운영자용 자세히 보기** ("Details for operators") at the very bottom is for the technician. Do not run any command shown there.

**Step 3 · Restart the laptop** — Start → Power → Restart. Wait until the music program and the monitor open by themselves and the sound plays. If it still does not work, call the technician (and tell them the words at the top of the monitor).

**Check phone** — Use it only when the technician tells you to on the phone. In the phone's Wi-Fi, connect to **PatternflowLocal** and type **192.168.66.1** in the address bar. If the top reads **Everything connected** (green), press the button that says **Send current pattern to Live** once. Do not press any other button (the similar-coloured **Firmware update**, the red **Reboot**, or the links in the Panel box). When you are done, **turn off the phone's Wi-Fi.**

## If the panel screen is stuck

- **The screen is black** → press any knob once.
- **Words like UPDATE or FAILED are showing** → **press** the knob that changes the picture once.
- **The picture is frozen and the knobs do nothing**, or the steps above did not work → switch off the panel power, wait a few seconds, and switch it on again. It starts from the first picture, and the sound usually links up again within 10 seconds (the monitor turns green). Write down the time it froze and tell the technician.
- If nobody touches it for 3 minutes, it goes back to the first picture. This is not a fault.

## Never do this

- **Never power the panel with a USB-C cable (a phone charger or similar).** Plug nothing into the USB socket of the board on the back of the panel. It may look fine at first, then burn and smoke a while later. Power it only at the place written under "Where to switch on the panel power" in the table above.
- **Do not put the laptop to sleep (power saving), and do not unplug its charger.** If the laptop sleeps, the small board switches off too, and the panel and the sound are cut. Do not move the small board's cable to another socket. Unplug it only when the **패턴플로우 모니터** ("Patternflow Monitor") screen on the laptop tells you to, and plug it back into the same taped spot after 5 seconds. Do not press the **BOOT** button on the small board. If the charger comes out, the sound keeps playing — don't rush, just plug it back in.
- **Do not connect your own phone or laptop to the PatternflowLocal Wi-Fi.** It only has 3 places; when they are full, the panel cannot get in and the sound is cut. Connect only the check phone, only when the technician tells you to, and disconnect as soon as you are done.
- Do not touch the music program's settings or buttons.

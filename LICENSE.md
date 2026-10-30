# Licences

This repository mixes code, designs, documentation, media and six artists' works. They do not all share one licence. This note says which applies where. Where a file states its own licence in its header, that header wins.

| What | Licence | Notes |
|---|---|---|
| **Patternflow code** in this repo: the Max for Live device ([`ableton/pf-exhibit-scenes/`](ableton/pf-exhibit-scenes/), the `.amxd` and its `.js` in [`set Project/Presets/`](set%20Project/Presets/)), the Python and Node tools in [`tools/`](tools/), the `.bat` helpers, and the panel firmware patch [`firmware/panel-exhibit-2026.patch`](firmware/panel-exhibit-2026.patch) | **MIT** | Patternflow's firmware and web code are MIT. `pf_exhibit_scenes.js` and the firmware files the patch adds say `License: MIT` in their headers. The patch applies to [Patternflow](https://github.com/engmung/Patternflow), whose own licence files govern the tree you build. |
| **The six artists' patterns** — [`patterns/1_bakwan/`](patterns/1_bakwan/) to [`patterns/6_yujin/`](patterns/6_yujin/) and [`patterns/_original/`](patterns/_original/) (`.js`, `.h`, project JSON) | **CC BY-SA 4.0, with credit to each artist** | Each pattern is drawn from an artist's work in *Anbu*. When you reuse one, credit the artist by name (Korean and English, see [`CREDITS.md`](CREDITS.md)) as well as Patternflow. The `.h` headers read `Author: @engmung` and `SPDX-License-Identifier: CC-BY-SA-4.0`: that is the author of the code. The artwork the code draws is the artist's. |
| **FlowLocal firmware** — [`firmware/flowlocal/`](firmware/flowlocal/) | **MIT** | `flowlocal.ino` and the source files under `src/` say `License: MIT`. The libraries it downloads at build time keep their own licences. The case models (`3D_STL/`), photos (`build_pics/`) and bundled web content come with the FlowLocal project. |
| **Documentation** — every `.md` file (English and Korean), and the patterns' web preview [`patterns/preview.html`](patterns/preview.html) | **CC BY-SA 4.0** | Patternflow's documentation, hardware and designs are CC BY-SA 4.0 (see the footer of [patternflow.work](https://patternflow.work)). |
| **The Ableton Live set** — [`set Project/set.als`](set%20Project/set.als) | **CC BY-SA 4.0** (a Patternflow design) | It refers to Ableton's own devices, presets and pack samples (Mood Reel, Drone Lab, Granulator III). Those are not part of this repository and stay under Ableton's licence terms. You need your own Live 12 Suite. |
| **Media** — [`media/`](media/): the demo video, GIF, poster and two exhibition photos (by the creator, Seunghun Lee), and screenshots of the tools | **CC BY-SA 4.0**, credit Patternflow / Seunghun Lee | The photos show the artists' works on the gallery walls. Those works remain the artists'. |

**In short:** reuse the code under MIT. For the patterns, documents, set and media, give credit and share alike under CC BY-SA 4.0. For the patterns, credit **the artist** as well.

Licence texts: [MIT](https://opensource.org/license/mit) · [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).

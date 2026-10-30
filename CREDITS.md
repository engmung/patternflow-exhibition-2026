# Credits

Patternflow was one interactive corner of the exhibition *Anbu*: there, visitors played LED patterns drawn from six artists' works, with sound. The works are the artists'. This repository only documents how they were played.

## The artists

The English spellings are our own approximate romanisation (Revised Romanization of Korean), not confirmed by the artists; the Korean names are the reference. The folder and code names in `patterns/` are looser spellings still (`bakwan` for 박환, `hanyeonghoe` for 한영희 — not the artists' names), kept as they are only because the firmware reads them.

| Artist | Their work in the show (from the press) | Pattern in this repo | What the pattern shows |
|---|---|---|---|
| **Bak Hwan** (박환) | mountains and waterfalls in earth, thread, denim scraps and bark | [`patterns/1_bakwan/`](patterns/1_bakwan/) — `BAKWAN` | a tree with golden leaves in the wind |
| **Choe Gyeong-eun** (최경은) | ceramics that start from the six dots of braille | [`patterns/2_choegyeongeun/`](patterns/2_choegyeongeun/) — `CHOEGYEONGEUN` | a wooden chest whose doors open on flowers |
| **Han Yeong-hui** (한영희) | cosmos flowers, the road to Jebudo, the South Sea | [`patterns/3_hanyeonghoe/`](patterns/3_hanyeonghoe/) — `HANYEONGHOE` | a green mountain, clouds and flowers in the wind |
| **Heo Eun-bin** (허은빈) | clay shapes built and painted on canvas; the character 「우산토끼」 (*Umbrella Rabbit*) | [`patterns/4_heoeunbin/`](patterns/4_heoeunbin/) — `HEOEUNBIN` | a walking lizard (the wall text beside her works, seen in [`media/anbu-visitors.jpg`](media/anbu-visitors.jpg), names a lizard among her characters) |
| **Jeong Eun-gyo** (정은교) | fish scales in hanji and silicone on black braille paper | [`patterns/5_jeongeungyo/`](patterns/5_jeongeungyo/) — `JEONGEUNGYO` | round shapes that drift together into a 3 × 3 grid |
| **Yu Jin** (유진) | afterimages of the sea in glow-in-the-dark paint | [`patterns/6_yujin/`](patterns/6_yujin/) — `YUJIN` | fish swimming in a sea current |

The descriptions of their work are from the press coverage listed under [Sources](#sources); they describe each artist's work in the show, and this repository does not name the specific piece each pattern follows. The pattern subjects match the names of the Live tracks: Ginkgo tree (은행나무), Wooden chest (괘짝), Mountain (산), Lizard (도마뱀), Geometry (기하학), Fish (물고기).

## The exhibition

- **Title:** 안부 — 보이지 않는 안부를 묻다 · ***Anbu — Asking After the Unseen*** (*안부* is a greeting that asks how someone is)
- A group show of six visually impaired artists, around nature and relationships: 40 works, 25 of which could be touched.
- **Dates:** 3–9 October 2026, 10:00–18:00, closed Sundays, free entry
- **Venue:** Gallery 1, Modu Art Space (모두미술공간 전시실1), Seoul Square annex 5F, Jung-gu, Seoul — next to Seoul Station
- **Organised by:** **Ablind** (에이블라인드), CEO **Yang Dream** (양드림)
- **Supported by:** the **Korea Arts Management Service (KAMS)** 2026 early-stage arts startup programme (예술경영지원센터 「2026 예술분야 초기창업 지원사업」)

The organiser allowed the creator, Seunghun Lee, to use the work from the exhibition freely. On that basis he publishes the six patterns under CC BY-SA 4.0 with credit to each artist ([`LICENSE.md`](LICENSE.md)); the artists' original works are not licensed here. The pattern files themselves name only the code's author, and [`patterns/preview.html`](patterns/preview.html) shows no artist names — so when you reuse a pattern, add the artist credit yourself (lines below). Artists or their representatives who want something changed can open an issue in this repository.

## Patternflow

**Patternflow** by **Seunghun Lee** — [patternflow.work](https://patternflow.work) · [github.com/engmung/Patternflow](https://github.com/engmung/Patternflow) (`@engmung` in the source files). For this exhibition: the panel and its exhibition firmware, the pattern code (the files name `@engmung` as their author) and its C++ versions for the panel, the FlowLocal exhibition build, the Ableton Live set, the Max for Live device *PF Exhibit Scenes*, the staff monitor, and the demo video and photos in [`media/`](media/).

## Software and sounds this setup builds on

- **FlowLocal** — the second board's firmware started from the FlowLocal show appliance ([github.com/SimonePDA/flowlocal](https://github.com/SimonePDA/flowlocal)); [`firmware/flowlocal/`](firmware/flowlocal/) is its exhibition branch.
- **Ableton Live 12 Suite** and **Max for Live** (Ableton, Cycling '74). The Live set uses Ableton's devices and presets, and samples from Ableton's **Mood Reel** and **Drone Lab** packs and the **Granulator III** pack; none of these files are included here.
- Libraries the firmware builds pull in: [AppleMIDI](https://github.com/lathoub/Arduino-AppleMIDI-Library) and the [Arduino MIDI Library](https://github.com/FortySevenEffects/arduino_midi_library) (FlowLocal), [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA), [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) and [arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets) (panel), and the Espressif Arduino core through PlatformIO. The Python tools use [python-rtmidi](https://github.com/SpotlightKid/python-rtmidi).

## How to credit a pattern when you reuse it

The six patterns are shared under **CC BY-SA 4.0 with credit to each artist** ([`LICENSE.md`](LICENSE.md)). CC BY-SA allows copying, adapting and commercial use of the patterns, as long as you give credit and share your version under the same licence. It does not cover the artists' original works or photographs of them. Ready-made lines (the titles are the working names of the Live tracks):

- *Ginkgo tree* — pattern after the work of **Bak Hwan (박환)**, shown in *Anbu — Asking After the Unseen* (Seoul, 2026, organised by Ablind). Pattern code: Patternflow / Seunghun Lee. CC BY-SA 4.0.
- *Wooden chest* — pattern after the work of **Choe Gyeong-eun (최경은)**, shown in *Anbu — Asking After the Unseen* (Seoul, 2026, organised by Ablind). Pattern code: Patternflow / Seunghun Lee. CC BY-SA 4.0.
- *Mountain* — pattern after the work of **Han Yeong-hui (한영희)**, shown in *Anbu — Asking After the Unseen* (Seoul, 2026, organised by Ablind). Pattern code: Patternflow / Seunghun Lee. CC BY-SA 4.0.
- *Lizard* — pattern after the work of **Heo Eun-bin (허은빈)**, shown in *Anbu — Asking After the Unseen* (Seoul, 2026, organised by Ablind). Pattern code: Patternflow / Seunghun Lee. CC BY-SA 4.0.
- *Geometry* — pattern after the work of **Jeong Eun-gyo (정은교)**, shown in *Anbu — Asking After the Unseen* (Seoul, 2026, organised by Ablind). Pattern code: Patternflow / Seunghun Lee. CC BY-SA 4.0.
- *Fish* — pattern after the work of **Yu Jin (유진)**, shown in *Anbu — Asking After the Unseen* (Seoul, 2026, organised by Ablind). Pattern code: Patternflow / Seunghun Lee. CC BY-SA 4.0.

## Sources

- patternflow.work — Inside the work: *Played at Anbu* — <https://patternflow.work/inside/seunghun-anbu-seoul>
- 국민일보 (Kukmin Ilbo), 「시각장애 예술가 6인 '안부' 묻다…자연을 그린 작품 전시」 — <https://www.kmib.co.kr/article/view.asp?arcid=9000018186>
- 소셜밸류 (Social Value), 「시각장애 예술가 6인전 '안부-보이지 않는 안부를 묻다' 개막」 — <https://www.socialvalue.kr/news/view/1065593479086632>
- 매일일보 (Maeil Ilbo), 「시각장애 예술가 6인 전시 '안부' 10월 3일 개막」 — <https://www.m-i.kr/news/articleView.html?idxno=1417179>

# Context — facts for the write-up (gathered 2026-10-30 by the PC session)

Use these facts as given; cite the sources at the end in the English docs where it helps a reader. Do not invent dates, names or numbers that are not here or in the repo.

## The exhibition

- **Title:** 안부 — 보이지 않는 안부를 묻다 (*Anbu — Asking After the Unseen*; 안부 means a greeting that asks how someone is). A group show of six visually impaired artists, around nature and relationships. 40 works, 25 of them could be touched.
- **When / where:** 3–9 October 2026, 10:00–18:00, closed Sundays, free. Gallery 1, Modu Art Space (모두미술공간 전시실1), Seoul Square annex 5F, Jung-gu, Seoul — next to Seoul Station.
- **Organised by:** Ablind (에이블라인드), CEO Yang Dream (양드림). Supported by the Korea Arts Management Service (KAMS) 2026 early-stage arts startup programme (예술경영지원센터 「2026 예술분야 초기창업 지원사업」).
- **The six artists** (Korean · romanised as used in English docs · the folder name in this repo):
  | Artist | English | Folder | Their work in the show (from press) |
  |---|---|---|---|
  | 박환 | Bak Hwan | `patterns/1_bakwan/` | mountains and waterfalls in earth, thread, denim scraps and bark |
  | 최경은 | Choe Gyeong-eun | `patterns/2_choegyeongeun/` | ceramics that start from the six dots of braille |
  | 한영희 | Han Yeong-hui | `patterns/3_hanyeonghoe/` | cosmos flowers, the road to Jebudo, the South Sea |
  | 허은빈 | Heo Eun-bin | `patterns/4_heoeunbin/` | clay shapes built and painted on canvas; the character 「우산토끼」 (Umbrella Rabbit) |
  | 정은교 | Jeong Eun-gyo | `patterns/5_jeongeungyo/` | fish scales in hanji and silicone on black braille paper |
  | 유진 | Yu Jin | `patterns/6_yujin/` | afterimages of the sea in glow-in-the-dark paint |
  English names follow the Revised Romanization of Korean — the artists' own preferred spellings are not known yet, so list it as an open question in the PR. The folder names are older romanisations (`hanyeonghoe` is 한영희 · `bakwan` is 박환) — keep the folders as they are (code reads them); use the English names above in prose and credits, always with the Korean beside them.
- **Permission:** the organiser told the creator the work can be used freely. Credit the artists, the organiser and the exhibition; no separate consent section is needed (creator, 2026-10-30).

## What Patternflow did there

- Patternflow was **not** an exhibit. It was a corner where visitors played the artists' paintings, illustrations and sculptures as LED patterns: **one knob picks the work, the other three play it, and the sound follows the knobs** (patternflow.work/inside/seunghun-anbu-seoul).
- The signal path, hardware, Live set and Max for Live device are in this repo — the Korean `README.md`, `ableton/README.md`, `PATTERN_KNOBS.md`, `STAFF_CARD.md`, `tools/monitor/README.md` are the source of truth.
- Media in `media/`: `demo-720p.mp4` (85 s, portrait, recorded by the creator), `demo.gif` (7 s from it), `demo-poster.jpg`, `anbu-visitors.jpg` · `anbu-demo.jpg` (exhibition photos, the creator's, also on patternflow.work).

## What is not in this public copy (and why)

- **Hotspot password** — replaced by `YOUR_HOTSPOT_PASSWORD` everywhere. The exhibition hotspot name `PatternflowLocal` stays (it is a default, not a secret).
- **Prebuilt firmware images and the web flasher** (`firmware_images/*.bin`, `flasher.html`) — they had the password compiled in. Rebuild from source with your own `patternflow_secrets.h` (template: `firmware/patternflow_secrets.example.h`).
- **Panel firmware** is here as `firmware/panel-exhibit-2026.patch` — the exhibition branch on top of the public Patternflow repo at commit `4d264d5` (`git clone https://github.com/engmung/Patternflow && git checkout 4d264d5 && git apply panel-exhibit-2026.patch`; checked to apply cleanly).
- **FlowLocal firmware** (the second board — an ESP32-S3 show appliance: Wi-Fi AP + MQTT + Director + USB/SD library, `engmung/flowlocal_lsh`) is a private project and is **not** included. Describe what it does and how it talks to the panel and to Live from the docs here; say plainly that its source is not public yet.
- **Audio samples** in `set Project/Samples/Imported/` (third-party one-shots and loops) — removed; the Live set will show them as missing. Say which tracks used them and that any sample of your own works.
- **Firmware backups (`backups/`)** — internal git bundles, not needed with the patch.

## The plan after this

The creator will later build a dedicated, free, lightweight Patternflow tool for sound/MIDI. This repo documents the 2026 exhibition setup as a working example of what is possible — it is not the future product. Say so once, briefly.

## Sources

- patternflow.work — Inside the work: *Played at Anbu* — https://patternflow.work/inside/seunghun-anbu-seoul
- 국민일보, 「시각장애 예술가 6인 ‘안부’ 묻다…자연을 그린 작품 전시」 — https://www.kmib.co.kr/article/view.asp?arcid=9000018186
- 소셜밸류, 「시각장애 예술가 6인전 ′안부-보이지 않는 안부를 묻다′ 개막」 — https://www.socialvalue.kr/news/view/1065593479086632
- 매일일보, 「시각장애 예술가 6인 전시 ‘안부’ 10월 3일 개막」 — https://www.m-i.kr/news/articleView.html?idxno=1417179

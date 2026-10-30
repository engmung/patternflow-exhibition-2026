# Worker rules — patternflow-exhibition-2026 (public write-up)

This repo is the **shareable copy** of the setup Patternflow used at the 2026 exhibition *Anbu* in Seoul: a Patternflow LED panel whose knobs pick and play six artists' works as patterns, a second board (FlowLocal), and Ableton Live on a laptop turning the same knob moves into sound over MIDI. Read [docs/context.md](docs/context.md) first — the facts, what was removed and why, and the sources.

## The job

Make this repo something an English-speaking maker can **understand in one minute and reproduce in an afternoon**, so people see what Patternflow + sound can do.

- **Write, do not change what runs.** The patterns, the Max for Live device, the Live set, the firmware patch, the Python tools and the `.bat` files stay byte-for-byte as they are (the creator, 2026-10-30: 「막 작동하는건 수정하지 말고 설명만 잘 해두면 댐 — 어차피 다시 만들거라」). If something in them is wrong or missing for a reader, say it in the docs (and in the PR), do not fix it.
- **English first.** `README.md` becomes the English front page. Keep every Korean document: move the Korean `README.md` to `README.ko.md` and add English versions beside the others (`ableton/README.md` → keep, add `ableton/README.en.md`; `STAFF_CARD.md` → add `STAFF_CARD.en.md`; `PATTERN_KNOBS.md` → `PATTERN_KNOBS.en.md`; `tools/monitor/README.md` → `tools/monitor/README.en.md`). Code comments and tools that point at `README.md` / `STAFF_CARD.md` mean the Korean text — note in `README.ko.md`'s first line that it is the original.
- **Reproducible.** Someone with a Patternflow, a second ESP32-S3 (or without one — say what changes), a laptop with Ableton Live (say which version and edition the set needs — read `set Project/set.als`, a gzipped XML) and Max for Live must be able to follow it: parts list · versions · wiring/network · build and flash (from `firmware/panel-exhibit-2026.patch` + `firmware/patternflow_secrets.example.h`) · open the Live set · what each track and device does · the MIDI map (channel · note/CC · which knob · what it controls in Live — read the set, the `.maxpat` and `pf_exhibit_scenes.js`, and the firmware patch's `docs/midi-spec.md`) · switching works · the monitor tool · a first-run checklist · troubleshooting (from the Korean docs' fixes).
- **Honest about gaps.** FlowLocal firmware is not public; the samples are not included; prebuilt images are not included. Say what a reader does instead, once, where it matters.
- **Credit.** A `CREDITS.md` and a credits section on the front page: the six artists with Korean and English names and their work in the show, the exhibition (title in Korean and English, dates, venue), the organiser Ablind (CEO Yang Dream), KAMS support, and Patternflow by Seunghun Lee. The organiser allowed free use — no consent section.
- **Show it.** Top of `README.md`: `media/demo.gif` (it plays inline), a line linking `media/demo-720p.mp4`, and the two exhibition photos. Leave a clearly marked spot for a YouTube link the creator may add later (`<!-- YOUTUBE: -->`).
- **Diagrams in text.** The signal path as a Mermaid diagram (GitHub renders it) plus the ASCII one kept from the Korean README.
- **Licences.** Add a `LICENSE.md` note, not a guess: Patternflow firmware and web code are MIT, hardware/designs/docs/patterns CC BY-SA 4.0 (patternflow.work footer). The six artists' patterns: credit them, licence as the creator decides — write 「CC BY-SA 4.0, with credit to the artist」 and flag it in the PR as a question for the creator.

## How

- One PR to `main`. Draft early, `[WIP]` → `[DONE]`.
- Check: every relative link in the docs resolves (a small script outside the repo is fine) · no Korean left untranslated in the English files except names and quoted titles · nothing outside docs/media/new English files changed (`git diff --stat` in the PR).
- Before `[DONE]`, three separate reviewer agents read only the English docs (not this file, not your notes): ① a maker who has a Patternflow but has never used Live — can they follow it? ② an Ableton/Max user — is the MIDI and Live part exact? ③ an exhibition producer — is the story and the credit clear and respectful? Fix the weakest points, at least twice.
- PR body: what a reader gets, the list of English files, the open questions for the creator (artists' pattern licence · FlowLocal source · YouTube link).

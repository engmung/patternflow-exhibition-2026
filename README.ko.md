> **원본(한국어) · Original (Korean).** 2026년 전시 때 쓴 설치·운영 안내 원문이다. 코드 주석·도구·한국어 문서가 `README.md`라고 부르는 것은 이 문서다(`STAFF_CARD.md`도 한국어 원본). 공개본에서는 핫스팟 비밀번호를 `YOUR_HOTSPOT_PASSWORD`로 바꿨고, 미리 구운 이미지·`flasher.html`·`backups/`·샘플은 빠졌다. 패널 펌웨어는 `firmware/panel-exhibit-2026.patch`, FlowLocal 소스(아래의 `flowlocal/` 클론)는 `firmware/flowlocal/`에 있다. 아래 "비공개 저장소" 안내와 첫 문장의 "유료 전시"는 준비 때의 표현이다(실제 전시는 무료였다). 전시: 「안부 — 보이지 않는 안부를 묻다」, 2026.10.3–9, 모두미술공간 전시실1. 작가: 박환·최경은·한영희·허은빈·정은교·유진. 주최: 에이블라인드. 지원: 예술경영지원센터 「2026 예술분야 초기창업 지원사업」. 크레딧·라이선스: [CREDITS.md](CREDITS.md) · [LICENSE.md](LICENSE.md). English: [README.md](README.md) (front page) · [docs/setup-notes.en.md](docs/setup-notes.en.md) (full translation).

# Patternflow 전시 2026 — 설치·운영 안내

2026년 10월 1일 무렵 여는 유료 전시에서, 관람객은 Patternflow 패널 한 대를 만지고 소리는 책상 아래 노트북의 Ableton Live가 낸다. 이 폴더에는 그 설치에 쓰는 펌웨어 두 가지(패널, FlowLocal), Live 장치, 점검 도구가 모여 있다.

> **이 문서에는 핫스팟 비밀번호가 들어 있다.** 이 저장소는 GitHub 비공개 저장소(`engmung/patternflow-exhibition-2026`)에만 올린다. 공개로 돌리거나 다른 곳에 복사해 공유하지 않는다.

기준 커밋(2026-09-25 갱신). 두 펌웨어 클론의 `exhibit-2026`은 `backups/`의 bundle에 들어 있다(복원은 `backups/README.md`).

| 저장소 | 브랜치 | 커밋 |
|---|---|---|
| `patternflow/` (패널 펌웨어) | `exhibit-2026` | `006e9c1` (2026-09-30): 거절 뒤 1초 재초대, 죽은 세션 12초 감지. 그 아래 `7350445` 자막 없음, `2ccb652`: `PatternflowLocal`에만 접속, 초대 호스트 192.168.66.1과 MIDI 켬 고정, 빠른 재초대, Wi-Fi 자가 복구, 전송 출력 19.5 dBm(3장). 그 아래 `0e1d957` 주사율 정확히 300 Hz, `497eb4a` 패턴 .h를 이 레포 `patterns/`에서 읽음, `235f06a`·`871ab33` HEOEUNBIN 발소리 노트, `fba3e53` 패턴 검토 수정, `a5b5db9` 작가 패턴 6개 재번역, `1d37256` K1–K3 노브 위치 공유, `ab5bcab` MIDI `PF_MIDI_OUT_LANE_ABSOLUTE`, `382147e` v3.10.3 코어 |
| `flowlocal/` (FlowLocal 펌웨어) | `exhibit-2026` | `c6360dc` (2026-09-30, `flx-mon3`): 거절 없음, 25초 조용한 세션 종료, 채널 명령. 그 아래 `22e6c2d` (2026-09-25, `flx-mon2`): 노트북 모니터에 USB SysEx로 상태를 답하고 명령을 받음, 켤 때 채널 1·6·11 중 가장 한산한 곳을 고름, 30초 조용한 기기를 핫스팟에서 내보냄, RTP 흔들림 측정, 멈춘 브리지·막힌 USB를 스스로 복구(USB 다시 연결, 안 되면 재시작, 전원을 켠 뒤 3번까지)하고 재시작 이유(`rsn`, `bc`, `why`, `ur`)를 알림. 그 아래 `f9dcd9d` 문서, `0c05e79` PC를 다시 보낼 때 패널의 CC 24–26 마지막 값도 붙임, `ebcbe08` 켤 때 핫스팟 12초 늦춤, `5bcce79` CC 118 프로그램 미러와 상태 페이지의 패널 주소 |
| 이 폴더 | `main` | 2026-09-25 커밋들: 노트북 모니터 `tools/monitor/`, `패턴플로우 모니터 열기.bat`, `노트북 전시용 전원 설정.bat`, 새 이미지(`firmware_images/`, `flasher.html`), bundle, 이 문서들. 그 아래 `3ef4033` |

보드에 지금 올라간 것(2026-09-30): 패널 `502a3c11…`(patternflow `006e9c1`), FlowLocal `4ecbcca0…`(`flx-mon3`, flowlocal `c6360dc`). 둘 다 `firmware_images/`, `flasher.html`과 같다(끝의 굽기 기록).

처음(9/17)에는 하드웨어 없이 만들었고, 9/18부터 두 보드와 Live를 실기에서 돌리며 고쳤다. 실기에서 본 것은 끝의 굽기 기록과 실측 기록에, 아직 못 본 것은 8장에 있다.

- 설치가 끝나면 한 번: **7장 "전시 전 점검표"**
- 전시 중 상태 보기와 고치기: 노트북의 **패턴플로우 모니터**(레포 맨 위 `패턴플로우 모니터 열기.bat`를 두 번 누른다. 설명은 [`tools/monitor/README.md`](tools/monitor/README.md))
- 현장 스태프에게 줄 것: **[`STAFF_CARD.md`](STAFF_CARD.md)** (한 장짜리 안내문. 빈칸을 채워 인쇄한다)

---

## 1. 한눈에 보기

### 신호 경로

```
 관람객이 노브를 돌린다
        │
        ▼
 ┌─ Patternflow 패널 ──────────────────────────────┐
 │ ESP32-S3, 128×64 LED.  전원: J4 나사 단자 5 V만  │
 │ K4 = 패턴 전환,  K1–K3 = 패턴 파라미터 + MIDI CC │
 └──────────────────────────────────────────────────┘
        │  Wi-Fi "PatternflowLocal" 에 접속
        │  RTP-MIDI (UDP 5004 제어 / 5005 데이터)
        │  패널이 192.168.66.1 을 초대한다
        ▼
 ┌─ FlowLocal ─────────────────────────────────────┐
 │ ESP32-S3-DevKitC-1 N16R8.  전원: 노트북 USB      │
 │ 핫스팟 + RTP-MIDI → USB-MIDI 중계                │
 │ 노트북 모니터의 상태 요청·명령(SysEx)에 답한다   │
 └──────────────────────────────────────────────────┘
        │  DevKit의 "USB" 잭 → 노트북  (COM 잭 아님)
        │  USB-MIDI 포트 이름: "Patternflow Exhibit"
        ▼
 ┌─ 노트북 (Windows 11) ───────────────────────────┐
 │ Ableton Live 12 Suite + Max 9                   │
 │ Max for Live 장치 "PF Exhibit Scenes"            │
 │   Program Change n → 씬 n 발사                   │
 │   CC 24/25/26 → Ctrl-M으로 매핑한 파라미터       │
 │ 패턴플로우 모니터 (tools/monitor, 스태프 화면)   │
 │   같은 포트로 FlowLocal에 상태를 묻는다          │
 └──────────────────────────────────────────────────┘
        │  노트북 오디오 출력
        ▼
     스피커
```

**노트북에서 거꾸로 가는 것은 두 가지뿐이다.**

- **Live 장치의 재동기화 요청**(채널 16의 CC 119 = 127, 바이트 `BF 77 7F`). 장치는 세트가 열리고 0.5초 뒤 저절로 한 번, 그리고 **Resync** 버튼을 누를 때 보낸다. FlowLocal은 그 요청에 마지막 PC와 패널의 K1–K3 값을 다시 보내 답한다(아래).
- **패턴플로우 모니터의 SysEx**(`F0 7D 50 46 …`, `tools/monitor/README.md`). 2초마다 상태를 묻고, 고치기 버튼을 누를 때만 명령(패널 연결 다시 잡기, 패널 다시 켜기, FlowLocal 다시 켜기)을 하나 보낸다. FlowLocal은 SysEx로 답한다. Live도 이 답을 받지만 장치가 통째로 버린다(`ableton/README.md` 3장).

FlowLocal은 노트북에서 온 나머지를 모두 버린다. 패널로는 MIDI를 아무것도 넘기지 않는다. 모니터의 **패널 다시 켜기**만 FlowLocal이 패널에 HTTP 요청(`POST /api/wifi/reboot`)으로 전한다. Live의 요청이 FlowLocal에 닿지 않을 때의 대체 수단은 모니터의 **소리 다시 맞추기** 버튼(같은 `BF 77 7F`를 Live를 거치지 않고 보낸다), 그다음이 운영자 폰으로 누르는 FlowLocal 상태 페이지의 **Send current pattern to Live** 버튼이다(6장).

노트북에는 rtpMIDI 드라이버도, Wi-Fi 연결도 필요 없다. 노트북을 핫스팟에 붙이는 것은 패널 무선 갱신 때뿐이고, 끝나면 바로 뗀다(3장).

FlowLocal은 PC를 넘기거나 다시 보낼 때마다 바로 뒤에 **CC 118 = 같은 번호**(채널 16, 바이트 `BF 76 n`)를 붙인다(프로그램 미러). Live가 PC를 장치에 넘겨주지 않을 때 장치의 **Alt CC**가 이것을 읽는다.

PC를 **다시** 보낼 때(6장 끝의 네 경우)는 미러 뒤에 패널이 마지막으로 보낸 **CC 24/25/26**(채널 1)도 붙인다(flowlocal `0c05e79`). 그래서 Live를 다시 열거나 USB가 다시 붙어도 Live가 패턴과 함께 노브 위치를 받는다. FlowLocal이 켜진 뒤 패널이 한 번도 보내지 않은 노브는 건너뛰고, PC를 받은 적이 없으면 아무것도 다시 보내지 않는다. 9/18에 구운 `aa3f12d`에는 이 기능이 없었고, 9/24부터 보드에 들어 있다(굽기 기록).

### 각 상자가 하는 일

| 상자 | 하는 일 | 하지 않는 일 |
|---|---|---|
| 패널 | 작가 패턴 6개를 펌웨어에 넣어 둔다(모듈 로딩 없음). K4를 3칸 돌릴 때마다 다음 패턴, 전환 때 자막은 띄우지 않는다(2026-09-25부터 빌드 기본값 끔: 영문 이름을 작가에게 확인하지 않았다. 켜는 방법은 5장). K1–K3는 노브마다 위치 하나를 기억하고, 그 위치가 그림(모든 패턴)과 소리(CC 24–26)를 같이 정한다(아래 "노브 위치"). 3분 동안 아무도 안 만지면 기본 패턴(1번)으로 돌아가고 노브 위치도 처음(가운데)으로 되돌린다. 부팅하면 항상 1번. 길게 누르는 메뉴는 모두 잠김. `PatternflowLocal`에만 붙고, 붙으면 1초 뒤 192.168.66.1을 초대한다(3장) | 밝기 조절(웹 콘솔에서만), 들어오는 MIDI 차단(FlowLocal이 패널로 아무것도 넘기지 않는다는 전제), 패널에 저장된 다른 Wi-Fi에 붙기, `/midi`에서 초대 호스트 바꾸기·MIDI 끄기(전시 빌드에서 잠금, 3장) |
| FlowLocal | 핫스팟 `PatternflowLocal`(192.168.66.1, 켤 때 채널 1·6·11 중 가장 한산한 곳). 패널의 RTP-MIDI 세션을 받아 CC·PC·노트를 USB-MIDI로 그대로 넘긴다. PC마다 CC 118(채널 16) 미러를 붙이고, 노트북의 재동기화 요청에 마지막 PC와 CC 24–26 마지막 값을 다시 보낸다. 노트북 모니터의 SysEx에 상태를 답하고 명령을 받는다. 상태 페이지 `http://192.168.66.1/` | USB 스틱, SD, TFT, MQTT, 인터넷 연결. 노트북 → 패널 방향 MIDI 전달(재동기화 요청과 모니터 SysEx 말고는 노트북에서 온 것을 모두 버린다) |
| 노트북 / Live | PC n이 오면 씬 n을 발사, K1–K3 CC로 파라미터를 움직인다. 세트를 열 때와 Resync 버튼을 누를 때 FlowLocal에 재동기화 요청. 패턴플로우 모니터가 신호가 가는 길을 한국어로 보여 주고 고치기 버튼을 둔다(스태프 화면, 이 노트북에서만) | 패널에 MIDI를 보내는 일 |

### 패널이 보내는 MIDI (발소리 노트만 채널 10, 나머지는 채널 1)

| 메시지 | 언제 |
|---|---|
| Program Change n | 패턴이 바뀔 때마다(K4, 대기 복귀, 부팅, 콘솔). RTP 세션이 새로 붙을 때 현재 PC를 한 번 더 |
| CC 24 / 25 / 26 | K1 / K2 / K3의 **위치**. 0–127 절대값 = round(위치 × 127). 패턴이 그리는 데 쓰는 바로 그 위치다. 켤 때와 대기 복귀 때 64(가운데), 끝에서 끝까지 32칸(약 1바퀴 반), 값이 바뀔 때만, 노브당 50 ms에 최대 1개. RTP 세션이 새로 붙으면 PC 바로 뒤에 세 값을 한 번 더(FlowLocal도 PC를 다시 보낼 때마다 마지막 값을 붙인다) |
| Note 60 / 61 / 62 | K1 / K2 / K3 누름(velocity 127), 떼면 note-off |
| Note 36 / 38 (**채널 10**) | HEOEUNBIN(4번 패턴)이 걸을 때 발이 땅에 닿는 프레임마다 하나, 둘이 번갈아(velocity 100, 40 ms 뒤 note-off). 초당 3 × Speed번이라 K3를 돌리면 빈도가 따라온다. 다른 패턴은 안 보낸다. 숫자는 `PATTERN_KNOBS.md` HEOEUNBIN, Live 쪽 트랙은 `ableton/README.md` 4장 "발소리 노트" |
| (없음) | K4는 MIDI를 아무것도 안 보낸다(CC 27, 노트 63 없음) |

### 노브 위치 — 모두가 같은 장면을 보고 같은 소리를 듣게

K1–K3는 노브마다 **위치 하나**(0–1)를 패널이 기억한다. 한 칸 돌리면 1/32만큼 움직이고, 양 끝에서 멈춘다. 그 위치 하나가 두 곳에 그대로 간다.

- **그림**: 지금 떠 있는 패턴의 파라미터 = 그 파라미터 범위의 아래 끝 + 위치 × 범위. 예를 들어 범위가 0.2–1.0인 파라미터는 위치 0.5에서 0.6이다. 패턴 코드의 한 칸 크기(`step`)와 시작값은 더 이상 쓰이지 않는다.
- **소리**: CC 24/25/26 = round(위치 × 127). 가운데(0.5)는 64.

그래서 전에 생기던 어긋남이 없어졌다.

| 상황 | 전 (두 계산이 따로) | 지금 (위치 하나) |
|---|---|---|
| 노브를 끝까지 돌림 | 패턴은 자기 시작값에서 자기 범위 끝까지(끝에서 끝 약 48칸), CC는 64에서 16칸 만에 끝에 닿아 서로 다른 곳에서 멈춤 | 끝에서 끝 32칸, 그림과 소리가 같은 곳에서 같이 멈춤 |
| K4로 패턴 전환 | 새 패턴은 자기가 전에 있던 값, CC는 그대로 | 새 패턴이 같은 노브 위치의 모습으로 나오고 CC는 안 움직임 |
| 3분 대기 복귀 | 패턴 값만 처음으로, CC는 앞사람 값 그대로 | 위치가 가운데로 돌아가고, 가운데가 아니던 노브는 CC 64를 보냄 |
| RTP 세션이 다시 붙음 | PC만 다시 보냄 | PC 바로 뒤에 CC 24–26 현재 값도 |
| Live만 재시작, USB 다시 연결 | FlowLocal이 PC만 다시 보냄 | FlowLocal이 PC 뒤에 CC 24–26 마지막 값도 |

알아 둘 것:

- **모든 패턴이 노브 가운데에서 시작한다.** 패턴 코드에 적힌 시작값이 아니라 범위의 가운데 모습으로 열린다. 패턴마다 무엇이 달라지는지는 5장 "노브 위치로 여는 모습" 표. 시작 위치는 노브별로 바꿀 수 있지만(`PF_EXHIBIT_KNOB_START_1/2/3`, 5장 끝) 여섯 패턴에 똑같이 적용된다.
- **방향**은 두 곳에서만 정한다: 패턴 안(작가가 파라미터의 뜻으로)과 Live 매핑(Min > Max). 패널에는 노브·패턴별 방향 뒤집기를 두지 않았다. 패턴마다 뒤집으면 K4 전환 때 같은 노브의 CC가 튀기 때문이다.
- **패널 `/midi` 페이지의 Sensitivity(×4 등)와 Knob value / Relative steps는 이 빌드의 K1–K3에 쓰이지 않는다.** 위치를 그대로 보낸다. 저장된 값이 달라도 상관없다.
- 버튼을 누른 그 한 프레임(1/60초)만은 그 노브의 위치를 패턴에 넘기지 않는다. 패턴은 "원격으로 움직이는 노브의 누름은 무시"하게 쓰여 있어서, 그렇게 하지 않으면 HEOEUNBIN의 K1(방향 돌리기)·K2(탄 사람)가 안 먹는다. 그 프레임에 같이 돌린 만큼은 다음 프레임에 반영된다.

### 패턴 번호 = Program Change = Live 씬

Live 씬은 **맨 위를 0번**으로 센다(장치가 `live_set scenes n`을 발사한다). Live 화면에 보이는 씬 번호는 1부터라 헷갈리니, 씬에는 이름을 붙인다.

| 패턴 번호 = PC | 맨 위에서 몇 번째 씬 | 패턴 | 언제 오나 |
|---|---|---|---|
| 0 | 첫째 (씬 0) | Origin | 웹 콘솔에서 고를 때만. K4로는 안 온다 |
| 1 | 둘째 (씬 1) | 작가 1 BAKWAN = **기본 패턴** | 부팅, 3분 대기 복귀, K4 |
| 2–6 | 셋째–일곱째 | 작가 2–6: CHOEGYEONGEUN, HANYEONGHOE, HEOEUNBIN, JEONGEUNGYO, YUJIN (이 순서, 5장 표) | K4 (1 → 2 → … → 6 → 1로 돈다) |

Origin이나 콘솔에서 고른 다른 패턴에 있을 때 K4를 앞으로 돌리면 1번, 뒤로 돌리면 6번으로 간다. K4 누름은 평소엔 아무 일도 하지 않는다. 예외 하나: 펌웨어 업로드가 실패해 UPDATE/FAILED 카드가 떠 있으면 K4를 한 번 눌러 그 카드를 닫는다.

### 폴더

```
패턴플로우서울역/                 이 저장소 (branch main, GitHub 비공개. 예전 데스크탑에서는 exhibition_2026/)
├── README.md                     이 문서
├── STAFF_CARD.md                 스태프 안내문 한 장 (인쇄용)
├── PATTERN_KNOBS.md              작가 패턴의 노브 표
├── 패턴플로우 모니터 열기.bat    두 번 누르면 노트북 모니터를 켜고 창을 연다 (tools/monitor/start_monitor.bat을 부른다)
├── flasher.html                  Chrome·Edge로 여는 굽기 도구. firmware_images/의 이미지가 들어 있다 (4장)
├── firmware_images/              바로 굽는 패널·FlowLocal 이미지와 SHA256SUMS (firmware_images/README.md)
├── backups/                      두 클론의 exhibit-2026 bundle과 patternflow_secrets.h (backups/README.md)
├── patterns/                     작가 패턴 6개의 JS와 .h (5장)
├── set Project/                  전시 Live 세트 (ableton/README.md)
├── .gitignore                    patternflow/, flowlocal/, .ap_pass_tmp 등을 뺀다
├── patternflow/                  Patternflow 클론. branch exhibit-2026. push URL은 일부러 막아 둠(DISABLED)
│   └── firmware/
│       ├── patternflow/features/exhibit/   전시 기능 (패턴 목록, 자막, K4, 대기 복귀, K1–K3 노브 위치)
│       ├── patternflow/features/midi/      MIDI 기능 (PC 재전송, 초대 호스트, 재초대와 Wi-Fi 자가 복구, 노브 위치를 CC로)
│       └── bundles/exhibit/                전시 빌드 구성 (features_local.h, overrides.h)
├── flowlocal/                    FlowLocal 개인 포크(engmung/flowlocal_lsh) 클론. branch exhibit-2026
│   ├── EXHIBIT.md                전시 빌드 설명(영문)
│   └── src/                      fl_midi_bridge.h, fl_pf_monitor.h(모니터 SysEx), fl_wifi.h, fl_ap_channel.h(채널 고르기),
│                                 fl_rtp_jitter.h(흔들림), fl_web_exhibit.h, main_exhibit.cpp
├── ableton/
│   ├── README.md                 Live 세트·장치·Windows 키오스크 설정 (자세한 판)
│   └── pf-exhibit-scenes/        장치 패치(.maxpat)와 로직(.js)
└── tools/
    ├── monitor/                  노트북 모니터: pf_monitor.py, dashboard.html, config.json, logs/ (tools/monitor/README.md)
    ├── flasher/                  flasher.html을 만드는 스크립트 (node tools/flasher/build.mjs)
    ├── patterns/                 패턴 호스트 하네스와 도구 (5장)
    ├── check_exhibit_feature.py  전시 기능 호스트 테스트 (자막 배치, K4, 대기 복귀, 메뉴 잠금, 노브 위치, 실제 패턴 + MIDI)
    ├── midi_monitor.py           들어오는 MIDI 보기
    ├── send_test.py              가짜 PC/CC 보내기 (loopMIDI 테스트)
    ├── test_pf_exhibit_scenes.js Live 장치 js 오프라인 테스트
    └── check_maxpat.py           .maxpat 구조 검사
```

**push 금지.** 두 클론 모두 push URL이 막혀 있다. `git remote -v`의 push 줄이 모두 `DISABLED-private-exhibition-clone`이다.

- `patternflow/`: `origin` = engmung/Patternflow
- `flowlocal/`: `origin` = engmung/flowlocal_lsh, `upstream` = SimonePDA/flowlocal 둘 다

두 클론의 `exhibit-2026` 브랜치는 이 전시만을 위한 것이다. `flowlocal/` 쪽에는 핫스팟 비밀번호가 `flowlocal_secrets.h`, `EXHIBIT.md`, `tools/check_exhibit_image.py`에 커밋돼 있다(패널 쪽 비밀번호는 클론에서 git에서 빠진 `patternflow_secrets.h`에만 있고, 사본이 이 폴더의 `backups/`에 있다). 어느 클론이든 push URL을 되살리지 않고, 리모트를 바꾸지 않고, `exhibit-2026`은 절대 push하지 않는다. 두 클론은 이 폴더의 `backups/` bundle로 백업한다. 이 폴더는 GitHub 비공개 저장소(`origin`) 하나에만 push한다(`backups/README.md`).

---

## 2. 하드웨어 연결

### 패널

- **전원은 J4 나사 단자(5 V)로만 넣는다. USB-C로 전원을 넣지 않는다.** 프로젝트의 고정 규칙이다. v3.9 보드에는 USB-C 입력 자체가 없고, v3.0 보드라면 `USB1`/`R1`/`R2`를 비워 둔 채 J4로만 넣는다. USB-C로 전원을 넣은 v3.0 보드가 20–30분 멀쩡히 돌다가 커넥터에서 연기가 난 적이 있다(#221). "잠깐 돌려 보니 괜찮다"는 근거가 안 된다.
- 패널에 꽂힌 ESP32-S3 DevKit의 USB 포트는 굽기·로그용이다. 전시 중에는 아무것도 꽂지 않는다.

### FlowLocal (ESP32-S3-DevKitC-1 N16R8)

| DevKit 잭 | 용도 |
|---|---|
| **USB** (native USB, 왼쪽) | **노트북에 꽂는다.** 보드 전원이자 USB-MIDI 장치 "Patternflow Exhibit"(제조사 Patternflow, VID:PID 303A:1001). 노트북 모니터의 SysEx도 이 케이블로 오간다. 평소에는 시리얼 포트가 없다. **다운로드 모드에서는 이 잭으로도 굽는다**: BOOT를 누른 채 RST를 한 번 눌렀다 떼고 BOOT를 떼면 새 COM 포트가 생기고, 굽고 나서 RST를 한 번 누른다(4.5) |
| **COM** (USB-UART, 오른쪽) | 굽기와 시리얼 로그(115200). 버튼 없이 굽는다. 전시 중에는 비워 둔다. 지금 설치(이 노트북)에서는 이 잭에 꽂을 수 없어서 USB 잭으로 굽고, 시리얼 로그 대신 모니터와 상태 페이지를 본다 |

- **USB-OTG 납땜 점퍼는 열어 둔다**(붙이지 않는다).
- USB 스틱, SD 카드, TFT, 배터리 케이스는 달지 않는다.
- 노트북의 USB 포트를 하나 정해 테이프로 표시한다. 다른 포트에 꽂으면 Windows가 새 장치로 잡아 포트 이름이 `2- Patternflow Exhibit`처럼 바뀔 수 있고, 그러면 Live 설정을 다시 켜야 한다. 모니터가 뽑았다 꽂으라고 할 때도 5초 뒤 같은 포트에 다시 꽂는다.
- 2026-09-25에 처음 꽂았을 때 한 번, Windows가 FlowLocal을 "알 수 없는 USB 장치(장치 설명자 요청 실패)"로 잡았다. 뽑았다 다시 꽂으니 됐고 다시 나오지 않았다(케이블이나 포트를 의심).

### 노트북과 소리

- 스피커는 노트북 오디오 출력에 연결한다. Live 설정 → Audio에서 출력 장치를 고정한다.
- **Live 오디오는 FlexASIO로 한다**(2026-09-25부터). Driver Type `ASIO`, Audio Device `FlexASIO`, Sample Rate 48000. FlexASIO 설정은 `C:\Users\user\FlexASIO.toml`(WASAPI 공유 모드, 버퍼 480 샘플 = 10 ms, 입력 끔). 실측 출력 지연 22.0 ms(버퍼 포함 26.7 ms). 지직거리면 그 파일의 `bufferSizeSamples`를 960으로 올린다. 파일을 지우면 FlexASIO 기본값으로 돌아간다.
  - **Realtek ASIO는 쓰지 않는다.** 2026-09-25 15:52와 16:17에 Live가 `rthdasio64.dll`(3.1.9.1) 안에서 c0000005로 튕겼다(Windows 응용 프로그램 로그). MME/DirectX는 안정적이지만 지연이 96 ms였다.
  - **Voicemeeter는 쓰지 않고 자동 실행을 끈다**(작업 관리자 → 시작 앱). 로그인할 때 켜지면 Realtek 장치를 먼저 잡는다.
  - **Live 자동 업데이트를 끈다**(Live → Settings → Licenses/Maintenance → Get Updates). 2026-09-25 하루에 12.3.7 → 12.3.8 → 12.4 → 12.4.1로 바뀌었고, 버전마다 설정 폴더(`%APPDATA%\Ableton\Live 12.x.y\Preferences`)가 따로 생긴다.
- 노트북은 항상 충전기에 연결한다. 절전·USB 절전 끄기 등은 레포 맨 위의 `노트북 전시용 전원 설정.bat`을 한 번 실행하면 된다(관리자 권한 필요 없음. 전원 연결·배터리 둘 다: 절전·최대 절전 안 함, 덮개 닫기 아무것도 안 함, USB 선택적 절전 사용 안 함, PCI Express 링크 상태 전원 관리 해제). 패턴플로우 모니터가 이 설정을 60초마다 보고, 켜져 있으면 노랑 "노트북 절전 설정을 고쳐야 해요"로 알린다. 이 노트북은 2026-09-25에 USB 선택적 절전이 "사용"이었다(실측 기록).

---

## 3. 네트워크

| 항목 | 값 |
|---|---|
| SSID | `PatternflowLocal` |
| 비밀번호 | `YOUR_HOTSPOT_PASSWORD` (WPA2) |
| FlowLocal IP | `192.168.66.1` (255.255.255.0) |
| 채널 | 켤 때마다 1·6·11 중 가장 한산한 곳을 스스로 고른다(20 MHz 폭). 스캔이 실패하면 6 |
| 동시 접속 | 최대 3대: 패널, 운영자 폰, 여분 하나(패널 무선 갱신 때의 노트북). 30초 동안 아무 신호가 없는 기기는 내보낸다 |
| RTP-MIDI | FlowLocal이 듣는 쪽. 세션 이름 `FlowLocal`, UDP 5004/5005 |
| 노트북 모니터 | 핫스팟이 아니라 USB 케이블로 FlowLocal에 묻는다(SysEx). 핫스팟 자리를 쓰지 않는다 |

- 예전 기본 비밀번호 `patternflow`는 이 빌드에서 안 된다(FlowLocal 빌드가 그 비밀번호면 컴파일을 거부한다).
- 핫스팟 이름·비밀번호·IP는 FlowLocal 펌웨어에 박혀 있고 상태 페이지에서 바꿀 수 없다.
- **채널은 켤 때 한 번 고른다.** FlowLocal은 켜고 12초 동안 전파를 내지 않는데(끝의 실측 기록 9/19), 그 마지막 3초에 주변 Wi-Fi를 한 번 훑어 1·6·11 중 가장 덜 붐비는 채널로 핫스팟을 연다(`FL_EXHIBIT_AP_CHANNEL 0` = 자동, `1`–`13`을 넣으면 그 채널로 고정). 전시 중에 주변 Wi-Fi가 늘어도 다시 켤 때까지 그대로다. 다시 고르게 하려면 모니터의 **한 번에 다시 연결하기**(FlowLocal을 다시 켠다). 자동으로 고른 채널이 실제로는 나쁘면(모니터 "무선 상태가 나빠요") 운영자용 칸의 **무선 채널 바꾸기**로 다음 채널(1→6→11)로 옮긴다. 그 선택은 FlowLocal에 저장돼 다음에 켤 때도 쓰이고, **채널 자동으로**를 누르면 다시 자동이 된다(FlowLocal `flx-mon3`부터, 상태 `chm=manual`). 켤 때 훑은 점수는 상태의 `cs=`(100 단위, 낮을수록 한산)에 나온다. 고른 채널은 모니터의 무선 카드, 상태 페이지의 **Channel**, 로그 `[FlWifi] channel scores ch1 …, ch6 …, ch11 … -> ch N (auto)`에 나온다. 2026-09-25 이 자리에서는 채널 1을 골랐다(실측 기록).
- **3자리 제한은 그대로이고, 패널용 자리를 따로 잡아 두지 않는다.** 폰 두 대가 붙어 있으면 패널이 못 들어온다. 인사 없이 범위를 벗어나거나 전원이 빠진 기기는 30초 동안 아무 신호가 없으면 FlowLocal이 내보낸다(`FL_EXHIBIT_AP_INACTIVE_S`. 예전 펌웨어는 드라이버 기본값 300초. 실기로 재지는 않았다). 살아 있는 패널은 10초마다 신호를 보내므로 밀려나지 않는다. 점검이 끝나면 폰의 Wi-Fi를 끈다. 가득 차면 모니터에 "핫스팟 자리가 꽉 찼어요", 상태 페이지에 `Hotspot full`이 뜬다.

### 노트북은 핫스팟에 붙이지 않는다

- 노트북과 FlowLocal은 USB 케이블 하나로만 이어진다. MIDI도 전원도 그 케이블로 간다.
- 노트북을 `PatternflowLocal`에 붙이는 경우는 **패널을 무선으로 다시 굽을 때(4.2) 하나뿐**이다. 붙이기 전에 운영자 폰 말고는 핫스팟에 아무것도 없게 한다.
- **굽기가 끝나면 바로 연결을 끊고 네트워크를 지운다.** Windows 설정 → 네트워크 및 인터넷 → Wi-Fi → 알려진 네트워크 관리 → `PatternflowLocal` → **삭제**. 연결만 끊고 저장해 두면 "자동으로 연결" 때문에 노트북이 켜질 때마다 다시 붙는다. 그러면 3자리 중 하나를 노트북이 차지하고, 폰 한 대만 더 붙어도 패널이 못 들어온다.

### 패널이 Wi-Fi를 아는 방법

**전시 패널 펌웨어는 컴파일된 `PatternflowLocal` 하나에만 붙는다**(`PF_WIFI_BUILTIN_ONLY 1`, `patternflow/firmware/bundles/exhibit/overrides.h`, 2026-09-25부터).

- 이름과 비밀번호는 `patternflow/firmware/patternflow/patternflow_secrets.h`(git에서 빠진 파일. 사본은 `backups/patternflow_secrets.h`)의 `PF_WIFI_SSID "PatternflowLocal"`, `PF_WIFI_PASS`다. **이 파일 없이 빌드하면 일부러 컴파일이 실패한다**(`PF_WIFI_BUILTIN_ONLY needs the network's PF_WIFI_SSID in patternflow_secrets.h`). 예전처럼 자리표시 SSID만 찾는 패널이 만들어지지 않게 하려는 것이다.
- 패널에 저장된 네트워크(NVS `pf_wifi`, 콘솔 `/wifi`나 Improv로 저장한 것)는 부팅 때도, 다시 붙을 때도 시도하지 않는다. 남아 있으면 부팅 로그에 `[WiFi] built-in network only: N saved network(s) not tried`가 찍힌다.
- 콘솔 `/wifi` 페이지는 맨 위에 `This firmware joins only PatternflowLocal, compiled in.`을 보이고, 저장된 것은 `not used`로 나온다. **Add a network**와 **Boot network**는 없다. **forget**은 된다(목록 정리일 뿐 동작은 안 바뀐다). `/api/wifi`로 추가하거나 고르면 403, Improv(시리얼로 Wi-Fi를 넣는 방식)는 "unable to connect"로 거절한다.
- 전송 출력은 19.5 dBm(무선 칩 기본값)이다. 코어는 EU 적합성 시험 여유로 13 dBm에 묶어 두는데, 전시에서는 패널 전파가 초대와 MIDI를 모두 나르므로 풀었다(`PF_WIFI_TX_POWER`). 실제 값은 패널 `/api/status`의 `txDbm`. 2026-09-25에 FlowLocal에서 잰 패널 신호 세기는 −37 ~ −43 dBm이었다.
- **예전 규칙은 다른 에디션(기본·오디오 등)과 9/24까지의 전시 이미지에 그대로 있다**: 패널에 저장된 네트워크가 하나라도 있으면 컴파일된 값을 아예 시도하지 않는다. 2026-09-25의 "안 붙음"이 이것이었다. 집 Wi-Fi 하나가 저장된 패널이 `PatternflowLocal`을 시도하지 않고 집 Wi-Fi에서 192.168.66.1을 계속 초대했다(실측 기록). 예전 이미지가 도는 패널은 4장대로 새 이미지로 굽는다.

### 패널이 MIDI를 보낼 곳을 아는 방법

- 전시 빌드는 `PF_MIDI_DEFAULT_HOST "192.168.66.1"`과 `PF_MIDI_SESSION_LOCKED 1`로 컴파일된다. **초대할 곳은 늘 192.168.66.1이고 MIDI는 늘 켜져 있다.** `/midi`에서 바꿀 수 없다.
  - `/midi` 페이지의 **Computer to invite** 칸은 고정돼 있고 **Save**, **Use this computer**, **Clear** 버튼이 없다. **MIDI on this panel** 스위치도 잠겨 있다. 아래에 `This firmware always calls the computer above and keeps MIDI on: neither can be changed from this page.`가 뜬다. `/api/midi`로 MIDI 끄기나 다른 호스트(빈 값 포함)를 보내면 403이다.
  - 예전 빌드가 NVS(`pf_midi`)에 저장한 호스트나 "끔"은 읽지 않는다. 다른 호스트가 저장돼 있으면 부팅 로그에 `[MIDI] host fixed at 192.168.66.1 by this build; the one saved on /midi ("…") is not used`가 찍힌다.
  - 절대값/상대값 모드와 노브별 감도(×4)는 여전히 누르는 순간 저장되지만 이 빌드의 K1–K3에는 쓰이지 않는다(1장 "노브 위치").
- 초대는 패널이 하고 FlowLocal은 듣기만 한다. 시각은 `overrides.h`가 정한다(괄호 안은 예전 값).
  - Wi-Fi에 붙고 **1초 뒤** 첫 초대(20초. 20초는 컴퓨터의 rtpMIDI가 스스로 다시 붙을 틈인데 FlowLocal에는 필요 없다).
  - 초대 하나를 1초 간격으로 **3번** 보내고 답이 없으면 포기하고, **1초 뒤** 새로 초대한다(13번 보낸 뒤 20초 주기). FlowLocal은 이미 답한 초대의 반복에는 답하지 않아서, 그 답이 무선에서 사라지면 새 초대만 통한다. 그 손해가 20초에서 약 4초로 줄었다.
  - 세션이 끝나면(FlowLocal이 끝낸 것 포함. 모니터의 **패널 연결 다시 잡기**가 이것이다) **1초 뒤** 다시 초대한다(20초 주기).
  - Wi-Fi가 끊겼다 돌아오면(FlowLocal이 다시 켜질 때 핫스팟도 같이 꺼진다) 남은 세션을 바로 끝내고 1초 뒤 초대한다. Wi-Fi가 끊긴 동안에는 초대를 보내지 않는다(최종 이미지 `07b4c70b…`부터).
  - **Wi-Fi 자가 복구**: Wi-Fi는 붙어 있는데 세션 없이 60초가 지나고 그동안 초대가 2번 이상 답을 못 받으면, 패널이 Wi-Fi를 끊고 다시 붙는다(2분에 한 번까지). FlowLocal이 패널이 못 알아챌 만큼 빨리 다시 켜진 경우를 위한 것이다. 로그 `[MIDI] … invitations to 192.168.66.1 unanswered in … s on a Wi-Fi link that is up - reconnecting the Wi-Fi (at most every 120 s)`, 패널 `/api/status`의 `midi.wifiHeals`(평소 0)와 `midi.invitesUnanswered`.
- 2026-09-25 실측(패널 `12bc93a1…`, FlowLocal `flx-mon1`): 모니터의 **패널 다시 켜기**로 패널을 다시 켜고 4.7초 뒤 MIDI가 돌아왔다. FlowLocal을 켜고는 약 14.7초 뒤다(12초에 핫스팟이 뜨고 2.7초 뒤 세션). 예전 이미지는 패널을 다시 켜고 MIDI까지 24초 걸렸다.

### 폰으로 패널 페이지 열기 — 패널 주소 찾기

운영자가 점검용 폰으로 패널 페이지(밝기, 노브 방향, 자막)를 열 때 쓴다. 전시 중의 상태 보기와 고치기는 노트북 모니터가 먼저다(7장). 패널 주소는 FlowLocal 상태 페이지가 알려 준다.

1. 운영자 폰을 `PatternflowLocal`에 붙이고 `http://192.168.66.1/`을 연다.
2. **Panel** 칸을 본다.
   - **`Panel: 192.168.66.x`** — 패널이 MIDI 세션을 연 주소다. 아래 링크가 새 탭으로 열린다.
     - **Console** `http://<패널>/` — 패널 콘솔 홈(밝기)
     - **MIDI** `/midi`, **Knobs** `/knobs`
     - **Captions off** `/api/exhibit?caption=0`, **Captions on** `/api/exhibit?caption=1`
     - `/wifi` 링크는 없다. Console 주소 뒤에 `wifi`를 붙여 연다.
   - **`Panel: unknown`** — MIDI 세션이 없어서 FlowLocal이 어느 기기가 패널인지 모른다. 대신 핫스팟에 붙은 다른 기기(페이지를 보는 폰은 빠진다)가 **Console**·**MIDI** 링크와 함께 나온다. 그중 하나가 패널일 것이라는 추측 목록이고, 자리가 3개라 많아야 둘이다. 전시 이미지는 MIDI를 끄거나 호스트를 바꿀 수 없으니, 패널이 막 켜졌거나 초대 답이 무선에서 사라진 것이다(패널이 곧 다시 초대한다). 목록이 비어 있으면 패널이 꺼졌거나 핫스팟에 못 붙은 것이다(위 "패널이 Wi-Fi를 아는 방법").
3. 링크를 연 뒤 MIDI 페이지에서는 아무것도 누르지 않는다. 호스트와 MIDI 켬은 잠겨 있고, 슬라이더는 누르는 순간 저장되지만 이 빌드의 K1–K3에 쓰이지 않는다.

- `http://patternflow.local/`도 쓸 수 있을지 모르지만, 폰에서 `.local`이 열리는지는 확인 안 됨.
- 상태 페이지가 없을 때 패널 주소는 패널 시리얼 로그의 `[OTA] Ready — hostname "patternflow.local", IP ...` 줄에도 있다.
- 핫스팟 안의 폰에서 패널 페이지로 실제로 가지는지, `Panel: unknown` 목록에 패널이 나오는지는 실기 확인이 안 됐다(8장).

---

## 4. 펌웨어 빌드·플래시

### 먼저 알아 둘 것

- 저장소 경로에 한글이 있어서 xtensa 링커가 출력 파일을 못 쓴다. **빌드 폴더는 반드시 ASCII 경로**로 준다. 이 노트북에서는 `C:/Users/user/` 아래다(아래 명령에 들어 있다). 예전 데스크탑에서는 같은 이름의 폴더가 `C:/Users/lsh/` 아래 있었다.
- 두 펌웨어 모두 **핫스팟 비밀번호가 평문으로 이미지에 들어간다.** 아래 폴더의 `.bin`은 공유·배포하지 않는다.
  - `C:/Users/user/pfx-exhibit-build/` (패널)
  - `C:/Users/user/pfx-all-build-editions/` 같은 `…-editions/` 폴더 (default/audio/performance/clock/midi 이미지도 비밀 파일이 있는 상태로 빌드됨)
  - `C:/Users/user/flx-pio/build/exhibit/` (FlowLocal)
- 빌드는 한 번에 하나씩. Bash 한 번 호출이 10분에 끊기는 환경이면 백그라운드로 돌린다.

### 4.1 패널 — 빌드

Git Bash에서:

```bash
cd /c/Users/user/Desktop/작업자료/2026/패턴플로우서울역/patternflow
PF_BUILD_DIR=C:/Users/user/pfx-exhibit-build ./firmware/bundles/build.sh exhibit
```

- 결과: `C:/Users/user/pfx-exhibit-build/firmware/firmware.bin` (2026-09-25 기준 1,170,496 B, sha256 07b4c70b…. `firmware_images/panel/firmware.bin`과 같다), 같은 폴더에 `bootloader.bin`, `partitions.bin`.
- 끝에 `NOTE: built WITH patternflow_secrets.h`가 나온다. 비밀 파일이 없으면 전시 빌드는 이제 컴파일에서 멈춘다(3장 "패널이 Wi-Fi를 아는 방법").
- **`firmware/patternflow`에서 `pio run -t upload`를 직접 하지 않는다.** `build.sh`가 전시 구성 파일을 잠깐 복사했다가 끝나면 치운다. PlatformIO를 직접 부르면 기능 없는 기본 펌웨어가 만들어진다.

빌드 뒤 점검(패널 클론에서, 그다음 이 폴더에서):

```bash
cd /c/Users/user/Desktop/작업자료/2026/패턴플로우서울역/patternflow
ZIGCXX=C:/Users/user/pfx-tools/zig-c++.exe        # 이 노트북: g++·MSVC가 없어서 zig를 쓴다 (아래)
python firmware/toolchain/check_boundaries.py     # boundary holds
CXX=$ZIGCXX python firmware/toolchain/check_midi.py      # PASS 세 줄 (기본, 노브 위치, 호스트·MIDI 잠금)
CXX=$ZIGCXX python firmware/toolchain/check_network.py   # network 두 줄 (기본, PF_WIFI_BUILTIN_ONLY)
grep -caF "PatternflowLocal" C:/Users/user/pfx-exhibit-build/firmware/firmware.bin   # 1 이상
grep -caF "[EXHIBIT]"        C:/Users/user/pfx-exhibit-build/firmware/firmware.bin   # 1 이상
cd ..
python tools/check_exhibit_feature.py             # PASS 네 줄. CXX 없이 돌린다
```

C++ 컴파일러: `check_exhibit_feature.py`는 `CXX`, g++, clang++, MSVC 순으로 찾고, 하나도 없으면 `pip install ziglang`으로 깐 zig의 Clang을 `python -m ziglang c++`로 저절로 쓴다(C++20, zig에 필요한 `-UNDEBUG -D_USE_MATH_DEFINES`도 붙인다). **`CXX`를 아래 zig-c++.exe로 둔 채 돌리면 real 프로그램이 `M_PI_2`에서 컴파일에 실패한다**(2026-09-25 확인). 그래서 이 스크립트는 `CXX` 없이 돌린다. 패널 클론의 `check_midi.py`·`check_network.py`는 ziglang 대체를 모르므로 `CXX`에 컴파일러 실행 파일을 준다. 이 노트북에는 zig c++를 `-UNDEBUG`와 함께 부르는 `C:/Users/user/pfx-tools/zig-c++.exe`가 있다(소스 `zig_cxx.c`가 옆에 있다. `pip install ziglang`으로 깐 `zig.exe` 경로가 박혀 있다). 2026-09-25에 위 점검이 모두 이렇게 통과했다.

`check_exhibit_feature.py`는 프로그램 세 개를 컴파일해 돌린다. 하나만 돌릴 때는 `--only logic|real|midi`.

- **logic** — 전시 기능의 프레임 논리(자막, K4, 대기 복귀, 메뉴 잠금, 노브 위치). 패턴은 가짜.
- **real** — **실제 작가 패턴 6개**와 실제 MIDI 코드, 스케치의 `applyLaneMotion`을 그대로 넣고 프레임 순서를 흉내 낸다. 매 프레임 떠 있는 패턴의 K1–K3 변수가 "범위 아래 끝 + 위치 × 범위"와 같은지, 나간 CC 24–26이 round(위치 × 127)과 같은지 본다. 패턴마다 노브 가운데에서 무엇으로 열리는지 표도 출력한다(5장).
- **midi** — `PF_MIDI_OUT_LANE_ABSOLUTE`를 켜지 않은 빌드(오디오 에디션 등)의 MIDI 출력이 이 변경 전(`013fd9b`)과 바이트 단위로 같은지. 이 비교는 `patternflow/` 클론의 git 기록을 읽는다.

펌웨어 코어(`patternflow.ino`, `src/`)를 건드렸다면 모든 구성을 빌드한다. 2026-09-25에는 전시 빌드 폴더를 덮어쓰지 않게 따로 `PF_BUILD_DIR=C:/Users/user/pfx-all-build ./firmware/bundles/build.sh all`로 빌드했다(다섯 에디션 모두 성공, 이 노트북에서 약 7분. 에디션 이미지는 `C:/Users/user/pfx-all-build-editions/`). 전시 빌드 폴더로 `all`을 돌렸다면 그 뒤 전시 이미지를 다시 빌드한다(`all`이 같은 빌드 폴더를 덮어쓴다). 예전 데스크탑에서는 약 27분 걸렸다.

### 4.2 패널 — 굽기

**빌드 없이 바로 구우려면** 레포 맨 위의 `flasher.html`을 Chrome이나 Edge로 열고 버튼을 누른다(보드를 꽂고, 뜨는 창에서 포트 고르기). `firmware_images/`의 이미지가 그 안에 들어 있다. 지금은 2026-09-25 빌드(패널 `07b4c70b…`, FlowLocal `b48b0ee8…` = `flx-mon2`)다. 명령줄로 하는 방법은 `firmware_images/README.md`.

**USB로 굽기.** BUILD_GUIDE 8장처럼 ESP32-S3 DevKit을 패널 보드에서 빼서, DevKit의 **자기 USB 잭**(native USB)으로 PC에 꽂는다. 버튼 없이 구워진다. 포트 번호는 장치 관리자에서 확인한다(이 노트북에서는 `COM10`, 예전 데스크탑에서는 `COM17`). **`--port`를 꼭 적는다.** 이 노트북에는 FlowLocal도 꽂혀 있어서, 포트를 안 주면 esptool이 스스로 찾은 보드에 굽는다.

설정을 남기고 앱만 바꿀 때(보통 이것. 9/19부터 USB로 구울 때는 늘 이렇게 했다):

```bash
export PYTHONUTF8=1 PYTHONIOENCODING=utf-8     # esptool 5.x가 한글 콘솔에서 진행 막대 글자로 죽지 않게
ESPTOOL=C:/Users/user/.platformio/penv/Scripts/esptool.exe
B=C:/Users/user/pfx-exhibit-build/firmware
BOOT_APP0=C:/Users/user/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin

"$ESPTOOL" --chip esp32s3 --port COM10 --baud 921600 write-flash 0xe000 "$BOOT_APP0" 0x10000 "$B/firmware.bin"
```

**처음 한 번, 또는 저장된 설정을 비우고 싶을 때: 지우고 굽기.**

```bash
"$ESPTOOL" --chip esp32s3 --port COM10 erase-flash
"$ESPTOOL" --chip esp32s3 --port COM10 --baud 921600 write-flash \
  0x0 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xe000 "$BOOT_APP0" 0x10000 "$B/firmware.bin"
```

- `erase-flash`는 저장된 Wi-Fi 목록, `/midi` 설정, 자막 선택, 밝기·노브 방향 같은 패널 설정, 업로드했던 `.pfm` 모듈(FATFS)까지 전부 지운다. 전시에는 모듈이 필요 없다. 밝기는 굽고 나서 웹 콘솔에서 다시 맞춘다. 전시 이미지는 저장된 Wi-Fi와 `/midi` 호스트를 읽지 않으므로(3장), 그것 때문에 지울 필요는 없다.
- 모듈을 남기고 설정(NVS)만 지우려면 `erase-flash` 대신 `"$ESPTOOL" --chip esp32s3 --port COM10 erase-region 0x9000 0x5000`을 쓰고 같은 `write-flash`를 한다.
- `0xe000`의 `boot_app0.bin`을 빼지 않는다. 무선 업데이트를 받은 적 있는 보드는 두 번째 앱 슬롯으로 부팅하고 있어서, `0x10000`에만 쓰면 옛 앱이 계속 뜬다.
- 오프셋은 파티션 표(`partitions/app3M_fat9M_16MB.csv`)와 맞다. 2026-09-18에 전체 지우기 + 네 파일로 구웠다(굽기 기록).
- 개발용 보드 `.119`(patternflow-044c)는 BOOT 핀 커패시터 때문에 리셋·전원 투입마다 다운로드 모드에 머문다(2026-09-12 기록). 커패시터를 떼기 전에는 전시 패널로 쓰지 않는다.

**이미 전시 펌웨어가 도는 패널을 갱신할 때(작가 패턴 교체 등): 무선.** 저장된 설정은 그대로 남는다. 패널과 같은 Wi-Fi에 붙은 컴퓨터에서 한다.

- 현장이면 노트북을 `PatternflowLocal`에 **잠깐** 붙인다. 핫스팟 3자리 중 하나를 쓰므로 먼저 운영자 폰 말고는 핫스팟에서 뗀다.
- `<패널-IP>`는 FlowLocal 상태 페이지의 `Panel: 192.168.66.x`(3장 "폰으로 패널 페이지 열기").
- 패널은 굽고 나서 재부팅한다. MIDI는 몇 초 뒤 다시 붙는다(2026-09-25 실측: 모니터의 패널 다시 켜기로 재부팅했을 때 4.7초).
- **끝나면 노트북의 `PatternflowLocal` 연결을 끊고 네트워크를 삭제한다**(3장 "노트북은 핫스팟에 붙이지 않는다").

```bash
cd /c/Users/user/Desktop/작업자료/2026/패턴플로우서울역/patternflow
PF_BUILD_DIR=C:/Users/user/pfx-exhibit-build ./firmware/bundles/build.sh exhibit flash <패널-IP>
```

`build.sh`를 못 쓰는 컴퓨터라면 빌드된 파일을 옮겨서:

```bash
F=firmware.bin
curl -F "firmware=@$F" "http://<패널-IP>/update?size=$(stat -c%s "$F")"
```

답이 `{"ok":true}`면 패널이 스스로 재부팅한다. 올리다가 실패하면 패널에 `UPDATE / FAILED ... K4 = EXIT` 카드가 남는다. **K4를 한 번 누르면 닫힌다**(또는 전원 재시작, 아니면 10분 뒤 자동으로 닫힘).

### 4.3 패널 — 됐는지 확인

1. **시리얼 로그** — 패널 펌웨어의 시리얼은 DevKit의 **USB(native) 잭**으로 나온다(115200. 이 노트북에서는 COM10). 부팅하면:
   - `[EXHIBIT] 6 patterns at 1..6, default 1 (BAKWAN), captions off`
   - `[WiFi] connecting to "PatternflowLocal" (built-in creds, non-blocking)...`. 패널에 예전에 저장한 네트워크가 있으면 `[WiFi] built-in network only: N saved network(s) not tried`도 찍힌다(무해)
   - `[OTA] Ready — hostname "patternflow.local", IP 192.168.66.x`. IP가 192.168.66.x가 아니면 패널이 핫스팟이 아닌 곳에 붙은 것이다(예전 이미지. 3장)
   - `[MIDI] rtp listening on ...`
   - 핫스팟에 붙고 몇 초 안에 `[MIDI] rtp session joined by "..." (1)`
   - `[MIDI] rtp invitation went unanswered - not a session (0)`: 초대 하나를 3번 보내도 답이 없었다는 뜻이다. 세션 수는 바뀌지 않고, 패널은 1초 뒤 새로 초대한다. 초대 답이 무선에서 사라지면 한 번쯤 나올 수 있다. 계속 반복되면 FlowLocal이 RTP에 답하지 않는 것이고, 60초 뒤 패널이 Wi-Fi를 다시 잡는다(`… reconnecting the Wi-Fi`, 3장 "Wi-Fi 자가 복구").
   - `[MIDI] no rtp session left: rtp list cleared, inviting 192.168.66.1 in 1000 ms`: 세션이 끝나서 1초 뒤 다시 초대한다(모니터의 **패널 연결 다시 잡기** 뒤에 나온다). `[MIDI] Wi-Fi back: rtp list cleared, inviting 192.168.66.1 in 1000 ms`: Wi-Fi가 돌아와 다시 초대한다(FlowLocal이 다시 켜진 뒤).
   - `[MIDI] rtp session "..." reported joining again - counted once (1)`은 같은 세션이 두 번 보고된 것을 한 번으로 센 것이다(8장 "고쳤지만 실기 확인 전"). `rtp session list was full`은 전시에서 나오면 안 된다.
   - `[EXHIBIT] ERROR: ...`가 보이면 패턴 목록이 레지스트리 끝과 안 맞는 것이다. K4 전환이 꺼진다.
2. **화면** — 부팅하면 1번 패턴이 뜬다. 자막(작가 이름)은 나오지 않는다(빌드 기본값 끔).
3. **`http://<패널>/api/status`**:
   - `variant` = `"exhibit"`, `variantVersion` = `"v0.1.0"`, `caps`에 `"exhibit"`
   - `ssid` = `"PatternflowLocal"`, `ip`, `txDbm` = 19.5(전송 출력)
   - `exhibit` 블록: `rangeOk: true`, `first: 1`, `count: 6`, `defaultIdx: 1`, `menusLocked: true`, `captions`, `restVars: 42`, `switches`, `idleReturns`, `idleMs`, `knobDetents: 32`, `knobPos`(K1–K3 위치 0–1, 켠 직후와 대기 복귀 뒤 `[0.5000,0.5000,0.5000]`)
   - `midi` 블록: `runtime: true`, `host: "192.168.66.1"`, `rtpPeers: 1`, **`outLaneAbs: true`**(K1–K3를 노브 위치 그대로 보내는 빌드라는 뜻. 이 줄이 없으면 전시 이미지가 아니다), **`locked: true`**(초대 호스트와 MIDI 켬이 잠긴 2026-09-25 이후 이미지라는 뜻), `invitesUnanswered`(부팅 뒤 포기한 초대 수), `wifiHeals`(자가 복구로 Wi-Fi를 다시 잡은 수, 평소 0). `outMode`와 `outMul`은 이 빌드의 K1–K3에 쓰이지 않으니 값이 무엇이든 상관없다
   - `heapInternal`, `heapLargest` (내부 RAM 여유. 아직 실기 기준값이 없다. 처음 켰을 때 적어 둔다)
4. **`http://<패널>/status`** 의 Frame rate (45 fps 이상이면 초록).

### 4.4 FlowLocal — 빌드

```bash
cd /c/Users/user/Desktop/작업자료/2026/패턴플로우서울역/flowlocal
export PLATFORMIO_PLATFORMS_DIR=C:/Users/user/flx-pio/platforms
export PLATFORMIO_PACKAGES_DIR=C:/Users/user/flx-pio/packages
export PLATFORMIO_BUILD_DIR=C:/Users/user/flx-pio/build
python -m platformio pkg install -e exhibit     # 처음 한 번
python -m platformio run -e exhibit
python tools/check_exhibit_image.py             # PASS가 나와야 한다
python tools/check_exhibit_bridge.py            # 호스트 테스트, PASS (모니터 SysEx 포함)
```

- 결과: `C:/Users/user/flx-pio/build/exhibit/firmware.bin` (2026-09-25 두 번째 빌드 `flx-mon2` 기준 1,166,528 B, sha256 6445a6b5…), `firmware.factory.bin` (1,232,064 B, sha256 b48b0ee8…, 0x0에 쓰는 통합 이미지). 둘 다 `firmware_images/flowlocal/`과 같다.
- 처음 빌드 때 pioarduino가 컴파일러를 까는 `idf_tools.py`는 Git Bash에서 돌지 않는다(`MSys/Mingw is not supported`). 처음 한 번은 PowerShell이나 cmd에서 빌드하거나 `flowlocal/EXHIBIT.md` "Build"의 방법을 쓴다.
- `check_exhibit_image.py`는 새 비밀번호·SSID·포트 이름, 모니터 SysEx와 채널 고르기의 흔적이 들어 있고, `patternflow` 문자열과 일반 빌드의 브로커·스틱·SD·TFT 흔적이 없어야 PASS다. `check_exhibit_bridge.py`의 컴파일러는 `flowlocal/EXHIBIT.md` "Host test"(없으면 `pip install ziglang`).
- **이 프로젝트의 어느 env에도 `lib_ignore`를 넣지 않는다.** 두 env를 동시에 빌드하지 않는다. pioarduino가 공유 프레임워크 빌드 스크립트를 고쳐 썼다가 링크가 성공해야 되돌리기 때문에, 한 번 실패하면 일반 `flowlocal` 빌드가 깨진다(실제로 겪음).

### 4.5 FlowLocal — 굽기

빌드 없이 구우려면 `flasher.html`(두 잭 모두 된다). 명령줄로는 두 길이 있다.

- **USB 잭(노트북에 꽂힌 그대로)** — 지금 설치의 방법이다. 평소 이 잭은 MIDI 장치라 굽기용 포트가 없으니 먼저 다운로드 모드로 만든다: **BOOT를 누른 채 RST를 한 번 눌렀다 떼고, BOOT를 뗀다.** 장치 관리자에 새 COM 포트가 생긴다(2026-09-25 이 노트북에서는 `COM11`). 그동안 MIDI 포트 `Patternflow Exhibit`는 사라지므로 Live와 모니터는 FlowLocal을 잃는다. 전시 중에는 하지 않는다.
- **COM 잭(USB-UART)** — 버튼 없이 된다(USB 잭은 노트북에 꽂혀 있어도 된다. OTG 점퍼가 열려 있을 때만). 지금 설치에서는 이 잭에 꽂을 수 없다.

**`--port`를 꼭 적는다.** 패널 DevKit(COM10)도 꽂혀 있어서, 포트를 안 주면 esptool이 스스로 찾은 보드에 굽는다. 새로 생긴 포트 번호를 장치 관리자에서 확인해 넣는다(아래 `COM11`). 둘 중 하나:

```bash
# A. PlatformIO: 전체를 지우고 bootloader, partitions, boot_app0, app을 쓴다
python -m platformio run -e exhibit -t erase_upload --upload-port COM11

# B. esptool만
export PYTHONUTF8=1 PYTHONIOENCODING=utf-8
ESPTOOL=C:/Users/user/.platformio/penv/Scripts/esptool.exe
"$ESPTOOL" --chip esp32s3 --port COM11 erase-flash
"$ESPTOOL" --chip esp32s3 --port COM11 --baud 921600 write-flash 0x0 C:/Users/user/flx-pio/build/exhibit/firmware.factory.bin
```

- **USB 잭으로 구웠으면 끝나고 RST를 한 번 누른다.** 버튼으로 들어간 다운로드 모드는 esptool이 끝에 `Hard resetting via RTS pin`을 찍고 재시작해도 그대로 남는다(2026-09-25에 겪음). RST를 누르면 새 펌웨어가 뜨고, 몇 초 뒤 MIDI 포트가 돌아오고, 12초 뒤 핫스팟이 뜬다.
- A는 4.4의 `export` 세 줄이 적용된 셸에서 실행한다.
- 지우는 이유: 옛 AP 설정(NVS)과 브라우저 업데이트로 바뀐 부팅 슬롯을 없애기 위해서다. 이미 전시 FlowLocal 펌웨어가 도는 보드는 지우지 않고 `write-flash 0x0 …factory.bin`만 해도 된다(2026-09-25에 이렇게 구웠다. factory.bin에 `boot_app0`가 들어 있다).
- 지운 뒤 첫 부팅에서는 드라이버 기본값인 열린 Wi-Fi `ESP_xxxxxx`가 1초쯤 보였다가 `PatternflowLocal`로 바뀐다. 두 번째 부팅부터는 바로 `PatternflowLocal`로 뜬다.
- LittleFS 업로드(`uploadfs`) 단계는 없다.

### 4.6 FlowLocal — 됐는지 확인

1. **시리얼 로그** (COM 잭, 115200. 지금 설치에서는 COM 잭을 못 쓰니 2–4번과 모니터의 **운영자용 자세히 보기**로 대신한다):
   - `[midi] usb device "Patternflow Exhibit" started (native USB jack)`
   - `[FlWifi] AP starts in 12 s so a panel from before this boot lets go`
   - 약 9초 뒤 `[FlWifi] channel scan: N networks in … ms (…)`와 `[FlWifi] channel scores ch1 …, ch6 …, ch11 … -> ch N (auto)`
   - `[FlWifi] AP "PatternflowLocal" on 192.168.66.1 chN, max 3 stations` (방금 고른 채널), `[FlWifi] AP drops a station after 30 s of silence`
   - `[midi] rtp listener "FlowLocal" on 192.168.66.1:5004 (data 5005), ssrc XXXXXXXX` (SSRC는 보드마다 고정, 재부팅해도 같다)
   - `[midi] program mirror: CC 118 ch 16 after every Program Change` (이 줄이 없거나 `off`면 Alt CC 비상 경로가 꺼진 빌드다)
   - `[midi] knob re-send: CC 24-26 ch 1 after every re-sent Program Change` (이 줄이 없으면 PC를 다시 보낼 때 노브 값을 붙이지 않는 옛 이미지다. `0c05e79`부터)
   - `[midi] pf monitor: status request F0 7D 50 46 01 F7 answered … (v1, fw=flx-mon2, …)`와 `[midi] pf monitor: commands 10 reconnect panel, 11 reboot panel …, 12 reboot FlowLocal …` (이 두 줄이 없으면 모니터에 답하지 못하는 옛 이미지다)
   - 패널이 붙으면 `[midi] rtp session joined by "..."`
2. **노트북** — MIDI 포트 목록에 `Patternflow Exhibit`이 있다(`python tools/midi_monitor.py --list`). 옛 이름이 보이면 장치 관리자에서 장치를 제거하고 다시 꽂는다. "알 수 없는 USB 장치(장치 설명자 요청 실패)"로 보이면 뽑았다 같은 포트에 다시 꽂는다(2장). 패턴플로우 모니터의 최근 기록에 `FlowLocal 상태를 받기 시작했어요 (펌웨어 flx-mon1, …)`가 찍히고, 운영자용 자세히 보기에 채널(`ch`, `chm=auto`)과 패널 신호 세기(`prs`)가 나온다.
3. **상태 페이지 `http://192.168.66.1/`** (운영자 폰. 또는 `http://flowlocal.local/`):
   - 맨 위 `Everything connected`
   - `Panel session (Wi-Fi)` = connected, `Panel last heard` 15초 이내(패널이 10초마다 신호를 보낸다. 2026-09-25에 확인)
   - Hotspot 아래 `Channel` = `1 (auto, least busy of 1/6/11 at boot)`처럼 고른 채널, `Wi-Fi timing (jitter / worst, last minute)`
   - `Laptop (USB jack)` = connected as "Patternflow Exhibit"
   - K4를 돌리면 `Current pattern (program)`이 바뀐다(패널이 아직 PC를 보내지 않았으면 `none yet`)
   - `Program mirror to Live`가 `off`가 아니고, K4를 돌릴 때마다 보낸 개수가 는다
   - `Knobs K1–K3 (re-sent with the pattern)`에 `CC 24-26 ch 1: 64 / 64 / 64, 0 re-sent`처럼 패널이 마지막으로 보낸 값이 보이고, K1–K3를 돌리면 바뀐다(패널이 아직 안 보낸 노브는 `–`)
   - `In / out / dropped`에서 dropped가 늘지 않는다
   - **Panel** 칸에 `Panel: 192.168.66.x`와 링크 다섯 개(Console, MIDI, Knobs, Captions off, Captions on). **Console**이 폰에서 패널 콘솔을 연다
4. **`GET http://192.168.66.1/api/status`** — `build: "exhibit"`, `ap_ssid`, `ap_clients` / `ap_max_clients`(3), `panel_ip`(세션이 없으면 `null`), `ap_stations`(핫스팟 기기 주소 목록), `viewer_ip`, 그리고 `midi` 아래 `peers`, `usb_mounted`, `rx`, `tx`, `dropped`, `last_program`, `last_rx_age_ms`, `last_heard_age_ms`, `rtp_gaps`, `resync_requests`, `web_resends`, `usb_in_discarded`, `program_mirror`, `program_mirrors`, `knob_resend`, `knob_values`(패널이 마지막으로 보낸 CC 24–26, 아직 없으면 `null`), `knob_resends`, `rtp_replaced`, `rtp_unknown`, `rtp_bye_sent`. `flx-mon1`부터는 맨 위에 `ap_channel`, `ap_channel_mode`(`"auto"`), `ap_channel_scan`(스캔 결과와 채널별 점수), `midi` 아래에 모니터의 `status_requests`, `status_replies`, `status_abandoned`(늘지 않아야 한다), `monitor_commands`, `monitor_commands_refused`, `panel_reboots`, 그리고 `rtp_jitter_ms`, `rtp_spread_ms`(모니터의 `jt`, `jm`)가 더 있다.
5. **`Send current pattern to Live` 버튼** — 누르면 `web_resends`가 늘고 Live가 같은 PC와 CC 118, 이어서 CC 24–26 마지막 값을 한 번 더 받는다(답: `Sending pattern n and 3 knob positions to Live.`). 패널이 FlowLocal 시작 후 PC를 한 번도 안 보냈으면 `No pattern yet`이 뜬다.

---

## 5. 작가 패턴 교체 방법

지금 들어 있는 것은 작가 6명의 패턴이다. 2026-09-19에 **우리가 직접 다시 번역했다**(펌웨어 클론 `a5b5db9`부터). Pattern Lab 내보내기가 아니라, 노브를 전시에 맞춰 고친 JS를 C++로 옮긴 것이다. 2026-09-24부터 JS와 .h는 이 레포의 `patterns/<번호>_<이름>/`에 나란히 있고, 펌웨어는 그 폴더의 .h를 바로 컴파일한다(클론 안에 사본 없음). 복사·갱신 방법은 `patterns/README.md`. 무엇을 고쳤는지는 `PATTERN_KNOBS.md`, JS와 같은 그림인지는 호스트 하네스 `tools/patterns/host/`(`pfhost.py run`, 하드웨어 없이)가 보여 준다. HANYEONGHOE의 .h는 손으로 고치지 않고 `tools/patterns/hanyeonghoe_h/build.sh`로 `patterns/3_hanyeonghoe/hanyeonghoe.project.json`에서 다시 만든다. 처음 받은 Pattern Lab 내보내기 `.h`는 `patterns/_original/h/`에 보관만 하고, 펌웨어에 들어가지 않는다. 메모리 변환한 곳에는 `exhibit:` 주석이 붙어 있고, 작가·라이선스 머리말과 꼬리말은 그대로다. 교체 절차의 원본 설명은 `patternflow/firmware/patternflow/features/exhibit/exhibit_presets.h` 머리 주석(1–7단계)에 있다. 아래 경로는 모두 `patternflow/firmware/patternflow/` 기준이다.

| 순서 = PC | 파일 | namespace | 지금 자막 | 그리는 방향 |
|---|---|---|---|---|
| 1 (기본) | `patterns/1_bakwan/bakwan.h` | `BAKWAN` | BAKWAN | 세로 64×128 |
| 2 | `patterns/2_choegyeongeun/choegyeongeun.h` | `CHOEGYEONGEUN` | CHOEGYEONGEUN (작은 글꼴) | 세로 64×128 (세로판으로 다시 그림) |
| 3 | `patterns/3_hanyeonghoe/hanyeonghoe.h` | `LabStack_HANYEONGHOE` | HANYEONGHOE (작은 글꼴) | 세로 64×128 |
| 4 | `patterns/4_heoeunbin/heoeunbin.h` | `HEOEUNBIN` | HEOEUNBIN | 세로 64×128 (머리가 아래, 원본이 패널에 나오던 방향 그대로) |
| 5 | `patterns/5_jeongeungyo/jeongeungyo.h` | `JEONGEUNGYO` | JEONGEUNGYO (작은 글꼴) | 세로 64×128 |
| 6 | `patterns/6_yujin/yujin.h` | `YUJIN` | YUJIN | 세로 64×128 |

**자막은 지금 이름 한 줄뿐이다.** 받은 파일에는 작가 이름(Title 칸이 작가 이름)만 있고 작품 제목이 없어서, 여섯 줄 모두 `X(namespace, "", "")`로 두었다. 작가 줄(주황)은 나오지 않고, 패턴의 `NAME`이 흰 글씨 한 줄로 나온다. 10글자가 넘는 CHOEGYEONGEUN(13)·HANYEONGHOE(11)·JEONGEUNGYO(11)은 작은 글꼴 한 줄이 된다. 제목이 오면 두 칸을 다 채운다: `X(YUJIN, "YUJIN", "<제목>")`(4단계). 제목만 넣으면 자막에 제목만 나오고 이름이 빠진다. 이름의 로마자 표기는 파일에 적힌 그대로다.

작가와 확인할 것(`PATTERN_KNOBS.md`의 노브 표 기준. 코드와 호스트 하네스로 본 것이고 실기로 보지 않았다):

- **방향** — 여섯 모두 세로(64×128)로 그려서 자막과 위가 같다. CHOEGYEONGEUN은 세로판(`choegyeongeun_portrait.js`)으로 다시 그렸다. HEOEUNBIN은 원본이 패널에 나오던 방향 그대로 머리가 아래다(작가가 고른 방향). 작가가 이 방향으로 미리 보았는지 묻는다.
- **노브 끝**(6단계) — 역할은 여섯 모두 K1 = 양, K2 = 색/공간, K3 = 속도다.
  - BAKWAN: K1 성긴 가지 ↔ 빽빽한 금빛, K2 약한 왼바람(6칸째 무풍) ↔ 강풍, K3 느리게 ↔ 빠르게.
  - CHOEGYEONGEUN: K1을 0에 두면 문이 닫혀(문틈 빛만 숨쉼) 거의 정지 화면이 된다. 닫았다 다시 열면 다음 장면으로 넘어가는 작품이라 범위를 좁히면 안 된다. K2 작게 ↔ 감실 안으로, K3 느리게 ↔ 빠르게.
  - HANYEONGHOE: K1 구름 2개·작은 봉오리(0.4) ↔ 6개·활짝 핀 큰 꽃(1.6), 줄기도 ±4px. K2 구름 작게 ↔ 크게, K3 느린 바람 ↔ 센 바람. 예전 파일처럼 구름 층이 사라지는 끝은 없다(고쳤다).
  - HEOEUNBIN: K1 작게 ↔ 머리 클로즈업, K2 어두운 주황 ↔ 꽃 6송이, K3는 가장 낮아도 걷는다.
  - JEONGEUNGYO: K1 흩어짐 ↔ 3x3 완성, K2 반대로 돎 ↔ 빠르게(정확히 멈추는 칸은 없다), K3 느리게 ↔ 빠르게.
  - YUJIN: K1 4 ↔ 24마리, K2 평온 순항 ↔ 화면 전체 소용돌이, K3 느리게 ↔ 빠르게.
  - 모두 3분 대기 복귀가 되돌린다. 화면이 완전히 꺼지거나 멈추는 끝은 코드상 없다(CHOEGYEONGEUN의 닫힌 문이 가장 가깝다).
- **노브 가운데에서 여는 모습** — 아래 표. 노브를 고칠 때 각 범위의 가운데가 작가가 원하는 첫 화면이 되게 잡았다(BAKWAN은 작가 값 그대로, YUJIN K2는 뭉치지 않게 0.625로). 작가에게 이 첫 모습이 맞는지 묻는다.
- **K4** — 여섯 모두 네 번째 파라미터를 그림에 쓰지 않는다. 할 일 없음.

#### 노브 위치로 여는 모습

K1–K3는 모든 패턴에 **같은 노브 위치**로 들어간다(1장 "노브 위치"). 파라미터 = 범위 아래 끝 + 위치 × 범위이고, 켤 때와 대기 복귀 때 위치는 가운데(0.5)다. 그래서 패턴 코드에 적힌 시작값이 아니라 범위의 가운데 모습으로 열린다. 지금 파일은 코드의 시작값도 가운데에 맞춰 두어서 두 값이 같다. `python tools/check_exhibit_feature.py --only real`이 이 표의 숫자를 `opens at`으로 출력한다(2026-09-19, 지금 파일 기준). 괄호 안의 설명은 `PATTERN_KNOBS.md`에서 옮긴 것이고 실기에서 보지 않았다.

| 패턴 | 노브 | 파라미터 | 범위 | 가운데(지금 여는 값) |
|---|---|---|---|---|
| BAKWAN | K1 | Foliage | 0.25–1 | 0.625 (작가 값 0.631) |
| | K2 | Wind | −0.75–3.25 | 1.25 (작가 값, 오른바람) |
| | K3 | Speed | 0.35–1.65 | 1.0 (원래 템포) |
| CHOEGYEONGEUN | K1 | DoorOpen | 0–1 (곡선) | 0.5 (열림 0.8, 문이 양옆으로 젖혀짐) |
| | K2 | Zoom | 0–1 → 0.7–2.2 | 0.5 (0.95, 궤짝 전체) |
| | K3 | FlowerSpeed | 0–1 → 0.35–8 | 0.5 (약 3.7) |
| HANYEONGHOE | K1 | Bloom | 2–8.5 | 5.25 (구름 5개, 꽃 1.0) |
| | K2 | CloudSize | 0.8–2.6 | 1.7 (구름 크기와 줄기 흔들림 폭) |
| | K3 | Wind | 0.3–5.5 | 2.9 |
| HEOEUNBIN | K1 | Size | 0.6–1.5 | 1.05 (온몸이 보임) |
| | K2 | Backdrop | 0.3–1 | 0.65 (꽉 찬 주황 + 꽃 1송이) |
| | K3 | Speed | 입력 0.2–1.2 → 실제 0.2–2.4 | 0.7 |
| JEONGEUNGYO | K1 | State | 0–0.99 | 0.495 (반쯤 모임) |
| | K2 | Rotation | −0.9–2.1 | 0.6 (rad/s, 약 10초에 한 바퀴) |
| | K3 | DriftSpeed | 0.4–3.6 | 2.0 (작가 값) |
| YUJIN | K1 | FishCount | 4–24 | 14마리 |
| | K2 | CurrentLoop | 0–1 (곡선) | 0.5 (0.625, 물살을 타고 원을 그리며 헤엄) |
| | K3 | SwimSpeed | 6–24 | 15 |

바꾸는 방법은 두 가지다.

- **작가가 범위를 옮긴다**: 원하는 첫 모습이 범위의 가운데에 오게 `PFParams::apply(input, i, &값, 아래, 위, 칸)`의 아래·위를 바꿔 다시 내보낸다. 그 패턴에만 적용된다. 한 칸 크기(마지막 인자)는 이 빌드에서 쓰이지 않는다.
- **시작 위치를 바꾼다**: `firmware/bundles/exhibit/overrides.h`에 `#define PF_EXHIBIT_KNOB_START_3 0.75f`처럼 넣고 다시 빌드한다. 여섯 패턴 모두의 K3에 적용되고, 소리 쪽 시작 CC도 같이 바뀐다(0.75 = CC 95). 한 칸 단위로 반올림된다.

**방향을 뒤집으려고 아래·위를 바꿔 넣으면 안 된다.** `PFParams::apply`는 적힌 순서대로 [아래, 위]로 자르기 때문에, 바꿔 넣으면 값이 늘 첫 번째 인자에 붙어 노브가 안 먹는다. 방향은 작가가 파라미터의 뜻으로 정하거나(예: 코드 안에서 `1 - 값`), Live 매핑에서 Min > Max로 정한다.

#### 교체 순서

1. **파일 위치** — `.h`를 이 레포의 `patterns/<번호>_<이름>/<이름>.h`에 두고(JS도 같은 폴더에), 펌웨어 클론의 `features/exhibit/exhibit_presets.h` include 줄을 그 경로로 맞춘다(`#include "../../../../../patterns/<번호>_<이름>/<이름>.h"`, 클론 밖을 가리킨다). **`presets/`에 두지 않는다.** `check_presets.py`가 웹 쪽 짝이 없는 헤더를 거부한다. 파일 안의 include 줄은 내보낸 그대로 두고, 작가·라이선스 머리말도 남긴다.
2. **namespace가 전체에서 하나뿐인지** — `grep -rn "namespace 이름" firmware/patternflow` 결과가 그 파일 하나여야 한다. `UntitledPattern` 같은 기본 이름이면 그 파일에서만 `ExhibitKimMinji`처럼 바꾼다.
3. **메모리** — 1 KB 이상인 `static` 배열(예: `static float trail[128 * 64]` = 32 KB)은 포인터로 바꾸고 `setup()`에서 `PFMem`으로 한 번 할당한다. `update()`/`draw()`는 포인터가 null이면 바로 돌아간다. `static const` 표는 플래시에 있어서 괜찮다. 6개 패턴의 `setup()`은 부팅 때 전부 한 번씩 돌기 때문에, 화면에 없는 패턴의 버퍼도 내내 메모리를 차지한다.
   ```cpp
   static float* trail = nullptr;
   void setup() { if (!trail) trail = PFMem::allocFloats(PANEL_RES_W * PANEL_RES_H); }
   ```
   - 구조체 배열은 `static Fish* fishes` + `(Fish*)PFMem::alloc(sizeof(Fish) * 24)`, 2차원 표 `static float lut[4][256]`은 `static float (*lut)[256]`으로 바꾸면 파일 안의 `lut[i][j]`를 하나도 안 고쳐도 된다.
   - **바꾸기 전에 `sizeof(배열)`을 찾는다.** 포인터가 되면 4가 되어 `memset(buf, 0, sizeof(buf))`가 4바이트만 지우는데도 컴파일은 된다(HEOEUNBIN에 있었다).
   - PFMem이 파일에 include돼 있지 않으면 `#include "src/core_mem.h"`를 더한다.
4. **목록 줄 바꾸기** — `exhibit_presets.h`의 `PF_EXHIBIT_LIST`에서 해당 줄을 `X(ExhibitKimMinji, "KIM MINJI", "Tidal Memory")`처럼 바꾸고, 위에 `#include "patterns/<artist>.h"`를 추가한다.
   - **자막은 ASCII만**(한글 등은 `?`로 나온다).
   - 세로 패널은 한 줄에 10글자. 제목은 띄어쓰기에서 최대 3줄, 작가는 최대 2줄로 접힌다. **10글자보다 긴 단어 하나**가 있으면 그 덩어리 전체가 작은 글꼴로 바뀌고 단어 중간에서 끊긴다. 작가 이름과 제목 단어는 10글자 이하가 좋다.
   - 제목을 `""`로 두면 패턴의 `NAME`을 쓴다. 작가를 `""`로 두면 작가 줄이 없다.
5. **대기 복귀 때 되돌릴 노브 변수** — 같은 파일의 `PF_EXHIBIT_REST_VARS`에서 바꾸기 전 패턴의 `V(...)`를 새 패턴 것으로 바꾼다. `update()`가 `PFParams::apply` / `applyInt` / `applyUnit`에 넘기는 변수들이다(보통 파일 위쪽 전역 4개). 예: `V(ExhibitKimMinji::knob1) V(ExhibitKimMinji::knob2) ...`. 구조체 필드도 되고, 노브를 `knobVal[i]`로 도는 패턴은 배열 원소로 하나씩 넣는다(`V(Ns::knobVal[0])` … `[3]`). 버튼으로 켜고 끄는 상태(bool, int)도 다음 관람객이 물려받지 않게 하려면 넣는다(HEOEUNBIN의 `dir`, `rider`). 시간 누적 변수(`timeAcc`, `t`)는 넣지 않는다. 함수 안에 선언된 변수는 넣을 수 없다. (K1–K3 변수 자체는 대기 복귀 때 노브 위치가 가운데로 돌아가면서 다음 프레임에 어차피 덮어써진다. 목록이 실제로 되돌리는 것은 버튼 상태 같은 나머지다. 테스트가 요구하니 K1–K3 변수도 목록에 둔다.)
6. **작가에게 부탁할 것** — K1–K3 어느 끝에서도 움직임이 멈추거나 화면이 꺼지지 않게(예: 속도 범위가 0에서 시작하지 않게). 대기 복귀 전까지는 다음 관람객이 앞사람이 둔 상태를 그대로 받는다. **K1–K3는 코드의 시작값이 아니라 범위의 가운데로 연다**(위 "노브 위치로 여는 모습"). 보여 주고 싶은 첫 모습이 범위의 가운데에 오게 아래·위를 잡아 달라고 한다. 방향을 바꾸려고 아래·위를 뒤바꿔 넣지 않는다(노브가 안 먹는다). 패턴이 `input.knobDeltas`나 `input.knobs`를 직접 읽으면 노브 위치를 받지 못한다. `knobDeltas`에는 코어가 위치의 변화로 만든 클릭(한 칸에 약 1.5, 누른 다음 프레임에 한꺼번에 튐)이, `knobs`에는 엔코더의 날 값이 온다. 어느 쪽이든 그림이 CC 24–26과 어긋나니 `PFParams::apply`로 바꿔 달라고 한다(8단계 테스트가 실패로 알려 준다). **K4 값은 패턴에 전달되지 않는다**(K4는 패턴 전환). 패턴의 네 번째 파라미터는 코드의 시작값에 머무니, 보여 주고 싶은 값을 시작값으로 넣어 달라고 한다.
7. **바꾸기 전 패턴의 include 지우기** — 더 이상 어느 줄도 안 쓰는 `#include`를 지운다.
8. **빌드와 점검** — 4.1의 빌드, `check_boundaries.py`, 그리고 이 폴더에서 `python tools/check_exhibit_feature.py`. 이 테스트는 `PFParams::apply*` 변수가 `PF_EXHIBIT_REST_VARS`에서 빠지면 실패하고, 자막 6개가 패널에 어떻게 놓이는지 출력한다(작은 글꼴 줄에는 `(small)`이 붙는다). 자막이 몇 줄인지, 작은 글꼴로 떨어졌는지 여기서 본다. 새 패턴 파일을 실제로 컴파일해 노브 위치로 K1–K3 변수가 맞게 움직이는지 보고, 가운데에서 무엇으로 여는지 표("노브 위치로 여는 모습")를 출력한다. 패턴이 `input.knobDeltas`/`input.knobs`를 직접 읽으면 실패한다.
9. **굽고 실기 확인** — 4.2 무선 갱신이면 된다.
   - 시리얼: `[EXHIBIT] 6 patterns at 1..6, default 1 (BAKWAN), captions off`
   - `/api/status`: `exhibit.rangeOk: true`, `exhibit.restVars`가 새 목록 개수(지금 42)와 같음, `heapInternal`/`heapLargest`가 바꾸기 전보다 크게 줄지 않았는지
   - `/status`의 Frame rate: 여섯 패턴을 K4로 하나씩 켜 보고 각각 45 fps 근처인지. 느린 패턴은 작가와 상의한다.
   - 자막이 LED에서 읽히는지, 노브 끝에서 멈추거나 꺼지지 않는지
10. **Live 쪽** — 씬 이름을 새 작가·제목으로 바꾼다. 패턴 개수를 바꿨다면 씬 개수도 맞춘다.

자막 켜고 끄기는 빌드 없이 된다. FlowLocal 상태 페이지 Panel 칸의 **Captions off** / **Captions on** 링크, 또는 폰으로 `http://<패널>/api/exhibit?caption=0`(끔), `?caption=1`(켬), `?caption=default`(빌드 기본값 = 켬). 선택은 패널에 저장돼 재부팅·무선 갱신 뒤에도 남고, `erase-flash`로만 지워진다. 그 밖의 조정값(대기 시간 `PF_EXHIBIT_IDLE_RETURN_MS` 180000, K4 칸 수 `PF_EXHIBIT_DETENTS_PER_STEP` 3, 자막 시간 `PF_EXHIBIT_CAPTION_MS` 2500, 자막 방향 `PF_EXHIBIT_CAPTION_ROTATION` 1=세로, 기본 패턴 `PF_EXHIBIT_DEFAULT_SLOT` 0, K1–K3 끝에서 끝까지 칸 수 `PF_EXHIBIT_KNOB_DETENTS` 32, K1–K3 시작 위치 `PF_EXHIBIT_KNOB_START_1`/`_2`/`_3` 0.5f)은 `firmware/bundles/exhibit/overrides.h`에 `#define`하고 다시 빌드한다. 기본 패턴을 바꾸면 Live 장치의 Start PC도 같이 바꾼다. 칸 수를 바꾸면 그림과 소리가 같이 바뀐다(둘 다 같은 위치를 쓴다). `PF_MIDI_OUT_LANE_ABSOLUTE 1`(같은 파일)은 K1–K3 CC를 노브 위치로 보내게 하는 줄이라 지우지 않는다. 같은 파일의 `PF_WIFI_BUILTIN_ONLY`, `PF_WIFI_TX_POWER`, `PF_MIDI_SESSION_LOCKED`와 초대 시각 줄(`PF_MIDI_FIRST_INVITE_DELAY_MS`, `PF_MIDI_INVITE_ATTEMPTS`, `PF_MIDI_REINVITE_AFTER_END_MS`, `PF_MIDI_REINVITE_ON_REJOIN`, `PF_MIDI_WIFI_HEAL_MS`)도 지우지 않는다(3장).

---

## 6. 에이블톤

자세한 절차는 **[`ableton/README.md`](ableton/README.md)**. 반드시 해야 하는 것만 추리면:

1. **장치 만들기** — `pf_exhibit_scenes.js`를 둘 폴더에 복사 → Live에서 Max MIDI Effect를 올리고 편집기를 연다 → **붙여넣기 전에 먼저** 그 폴더에 `PF Exhibit Scenes.amxd`로 저장 → `.maxpat` 내용을 복사해 기본 내용을 **지우고** 붙여넣기 → Open in Presentation → 저장 → Freeze 후 다시 저장. 상태 줄이 `ready - scenes 0-6`이면 된 것이다.
2. **Live 설정 → Link, Tempo & MIDI**
   - In `Patternflow Exhibit`: **Track ON, Remote ON**
   - Out `Patternflow Exhibit`: **Track ON, Remote OFF**. Track은 장치의 재동기화 요청이 FlowLocal로 나가는 길이다(끄면 컨트롤 트랙의 MIDI To 목록에 이 포트가 안 보인다). Remote를 켜면 매핑된 파라미터 값이 CC로 되돌아 나간다.
   - 두 포트 모두 **Sync OFF**
   - Takeover Mode: **None**. 패널의 CC 24–26은 언제나 노브 위치 그 자체라, None이어야 같은 노브 위치가 늘 같은 소리가 된다. 이유와 Value Scaling과의 차이는 `ableton/README.md` 2장
3. **컨트롤 트랙** — MIDI 트랙 하나. MIDI From `Patternflow Exhibit` / **All Channels**(Ch. 1로 두면 채널 16의 CC 118 미러가 걸러진다), MIDI To `Patternflow Exhibit` / **Ch. 16**, Monitor **In**. 장치는 `PF Exhibit Scenes` **하나만**, 악기·클립 없음(장치 뒤에 악기를 두면 요청이 FlowLocal에 닿지 않는다). 다른 악기 트랙은 MIDI From **No Input**. K1–K3 누름 소리를 쓰려면 **별도 트랙**을 MIDI From `Patternflow Exhibit` / **Ch. 1**, Monitor In으로 두고 거기에 악기를 올린다. HEOEUNBIN의 발소리(노트 36/38)를 쓰려면 또 하나를 **Ch. 10**, Monitor In으로 두고 드럼 랙의 C1·D1에 샘플을 넣는다(`ableton/README.md` 4장 "발소리 노트").
4. **씬** — 맨 위부터 씬 0 = Origin(조용하거나 빈 씬), 씬 1 = 작가 1(기본), 씬 2–6 = 작가 2–6. 이름을 붙인다(순서와 이름은 5장 표: BAKWAN, CHOEGYEONGEUN, HANYEONGHOE, HEOEUNBIN, JEONGEUNGYO, YUJIN). Global Launch Quantization은 **None**(K4를 돌리는 즉시 씬이 바뀜, 2026-09-19 사용자 결정. 너무 뚝 끊기면 1/16이나 1/8, `ableton/README.md` 3장).
5. **장치 설정** — 전시 세트에서 **Autostart ON**으로 저장(열고 1.5초 뒤 Start PC의 씬을 틀고 트랜스포트를 켠다). Start PC = 1, Offset = 0, Enable ON, **Thru OFF**, **Alt CC OFF**(아래 확인에서 Live가 PC를 안 넘겨주면 켠다).
   - **재동기화는 저절로 된다.** 세트가 열리면 장치가 0.5초 뒤 FlowLocal에 재동기화 요청을 보낸다. FlowLocal이 마지막 PC(와 CC 118, CC 24–26 마지막 값)를 다시 보내면 장치가 그 씬을 틀고, 매핑된 파라미터는 패널의 노브 위치로 간다(Takeover None일 때). Autostart는 그 씬을 유지한다. 그래서 Live만 재시작해도 패널의 패턴과 노브 위치에서 시작한다. FlowLocal이 켜진 뒤 패널이 PC를 보낸 적이 없으면 답이 없고, Autostart가 Start PC의 씬을 튼다.
   - 노트북 앞에서 손으로 할 때는 장치의 **Resync** 버튼. 같은 요청을 한 번 보낸다.
   - **대체 수단**: 노트북 모니터의 **소리 다시 맞추기**(같은 요청을 Live를 거치지 않고 USB로 보낸다. 스태프 안내문 `STAFF_CARD.md`가 쓰는 것이 이 버튼이다), 또는 운영자 폰으로 FlowLocal 상태 페이지의 **Send current pattern to Live**. 둘 다 Live의 요청이 FlowLocal에 닿지 않는 경우(컨트롤 트랙 MIDI To, Out 포트 Track이 틀렸을 때)에도 된다.
6. **K1–K3 매핑(Ctrl-M)** — CC 24/25/26. 값은 노브 위치 그대로다(1장 "노브 위치"). **매핑 범위의 가운데(CC 64)가 시작 소리**가 되게 잡는다. 켤 때(세션이 붙으면 PC 바로 뒤)와 3분 대기 복귀 때(가운데가 아니던 노브만) 패널이 CC 64를 보낸다. Takeover None(위 2번)이라야 이 CC가 파라미터를 그 자리로 옮긴다. 씬 템포나 클립 자동화가 매핑한 파라미터를 움직이지 않게 한다(K1을 템포에 매핑했다면 씬 템포를 쓰지 않는다). 그런 것이 파라미터를 옮겨 놓으면 같은 노브 위치에서 다른 소리가 난다. **Min/Max 범위는 양 끝 어디서나 들을 만하게** 잡는다. 대기 복귀 전까지는 앞사람이 둔 끝(예: 필터를 끝까지 닫은 상태)이 그대로 남는다. 방향은 Min > Max로 뒤집는다.
7. **키오스크** — 자동 로그인, 시작 프로그램에 Live 실행 파일 + `.als` 바로가기, 절전·최대 절전·덮개 동작·USB 선택적 절전 끄기, USB 루트 허브 절전 끄기, Windows 업데이트 일시 중지, 항상 충전기. **전원을 뽑은 상태에서 시작하는 리허설**을 한 번 하고, Live 강제 종료 후 복구 대화상자에서 무엇을 누르는지 스태프 안내문에 적는다.

FlowLocal이 마지막 PC를 다시 보내는 경우는 네 가지다: 그 PC를 USB로 못 썼을 때, USB가 다시 연결되거나 노트북이 깨어날 때, 재동기화 요청이 올 때(Live 장치, 또는 모니터의 **소리 다시 맞추기**), 상태 페이지 버튼을 누를 때. 어느 경우든 CC 118 미러가 뒤따르고, 그 뒤에 패널이 마지막으로 보낸 CC 24/25/26이 온다(패널이 보낸 적 있는 노브만).

하드웨어 없이 먼저 확인할 것: loopMIDI 포트 `PF Test`를 만들고 `python tools/send_test.py --port "PF Test" note 60`, `... pc 1`을 보낸 뒤 장치의 `dump`를 누른다. Max Console에 `bytes=8 programs=1`이면 Live가 PC를 장치까지 넘겨주는 것이다. `bytes=6 programs=0`이면 넘겨주지 않는 것이다. 그때는 장치의 **Alt CC 스위치를 켜고 세트를 저장한다. 그것만 하면 된다.** 번호 칸은 기본값 118 그대로 두고, 패널·FlowLocal 펌웨어는 고치지 않는다. Alt CC는 FlowLocal이 PC마다 붙이는 CC 118을 채널과 상관없이 읽는다(Live가 트랙 입력을 채널 1로 합쳐 넘긴다는 Cycling '74 포럼 보고가 있어서다. 실기 확인 전). `... altcc 1`로 씬 1이 발사되는지 본다(`ableton/README.md` 9장 1번). 재동기화 요청이 Live 밖으로 나가는지는 `ableton/README.md` 5장 6번으로 본다.

---

## 7. 매일 운영

### 전시 전 점검표

설치 때 한 번, 그리고 패널을 다시 굽거나 다른 패널로 바꿨을 때마다 한다. 패널 쪽 점검은 **여기에만** 적는다(`ableton/README.md`는 이곳을 가리킨다).

**패턴플로우 모니터** (노트북, [`tools/monitor/README.md`](tools/monitor/README.md))

- [ ] "처음 한 번"을 해 둔다: `python -m pip install python-rtmidi`, `tools/monitor/install_startup.bat`(시작 프로그램과 바탕화면에 **패턴플로우 모니터** 바로가기를 만든다. 없어도 모니터는 레포 맨 위 `.bat`로 열리지만, 스태프 안내문과 모니터 화면이 바탕화면 바로가기를 가리키므로 전시 노트북에는 해 둔다), `tools/monitor/config.json`에 연락처·패널 전원 켜는 곳·Live 복구 창의 버튼(`STAFF_CARD.md` 빈칸과 같은 값).
- [ ] 노트북을 다시 켰을 때 모니터 창이 저절로 열린다. 레포 맨 위의 `패턴플로우 모니터 열기.bat`도 같은 일을 한다.
- [ ] 맨 위가 초록 **모두 정상이에요**, 신호가 가는 길 일곱 칸(패널 → Wi-Fi → FlowLocal → USB → 노트북 → Ableton Live → 스피커)에 빨간 칸이 없다.
- [ ] **운영자용 자세히 보기**: FlowLocal 펌웨어 `flx-mon2`(재시작 이유 `rsn`, 켜진 횟수 `bc`가 보인다), 채널(`chm=auto`), 패널 신호 세기 `prs`가 −75 dBm보다 강하다. 노트북 전원 설정에 전시에 맞지 않는 값이 없다(있으면 레포 맨 위의 `노트북 전시용 전원 설정.bat`을 한 번 실행한다). **Live 자동 재시작** 스위치가 켜져 있다(Live가 꺼지면 8초 뒤 전시 세트를 다시 연다. 15분에 3번까지). Live 오디오가 FlexASIO다(2장 "노트북과 소리").
- [ ] 리허설 때 고치기 버튼을 하나씩 눌러 **됐어요**가 나오는지 본다. 2026-09-25 실측: **패널 연결 다시 잡기** MIDI 1.0초, **패널 다시 켜기** 4.7초, **FlowLocal 다시 켜기** FlowLocal이 켜지고 약 14초.

운영자 폰을 `PatternflowLocal`에 붙이고 `http://192.168.66.1/`의 **Panel** 칸 링크로 패널 페이지를 연다(3장 "폰으로 패널 페이지 열기"). 끝나면 폰의 Wi-Fi를 끈다.

**패널 MIDI 페이지** — Panel 칸의 **MIDI** (`http://<패널>/midi`). 아무것도 누르지 않는다.

- [ ] 맨 위가 **Connected to FlowLocal**이다.
- [ ] **Computer to invite**가 `192.168.66.1`로 고정돼 있고(Save, Use this computer, Clear 버튼 없음) 아래에 `This firmware always calls the computer above and keeps MIDI on` 안내가 있다. 안내가 없으면 잠금이 들어간 2026-09-25 이후 이미지가 아니다. 4장대로 새 이미지로 굽는다.
- **Knob value / Relative steps**와 **Sensitivity**는 확인하지 않아도 된다. 이 빌드의 K1–K3는 노브 위치를 그대로 보내고(1장 "노브 위치") 이 두 설정을 쓰지 않는다. 누르지도 않는다(누르는 순간 저장된다).
- [ ] Console 주소 뒤에 `api/status`(`http://<패널>/api/status`)를 열어 `"midi"` 안에 **`"outLaneAbs":true`**와 **`"locked":true`**가 있는지 본다. 없으면 전시 이미지가 아니다(4.3).

**패널 노브 페이지** — Panel 칸의 **Knobs** (`http://<패널>/knobs`)

- [ ] 네 노브 모두 **Direction = Normal**(Reversed 아님), **Edges per click = 4 (most encoders)**. 둘 다 기본값이다.
- [ ] 노브마다 시계 방향으로 한 칸 돌리면 **Position이 1 오른다**.
- [ ] 기본값이 아닌 값이 저장돼 있으면 기본값으로 되돌리고 **Save**한 뒤 다시 돌려 본다(저장해도 노브 위치는 움직이지 않는다). 기본값에서 한 칸이 1이 아니면(내려가거나, 2씩 오르거나, 두 칸에 1) 그 노브의 엔코더가 다른 것이다. 그때만 페이지 아래 안내대로 그 노브를 맞추고, 무엇을 바꿨는지 적어 둔다.

**패널 Wi-Fi 페이지** — Console 주소 뒤에 `wifi` (`http://<패널>/wifi`)

- [ ] 맨 위 **Network** 칸에 `This firmware joins only PatternflowLocal, compiled in.`이 있다. 이 칸이 없으면 예전 이미지다(저장된 네트워크가 컴파일된 값을 막는다. 3장). 4장대로 새 이미지로 굽는다.
- 저장된 네트워크는 `not used`로 나오고 쓰이지 않는다. 지워도 되고(**forget**) 둬도 된다. **Add a network**와 **Boot network**는 없다.

**자막**

- [ ] 켤지 끌지 정한다(빌드 기본값은 끔. 2026-09-25에 정했다: 작가 영문 이름을 확인하지 않아 이름을 띄우지 않는다). Panel 칸의 **Captions off** 또는 **Captions on**을 누르면 답의 `"caption"`이 `false` / `true`로 바뀐다. 패널에 저장돼 재부팅·무선 갱신 뒤에도 남는다. K4를 3칸 돌려 확인한다.

**밝기**

- [ ] 전시장 조명을 실제 상태로 켠 뒤, Panel 칸의 **Console**(패널 콘솔 홈)에서 **Brightness** 슬라이더로 맞춘다. 손을 떼고 잠시 두면 패널에 저장된다. 밝기를 올리면 전원(J4) 소모도 는다.

**그 밖에 (다른 장에 절차가 있는 것)**

- [ ] FlowLocal 상태 페이지: `Everything connected`, `Program mirror to Live`가 `off`가 아님, `Panel: 192.168.66.x`, `Channel`이 `auto` (4.6)
- [ ] Live: 6장 목록, 그리고 Live가 PC를 장치까지 넘겨주는지 확인한 결과대로 **Alt CC** 스위치(6장 끝)
- [ ] 노트북: `ableton/README.md` 7장 키오스크 체크리스트(모니터가 그중 절전·USB 선택적 절전을 60초마다 다시 본다). **알려진 Wi-Fi 네트워크에 `PatternflowLocal`이 없다**(3장)
- [ ] FlowLocal 보드에 `FlowLocal` 라벨, 노트북의 그 USB 자리에 테이프 표시(2장). `STAFF_CARD.md`는 이 표시를 기준으로 쓰였다
- [ ] `STAFF_CARD.md`의 빈칸(연락처, 점검용 폰, 패널 전원 켜는 곳, 그림 바꾸는 노브 = K4의 위치, Live 복구 창에서 누를 버튼)을 채워 인쇄해 둔다. 같은 값을 `tools/monitor/config.json`에도 적는다. 점검용 폰에는 `PatternflowLocal` 비밀번호를 저장해 두고, 카드에는 적지 않는다

### 여는 순서 (노트북 → Live → 패널)

- [ ] 노트북 충전기 연결 확인, 스피커 전원·볼륨 확인
- [ ] FlowLocal이 노트북의 **표시한 USB 포트**에, DevKit의 **USB** 잭으로 꽂혀 있다
- [ ] 노트북 켜기 → 자동 로그인 → Live 세트가 열릴 때까지 기다린다. 패턴플로우 모니터 창도 저절로 열린다(안 열리면 바탕화면의 **패턴플로우 모니터**나 레포 맨 위의 `패턴플로우 모니터 열기.bat`)
- [ ] Live 컨트롤 트랙 장치의 아랫줄이 잠깐 `ready - scenes 0-6`이었다가 `fired 시:분:초`로 바뀌고, 윗줄은 `autostart PC 1 -> 1: ...`, 소리가 난다
- [ ] 패널 전원 켜기 (J4 쪽 전원). 1번 패턴이 뜬다(자막 없음)
- [ ] 모니터가 초록 **모두 정상이에요**가 되기를 기다린다. FlowLocal은 켜지고 12초 뒤 핫스팟을 열고, 패널은 핫스팟에 붙고 몇 초 안에 이어진다(2026-09-25 실측: FlowLocal을 켜고 약 14.7초, 패널을 다시 켜고 4.7초). 1분이 지나도 노랑·빨강이면 모니터의 **지금 할 일**을 따른다
- [ ] K4를 3칸 돌린다 → 패턴이 바뀌고, Live 씬이 바뀌고, 모니터의 지금 그림(번호)이 바뀐다
- [ ] K1, K2, K3를 돌린다 → 패턴과 소리가 같이 변하고, 모니터의 노브 카드가 따라오고 **신호 도착** 막대가 고르게 이어진다
- [ ] (운영자 폰으로 볼 때만) 폰을 `PatternflowLocal`에 붙여 `http://192.168.66.1/`의 `Everything connected`와 밝기(Panel 칸의 **Console** 링크 → Brightness)를 본다. 끝나면 폰 Wi-Fi를 끈다(핫스팟 자리 비우기)

### 닫는 순서

- [ ] 패널 전원 끄기
- [ ] Live 종료 → 노트북 종료 (FlowLocal은 노트북 USB 전원이 끊기면 같이 꺼진다)
- [ ] 패널 전원이 보조배터리라면 충전

### 문제 생겼을 때

**먼저 노트북의 패턴플로우 모니터를 본다.** 맨 위 제목과 신호가 가는 길의 빨간 칸이 어느 구간이 끊겼는지 알려 주고, **지금 할 일**이 나온다. 고칠 때는 큰 버튼 **한 번에 다시 연결하기** 하나만 누른다: FlowLocal 확인 → FlowLocal 다시 켜기(채널 다시 고름) → 패널 연결(안 붙으면 패널 원격 재시작) → Live 확인(꺼져 있으면 세트를 연다, 켜져 있으면 소리 다시 맞추기) → 확인을 순서대로 하고, 손으로 할 일(RST, 다시 꽂기, 패널 전원)은 하나씩만 알려 준 뒤 그것을 감지하면 이어 간다. 개별 버튼(소리 다시 맞추기, 패널 연결 다시 잡기, 패널 다시 켜기, FlowLocal 다시 켜기, 무선 채널 바꾸기)은 운영자용 칸에 있다. 판정 규칙과 버튼은 [`tools/monitor/README.md`](tools/monitor/README.md). 모니터가 꺼져 있어도 전시(소리)는 돈다. 아래 표는 모니터가 가리키는 원인과, 모니터가 모르는 것(Live 설정, 패널 화면)을 적은 것이다. 운영자 폰으로 `http://192.168.66.1/`을 보는 것은 모니터로 모자랄 때다.

| 증상 | 원인 | 해결 |
|---|---|---|
| 모니터 창이 없음, "모니터 프로그램이 꺼졌어요" | 창을 닫았거나 모니터가 켜지지 않음 | 바탕화면의 **패턴플로우 모니터** 또는 레포 맨 위 `패턴플로우 모니터 열기.bat`를 두 번 누른다. 그 밖은 `tools/monitor/README.md` "모니터 자체가 이상할 때" |
| 소리가 전혀 안 남, 장치 아랫줄은 `ready …` 또는 `fired 시:분:초` | 트랜스포트 정지, 오디오 출력 장치가 바뀜, 스피커 | Live 재생 확인, Live 설정 → Audio 출력 장치, 스피커 전원·볼륨 |
| 소리가 안 나고 장치가 `waiting for Live`에 머묾 | 장치가 js를 못 찾음 | Max Console 확인, `.amxd` 옆에 js 두거나 Freeze (`ableton/README.md` 1장) |
| Live가 안 열려 있음 / 복구 대화상자 | 크래시 뒤 재시작 | 리허설 때 적어 둔 버튼(`STAFF_CARD.md` 빈칸)을 누른다. 안 되면 노트북을 재시작하고 Live가 다시 열려 장치 아랫줄이 `ready …` 또는 `fired 시:분:초`가 될 때까지 기다린다 |
| K4를 돌리면 패턴은 바뀌는데 씬이 안 바뀜 — 모니터가 빨강 "패널이 핫스팟에 없어요", "패널이 핫스팟에는 있는데 신호 연결이 안 돼요", "패널 신호가 끊겼어요"(상태 페이지 `Panel not connected`) | 패널이 FlowLocal에 세션을 못 붙임: 패널 전원이 없거나 켠 지 얼마 안 됨, 핫스팟이 가득 참, 초대 답이 무선에서 사라짐. 전시 이미지는 저장된 다른 Wi-Fi나 `/midi` 호스트 때문에 안 붙는 일이 없다(3장) | 먼저 패널에 그림이 보이는지(전원). 그다음 모니터의 **한 번에 다시 연결하기**. 패널이 핫스팟에 있는데 연결이 안 되면 모니터가 패널을 원격으로 다시 켜고, 핫스팟에 없으면 패널 전원을 끄고 켜라고 알려 준다. 핫스팟이 꽉 찼으면 폰 Wi-Fi 끄기, 노트북이 핫스팟에 붙어 있으면 떼기(3장) |
| 같은 경우, 모니터가 빨강 "작은 보드(FlowLocal)가 노트북에 안 보여요"(상태 페이지 `Laptop (USB jack)` = not connected) | FlowLocal이 COM 잭에 꽂힘, 다른 USB 포트, 노트북 USB 절전, 케이블 | DevKit **USB** 잭을 뽑았다가 5초 뒤 표시한 포트에 다시 꽂기. Windows가 "알 수 없는 USB 장치"로 잡으면 한 번 더(2장). USB 절전 설정(`ableton/README.md` 7장) |
| 모니터가 빨강 "작은 보드(FlowLocal)가 대답하지 않아요", 또는 노랑 "FlowLocal 상태를 알 수 없어요 — 새 FlowLocal 펌웨어가 필요해요" | 빨강: FlowLocal이 멈춤. 노랑: 모니터에 답하지 못하는 옛 FlowLocal 펌웨어(`flx-mon1` 전) | 빨강이면 **한 번에 다시 연결하기**: FlowLocal이 스스로 USB를 고치기를 25초 기다리고, 안 되면 RST(또는 뽑았다 5초 뒤 같은 포트에 꽂기)를 하나만 알려 준다. 노랑이면 4.5로 새 이미지를 굽는다(그동안 모니터는 패널 신호만 보여 준다) |
| 모니터가 노랑 "노트북 절전 설정을 고쳐야 해요" | 전원 연결 때 절전·최대 절전·USB 선택적 절전이 켜져 있음(이 노트북은 2026-09-25에 USB 선택적 절전 = 사용) | 레포 맨 위의 `노트북 전시용 전원 설정.bat`을 한 번 실행한다(관리자 권한 필요 없음. 끝에 각 설정의 AC/DC 값이 0x00000000인지 보여 준다). 모니터가 1분 안에 초록으로 바꾼다 |
| 모니터가 노랑 "무선 상태가 나빠요" | 패널 신호 세기·흔들림·늦음·빠진 패킷·몰림 중 하나가 기준을 넘음(`tools/monitor/README.md` "기준값") | 폰 핫스팟과 쓰지 않는 폰 Wi-Fi를 끈다. 그래도 계속되면 화면에 나오는 **무선 채널 바꾸기**로 다음 채널(1·6·11)로 옮긴다(FlowLocal `flx-mon3`부터. 약 15초 동안 소리가 패널을 따라가지 않는다). 옛 펌웨어면 **한 번에 다시 연결하기**(FlowLocal이 켜지며 채널을 다시 고른다). 계속되면 FlowLocal을 USB 연장선으로 노트북에서 떼어 높은 곳에 둔다(같은 테이프 포트에 꽂는다) |
| 모니터의 지금 그림은 바뀌는데 Live 씬이 그대로(상태 페이지 Current pattern도 같다) | Live 쪽 설정 | Live In 포트 Track ON, 컨트롤 트랙 MIDI From(All Channels) / Monitor In, 장치 Enable. 상태 줄에 `missing`이면 씬 개수·Offset. 편집기 `dump`가 `programs=0`인데 `altccIn`이 늘면 장치 **Alt CC** 켜기(`ableton/README.md` 8장) |
| 씬이 하나씩 밀려서 바뀜 | 씬 번호를 1부터 셈 | 맨 위 씬이 0번(Origin). 씬 순서를 1장 표대로 |
| Live만 재시작한 뒤 소리와 패턴이 다름 | 장치의 재동기화 요청에 답이 없어 Autostart가 씬 1을 틀었다. 요청이 FlowLocal에 안 닿았거나(컨트롤 트랙 MIDI To, Live Out 포트 Track), FlowLocal이 켜진 뒤 패널이 PC를 보낸 적이 없다(상태 페이지 Current pattern이 `none yet`, 모니터 운영자용 자세히 보기의 `lp`가 `-1`) | 노트북 앞이면 장치의 **Resync** 버튼. 그래도 그대로면(요청이 FlowLocal에 안 닿는 경우) 모니터의 **소리 다시 맞추기**(Live를 거치지 않는다), 그다음 운영자 폰으로 상태 페이지 **Send current pattern to Live**. `none yet`이면 K4 한 번. 원인 확인은 `ableton/README.md` 8장 |
| K1–K3를 돌려도 소리가 안 변함 | 매핑 없음, In Remote OFF, Takeover가 Pick-Up | Ctrl-M 매핑 확인, In Remote ON, Takeover None. (패널 `/midi`의 Relative steps·감도는 이 빌드에서 원인이 아니다) |
| 사람이 떠난 뒤 소리가 이상한 상태로 남음(필터 닫힘 등) | 3분 대기 복귀 전까지는 앞사람이 둔 노브 위치가 그대로다 | 3분 뒤 패널이 노브를 가운데로 돌리고 CC 64를 보낸다. 그 전에도 들을 만하게 매핑 Min/Max를 좁힌다 |
| 그림과 소리가 따로 논다(노브 끝에서 한쪽만 멈춤, K4 뒤 소리가 튐 등) | 전시 이미지가 아님(`/api/status`의 `midi.outLaneAbs`가 없음), 또는 콘솔 홈의 노브 슬라이더를 움직임(슬라이더는 그림만 움직인다), 또는 Live 쪽 매핑·Takeover | `/api/status` 확인(4.3). 콘솔 슬라이더를 썼다면 그 노브를 한 칸 돌리면 다시 맞는다(슬라이더를 5초 안에 움직였다면 그 5초 뒤). Live는 Takeover None(Value Scaling이면 Live 쪽에서 바뀐 파라미터가 노브가 끝에 닿을 때까지 어긋난 채 남는다), 씬 템포·클립 자동화가 그 파라미터를 움직이지 않는지 |
| K4를 돌려도 패턴이 안 바뀜 | 3칸마다 한 번 바뀐다. 깨어난 직후 1초는 K4를 무시한다 | 3칸 이상 돌린다. 계속 안 되면 `/api/status`의 `exhibit.rangeOk` 확인(false면 빌드 문제) |
| 패널에 `UPDATE` / `FAILED` / `K4 = EXIT` 카드 | 콘솔에서 펌웨어 올리기가 실패함 | K4를 한 번 누른다. 또는 전원 재시작 |
| 패널이 검음, 노브에 반응 없음 | 콘솔에서 패널 끄기(sleep)를 눌렀을 수 있음, 또는 전원 | 아무 버튼이나 누른다. 안 되면 J4 전원 확인 후 전원 재시작 |
| 패널 화면이 멈춤 / 콘솔이 안 열림 | 원인 미상 (실기 기록 없음) | 패널 전원 재시작. 가능하면 전에 `/api/status`의 `heapInternal`, `uptime`을 적어 둔다 |
| 핫스팟 `PatternflowLocal`이 안 보임 — 모니터 "작은 보드(FlowLocal)가 켜지는 중이에요" 또는 "FlowLocal 핫스팟(PatternflowLocal)이 꺼져 있어요" | FlowLocal이 켜진 지 12초가 안 됨(일부러 전파를 내지 않는다), FlowLocal 전원 없음(노트북 꺼짐·절전으로 USB 전원 끊김), FlowLocal 펌웨어가 전시 빌드가 아님 | 켜지는 중이면 기다린다. 꺼져 있으면 **한 번에 다시 연결하기**, 안 되면 USB 뽑았다 5초 뒤 같은 포트에 꽂기, 노트북 켜기. 필요하면 4.5로 다시 굽기 |
| 폰이 핫스팟에 안 붙음 | 3대가 이미 붙어 있음(노트북이 붙어 있지 않은지도 본다), 떠난 기기가 아직 자리를 쥐고 있음(30초 조용하면 FlowLocal이 내보낸다) | 다른 폰 Wi-Fi 끄기, 노트북의 `PatternflowLocal` 삭제(3장). 그래도 안 되면 30초쯤 기다렸다 다시. 비밀번호 `YOUR_HOTSPOT_PASSWORD` |
| FlowLocal을 다시 켠 뒤(모니터 버튼, 상태 페이지 Reboot, 업데이트, USB 뽑기) MIDI 멈춤 | FlowLocal은 켜지고 12초 동안 일부러 핫스팟을 열지 않는다. 핫스팟이 뜨면 패널이 Wi-Fi에 다시 붙고 1초 뒤 초대한다 | 기다린다. FlowLocal이 켜지고 약 15초면 붙는다(2026-09-25 실측 14–14.7초). 1분이 지나도 안 붙으면 모니터 안내대로 **패널 다시 켜기**나 패널 전원 재시작 |
| 패널 전원을 껐다 켠 뒤 MIDI 멈춤 | 패널이 Wi-Fi에 붙고 초대해 세션이 붙을 때까지 걸린다 | 기다린다. 몇 초면 붙고(2026-09-25 실측: 모니터의 패널 다시 켜기 4.7초), 붙으면 패널이 현재 PC와 K1–K3 값을 다시 보낸다 |
| Live 포트 목록에 `Patternflow Exhibit` 없음 / 옛 이름 | COM 잭에 꽂음, 다른 USB 포트, Windows가 옛 이름을 기억, Windows가 "알 수 없는 USB 장치"로 잡음 | USB 잭·표시한 포트 확인. 뽑았다 5초 뒤 같은 포트에 다시 꽂기. 옛 이름이면 장치 관리자에서 장치 제거 후 다시 꽂기 |
| `midi_monitor.py`가 포트를 못 엶 | Live가 포트를 혼자 잡고 있음(옛 MIDI 드라이버) | 이 노트북은 Windows MIDI 서비스가 포트를 여럿이 함께 쓰게 해 주어 Live와 패턴플로우 모니터가 같이 연다(`midi_monitor.py`도 같은 python-rtmidi다). 다른 노트북에서 안 열리면 Live를 끄고 모니터 사용, 끝나면 모니터 닫고 Live 켜기 |
| 노트가 두 번 들림 | 누름 소리 트랙이 둘이거나, 그 트랙이 All Ins + Arm으로도 받음 | 누름 소리 트랙 하나만 MIDI From `Patternflow Exhibit` / Ch. 1 (`ableton/README.md` 4장 "누름 소리") |
| FlowLocal `/api/status`의 `midi.usb_in_discarded`가 계속 늘어남 | 노트북이 요청 말고도 무언가를 보냄: 장치를 만들 때 기본 midiin/midiout을 안 지움, 장치 Thru 켜짐(모니터의 SysEx 답도 되돌아간다), Live Out 포트 Remote·Sync 켜짐. 모니터의 상태 요청과 명령은 여기에 세지 않는다 | FlowLocal이 버리므로 고장은 아니다. `ableton/README.md` 1장 5번, 8장대로 끈다 |
| 자막(작가 이름)이 나온다 / 켜고 싶음 | 빌드 기본값은 끔. 누가 Captions on을 눌러 패널에 켬이 저장됨 | 끄기: 상태 페이지 Panel 칸의 **Captions off**(= `http://<패널>/api/exhibit?caption=0`). 켜기: **Captions on**(`?caption=1`). 저장을 지우고 빌드 기본값(끔)으로: `?caption=default` |
| 콘솔 홈의 ← / → 버튼으로 전시 패턴이 안 넘어감 | 전시 패턴은 숨김 항목으로 등록됨(가운데 이름 표시를 막으려고) | 콘솔의 패턴 **목록**에서 고른다. 콘솔에서 고른 패턴도 3분 뒤 1번으로 돌아간다 |

---

## 8. 아직 실기로 확인 안 된 것

9/17에는 하드웨어 없이 빌드, 이미지 문자열 검사, 호스트 테스트, 코드 읽기만 했다. 9/18부터 두 보드와 Live를 실기에서 돌렸다(끝의 굽기 기록, 실측 기록). 아래에서 **확인됨**은 실기에서 본 것이고, 나머지가 아직 남은 것이다. 앞 단계가 안 되면 뒤는 의미가 없다.

1. **FlowLocal USB-MIDI** — **확인됨**(9/19 데스크탑, 9/25 노트북): Windows 11에서 `Patternflow Exhibit`로 잡히고, Live와 패턴플로우 모니터가 포트를 같이 쓰고, OTG 점퍼를 연 채 노트북 USB 전원으로 돈다. 남은 것: 9/25 첫 연결 때 한 번 나온 "알 수 없는 USB 장치(장치 설명자 요청 실패)"가 다시 나오는지(케이블·포트 의심). 같은 보드에 옛 펌웨어를 쓴 적 있으면 Windows가 옛 이름(VID:PID 303A:1001)을 기억할 수 있다.
2. **Live가 Program Change를 장치까지 넘겨주는지** — **확인됨**(9/19, 끝의 "Live 확인"): 넘겨주므로 Alt CC는 끈다. **Resync** 버튼 → FlowLocal의 답 → 씬 발사도 됐다. 남은 것: `altcc 1`의 상태 줄이 `ch1`인지 `ch16`인지, 장치의 `deferlow` 바이트 순서, 로드 때 상태 줄 초기화, 설정값 저장·복원, "already on" 판정(`playing_slot_index` 등), 그리고 모니터가 도는 동안 Live가 FlowLocal의 SysEx 답을 받아도 씬이 엉뚱하게 바뀌지 않는지(Node 테스트 26으로만 확인. `ableton/README.md` 3장).
3. **패널이 핫스팟에 붙고 FlowLocal을 초대하는지** — **확인됨**(9/19, 9/24, 9/25): 세션이 붙자마자 `PROGRAM 1`, CC 118 미러, CC 24–26 = 64가 노트북에 온다. 9/25에는 예전 규칙(저장된 Wi-Fi가 컴파일된 값을 막음) 때문에 안 붙는 것을 겪었고, 전시 이미지에서 없앴다(3장). 새 이미지로 패널을 다시 켜고 MIDI까지 4.7초, FlowLocal을 켜고 약 14.7초. 남은 것: 최종 이미지 `07b4c70b…`를 구운 뒤 같은 숫자가 나오는지, 잠긴 `/wifi`·`/midi` 페이지가 폰에서 3장대로 보이는지, Improv 거절.
4. **패널 조작** — 길게 누르기 메뉴 4개가 모두 안 열리는지, K4 3칸 느낌, 자막이 LED에서 읽히고 위치가 맞는지(자막 배치는 실제 Adafruit_GFX 글꼴 코드로 호스트에서만 확인했다. 여러 줄이면 아랫줄 배경이 윗줄 맨 아래 픽셀 한 줄을 덮는다), 숨김 등록으로 가운데 이름 표시가 안 뜨는지.
5. **대기 복귀 실시간** — 180초 뒤 1번으로 한 번 돌아가는지, 노브 값 되돌림이 보는 사람에게 튀어 보이는지, 콘솔에서 고른 패턴이 180초 뒤 돌아가는지, Live 파라미터는 그대로 남는지.
6. **복구 시나리오** — **확인됨**(9/25, 모니터 버튼으로 한 번씩): **패널 연결 다시 잡기** 뒤 MIDI 1.0초, **패널 다시 켜기** 뒤 4.7초, **FlowLocal 다시 켜기** 뒤 FlowLocal이 켜지고 약 14초(그중 12초는 일부러 조용히 있는 시간). 남은 것: 패널 전원을 손으로 껐다 켤 때 두 번째 끊김이 없는지, 여러 번 반복해도 같은지, Live만 재시작했을 때 장치의 재동기화 요청으로 패널의 씬에서 시작하는지(FlowLocal `/api/status`의 `midi.resync_requests`가 세트를 열 때마다 1씩 느는지), 노트북 절전·깨어남에서 USB 전원이 유지되는지. FlowLocal의 세션 수선 코드는 AppleMIDI 3.5.0 소스를 읽고 손으로 따라가 본 것뿐이다.
7. **Live가 포트를 닫은 동안 Windows가 USB-MIDI를 읽어서 버리는지** — 그렇다면 그동안 보낸 PC는 사라진다. Live가 다시 열리면 장치의 요청이 되찾아 오고, 아니면 Resync 버튼, 모니터의 **소리 다시 맞추기**, 상태 페이지 버튼, K4로 맞춘다.
8. **핫스팟 3대 제한과 폰 접근** — FlowLocal 상태 페이지의 `Panel: 192.168.66.x`가 패널 주소와 맞는지, 그 링크(Console, MIDI, Knobs, Captions)가 핫스팟 안의 폰에서 패널 페이지를 여는지, 세션이 없을 때 `Panel: unknown` 아래 목록(`ap_stations`)에 패널이 나오는지, 폰에서 `patternflow.local`이 열리는지, 3대가 찼을 때 `Hotspot full`과 모니터의 "핫스팟 자리가 꽉 찼어요", 전원이 빠진 기기가 30초 뒤 정말 빠지는지(`FL_EXHIBIT_AP_INACTIVE_S`).
9. **`/api/exhibit` 자막 켜고 끄기와 저장**, 실패한 업로드의 UPDATE 카드를 K4로 닫기.
10. **작가 패턴을 넣은 뒤** 실행 중 내부 힙 여유(`heapInternal`, `heapLargest`)와 패턴별 fps. 자리표시 상태의 실행 중 기준값은 없다(9/18에 구울 때 재지 않았다).
    - 정적 DRAM: 작가 6명 빌드 145,816 B, 같은 커밋의 자리표시 빌드 143,688 B(+2,128 B), 기본 펌웨어 141,424 B. 받은 파일 그대로였다면 57,656 B가 더 붙었을 것을 3단계 변환으로 PFMem에 옮겼다. 패턴마다 남은 가장 큰 정적 변수는 BAKWAN `neighborDrifters` 640 B, HEOEUNBIN `pts` 456 B, JEONGEUNGYO `nodes` 396 B다. 1 KB 넘는 것은 없다.
    - PSRAM: 여섯 `setup()`이 부팅 때 PFMem에서 모두 합쳐 약 253 KB를 잡는다(HANYEONGHOE 138,792 B, BAKWAN 45,968 B, YUJIN 34,112 B, CHOEGYEONGEUN 24,256 B, HEOEUNBIN 16,384 B, JEONGEUNGYO 0). PSRAM이 잡히지 않은 보드면 이것이 내부 힙으로 떨어지고 대부분 실패해 그 패턴이 검은 화면이 된다. 부팅 직후 `heapInternal`이 이만큼 줄었는지 본다.
    - fps: 새 헤더(`a5b5db9` 이후)는 호스트(PC) 숫자만 있다 — 패널에서 `/status`로 잰다. 호스트에서 한 프레임(가운데): YUJIN 0.22 ms, HANYEONGHOE·HEOEUNBIN 0.09 ms, BAKWAN 0.06 ms, CHOEGYEONGEUN 0.05 ms, JEONGEUNGYO 0.04 ms. 패널의 속도로 옮겨 읽을 수 없는 숫자라 순서만 참고한다. JEONGEUNGYO는 20 fps 아래로 가면 움직임 자체도 느려진다(`dt`를 0.05 s로 자름). 느리면 작가와 상의한다.
11. **Windows 키오스크** — 전원을 뽑고 시작하는 리허설, 크래시 복구 대화상자, 전원 설정(9/25에 모니터가 이 노트북의 USB 선택적 절전, 배터리 최대 절전 5시간, 덮개 닫기 = 절전, PCI Express 최대 절전을 잡아냈다. 레포 맨 위의 `노트북 전시용 전원 설정.bat`로 고친다), 시작 바로가기 경로(Live와 패턴플로우 모니터).
12. **세션 초대 응답이 Wi-Fi에서 하나 사라지는 경우** — 패널이 그 초대를 3번(약 4초) 보낸 뒤 포기하고 1초 뒤 새로 초대하므로 몇 초 늦게 붙는다(예전 이미지는 20초 넘게. FlowLocal 쪽은 일부러 고치지 않음). 로그에 `rtp invitation went unanswered`가 찍히고 세션 수는 그대로여야 한다. 일부러 만들어 보지는 않았다.
13. **FlowLocal 첫 부팅** — 지운 직후 열린 `ESP_xxxxxx` 네트워크가 1초쯤 보이는 것.
14. **패널 전원** — 하루 운영 동안 J4 전원(보조배터리 등)이 버티는지는 어디에도 측정이 없다(이 문서를 쓰며 추가한 항목).
15. **노브 위치 공유**(1장 "노브 위치", 2026-09-19 추가) — 실기로 확인된 것은 세션이 붙을 때(9/20, 9/25)와 FlowLocal이 재동기화 요청에 답할 때(9/24) `PROGRAM 1` 뒤에 CC 24–26 = 64가 온다는 것뿐이다. `midi_monitor.py`를 켜고:
    - K1을 끝까지 돌리면 그림이 멈추는 순간 CC 24도 0 또는 127에서 멈추는지, 거꾸로 한 칸 돌리면 둘 다 바로 움직이는지
    - K4로 패턴을 바꿀 때 CC 24–26이 한 줄도 안 찍히고 `PROGRAM`만 찍히는지, 새 패턴이 같은 노브 위치의 모습으로 나오는지
    - 3분 대기 복귀: 먼저 K1–K3를 가운데에서 비키고 K4로 1번이 아닌 패턴에 둔 뒤 3분 기다린다. 가운데가 아니던 노브마다 CC = 64가 찍히고, 그다음 프레임에 `PROGRAM 1`이 찍히는지. (노브가 이미 가운데이고 1번 패턴이면 아무것도 안 찍히는 것이 정상이다. 값이 바뀐 CC와 패턴이 바뀐 PC만 나간다)
    - FlowLocal을 재시작해 세션이 다시 붙을 때 `PROGRAM n` 바로 뒤에 CC 24, 25, 26 현재 값이 찍히는지(9/25에 가운데 값 64로는 확인. 노브를 옮겨 둔 값으로는 아직)
    - FlowLocal이 다시 보낼 때(장치의 Resync, 모니터의 **소리 다시 맞추기**, 상태 페이지 **Send current pattern to Live**, USB 뽑았다 꽂기) `PROGRAM n`, `CC ch16 #118`, 이어서 CC 24, 25, 26이 찍히는지(9/24에 `BF 77 7F`로 64 값은 확인). FlowLocal이 `0c05e79` 이후로 구워져 있어야 한다(9/18에 구운 `aa3f12d`에는 없다)
    - Live에서 매핑한 파라미터를 마우스로 옮겨 놓은 뒤 노브를 한 칸 돌리면 Takeover None에서 파라미터가 바로 패널 위치로 가는지, 그 튐이 들을 만한지. Live만 재시작했을 때 FlowLocal이 다시 보낸 CC로 파라미터가 패널 위치로 가는지(`ableton/README.md` 9장 11번)
    - HEOEUNBIN에서 K1 누름(방향), K2 누름(탄 사람)이 여전히 먹는지
    - 끝에서 끝까지 32칸이 손에 맞는지(`PF_EXHIBIT_KNOB_DETENTS`), 가운데에서 여는 모습(5장 표)이 작가 뜻과 맞는지. 특히 HANYEONGHOE(구름 없음)와 JEONGEUNGYO(회전 없음)

16. **발소리 노트**(2026-09-20 추가) — **확인됨**(9/20, 굽기 기록): HEOEUNBIN에서 채널 10에 `NoteOn 36`과 `38`이 번갈아 나오고, K3 가운데에서 간격 476 ms(계산 473 ms), 40 ms 뒤 note-off, 남은 노트 없음, 다른 패턴은 안 보낸다. 남은 것: K3 양 끝의 빈도(초당 0.6 / 7.2), LED에서 발이 닿는 순간과 노트가 같이 오는지, K4 전환과 잠들 때 멈추는지. Live 쪽은 `ableton/README.md` 9장 12번.
17. **노브를 돌릴 때 신호가 몰려 오는 증상**(2026-09-25 추가) — "쌓인 신호가 하나씩 넘어오는 느낌"은 아직 노브를 돌리며 재지 않았다. 이제 모니터가 잰다. K1–K3를 계속 돌리며 **신호 도착** 막대가 고르게 이어지는지, **몰림 n개**가 1분에 3번을 넘지 않는지, FlowLocal이 잰 흔들림 `jt`가 20 ms, 1분 최대 늦음 `jm`이 250 ms 아래인지 본다(`tools/monitor/README.md` "기준값"). `jt`·`jm`은 아직 호스트 테스트 값만 있다. 채널 1(9/25에 고른 채널)에서 재고, 전시장에서 다시 잰다.
18. **2026-09-25 새 기능 중 실기로 못 본 것** — 패널의 Wi-Fi 자가 복구(60초, `midi.wifiHeals`), 전원이 빠진 패널에 대한 모니터 안내, 채널 스캔이 걸리는 시간과 전시장에서 무엇을 듣는지(COM 잭 로그를 볼 수 없으니 FlowLocal `/api/status`의 `ap_channel_scan`으로 본다), 모니터 버튼이 거절되는 경우(패널이 핫스팟에 없을 때, 핫스팟 기기가 여럿일 때)의 안내.

고쳤지만 실기에서 확인 안 된 것:

- **패널이 RTP 세션을 두 번 세던 문제** (패널 `ba29402`, `198abaa`). 예전에는 라이브러리가 같은 세션의 연결을 두 번 알리면 패널이 세션을 둘로 셌다. 그러면 세션이 끝난 뒤에도 수가 0으로 돌아가지 않아, 재부팅 전까지 FlowLocal을 다시 초대하지 않을 수 있었다(예전 7장 표의 "MIDI가 영영 안 붙음"). 지금은:
  - 세션을 SSRC로 센다. 같은 세션의 두 번째 보고는 한 번으로 치고(로그 `reported joining again - counted once`), 그때 PC를 다시 보내지 않는다.
  - 라이브러리가 포기한 초대의 종료 보고는 세션 종료로 치지 않는다(로그 `rtp invitation went unanswered - not a session`).
  - 앞선 초대가 아직 열려 있을 수 있는 동안에는 같은 주소로 두 번째 초대를 보내지 않는다.
  - MIDI 출력이 세션 수에 묶이지 않는다. 세션 수는 상태 표시와 다시 초대할지 판단하는 데만 쓴다.
  - 확인한 것은 호스트 테스트(`check_midi.py`)와 빌드, 그리고 9/25에 모니터로 패널 다시 켜기·FlowLocal 다시 켜기를 한 번씩 해서 다시 붙은 것(4.7초, 약 14초)이다. 실기에서는 패널 전원 재시작과 FlowLocal 재부팅을 몇 번씩 반복하며, 패널 `/api/status`의 `midi.rtpPeers`가 1을 넘지 않는지, 끊긴 뒤 매번 그만큼 안에 다시 붙는지, 시리얼에 `rtp session list was full`이 나오지 않는지 본다.

알고 둔 채 넘어간 것(고장이 아니라 설계상 남은 것):

- 패널은 들어오는 MIDI를 막지 않는다. FlowLocal이 노트북에서 온 것(재동기화 요청 포함)을 패널로 넘기지 않는다는 데 기대고 있다. 무언가 패널로 PC를 보내면 어떤 패턴 번호든 고를 수 있다.
- 노브 위치 공유가 깨지는 경우(모두 전시 운영에서는 안 생긴다. FlowLocal은 패널로 아무것도 넘기지 않고, 전시 중에는 콘솔을 쓰지 않는다):
  - 콘솔 홈의 노브 슬라이더(`/api/params`의 `pN`)와 패널로 들어오는 MIDI CC 20–22(절대값)는 그 노브의 **그림만** 그 값으로 붙잡고 소리(CC)는 그대로 둔다. 그 노브를 손으로 한 칸 돌리면 풀린다. 붙잡은 값(슬라이더, CC 20–22)이 마지막으로 바뀐 지 5초가 안 됐으면, 그 5초가 지날 때까지 그림과 소리가 둘 다 멈춰 있다가 함께 노브 위치로 간다.
  - `/api/params`의 `dN`이나 패널로 들어오는 CC 24–26(상대값)이 오면, 그 노브는 5초 동안 패턴이 그 클릭을 자기 방식으로 따라간다. 그 5초 동안은 관람객이 그 노브를 돌려도 **그림도 소리도 움직이지 않는다**(돌린 만큼 위치는 움직인다). 5초 뒤 그림과 소리가 같이 노브 위치로 간다. `dN`은 그동안 CC도 움직인다(클릭당 ×4, `/midi`에 Relative가 저장돼 있으면 상대값). 들어온 CC 24–26은 CC를 내보내지 않는다(패널은 받은 MIDI를 되돌려 보내지 않는다).
  - 노브 클릭을 직접 읽는 것은 노브 위치를 받지 않는다. 코어의 보정 카드(`/api/display?screen=N`, 웹사이트 패널 튜너가 쓰는 주소)는 이 빌드에서 K1(화면)·K2(White Lv)가 한 칸에 1–2씩 움직이고, 공유 위치의 끝에서 멈추고, K1–K3를 누른 다음 프레임에 한꺼번에 튄다(K2가 끝에 있으면 White Lv 약 192). 카드는 `/api/display`의 `screen=`·`level=`로 조정한다. 콘솔에서 고른 옛 `.pfm` 모듈 중 클릭을 직접 읽는 것도 같다. 작가 패턴 6개는 해당 없다(모두 `PFParams::apply`).
- 버튼을 누른 한 프레임(1/60초)은 그 노브의 위치가 패턴에 안 간다(1장 "노브 위치"). 누르면서 같이 돌린 만큼은 한 프레임 늦게 보인다. 누름으로 파라미터를 되돌리는 패턴(Origin)은 그 되돌림이 한 프레임만 보이고 노브 위치가 이긴다. 작가 패턴 6개 중 누름을 쓰는 HEOEUNBIN은 파라미터가 아닌 상태(방향, 탄 사람)를 바꾸므로 해당 없다.
- 패턴 전환·대기 복귀·부팅 때 고른 패턴을 3초 뒤 NVS에 쓴다. 전시 규모에서 수명 문제는 없다.
- 패턴의 시간 누적값(시계)은 대기 복귀 때도 계속 흐른다.
- 코어의 가운데 이름 표시(`src/core_ui_text.h`)도 세로에서 10글자 넘는 이름을 단어 중간에서 끊는다. 전시에서는 콘솔로 패턴을 고를 때만 보인다. 본 저장소에서 따로 고칠 문제로 남겼다.

## 굽기 기록

| 날짜 | 보드 | 무엇 | 결과 |
|---|---|---|---|
| 2026-09-18 | 패널용 DevKit, MAC `44:bd:8d:75:e3:54` (COM17, 패널에서 뺀 상태) | 전체 지우기 + 패널 exhibit `382147e` (bootloader/partitions/boot_app0/firmware) | 해시 확인. 부팅 로그: `[EXHIBIT] 6 patterns at 1..6, default 1 (Lissajous Weave), captions on`, `connecting to "PatternflowLocal" (built-in creds)`. FFat은 지운 뒤라 `not formatted - presets only`(전시엔 필요 없음, 원하면 콘솔 `/patterns`에서 포맷) |
| 2026-09-18 | FlowLocal DevKit, MAC `14:c1:9f:32:29:6c` (COM11 = CH343 COM 잭) | 전체 지우기 + `firmware.factory.bin` @0x0 (flowlocal `aa3f12d`, 코드는 `5bcce79`와 같음, 이미지 검사 PASS) | 해시 확인. 부팅 로그: `usb device "Patternflow Exhibit" started`, `AP "PatternflowLocal" on 192.168.66.1 ch6, max 3 stations`, `rtp listener "FlowLocal" … ssrc 5b4ddcda`, `program mirror: CC 118 ch 16`, `exhibit ready`. `No core dump partition found`는 파티션표에 코어덤프 칸이 없어서 나오는 줄로 무해 |
| 2026-09-19 | 패널용 DevKit `44:bd:8d:75:e3:54` (COM17) | 작가 패턴 6개 이미지 `013fd9b` (boot_app0 + firmware만, 설정·FFat 유지, sha256 575244c6…) | 해시 확인. 부팅 로그: `[EXHIBIT] 6 patterns at 1..6, default 1 (BAKWAN), captions on`, 예전 임시 패턴 기억은 없어져 Origin → BAKWAN으로 시작. fps는 아직 못 잼(이 데스크탑엔 Wi-Fi가 없음) |
| 2026-09-19 (밤) | 패널용 DevKit `44:bd:8d:75:e3:54` (COM17) | 재번역한 패턴 6개 이미지 `fba3e53` (boot_app0 + firmware만, 설정·FFat 유지, 1,167,952 B, sha256 95de6f07…) | 해시 확인. 부팅 로그: `[EXHIBIT] 6 patterns at 1..6, default 1 (BAKWAN), captions on`, 멈춤·에러 없음(`nvs_open NOT_FOUND`와 FFat -1은 예전과 같음). esptool 5.2는 한글 콘솔(cp949)에서 진행 막대 글자로 죽는다 - `PYTHONUTF8=1 PYTHONIOENCODING=utf-8`을 붙여서 굽는다. fps는 아직 못 잼 |
| 2026-09-20 | 패널용 DevKit `44:bd:8d:75:e3:54` (COM17) | 발소리 노트 이미지 `235f06a` (boot_app0 + firmware, 1,168,608 B, sha256 bd70a7b2…) | 부팅 뒤 14초에 `PROGRAM 1` + CC 24/25/26 = 64. HEOEUNBIN으로 바꾸니 채널 10에 노트 36/38이 번갈아 나왔고, K3 가운데에서 간격 476 ms(계산 473 ms, 오차 0.6%), 40 ms 뒤 note-off, 남은 노트 없음. 다른 패턴에서는 안 나옴. 기록은 `C:/Users/lsh/pfx-logs/footsteps.log` |
| 2026-09-24 | 패널용 DevKit `44:bd:8d:75:e3:54` (COM17) | **300 Hz** 이미지 `0e1d957` (`firmware_images/panel/`, boot_app0 + firmware, 1,168,944 B, sha256 94633a5a…) | 해시 확인. 핫스팟 접속(192.168.66.2), 에러 없음. main v3.10.4의 300 Hz 커밋 하나만 가져왔다 |
| 2026-09-24 | FlowLocal `14:c1:9f:32:29:6c` (COM11 = 오른쪽 COM 잭) | 전체 지우기 + `firmware_images/flowlocal/firmware.factory.bin` @0x0 (`0c05e79` = `f9dcd9d`, 노브 값 재전송) | 해시 확인. 패널 RTP 세션 연결. 왼쪽 잭으로 옮겨 재동기화(BF 77 7F)를 보내면 `PROGRAM 1` + CC118 미러 + **CC 24/25/26 = 64**가 돌아온다(3번 모두). 로그의 `wifi:CCMP mgmt frame ... non-zero reserved bit`는 9/19 옛 펌웨어 때도 있던 것이라 문제 아님 |
| 2026-09-25 | 패널용 DevKit `44:bd:8d:75:e3:54` (이 노트북 COM10, DevKit 자체 USB) | 2026-09-25 중간 이미지 (boot_app0 + firmware, 설정 유지, 1,170,496 B, sha256 12bc93a1…): `0e1d957` + `PatternflowLocal`만 접속, 초대 호스트·MIDI 켬 잠금, 첫 초대 1초·3번 시도·끝나면 1초 뒤 재초대·Wi-Fi 복귀 때 재초대, Wi-Fi 자가 복구, 19.5 dBm | 해시 확인. 붙은 뒤의 숫자는 아래 "실측 기록 (2026-09-25)" |
| 2026-09-25 | 패널용 DevKit `44:bd:8d:75:e3:54` (COM10) | 최종 이미지 `07b4c70b…` (`firmware_images/panel/firmware.bin`, 1,170,496 B, boot_app0 + firmware, 설정 유지). 위 이미지와 같은 코드에 "Wi-Fi가 끊긴 동안 초대하지 않음"만 더했다 | 해시 확인. 다시 켜지고 약 1초 뒤 `PROGRAM 1`(BAKWAN)이 노트북에 왔다(모니터 기록 19:12:47 → 19:12:48). 모니터 "모두 정상" |
| 2026-09-25 | FlowLocal `14:c1:9f:32:29:6c` (USB 잭. BOOT+RST 다운로드 모드에서 이 노트북에 COM11로 보임) | `firmware_images/flowlocal/firmware.factory.bin` @0x0 (`flx-mon1`, 1,227,168 B, sha256 3e3b7e9d…), 지우지 않음 | 해시 확인. esptool이 `Hard resetting via RTS pin`을 찍은 뒤에도 다운로드 모드에 남아 있다가, RST를 누르자 새 펌웨어로 떴다. 켤 때 채널 1을 스스로 골랐고, 모니터가 상태를 받기 시작했다(`펌웨어 flx-mon1`) |
| 2026-09-30 | 패널 DevKit (COM10) + FlowLocal (USB 잭, 다운로드 모드) | 패널 `502a3c11…`(patternflow `006e9c1`: 거절 뒤 1초 재초대, 죽은 세션 12초, 자막 없음, YUJIN 새 색), FlowLocal `4ecbcca0…`(`flx-mon3`, flowlocal `c6360dc`) | 해시 확인, FlowLocal은 굽고 RST. 실측: 연결 다시 잡기 15/15 1.5초, 패널 재부팅 8/8(핫스팟 접속 뒤 0.8–1.3초), FlowLocal 재부팅 6/6(15–16.5초), 채널 명령 3/3, 명령 연타 복구 ≤1.8초(거절·시간 초과·빠진 패킷 0), 모니터 **한 번에 다시 연결하기** 2/2(Live가 꺼져 있을 때 86초, 켜져 있을 때 17초). 채널 점수 예: 1:93, 6:186, 11:119 |
| 2026-09-25 | 패널용 DevKit `44:bd:8d:75:e3:54` | **굽기 대기**: 자막 없는 이미지 `2471b15b…` (`firmware_images/panel/firmware.bin`, 1,170,480 B, patternflow `7350445`). 굽는 방법은 4.2(COM10, boot_app0 + firmware, 설정 유지) | — |
| 2026-09-25 | FlowLocal `14:c1:9f:32:29:6c` (USB 잭, 다운로드 모드 COM11) | `firmware_images/flowlocal/firmware.factory.bin` @0x0 (`flx-mon2`, 1,232,064 B, sha256 b48b0ee8…), 지우지 않음: 스스로 복구(USB 다시 연결·재시작)와 재시작 이유 | 해시 확인. 이번에도 RST를 눌러야 떴다. `fw=flx-mon2`, `rsn=poweron`(RST는 전원 켜기로 보인다), `bc=1`, `ur=0`. 채널 1. 패널을 켜니 핫스팟 접속 2초 뒤 세션 |

## 실측 기록 (2026-09-19, 데스크탑에서 패널 DevKit + FlowLocal)

| 상황 | 결과 |
|---|---|
| 패널 재부팅 (FlowLocal 켜진 채) | 3.2초 핫스팟 접속 → 23.2초 MIDI 연결, 그 순간 PC에 `PROGRAM 1` + `CC 118=1` 도착 |
| FlowLocal 빠른 재시작 (고치기 전, `ebcbe08` 이전) | 패널이 끊긴 걸 모르고 옛 연결을 붙잡음 → **2분 넘게 MIDI 먹통** (패널을 재부팅해야 풀림) |
| FlowLocal 15초 꺼짐 | 패널이 8.5초에 끊김 감지 → 핫스팟이 뜨고 0.2초 만에 재접속 → 1초 뒤 MIDI 복구 |
| FlowLocal 빠른 재시작 (고친 뒤: 켜질 때 핫스팟 12초 늦게) | 리셋: 8.5초 끊김 감지 → 13.4초 재접속. USB 뽑았다 꽂기(약 1초): 8.5초 감지 → 16초 재접속 → 17초 MIDI 복구. USB 약 6초 뽑았다 꽂기: 22초 뒤 PC에 `PROGRAM 1` 도착 |
| 재동기화 요청 `BF 77 7F` (PC → FlowLocal) | 10ms 안에 `PROGRAM n` + `CC 118=n` 응답 |

원인과 수정: FlowLocal이 1초 안에 같은 SSID·BSSID로 다시 뜨면, 패널은 비콘 끊김(약 8초)을 못 느끼고 보호된 관리 프레임(PMF) 때문에 새 AP의 연결 해제도 무시해서, 죽은 연결을 계속 붙잡는다. 두 쪽 다 PMF는 설정으로 끌 수 없어서(`capable`은 폐기된 옵션), FlowLocal이 켜질 때 12초 동안 전파를 내지 않게 했다(`FL_EXHIBIT_AP_START_DELAY_MS`, flowlocal `ebcbe08`). 그 사이 USB MIDI 포트는 바로 뜬다.

같이 고친 것: `tools/midi_monitor.py`가 USB MIDI 장치가 사라질 때 Windows에서 포트를 닫다가 조용히 죽던 문제. 사라진 입력은 닫지 않고 두었다가 끝날 때 `os._exit`로 빠진다. 고친 뒤 뽑았다 꽂기에서 `port disappeared` → `port back` → `PROGRAM 1`까지 기록됐다.

### Live 확인 (2026-09-19, 이 데스크탑의 Live 12 Suite)

- **Live는 트랙 입력의 Program Change를 Max for Live 장치의 `[midiin]`까지 넘겨준다.** 장치 윗줄에 `PC 6 ch1 -> 6: …`가 뜨고 씬이 발사됐다(CC 118 줄이 아니라 PC 줄). 그러니 **Alt CC는 끈다**. 비상 경로로만 남긴다.
- 장치의 **Resync** → FlowLocal의 답(PC)으로 씬 발사까지 됨. MIDI To 경로도 확인.
- 처음에 안 됐던 이유(실제로 겪음): ① `Ch. 16`을 **MIDI From** 채널 칸에 넣음(→ 채널 1의 PC가 걸러짐) ② Monitor가 **Auto**(트랙을 Arm하지 않으면 입력이 장치까지 안 감) ③ MIDI To가 **No Output**(요청이 FlowLocal로 안 나감). 맞는 값: From = Patternflow Exhibit / **All Channels**, Monitor = **In**, To = Patternflow Exhibit / **Ch. 16**.

## 실측 기록 (2026-09-25, 노트북에서 패널 DevKit + FlowLocal)

패널은 `12bc93a1…`, FlowLocal은 `flx-mon1`(`3e3b7e9d…`)일 때다. "예전 이미지"는 9/24에 구운 것(패널 `0e1d957`, FlowLocal `0c05e79`)이다.

| 상황 | 결과 |
|---|---|
| "안 붙음"의 원인 | 패널에 집 Wi-Fi(`KT_GiGA_E368`) 하나만 저장돼 있었다. 저장된 네트워크가 있으면 컴파일된 값을 시도하지 않는 예전 규칙 때문에 패널은 `PatternflowLocal`을 한 번도 시도하지 않고, 집 Wi-Fi에서 192.168.66.1을 계속 초대했다(`rtp invitation went unanswered`만 반복). 저장된 것을 손으로 지워 풀었다. 전시 이미지에서는 이제 일어날 수 없다(3장) |
| 이 자리의 Wi-Fi (노트북 스캔) | 집 공유기와 메시가 채널 9(신호 약 60%), 이웃이 채널 8. FlowLocal은 채널 6에 있었다(겹침). 새 FlowLocal은 켤 때 채널 1을 스스로 골랐다 |
| 패널 신호 세기 (FlowLocal에서) | −37 ~ −43 dBm |
| 예전 이미지: 패널 재부팅 → MIDI | 24초 (9/19의 23.2초와 거의 같다. 예전 패널은 Wi-Fi에 붙고 20초 뒤 첫 초대를 했다) |
| 새 이미지: FlowLocal 켜기 → 첫 MIDI | 12초에 핫스팟이 뜨고, 2.7초 뒤 패널 세션과 첫 MIDI(`PROGRAM 1`, CC 118 미러, CC 24–26 = 64). FlowLocal 전원을 넣고 약 14.7초 |
| 모니터 **패널 연결 다시 잡기** | 답은 바로, MIDI 1.0초 뒤 돌아옴 |
| 모니터 **패널 다시 켜기** | 답 0.04초, 패널 재부팅, MIDI 4.7초 뒤 돌아옴 |
| 모니터 **FlowLocal 다시 켜기** | FlowLocal 재부팅, 켜지고 약 14초 뒤 MIDI 돌아옴(그중 12초는 일부러 조용히 있는 시간) |
| 패널 신호(하트비트) | 10초마다 FlowLocal에 닿는다 |
| 처음 USB 연결 | 한 번, Windows가 FlowLocal을 "알 수 없는 USB 장치(장치 설명자 요청 실패)"로 잡았다. 뽑았다 꽂으니 됐고 다시 나오지 않았다. 케이블이나 포트를 의심한다 |
| 노트북 전원 설정 | 모니터가 "USB 선택적 절전 = 사용"을 잡아냈다(19:15에 AC·DC 모두 끔). 이어서 배터리 최대 절전 5시간, 덮개 닫기 = 절전, PCI Express 최대 절전도 잡아냈다. 레포 맨 위의 `노트북 전시용 전원 설정.bat`로 한 번에 고친다 |
| FlowLocal 멈춤 (옛 `flx-mon1`) | 21:02에 다시 꽂은 뒤 80초 동안 4번 스스로 다시 켜졌고(Windows·Live 기록), 그 뒤 USB로는 아무 대답이 없었다(핫스팟은 떠 있음). RST로 풀렸다. 22:09에 다시 꽂고 22:10:42부터 또 같은 상태가 한 시간 이어졌다. 원인은 확정하지 못했다(USB 잠듦을 놓친 것, 막힌 USB 끝점, 전원 흔들림 중 하나로 본다). `flx-mon2`가 이 상태에서 스스로 USB를 다시 연결하거나 재시작하고 이유를 남긴다 |
| Live 튕김 | 15:52, 16:17에 `rthdasio64.dll`(Realtek ASIO) 안에서 c0000005. FlexASIO로 바꿨다(2장 "노트북과 소리") |
| 패널 새 연결 (`flx-mon2`) | 23:10:01 핫스팟 접속 → 23:10:03 세션과 `PROGRAM 1`. 신호 −17 dBm |

정리: 패널만 다시 켤 때 MIDI까지가 24초에서 4.7초로 줄었다. FlowLocal을 다시 켤 때는 일부러 조용히 있는 12초가 대부분이다(9/19 "원인과 수정").

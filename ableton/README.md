# 전시용 Ableton 세팅 — PF Exhibit Scenes

패널에서 K4로 패턴을 바꾸면 Live의 씬이 따라 바뀌고, K1–K3는 Live 파라미터를 움직인다.

```
패널 --(Wi-Fi, RTP-MIDI)--> FlowLocal --(USB, DevKit의 "USB" 포트)--> 노트북 --> Live 12
                                     <------- 재동기화 요청 하나만 (이 장치가 보냄)
                                                        MIDI 포트 이름: "Patternflow Exhibit"
패널이 보내는 것 (채널 1):  Program Change n = 패턴 n  ·  CC 24/25/26 = K1–K3 위치 (0–127 절대값, 그림과 같은 값)  ·  노트 60–62 = K1–K3 누름
패널이 보내는 것 (채널 10): 노트 36/38 = HEOEUNBIN(4번, 도마뱀)의 발이 땅에 닿는 프레임마다 하나 (4장 "발소리 노트")
FlowLocal이 붙이는 것:     모든 Program Change 바로 뒤에 CC 118 = n (채널 16, 바이트 BF 76 n) = 프로그램 미러
                           다시 보내는 PC는 미러 뒤에 CC 24/25/26 = 패널이 마지막으로 보낸 값 (채널 1)
장치가 보내는 것:          재동기화 요청 CC 119 = 127 (채널 16, 바이트 BF 77 7F)
패턴 번호:  0 = Origin   ·   1–6 = 작가 6명 (K4 순서)   ·   부팅·대기 복귀 = 1
```

- K4는 MIDI로 아무것도 안 보낸다(CC 27, 노트 63 없음). 패턴이 바뀌었다는 Program Change만 나간다.
- 노트북에서 FlowLocal로 가는 것은 이 장치의 **재동기화 요청** 하나다. FlowLocal은 노트북에서 오는 나머지를 모두 버리고, 패널로는 아무것도 넘기지 않는다. 노트북에는 rtpMIDI 드라이버도 Wi-Fi도 필요 없다.
- 패널은 세션이 다시 붙으면 현재 PC를 한 번 더 보내고, 바로 뒤에 CC 24/25/26 현재 값도 보낸다(4장). FlowLocal은 마지막 PC(와 CC 118 미러, 이어서 패널이 마지막으로 보낸 CC 24/25/26)를 다음 경우에 다시 보낸다. 그 밖에는 다시 보내지 않는다. 패널이 FlowLocal이 켜진 뒤 한 번도 보내지 않은 노브는 빠진다(flowlocal `0c05e79`부터).
  1. 그 PC를 USB로 못 썼을 때(버퍼가 참, 호스트 없음)
  2. USB가 다시 연결되거나 노트북이 절전에서 깨어날 때
  3. 재동기화 요청을 받았을 때. 이 장치가 로드되고 0.5초 뒤에 한 번, 그리고 장치의 **Resync** 버튼을 누를 때 보낸다.
  4. `http://192.168.66.1/`에서 **Send current pattern to Live**를 누를 때
- Live만 껐다 켤 때는 3번이 현재 패턴과 노브 위치를 되찾아 온다(3장 끝).
- FlowLocal 초대와 MIDI 켬은 패널 펌웨어의 기본값이다. 이 패널의 MIDI 페이지에서 한 번이라도 저장한 값이 있으면 그 값이 이긴다. 전시 전에 한 번 확인한다(루트 `README.md` 7장 "전시 전 점검표"). K1–K3는 이 빌드에서 노브 위치를 그대로 보내므로, MIDI 페이지의 Knob value / Relative steps와 Sensitivity는 상관없다.

```
ableton/
├── README.md                         ← 이 문서
└── pf-exhibit-scenes/
    ├── pf_exhibit_scenes.maxpat      ← 장치 패치 (Max에서 열어 .amxd로 저장)
    └── pf_exhibit_scenes.js          ← 장치 로직 전부 (패치가 불러씀)
tools/
├── midi_monitor.py                   ← 들어오는 MIDI 보기 (현장 점검)
├── send_test.py                      ← 가짜 PC/CC, 재동기화 요청, CC 118 보내기 (하드웨어 없이 테스트)
├── test_pf_exhibit_scenes.js         ← js 오프라인 테스트 (Node)
└── check_maxpat.py                   ← .maxpat 구조 검사
```

---

## 1. .amxd 만들기 (한 번만)

기존 Patternflow Bridge와 같은 방식이다. 저장소에는 텍스트 `.maxpat`만 두고, `.amxd`는 한 번 만들어 쓴다. 순서가 중요한 곳은 3번이다.

1. 장치를 둘 폴더를 정하고 `pf_exhibit_scenes.js`를 거기 복사한다. 예: `문서\Ableton\User Library\Presets\MIDI Effects\Max MIDI Effect\` (Live 브라우저의 User Library에 바로 보인다)
2. Live에서 MIDI 트랙을 하나 만들고, 브라우저의 Max for Live에서 **Max MIDI Effect**를 그 트랙에 끌어다 놓는다. 장치 제목 표시줄의 **편집 버튼**을 누르면 Max가 열린다.
3. **붙여넣기 전에 먼저 저장한다.** Max에서 **File → Save As** → 1번 폴더에 `PF Exhibit Scenes.amxd`. 장치가 js와 같은 폴더에 있어야 붙여넣는 순간 js를 찾는다. 순서를 바꾸면 js가 로드에 실패한 채로 남는다.
4. **File → Open** → `ableton/pf-exhibit-scenes/pf_exhibit_scenes.maxpat`. 전체 선택(Ctrl-A), 복사(Ctrl-C) 후 그 창을 닫는다.
5. 장치 패처로 돌아와 **기본 내용(midiin → midiout, 설명 코멘트)을 전부 지우고** 붙여넣는다(Ctrl-V). 이 패치에 midiin/midiout이 이미 들어 있다. 기본 midiin → midiout을 남기면 Thru를 꺼 둬도 패널의 MIDI가 전부 트랙의 MIDI To(FlowLocal)로 되돌아 나간다.
6. **View → Patcher Inspector**(또는 빈 캔버스 우클릭) → **Open in Presentation** 체크. 붙여넣기로는 이 설정이 따라오지 않는다.
7. 저장(Ctrl-S). 장치의 아래 상태 줄이 `ready - scenes 0-…`로 바뀌고, Max 메뉴 **Window → Max Console**에 `js: can't find file`이 없으면 된 것이다. 아니면 js 위치를 확인하고, 장치를 트랙에서 지웠다가 브라우저에서 다시 끌어다 놓는다.
8. (권장) 동작 확인이 끝나면 편집기 툴바의 **Freeze Device**(눈송이 아이콘)를 누르고 다시 저장한다. js가 `.amxd` 안에 들어가 파일 하나로 옮겨진다. 이어서 전시 세트에서 **File → Collect All and Save**를 하면 장치가 프로젝트 폴더로 복사된다.

js를 고친 뒤에는 장치를 지우고 다시 끌어다 놓는다(Freeze했다면 Unfreeze → 수정 → 다시 Freeze). js 자동 리로드는 일부러 꺼 두었다. 리로드되면 상태가 초기화되는데 live.thisdevice가 다시 bang하지 않아서, 장치가 멈춘 채로 남기 때문이다. 전시 세트가 여는 `set Project/Presets/MIDI Effects/Max MIDI Effect/PF Exhibit Scenes.amxd`는 Freeze된 것이라 js 사본을 안에 품고 있다(2026-09-25에 `ableton/pf-exhibit-scenes/pf_exhibit_scenes.js`와 줄 끝만 다르고 같음을 확인). 저장소의 js만 고치면 Live는 옛 사본을 계속 돌리므로, 고친 뒤에는 그 `.amxd`를 다시 Freeze하고 옆의 js 사본도 같이 바꾼다.

## 2. Live 설정 — Settings → Link, Tempo & MIDI

| 포트 | Track | Remote | 이유 |
|---|---|---|---|
| **In: Patternflow Exhibit** | **ON** | **ON** | Track: Program Change가 트랙의 장치로 들어간다. Remote: K1–K3를 Ctrl-M으로 매핑할 수 있다. |
| **Out: Patternflow Exhibit** | **ON** | **OFF** | Track: 컨트롤 트랙의 MIDI To 목록에 이 포트가 나타나고, 장치의 재동기화 요청이 FlowLocal로 나간다. Remote: 아래 설명 참고 |

두 포트 모두 **Sync**(MIDI 클럭)는 끈다. 켜면 클럭 바이트가 끊임없이 FlowLocal로 가서 버려진다.

**Output의 Remote는 반드시 끈다.** 켜져 있으면 Live가 매핑된 파라미터 값을 같은 CC 번호로 포트에 되돌려 보낸다. 패널은 들어오는 CC 24–27을 상대 회전으로 읽기 때문에, 그 값이 패널까지 가면 노브가 한 값에 붙잡히고 돌릴 때마다 튄다. 9/15에 실제로 겪은 버그다(`docs/director-midi.md` Route D에도 적혀 있다). 지금은 FlowLocal이 노트북에서 온 것 중 재동기화 요청 말고는 모두 버리고 패널로 넘기지 않아서 당장 터지지는 않는다. 그래도 켜 두면 USB로 쓸데없는 CC가 오가고, 나중에 경로가 양방향이 되는 순간 바로 터진다.

같은 탭에서 **Takeover Mode → None**으로 둔다(2026-09-19 변경. 전에는 Value Scaling). 패널이 보내는 CC 24–26은 언제나 노브 위치 그 자체이고(4장), K4 전환 때는 CC를 보내지 않는다. 그래서 매핑된 파라미터가 패널이 보낸 값대로만 움직여 왔다면 None과 Value Scaling은 똑같이 움직인다.

둘이 다른 것은 파라미터가 패널 위치와 달라져 있을 때다: 세트를 다시 열어 저장된 값이 들어옴, 마우스, 씬 템포, 클립 자동화, Live가 꺼져 있거나 USB가 빠져 CC를 못 받은 동안 패널이 움직임.

- **None**: 다음 CC에서 파라미터가 바로 패널 위치로 돌아간다. 같은 노브 위치가 늘 같은 소리다. 그 순간 한 번 튄다.
- **Value Scaling**: 튀지 않는다. 대신 노브가 끝에 닿을 때까지 파라미터가 패널 위치와 다른 곳에 머물러, 같은 노브 위치에서 다른 소리가 난다. FlowLocal이 다시 보내는 CC(3장 끝)도 파라미터를 패널 위치로 옮기지 못할 수 있다.

전시의 목표(모두가 같은 장면을 보고 같은 소리를 듣는다)에 맞는 것은 None이다. 튀는 소리가 거슬리는지는 실기 확인 전이다(9장 11번). 튀는 일을 줄이려면 매핑한 파라미터를 Live 안에서 움직이는 것(씬 템포, 클립 자동화)을 두지 않는다.

포트 이름은 FlowLocal 펌웨어가 정한다(`FL_EXHIBIT_USB_PRODUCT`). 목록에 다른 이름으로 보이면 `python tools/midi_monitor.py --list`로 확인한다. FlowLocal을 **다른 USB 포트에 꽂으면** Windows가 새 장치로 잡아 이름이 `2- Patternflow Exhibit`처럼 바뀔 수 있다. 그러면 위 스위치를 다시 켜야 하니, 노트북 USB 포트를 하나 정해 테이프로 표시해 둔다.

## 3. 세트 구성 (권장)

**컨트롤 트랙** — MIDI 트랙 하나, 이름은 예를 들어 `PF Control`.

- MIDI From: **Patternflow Exhibit**, **All Channels** (PC는 채널 1, CC 118 미러는 채널 16으로 온다. Ch. 1로 두면 미러가 트랙에서 걸러진다). 트랙을 통과한 메시지는 Live가 채널 1로 합쳐 장치에 넘긴다고 알려져 있다(Cycling '74 포럼, 실기 확인 안 됨). 그래서 장치는 채널을 보지 않는다.
- MIDI To: **Patternflow Exhibit**, **Ch. 16**. 장치의 재동기화 요청이 이 길로 FlowLocal에 간다. Live는 트랙이 외부 포트로 내보내는 메시지의 채널을 여기서 고른 채널로 바꿀 수 있다(실기 확인 안 됨). 요청은 원래 채널 16이라 Ch. 16이면 어느 쪽이든 맞다.
- Monitor: **In** (Arm 없이도 항상 입력이 통과한다)
- 장치: **PF Exhibit Scenes** 하나. 악기, 다른 MIDI 이펙트, 클립은 없다. 장치 뒤에 악기를 두면 요청이 그 악기로 가고 FlowLocal에 닿지 않는다.
- 이 장치는 세트에 **하나만** 둔다. 두 개면 씬이 두 번 발사되고 요청도 두 번 나간다.
- 다른 악기 트랙은 MIDI From을 **No Input**으로 둔다. All Ins에 Arm이 켜져 있으면 패널의 노트가 그 악기로도 들어간다. 누름을 소리로 쓰는 트랙은 4장 "누름 소리".

**되먹임 고리가 생기지 않는 이유.** 장치는 요청을 로드 뒤 한 번과 Resync 버튼을 누를 때만 보낸다. 받은 메시지에 답해서 보내는 일은 없다. FlowLocal은 노트북에서 온 메시지 중 요청 말고는 모두 버리고 패널로 넘기지 않는다. 요청에 대한 답(PC, CC 118, CC 24–26)을 장치가 요청으로 읽는 일도 없다. Thru를 켜도 패널의 MIDI가 FlowLocal로 되돌아가 버려질 뿐이다. 고리가 생기는 구성은 MIDI From과 MIDI To가 **같은 loopMIDI 포트**이고 Thru가 켜진 경우 하나다(5장).

**노트북 모니터의 상태 응답(SysEx).** FlowLocal은 노트북 모니터가 2초마다 보내는 상태 요청에 SysEx `F0 7D 50 46 02 <텍스트> F7`(텍스트 240바이트 이하)로 답한다. 이 답은 같은 USB 입력으로 오므로 Live도 받는다. Live 10.0.1부터 SysEx는 MIDI 트랙의 장치 체인을 지나가므로(Live 10 릴리스 노트) 이 장치의 `midiin`에도 들어온다. 장치는 `F0`부터 `F7`까지를 통째로 버리고 running status도 끊는다(그 사이의 real-time 바이트는 SysEx를 끝내지 않는다). 그래서 텍스트 바이트가 PC나 CC 118로 읽히지 않는다. 텍스트 첫 글자 `v`가 0x76 = 118이라 running status가 남아 있었다면 바로 CC 118로 읽혔을 자리다. Node 테스트 26이 128씬 세트로 이것을 확인한다. Live 기본 악기는 SysEx에 반응하지 않고, Live는 VST에 SysEx를 넘기지 않는다. Ctrl-M 매핑은 노트·CC·피치 벤드만 받으므로 In 포트의 Remote가 켜져 있어도 SysEx로는 아무것도 움직이지 않는다. 모니터가 돌면 `dump`의 `bytes`가 2초마다 응답 길이만큼 는다. Thru를 켜면 응답이 FlowLocal로 되돌아가 버려진다(`usb_in_discarded`가 는다).

**씬 번호는 Program Change와 같게 센다. 맨 위 씬이 0번이다.** PC n은 `live_set scenes n`, 즉 위에서 0, 1, 2… 로 셌을 때 n번 씬을 발사한다. Live 화면의 번호와 헷갈리지 않게 씬 이름을 내용으로 붙여 둔다(`Origin`, `ARTIST A / Title` …). 장치 상태 줄에 번호와 그 이름이 같이 뜬다.

| 씬 (= PC) | 패널 패턴 | 언제 오나 | 내용 |
|---|---|---|---|
| **0** | Origin | K4로는 안 온다. 웹 콘솔에서 Origin을 고를 때만 | 조용한 소리, 또는 빈 씬(정지) |
| **1** | 작가 1 = **기본 패턴** | 부팅할 때, 한동안(기본 3분) 아무도 안 만졌을 때, K4 | 작가 1 |
| **2–6** | 작가 2–6 | K4 | 작가 2–6 |

K4는 1 → 2 → … → 6 → 1로 돈다. 기본 패턴은 패널 펌웨어의 `PF_EXHIBIT_DEFAULT_SLOT`(0 = 작가 1 = PC 1)이 정한다. 그걸 바꾸면 장치의 **Start PC**도 그 PC 번호로 바꾼다. 씬 순서를 옮기기 싫으면 **Offset**으로 밀어도 된다(Offset 1이면 PC 1이 씬 2를 발사). Offset은 Start PC에도 똑같이 더해지므로 Start PC는 그대로 둔다.

- **Global Launch Quantization**(컨트롤 바): 전시에서는 **None**(K4를 돌리는 즉시 씬이 바뀜, 2026-09-19 사용자 결정). 너무 뚝 끊기면 **1/16**이나 **1/8**(거의 즉시, 박자에는 맞춤). 클립마다 Launch Quantization이 **Global**이어야 이 설정을 따른다. 바뀐 씬이 박자 위치를 이어받게 하려면 클립 Launch Mode를 **Legato**로 둔다.
- 씬마다 쓰지 않는 트랙 칸에는 **정지 버튼**이 있어야 앞 씬의 소리가 멈춘다. 빈 칸에는 기본으로 있지만, "Remove Stop Button"을 한 칸에서는 이전 클립이 계속 돈다.
- 씬에 템포를 지정해 두면 씬마다 기본 템포가 바뀐다. **K1을 템포에 매핑했다면 씬 템포는 쓰지 않는다.** 씬 템포가 K4마다 템포를 바꿔 놓으면 같은 K1 위치에서 다른 템포가 되고(Value Scaling), None이면 다음 K1 CC에서 템포가 튄다. 씬마다 템포를 바꾸고 싶으면 K1을 템포가 아닌 것에 매핑한다.

### 장치 옵션

| 항목 | 기본값 | 뜻 |
|---|---|---|
| Enable | on | 끄면 Program Change를 무시하고, Autostart와 로드 뒤 재동기화 요청도 하지 않는다. Resync 버튼은 그래도 요청을 보낸다. |
| Autostart | off | 세트를 열면 1.5초 뒤 Start PC의 씬을 발사하고, 트랜스포트가 멈춰 있으면 재생한다. 그 전에 PC가 이미 왔으면(로드 뒤 요청에 대한 FlowLocal의 답 포함) 그 씬을 유지한다. **전시 세트에서는 켜고 저장한다.** 편집 중에는 끄는 편이 편하다. |
| Start PC | **1** | Autostart가 패널에서 이 PC가 온 것처럼 발사한다. 씬 = Start PC + Offset. 1 = 패널이 부팅할 때 켜는 패턴이라, 전원을 같이 켜면 소리와 빛이 처음부터 맞는다. |
| Offset | 0 | 씬 = PC + Offset (Start PC 포함) |
| Thru | **off** | 켜면 들어온 MIDI를 장치 출력으로 그대로 내보낸다. 컨트롤 트랙에서는 출력이 FlowLocal이라 켤 이유가 없다(패널의 MIDI가 되돌아가 버려질 뿐이다). 이 장치를 다른 구성의 트랙에 둘 때만 쓴다. |
| Alt CC | **off** | 비상용. 켜면 CC(번호는 아래 칸, 기본 118) 값을 Program Change처럼 취급한다. FlowLocal이 모든 PC 바로 뒤에 같은 번호를 CC 118(채널 16)로 보내므로, Live가 PC를 장치에 넘겨주지 않으면 **켜기만 하면 된다**. 펌웨어 수정은 필요 없다(9장 1번). **채널은 보지 않는다.** Live가 트랙 입력을 채널 1로 합쳐 장치에 넘기기 때문에, 채널 16으로 거르면 미러를 하나도 못 읽는다. 전시 패널이 보내는 CC는 24–26(K1–K3)뿐이라 118과 겹치지 않는다(midi 피처 범위는 24–27). |
| Alt CC number | 118 | Alt CC가 읽는 CC 번호(1–118, 어느 채널이든). FlowLocal 빌드의 `FL_EXHIBIT_PROGRAM_CC`와 같아야 한다. 24–27로 바꾸지 않는다(패널 노브가 프로그램으로 읽힌다). |
| **Resync** (버튼) | — | 누르면 재동기화 요청(`BF 77 7F`)을 보낸다. 버튼 모드의 live.text는 누를 때 bang을 내보내고, 패치가 그것을 `resync bang`으로 js에 넘긴다. FlowLocal이 마지막 PC, CC 118, CC 24–26 마지막 값을 다시 보내고, 장치가 그 씬을 맞춘다. 그 씬이 이미 돌고 있으면 다시 발사하지 않는다(`already on`). |

Resync를 뺀 모든 값은 세트와 함께 저장되고, 자동화 대상에서는 빠진다(Stored Only). Resync 버튼은 저장도 자동화도 안 된다(Hidden). 그래서 세트를 열 때 저장된 누름이 다시 실행되는 일이 없다.

**상태 줄 읽기**

- `waiting for Live` / `no program yet` — 장치가 로드될 때마다 패치가 두 줄을 이 글자로 되돌린다(js 없이도 된다). 세트를 열고 몇 초가 지나도 `ready`로 바뀌지 않으면 js를 못 찾은 것이다(1장 7번, Max Console).
- `ready - scenes 0-6` — 준비 완료, 세트에 씬 7개(0–6번). 로드 뒤 자동으로 보내는 요청은 이 줄을 바꾸지 않는다.
- `fired 12:34:56` — 장치가 씬을 발사할 때마다 아랫줄이 이 글자로 바뀐다. Autostart가 켜져 있으면 로드 1.5초 뒤에 바로 이렇게 되므로, `ready`가 잠깐만 보이는 게 정상이다.
- `PC 3 ch1 -> 3: ARTIST C / Title` — 마지막 PC와 발사한 씬(번호: 씬 이름)
- `CC118=3 ch1 -> 3: ARTIST C / Title` — Alt CC가 FlowLocal의 CC 118 미러로 발사한 씬. `ch` 뒤 숫자는 장치가 받은 채널이다. FlowLocal은 채널 16으로 보내지만 Live가 채널 1로 합치면 `ch1`로 보인다. Live가 PC도 넘겨주는데 Alt CC를 켜 두면, K4마다 PC가 먼저 발사하고 이 줄이 `(already on)`으로 끝난다. 정상이다.
- `autostart PC 1 -> 1: ARTIST A / Title` — Autostart가 Start PC로 발사한 씬
- `resync request sent 12:34:56` — Resync 버튼으로 요청을 보냈다. FlowLocal이 답하면 윗줄이 `PC n ch1 -> …`로 바뀐다(이미 그 씬이면 `(already on)`).
- `... (already on)` — 그 씬이 이미 돌고 있어서 다시 발사하지 않았다(아래 참고)
- `... scene 7 missing (scenes 0-6)` — 그 번호의 씬이 없다. 씬 개수나 Offset을 확인한다.
- `... ignored (Enable off)`

**같은 PC가 다시 올 때** — 재연결, 재동기화 요청에 대한 답, 또는 Alt CC를 켰을 때 PC 바로 뒤에 오는 CC 118 미러 때문에 같은 프로그램이 또 오면, 아래가 모두 맞을 때만 다시 발사하지 않는다. 음악이 처음부터 재시작되지 않게 하려는 것이다.

1. 이 장치가 마지막으로 발사한 씬과 같다
2. 트랜스포트가 돌고 있다
3. 그 씬 줄에 클립이 있는 트랙 중 하나라도 아직 그 클립을 재생 중이거나 대기 중이다. 클립이 하나도 없는 줄(정지용 빈 씬)은 3번을 따지지 않는다.

그래서 누가 마우스로 다른 씬을 틀었거나 Stop All Clips를 눌렀다면, 같은 PC가 와도 다시 발사해서 패널과 맞춘다.
예외 하나: 트랜스포트가 멈춘 상태에서 클립이 하나도 없는 씬이 오면 2번이 맞지 않아, Alt CC가 켜져 있으면 PC와 CC 118이 그 씬을 한 번씩 발사한다. 빈 씬을 한 번 더 발사해도 들리는 것은 같다.

**Live만 재시작했을 때** — Live를 껐다 켜도 USB 연결은 그대로라 FlowLocal은 스스로 PC를 다시 보내지 않고, 아무도 안 만지는 패널도 보낼 것이 없다. 그래서 장치가 직접 묻는다. 세트가 열려 장치가 준비되면(live.thisdevice) 0.5초 뒤 컨트롤 트랙의 MIDI To로 재동기화 요청을 한 번 보낸다. FlowLocal이 마지막 PC와 CC 118을 다시 보내면 장치가 그 씬을 발사하고, 1.5초에 도는 Autostart는 이미 온 PC를 보고 그 씬을 유지한다. 그 뒤에 오는 CC 24–26(패널이 마지막으로 보낸 값)이 매핑된 파라미터를 패널의 노브 위치로 옮긴다(Takeover None일 때, 2장). 세트에 저장돼 있던 값이 무엇이든, 패널이 어느 패턴과 노브 위치에 있든 소리가 처음부터 맞는다.

답이 오지 않으면 Autostart가 Start PC 1의 씬을 튼다. 패널이 다른 패턴에 있었다면 누가 K4를 돌리거나 대기 복귀(3분, 패턴 1)할 때까지 어긋난다. 답이 오지 않는 경우는 둘이다.

- FlowLocal이 켜진 뒤 패널이 PC를 한 번도 보내지 않았다. 상태 페이지의 `Current pattern (program)`이 `none yet`이고, **Send current pattern to Live**를 누르면 `No pattern yet: …`이 뜬다. 패널은 세션이 붙을 때 현재 PC를 보내므로, 패널이 붙어 있으면 보통 이미 있다.
- 요청이 FlowLocal에 닿지 않았다. 컨트롤 트랙의 MIDI To(Patternflow Exhibit, Ch. 16), Live 설정의 Out Track, 장치 Enable을 본다. 세트를 열 때마다 `http://192.168.66.1/api/status`의 `midi.resync_requests`가 1씩 늘어야 한다. 고친 뒤 **Resync**를 누르거나, 폰으로 상태 페이지의 **Send current pattern to Live**를 누르면 바로 맞춰진다.

Live는 세트와 장치를 불러오기 전에 포트부터 연다. 그래서 FlowLocal이 1, 2번 이유(맨 위 목록)로 보낸 PC가 장치가 생기기 전에 도착해 사라질 수 있다. 장치가 로드 뒤에 스스로 묻기 때문에 이것도 위와 같이 맞춰진다.

## 4. K1–K3 매핑 (Ctrl-M)

패널은 K1/K2/K3를 **CC 24/25/26**, 0–127 절대값으로 보낸다. 이 값은 **노브 위치 그 자체**다. 패널은 노브마다 위치 하나(0–1)를 기억하고, 그 위치로 지금 떠 있는 패턴을 그리고(파라미터 = 그 패턴 범위의 아래 끝 + 위치 × 범위), 같은 위치를 round(위치 × 127)로 보낸다. 그래서 관람객이 보는 장면과 듣는 소리가 한 숫자에서 나온다(루트 `README.md` 1장 "노브 위치").

- **시작과 대기 복귀** — 패널이 켜질 때 세 노브는 가운데(0.5 = **CC 64**)에서 시작하고, 세션이 붙으면 PC 바로 뒤에 CC 64 세 개가 간다. 3분 동안 아무도 안 만지면 세 노브가 가운데로 돌아가고, 가운데가 아니던 노브만 **CC 64**를 보낸다(이미 가운데면 보낼 것이 없다). 패널이 1번이 아닌 패턴에 있었으면 그다음 프레임에 PC 1이 간다. 그래서 매핑 범위의 가운데가 곧 "처음 소리"다. 단, Live 안에서 그 파라미터를 움직이는 것이 없을 때다(2장 Takeover, 3장 씬 템포).
- **범위** — 끝에서 끝까지 **32칸**(약 1바퀴 반, 한 칸에 CC 약 4). 끝에 닿으면 그림과 소리가 같이 멈춘다. 칸 수는 패널 펌웨어의 `PF_EXHIBIT_KNOB_DETENTS`(다시 빌드)로만 바뀌고, 바꾸면 그림과 소리가 같이 바뀐다. **패널 MIDI 페이지의 Sensitivity(×4 등)와 Knob value / Relative steps는 이 빌드의 K1–K3에 아무 영향이 없다.**
- **K4로 패턴을 바꿀 때** — 노브 위치는 그대로다. CC 24–26은 한 줄도 나가지 않고(값이 안 바뀌었으니) PC만 나간다. 새 패턴은 같은 노브 위치의 모습으로 나온다. 씬이 바뀌어도 매핑된 파라미터는 제자리다(씬 템포나 클립 자동화가 그 파라미터를 움직이지 않는 한. 3장).
- **세션이 다시 붙을 때**(패널 재부팅, FlowLocal 재시작) — 패널이 PC 바로 뒤에 CC 24, 25, 26 현재 값을 보낸다. **Live만 재시작하거나 USB가 다시 붙을 때**는 FlowLocal이 PC(와 CC 118) 뒤에 패널이 마지막으로 보낸 CC 24–26을 다시 보낸다(맨 위 목록). 어느 쪽이든 Takeover None(2장)이면 Live 파라미터가 패널의 노브 위치로 맞춰진다. Value Scaling이면 그 사이 Live 쪽에서 바뀐 파라미터는 맞춰지지 않을 수 있다.
- **방향** — Live 매핑에서 **Min > Max**로 뒤집는다. 패널에는 노브별 방향 설정이 없다(패턴마다 방향이 다르면 K4 전환 때 CC가 튀기 때문에 일부러 두지 않았다). 그림 쪽 방향은 작가가 패턴 안에서 정한다.

1. **Ctrl-M**(MIDI Map 모드)
2. Live에서 파라미터 클릭 → 패널 노브를 조금 돌리기 → 다음 파라미터 …
   매핑하는 동안 K4는 돌리지 않는다. PC 바로 뒤에 오는 CC 118(채널 16)이 클릭해 둔 파라미터에 매핑될 수 있다. Ctrl-M으로 매핑된 CC는 트랙으로 가지 않아서, Alt CC도 그 CC를 못 읽게 된다.
3. 왼쪽 매핑 목록에서 **Min / Max**로 움직일 범위를 좁힌다(Min > Max로 두면 반대 방향).
4. **Ctrl-M**으로 빠져나오기

예시:

- **K1 → 템포**(컨트롤 바의 템포 칸). Min 100 / Max 130 BPM처럼 좁게 잡는다. 전체 범위면 과하다.
- **K2 → 마스터 트랙 Auto Filter의 Frequency.** 예: Min 300 Hz / Max 18 kHz
- **K3 → 레이어 페이드 매크로.** 레이어 트랙들을 한 랙(Instrument/Audio Effect Rack)의 체인으로 두고, 매크로 하나에 체인 볼륨들을 매핑한다. 레이어마다 매크로 매핑 범위를 반대로 두면 크로스페이드가 된다. 그 매크로를 Ctrl-M으로 K3에 매핑한다.

**범위는 가운데가 시작 소리가 되게, 양 끝 어디에 있어도 들을 만하게 잡는다.** 대기 복귀(3분)는 CC를 64로 되돌리지만, 그 전까지 Live 파라미터는 마지막 관람객이 둔 자리에 그대로 있다. 필터를 끝까지 닫아 소리가 거의 안 나는 상태로 떠나면 3분 동안은 그대로다. 예시의 Min 300 Hz가 그 안전장치다.

매핑은 세트 전체에 한 벌이다. 씬이 바뀌어도 K1–K3는 같은 대상을 움직인다. 노브를 얼마나 돌려야 끝에서 끝까지 가는지는 Live가 아니라 패널이 정한다(32칸, 위 "범위").

**누름 소리** — K1–K3를 누르면 노트 60–62가 나간다. 컨트롤 트랙에는 악기를 두지 않는다(장치 출력은 FlowLocal로 가고, Thru는 꺼져 있다). 소리로 쓰려면 MIDI 트랙을 하나 더 만들어 MIDI From **Patternflow Exhibit**, **Ch. 1**, Monitor **In**으로 두고 악기를 올린다. Ch. 1로 두면 FlowLocal의 CC 118(채널 16)이 악기로 가지 않는다. 악기는 Live 기본 악기(Simpler, Drum Rack 등)를 쓴다. VST 악기는 같은 채널 1로 오는 Program Change를 받으면 프리셋이 바뀔 수 있다.

### 발소리 노트 (HEOEUNBIN)

네 번째 패턴(도마뱀)이 걸을 때, **발이 땅에 닿는 그 프레임에** 패널이 노트를 하나 보낸다. Live가 박자를 세는 것이 아니라 패널이 그림과 함께 알려 주는 것이라, 관람객이 K3(속도)를 어디로 돌려 놓든 샘플이 발에 붙는다. 다른 다섯 패턴은 이 노트를 보내지 않는다.

| | |
|---|---|
| 채널 | **10** (드럼 채널). 노브 CC 24–26, 누름 노트 60–62, Program Change는 그대로 채널 1이다 |
| 노트 | **36**(Live 표기 **C1**) = 앞쪽 앞발 + 먼쪽 뒷발, **38**(**D1**) = 앞쪽 뒷발 + 먼쪽 앞발. 둘이 번갈아 온다. 두 패드에 다른 샘플을 넣어도 되고, 같은 샘플을 넣어도 된다 |
| 벨로시티 | 100 고정. 발소리는 사건이지 연주가 아니라서 세기를 바꾸지 않는다 |
| 길이 | note-on 40 ms 뒤 note-off. 원샷 샘플이면 무시해도 된다(신스 패드에 쓸 때만 필요해서 있다) |
| 빈도 | 초당 **3 × Speed**번, 번갈아 36, 38, 36, 38… K3 끝에서 0.6번/초, 가운데(켤 때·대기 복귀) 2.1번/초, 반대 끝 7.2번/초. 숫자와 BPM 환산은 루트 [`PATTERN_KNOBS.md`](../PATTERN_KNOBS.md) HEOEUNBIN |

**트랙 만들기**

1. MIDI 트랙을 하나 더 만든다(이름 예: `PF Footsteps`). 컨트롤 트랙에는 올리지 않는다(3장: 그 트랙의 출력은 FlowLocal로 간다).
2. MIDI From: **Patternflow Exhibit**, **Ch. 10**. MIDI To: **No Output**.
3. Monitor: **In** (Arm 없이도 입력이 악기까지 간다).
4. 악기: **Drum Rack**. C1과 D1 패드에 발소리 샘플을 넣는다. 한 소리로 쓰려면 C1 패드의 체인을 D1로 복사한다 — 패드는 음정을 올리지 않아서 두 노트가 같은 소리가 된다. Simpler 하나에 매핑하면 D1이 두 반음 높게 재생된다.
5. 클립은 두지 않는다. 씬이 바뀌어도(K4) 이 트랙은 그대로 둔다. 패널이 다른 패턴에 있으면 노트 자체가 오지 않으므로, 늘 켜 둬도 조용하다.

**씬 장치와 섞이지 않게** — 채널 하나로 양쪽이 막힌다.

- 이 트랙을 **All Channels**로 두면 채널 1의 Program Change가 악기까지 가서 VST 악기의 프리셋을 바꾸고, 누름 노트 60–62도 같이 울린다. **Ch. 10**으로 둔다.
- 반대 방향은 걱정할 것이 없다. 컨트롤 트랙은 All Channels라 발소리도 들어가지만, `PF Exhibit Scenes` 장치는 노트를 읽지 않는다(Program Change와 Alt CC만). 씬은 발소리에 발사되지 않는다. 컨트롤 트랙에 악기를 올리지 않는 규칙(3장)은 그대로다.
- 다른 악기 트랙의 MIDI From은 **No Input**(3장). All Ins + Arm이면 발소리가 그 악기에서도 울린다.
- 패널 `/midi` 페이지에서 MIDI를 끄면 노브·패턴과 함께 발소리도 멈춘다. 같은 RTP 세션, 같은 USB 포트, 같은 tx 카운터다.

**몇 ms를 맞추는 법 (Track Delay)**

믹서의 **Track Delay** 칸(세션 뷰 오른쪽 아래의 표시 스위치 중 **Delay**를 켜면 트랙마다 한 칸씩 나온다. 안 보이면 View 메뉴)은 트랙 출력을 ms 단위로 앞뒤로 민다. 패널 → FlowLocal → USB → Live까지 몇 ms가 걸린다(실측은 재동기화 요청 왕복 10 ms 안, 루트 [`README.md`](../README.md) "실측 기록"). 45 fps면 한 프레임이 22 ms이니 링크 지연은 한 프레임 안쪽이다.

- **음수 지연으로 링크 지연을 되돌릴 수는 없다.** 실시간으로 들어오는 입력을 Live가 미리 알 수는 없다. 실제로 줄일 수 있는 것은 Live 오디오 설정의 버퍼 크기와 샘플 앞의 묵음이다(Drum Rack 패드의 Simpler에서 시작점을 발소리 바로 앞으로 당긴다).
- Track Delay는 **발소리를 세트의 다른 소리와 맞추는 데** 쓴다. 발소리가 음악보다 앞서 들리면 이 트랙에 양수 지연을, 뒤처져 들리면 다른 트랙들에 같은 크기의 양수 지연을 준다.
- 처음에는 0으로 두고, 실기에서 패널을 보며 5 ms 단위로 움직인다. 값은 세트에 저장되고 자동화 대상이 아니다.

**확인** — `python tools/midi_monitor.py`에 `NoteOn   ch10  36 C2   vel 100`이 찍힌다. 모니터는 C4 = 60으로 이름을 붙이고 Live는 C3 = 60으로 붙여서, 같은 노트를 모니터는 `C2`, Live는 `C1`이라고 쓴다. 숫자 36/38로 보면 헷갈리지 않는다. 하드웨어 없이 흉내 내려면 `python tools/send_test.py --port "PF Test" --channel 10 note 36 --velocity 100`(5장).

### 전시 전 한 번 — 패널 쪽 점검

전시 펌웨어의 MIDI 기본값(MIDI 켬, FlowLocal `192.168.66.1` 초대)은 **패널에 저장된 값이 없을 때만** 쓰인다(K1–K3를 노브 위치로 보내는 것은 설정이 아니라 전시 이미지에 박혀 있어서 저장된 값과 상관없다. `/api/status`의 `midi.outLaneAbs: true`). 예전 펌웨어(Audio·MIDI 에디션 등)에서 MIDI 페이지로 무언가 저장한 패널은 전시 펌웨어를 올려도 그 값을 그대로 쓴다. 다른 호스트 주소가 남아 있거나 주소를 지운 채 저장했다면, 패널은 FlowLocal을 초대하지 않아서 Live에 아무것도 오지 않는다. 노브 방향·클릭 크기와 패널에 저장된 Wi-Fi도 마찬가지다.

점검 항목(패널 MIDI 페이지, 노브 페이지, 저장된 Wi-Fi, 자막, 밝기)과 패널 페이지를 여는 방법은 한곳에만 적는다: **루트 [`README.md`](../README.md) 7장 "전시 전 점검표"**, 패널 주소는 같은 문서 3장 "폰으로 패널 페이지 열기"(FlowLocal 상태 페이지의 `Panel: …` 링크).

## 4.5 다른 컴퓨터에서 이어서 작업하기 (노트북)

세트는 레포 안(`set Project/`)에 있고, 쓰는 소리는 전부 Live 기본 악기(Collision, Drift, Wavetable, Dream Keys)와 이 폴더의 M4L 장치뿐이다. 바깥 샘플을 참조하지 않으므로 레포만 받으면 그대로 열린다.

받는 쪽 준비물:

- **Live 12 Suite**와 Core Library. Collision·Wavetable은 Suite 장치라 Intro/Standard에서는 트랙이 비어서 열린다. Max for Live도 Suite에 들어 있다(장치는 Freeze 상태라 Max를 따로 만질 일은 없다).
- git. 비공개 레포라 로그인이 필요하다.

```bash
git clone https://github.com/engmung/patternflow-exhibition-2026.git
```

그 다음 `set Project/set.als`를 연다. 패널과 FlowLocal이 없으면 MIDI 트랙 입력에 `Patternflow Exhibit`이 없다고 나오지만, 세트는 그대로 열리고 소리 작업은 다 된다. 하드웨어를 붙이는 날 2장의 Live 설정(입력 Track·Remote 켜기, 출력 Track만)을 그 컴퓨터에서 한 번 해 주면 된다. **Live 설정은 세트가 아니라 컴퓨터에 저장된다.**

작업한 뒤:

```bash
git add "set Project" && git commit -m "ableton: ..." && git push
```

- **샘플을 새로 가져다 썼으면** 커밋 전에 File → **Collect All and Save**를 한다. 그래야 샘플이 `set Project/Samples/`로 들어와 다른 컴퓨터에서도 소리가 난다.
- `set Project/Backup/`은 레포에 안 올라간다(Live가 자동으로 쌓는 폴더).
- 두 컴퓨터에서 번갈아 작업하면 시작 전에 `git pull`부터 한다. `.als`는 압축 파일이라 합쳐지지 않고, 나중에 올린 쪽이 통째로 덮어쓴다.

## 5. 하드웨어 없이 테스트 (loopMIDI)

패널과 FlowLocal 없이 노트북 하나로 장치와 매핑을 먼저 확인한다.

1. **loopMIDI**(Tobias Erichsen, 무료)를 설치하고 포트 `PF Test`를 추가한다.
2. Live 설정: **In: PF Test** → Track ON, Remote ON. **Out: PF Test** → 전부 OFF(루프 방지).
3. 컨트롤 트랙의 MIDI From을 잠시 `PF Test`로, MIDI To를 **No Output**으로 바꾼다. loopMIDI 포트는 보낸 것이 그대로 다시 들어오는 고리다. MIDI From과 MIDI To를 같은 loopMIDI 포트로 두면 장치의 요청이 되돌아오고, 거기에 Thru까지 켜면 메시지가 끝없이 돈다.
4. 터미널에서 (`exhibition_2026` 폴더, `pip install python-rtmidi` 필요):

```bash
python tools/send_test.py --list
python tools/send_test.py --port "PF Test" note 60                     # 먼저 노트 하나(9장 1번의 dump 비교용)
python tools/send_test.py --port "PF Test" pc 1                        # 씬 1 (가장 먼저 확인할 것, 9장 1번)
python tools/send_test.py --port "PF Test" pc 1 2 3 4 5 6 --interval 4 # 작가 씬 한 바퀴
python tools/send_test.py --port "PF Test" pc 3 3                      # 클립이 돌고 있으면 두 번째는 (already on)
python tools/send_test.py --port "PF Test" altcc 2                     # CC 118 = 2, 채널 16. Alt CC 켬: 씬 2 / 끔: 아무 일 없음. 상태 줄의 ch1/ch16을 적어 둔다(9장 1번)
python tools/send_test.py --port "PF Test" pc 4 --mirror               # FlowLocal처럼 PC 뒤에 CC 118. Alt CC를 켜도 발사는 한 번
python tools/send_test.py --port "PF Test" demo --hold 8               # 패널 흉내: 부팅 1 → K4로 2…6 → 1, K1–K3 스윕
```

5. 매핑 연습: Ctrl-M → 파라미터 클릭 → `python tools/send_test.py --port "PF Test" cc 24 64` → Ctrl-M 해제. 이어서 `python tools/send_test.py --port "PF Test" sweep --cc 24`로 움직임을 본다. 마우스로 파라미터를 옮겨 놓고 `cc 24 64`를 다시 보내면 Takeover None에서는 파라미터가 바로 그 값으로 간다.
6. 재동기화 요청이 장치에서 나가는지 본다. loopMIDI에 포트 `PF Resync`를 하나 더 만든다. Live 설정에서 **Out: PF Resync** → Track ON(In: PF Resync는 전부 OFF). 컨트롤 트랙의 MIDI To를 `PF Resync`, Ch. 16으로 바꾼다. 터미널에서 모니터를 켠다(loopMIDI 포트는 여러 프로그램이 같이 열 수 있다).
   ```bash
   python tools/midi_monitor.py --port "PF Resync" --raw
   ```
   장치의 **Resync**를 누르면 `CC ch16 #119 = 127 (resync request to FlowLocal) | BF 77 7F`가 한 줄 찍힌다. 장치를 트랙에서 지웠다가 다시 올리면 0.5초쯤 뒤에 같은 줄이 한 번 찍힌다. 채널이 16이 아니거나 아무것도 안 찍히면 9장 2번이다.
7. 끝나면 컨트롤 트랙을 MIDI From **Patternflow Exhibit**(All Channels), MIDI To **Patternflow Exhibit**(Ch. 16)로 되돌린다.

`send_test.py`는 하드웨어에 닿을 수 있는 포트(이름에 patternflow/flowlocal이 들어가거나, 컴퓨터 이름으로 된 rtpMIDI 세션)로는 `--allow-device` 없이는 보내지 않는다. `resync`와 `altcc`는 `--channel`과 상관없이 채널 16으로 보낸다.

## 6. 현장 점검 — 들어오는 MIDI 보기

```bash
python tools/midi_monitor.py            # "Patternflow Exhibit" 자동 선택
python tools/midi_monitor.py --raw      # 바이트도 같이
```

K4를 돌리면 `PROGRAM`과 바로 뒤에 `CC ch16 #118 = n (program mirror from FlowLocal)`, K1–K3를 돌리면 `CC #24–26`이 시각, 간격, 초당 개수와 함께 찍힌다. Ctrl-C를 누르면 요약이 나온다. FlowLocal USB를 뽑으면 `port disappeared`, 다시 꽂으면 `port back`이 뜬다.

Live 없이 FlowLocal이 재동기화 요청에 답하는지 보려면, 모니터를 켜 둔 채 다른 터미널에서 요청을 보낸다. `PROGRAM`과 `CC ch16 #118`, 이어서 `CC #24–26`(패널이 보낸 적 있는 노브만)이 한 번 더 찍히면 된다. FlowLocal이 켜진 뒤 패널이 PC를 보낸 적이 있어야 답한다. 같은 포트의 입력과 출력을 서로 다른 프로그램이 동시에 열 수 있는지는 Windows에서 확인하지 않았다.

```bash
python tools/send_test.py --port "Patternflow Exhibit" --allow-device resync
```

**Windows에서는 USB MIDI 포트를 보통 한 프로그램만 열 수 있다.** 모니터를 쓸 때는 Live를 끄고(또는 Live 설정에서 그 포트를 끄고), 끝나면 모니터를 닫은 뒤 Live를 켠다.

## 7. Windows 키오스크 체크리스트

목표는 전원만 켜면 아무도 손대지 않아도 소리가 나오는 것이다. 마지막에 반드시 **전원을 뽑은 상태에서 시작하는 전체 리허설**을 한다.

**자동 로그인**
- [ ] 가능하면 로컬 계정. 설정 → 계정 → 로그인 옵션 → "보안 향상을 위해 … Windows Hello 로그인만 허용" 끄기
- [ ] `Win+R` → `netplwiz` → "사용자 이름과 암호를 입력해야 이 컴퓨터를 사용할 수 있음" 체크 해제. 체크박스가 안 보이면 Sysinternals **Autologon** 사용
- [ ] 로그인 옵션 → "자리를 비운 경우 다시 로그인 요구" → **안 함**. 화면 보호기 끄기

**부팅하면 세트 열기**
- [ ] `Win+R` → `shell:startup` → 바로가기를 만든다. `.als` 연결 프로그램에 기대지 말고 Live 실행 파일과 세트를 직접 적는다. 경로는 실제 설치 위치로 바꾼다.
  `"C:\ProgramData\Ableton\Live 12 Suite\Program\Ableton Live 12 Suite.exe" "D:\Exhibit\PF Exhibit Project\PF Exhibit.als"`
- [ ] 오디오 장치가 늦게 잡혀 Live가 엉뚱한 출력으로 뜨면, 바로가기 대신 작업 스케줄러에서 "로그온할 때 + 30초 지연"으로 연다
- [ ] Live 설정 → Audio: 출력 장치를 고정한다. HDMI 등을 꽂아도 바뀌지 않는지 확인
- [ ] Live 설정에 자동 업데이트 항목이 있으면 끈다. 오프라인 상태로 Live를 한 번 켜서 인증 창이 안 뜨는지 확인

**절전 끄기** (관리자 권한 터미널 — PowerShell/cmd 둘 다 됨)
- [ ] 설정 → 시스템 → 전원: 전원 연결 시 화면·절전 **안 함**
- [ ] 아래를 한 줄씩 실행. 절전과 최대 절전을 끄고, 덮개를 닫아도 계속 돌게 하고(책상 아래에 닫아 둘 경우), USB 선택적 절전을 끈 뒤 적용한다. 모두 전원 연결(AC) 기준이다.
```
powercfg /change standby-timeout-ac 0
powercfg /change hibernate-timeout-ac 0
powercfg /hibernate off
powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0
powercfg /setacvalueindex SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0
powercfg /setactive SCHEME_CURRENT
```
- [ ] 장치 관리자 → 범용 직렬 버스 컨트롤러 → 각 **USB 루트 허브** → 전원 관리 → "전원을 절약하기 위해 컴퓨터가 이 장치를 끌 수 있음" 해제. 요즘 노트북은 powercfg 설정이 안 먹는 경우가 있어 이쪽까지 한다.

**업데이트·알림**
- [ ] 설정 → Windows 업데이트 → **업데이트 일시 중지**(최대 5주, 전시 기간이 덮이는지 확인). 사용 시간도 전시 시간으로
- [ ] 설정 → 시스템 → 알림 → **방해 금지** 켜기
- [ ] 이 구성에서 노트북은 인터넷이 필요 없다. 전시장에서는 Wi-Fi에 붙이지 않는 편이 업데이트 걱정이 적다.

**전원**
- [ ] 항상 충전기 연결. 전원 모드 "최고 성능", 배터리 절약 모드 자동 켜짐 끄기
- [ ] (선택) BIOS에 "AC 연결 시 자동 켜짐"이 있으면 켠다. 정전 뒤 복구용

**크래시 복구 테스트**
- [ ] Live가 도는 중에 작업 관리자로 **강제 종료** → 바로가기(또는 재부팅)로 다시 연다 → 어떤 창이 뜨는지 본다. 복구 대화상자는 누가 눌러야 넘어간다. 어떤 버튼을 누르면 전시 상태로 돌아오는지 확인해서 **스태프 안내문(`STAFF_CARD.md`)의 빈칸에** 적는다.
- [ ] 복구 후 장치 아랫줄이 `ready …` 또는 `fired 시:분:초`가 되고 소리가 나는지 본다
- [ ] 패널을 K4로 3번 패턴에 둔 채 Live만 종료 → 다시 연다 → 씬 3으로 시작하는지 본다(3장 끝의 재동기화 요청). 씬 1로 시작하면 FlowLocal `/api/status`의 `midi.resync_requests`가 늘었는지 본다
- [ ] Live가 도는 중에 FlowLocal USB를 뽑았다 꽂기 → Live가 포트를 다시 잡는지, K4가 다시 씬을 바꾸는지 본다

**스태프 안내문** — 루트의 [`STAFF_CARD.md`](../STAFF_CARD.md)가 한 장짜리 안내문이다(켜고 끄는 순서, 소리가 안 날 때, 화면이 멈췄을 때, 하지 말 것). 위 테스트로 확인한 뒤 빈칸(Live 복구 창의 버튼, 연락처 등)을 채워 인쇄한다.

## 8. 문제 해결

| 증상 | 확인 |
|---|---|
| 세트를 열어도 아랫줄이 `ready …`/`fired …`로 안 바뀌고 `waiting for Live`에 머묾 | js를 못 찾음 → Max Console 확인. `.amxd` 옆에 js를 두거나 Freeze(1장) |
| 포트는 목록에 있는데 모니터에 아무것도 안 찍힘 / FlowLocal 페이지에 `Panel not connected` | 패널이 FlowLocal을 초대하지 않는다 → 패널 MIDI 페이지: MIDI on, Computer to invite `192.168.66.1`(루트 `README.md` 7장 점검표). FlowLocal 페이지가 `Panel: unknown`이면 그 아래 목록의 **MIDI** 링크로 연다. 패널이 PatternflowLocal Wi-Fi에 붙었는지 |
| 모니터에는 PROGRAM이 찍히는데 씬이 안 바뀜 | Live In 포트 **Track** ON? 컨트롤 트랙 MIDI From / Monitor **In**? 장치 Enable? 상태 줄에 `missing`이 뜨면 씬 개수·Offset. 편집기의 `dump`가 `programs=0`인데 `altccIn`이 늘면 Live가 PC를 안 넘겨주는 것이다 → **Alt CC** 켜기(9장 1번) |
| Live만 재시작했더니 씬이 패널과 다름 | 요청이 FlowLocal에 안 닿았다: 컨트롤 트랙 MIDI To = Patternflow Exhibit **Ch. 16**, Live Out 포트 **Track** ON, Enable. FlowLocal `/api/status`의 `midi.resync_requests`가 세트를 열 때마다 느는지. 늘었는데도 그대로면 상태 페이지의 `Current pattern (program)`이 `none yet`인지(K4 한 번). 당장은 장치의 **Resync** 또는 상태 페이지 **Send current pattern to Live** (3장 끝) |
| Resync를 눌러도 반응이 없어 보임 | 위 줄과 같이 확인. 답이 와도 윗줄이 전과 같은 글자일 수 있다(이미 `(already on)`이었을 때). 편집기의 `dump`에서 누를 때마다 `programs`(Alt CC를 쓰면 `altccIn`)가 느는지 본다. `resync_requests`는 느는데 이 숫자가 그대로면 FlowLocal의 답이 장치까지 오지 않는 것이다 |
| Alt CC를 켰는데 K4에 반응 없음 | Alt CC number가 118인지. CC 118을 Ctrl-M으로 매핑해 버리지 않았는지(매핑 목록 확인, 4장). 컨트롤 트랙 MIDI From이 **All Channels**인지(Ch. 1이면 채널 16의 미러가 트랙에서 걸러진다). FlowLocal 상태 페이지의 `Program mirror to Live`가 `off`가 아닌지, Live를 끄고 `midi_monitor.py`에서 `PROGRAM` 뒤에 `CC ch16 #118`이 찍히는지(안 찍히면 FlowLocal이 미러를 끈 빌드) |
| FlowLocal `/api/status`의 `midi.usb_in_discarded`가 계속 늘어남 | 노트북이 요청 말고도 무언가를 FlowLocal로 보내고 있다: 장치 **Thru** 켜짐, 1장 5번에서 기본 midiin/midiout을 안 지움, Live Out 포트 **Remote**나 **Sync** 켜짐, 다른 트랙의 MIDI To가 Patternflow Exhibit. 버려지므로 고장은 아니지만 끈다 |
| 씬이 하나씩 밀려서 바뀜 | 맨 위 씬이 0번이다(3장). 씬 순서나 Offset 확인 |
| 씬이 바뀌긴 하는데 늦음 | Global Launch Quantization이 None인지(3장), 클립의 Launch Quantization이 Global인지 |
| 노브가 매핑이 안 됨 | Live In 포트 **Remote** ON? Ctrl-M 중에 패널 노브를 실제로 돌렸나 |
| 노브를 돌려도 파라미터가 안 움직이거나 64 근처에서만 까딱임 | Takeover Mode가 **None**인지(Pick-Up이면 노브 값이 파라미터 값을 지나야 움직이고, Value Scaling이면 Live 쪽에서 바뀐 파라미터가 노브 끝에서야 패널 위치와 만난다). 패널이 전시 이미지인지(`/api/status`의 `midi.outLaneAbs: true`. 전시 이미지는 MIDI 페이지의 Relative steps와 상관없이 위치를 보낸다). 같은 파라미터에 다른 매핑·자동화가 걸려 있지 않은지. Out 포트 Remote는 꺼 두지만(2장), FlowLocal이 노트북에서 온 CC를 패널로 넘기지 않아서 지금 구성에서는 원인이 아니다 |
| K4로 패턴을 바꿀 때 파라미터가 튐 | 패널은 K4 전환 때 CC 24–26을 보내지 않는다(4장). `midi_monitor.py`에 전환 순간 `CC #24–26`이 찍히면 전시 이미지가 아니다. 안 찍히는데 튀면 씬 쪽이다(씬 템포, 클립 자동화, 씬마다 다른 체인) |
| 그림은 끝에서 멈췄는데 소리는 더 움직임(또는 반대) | 전시 이미지가 아니다(`/api/status`에 `midi.outLaneAbs`가 없음). 전시 이미지는 그림과 소리를 같은 칸에서 같이 멈춘다. 콘솔 홈의 노브 슬라이더를 움직였다면 그 노브를 한 칸 돌리면 다시 맞는다(슬라이더를 5초 안에 움직였다면 그 5초 뒤. 루트 `README.md` 8장 "알고 둔 채 넘어간 것"). Live 쪽이면 Takeover None인지, 씬 템포·클립 자동화가 그 파라미터를 움직이지 않는지 |
| 매핑한 파라미터가 확 튐 | Takeover None에서는 Live 쪽에서 바뀐 파라미터(세트를 다시 엶, 마우스, 씬 템포, 클립 자동화)가 다음 CC에 패널 위치로 돌아가며 튄다. 의도된 동작이다(2장). 자주 튀면 그 파라미터를 움직이는 씬 템포·자동화를 없앤다. 튀지 않는 것이 더 중요하면 Value Scaling(대신 같은 노브 위치에서 소리가 다를 수 있다) |
| 모니터가 포트를 못 엶 | Live(또는 다른 프로그램)가 포트를 잡고 있다 → Live 종료 |
| 포트가 목록에 없음 | FlowLocal이 DevKit의 **USB** 포트(COM 아님)에 꽂혔나. 다른 USB 포트에 꽂아 이름이 바뀌었나(2장) |
| 누름 노트가 안 들림 | 악기를 컨트롤 트랙이 아니라 별도 트랙(MIDI From Patternflow Exhibit **Ch. 1**, Monitor In)에 두었는지(4장 "누름 소리") |
| 발소리가 안 들림 | 패널이 **HEOEUNBIN(4번 패턴)**에 있나(다른 패턴은 안 보낸다). 발소리 트랙 MIDI From이 **Ch. 10**, Monitor **In**인가. 드럼 랙의 **C1·D1** 패드에 샘플이 있나(36·38). `midi_monitor.py`에 `NoteOn ch10 36`이 찍히나 — 안 찍히면 패널 `/midi`가 꺼졌거나 세션이 끊긴 것이다(노브 CC도 같이 안 올 것이다) |
| 발소리 트랙에서 VST 프리셋이 바뀌거나 누름 소리가 같이 남 | 그 트랙의 MIDI From이 All Channels다 → **Ch. 10**(4장 "발소리 노트") |
| 발이 닿는 것과 소리가 어긋남 | 몇 ms는 링크 지연이라 Live에서 되돌릴 수 없다. 샘플 앞의 묵음과 오디오 버퍼 크기를 먼저 본다. 크게 어긋나면 발소리가 아니라 세트 쪽이다(Track Delay, 4장) |
| 노트가 두 번 들림 | 누름용 트랙이 둘이거나, 그 트랙이 All Ins + Arm으로도 받고 있다 |
| K1–K3를 누르면 VST 악기 프리셋이 바뀜 | 같이 흘러간 Program Change 때문 → Live 기본 악기를 쓴다(4장) |

## 9. 확인 못 한 것 (하드웨어·Live 없이 만들었음)

이 장치는 Live와 Max 없이, 기존 Bridge 패치 형식과 문서를 참고해서 만들었다. 검증한 것은 `.maxpat` JSON 파싱과 구조 검사(연결선, 인렛/아웃렛 번호, 파라미터 이름, 프레젠테이션 크기), js 문법(`node --check`, acorn ES5), 그리고 Max/Live를 흉내 낸 Node 테스트다. 이 테스트는 바이트 파서(running status, real-time 바이트, sysex, FlowLocal의 상태 응답 SysEx), 초기화 전 보류, 범위 검사, Offset(Start PC 포함), 중복 발사 방지, Autostart, 채널을 보지 않는 Alt CC(Live가 입력을 채널 1로 합칠 때와 채널을 그대로 둘 때 둘 다), dump 카운터, 재동기화 요청(로드 뒤 0.5초에 한 번, 버튼 모드 live.text가 보내는 `resync bang`, Enable), Thru 게이트, PC와 CC 118 미러가 같이 올 때 한 번만 발사하는지, 요청에 답하는 가짜 FlowLocal로 흉내 낸 Live만 재시작, 그리고 패치 배선(`midiin → deferlow → js`, `midiin → gate → midiout`, `js → midiout`, 로드 때 상태 줄 되돌리기, 설정 이름·기본값·메시지 짝)을 다룬다. 테스트가 실제로 실패할 수 있는지는 코드를 일부러 망가뜨려 확인했다. 실제 Live에서는 아래를 먼저 확인한다.

1. **Live가 MIDI 트랙 입력의 Program Change를 M4L MIDI Effect의 `midiin`까지 넘겨주는지.** 가장 중요하다. 5장의 loopMIDI 테스트에서 매핑하기 전에 `note 60`, 이어서 `pc 1`을 보내고 편집기(패칭 화면)의 `dump`를 누르면 Max Console에 한 줄이 찍힌다.
   - `bytes=8 programs=1`인데 씬이 안 바뀜 → 상태 줄을 본다(`missing`, `Enable off` 등).
   - `bytes=6 programs=0` → 노트는 오는데 PC가 안 온다. Live가 PC를 넘겨주지 않는 것이다. 장치의 **Alt CC를 켠다.** 펌웨어는 고치지 않는다. FlowLocal이 모든 PC 바로 뒤에 같은 번호를 CC 118(채널 16)로 보내고, Alt CC는 기본으로 그 CC를 읽는다. `python tools/send_test.py --port "PF Test" altcc 1`로 씬 1이 발사되는지 확인한다. 실제 FlowLocal에서는 K4를 돌릴 때마다 `dump`의 `altccIn`이 1씩 는다(Alt CC가 꺼져 있어도 센다).
   - 같은 `altcc 1`에서 상태 줄이 `CC118=1 ch1`인지 `ch16`인지 적어 둔다. `ch1`이면 Live가 채널을 합친 것이고(포럼에서 알려진 동작), 장치는 어느 쪽이든 읽는다.
   - `bytes=0` → 장치에 아무것도 안 온다. 트랙 MIDI From, Monitor In, Live 설정의 In Track을 확인한다. Ctrl-M으로 매핑된 메시지는 Live가 트랙으로 넘기지 않으니, 확인에는 매핑하지 않은 노트를 쓴다.

   `midiin`은 바이트를 하나씩 보내고, `[js]`가 그 호출을 저우선 큐로 넘길 때 순서를 지키는지는 Max 문서에 없다. 그래서 패치는 바이트를 `deferlow`(항상 큐 뒤에 붙여 순서를 지킨다고 문서화됨)로 먼저 넘긴 뒤 js에 준다. 이 순서 보장도 Max에서 직접 돌려 본 것은 아니다.
2. **장치의 재동기화 요청이 FlowLocal까지 가는지.** js가 [midiout]으로 내보낸 CC를 Live가 트랙의 MIDI To(외부 포트)로 내보내는지, 채널을 MIDI To의 Ch. 16으로 바꾸는지 그대로 두는지, 로드 뒤 0.5초면 출력이 준비돼 있는지. 먼저 5장 6번(`PF Resync` 모니터), 이어서 실제 FlowLocal에서 세트를 열 때와 Resync를 누를 때마다 `/api/status`의 `midi.resync_requests`가 1씩 느는지 본다. 안 되면 Live만 재시작했을 때는 상태 페이지의 **Send current pattern to Live**로 맞춘다.
3. **Resync** 버튼이 한 번 누를 때 요청을 한 번만 보내는지. Max 레퍼런스대로라면 버튼 모드의 live.text는 누를 때 왼쪽 아웃렛으로 bang을 내고(`transition` 기본값), `[prepend resync]`가 그것을 `resync bang`으로 바꾸고, js가 그 메시지를 요청으로 읽는다. 5장 6번 모니터에서 누를 때마다 `BF 77 7F`가 한 줄씩 찍히면 된다. 세트를 열 때 버튼이 bang을 내보내는지도 본다(내보내도 요청이 한 번 더 갈 뿐이다). js가 보내는 0/1로 `[gate 1]`이 열리고 닫히는지(Thru를 켜고 5장 6번 모니터에 `send_test.py`의 메시지가 보이는지, 끄면 사라지는지).
4. 붙여넣은 뒤 live.toggle/live.numbox 값이 세트와 함께 저장·복원되는지, 코멘트 상태 줄이 갱신되는지
5. "already on" 판정에 쓰는 트랙 속성(`playing_slot_index`, `fired_slot_index`, 클립 슬롯 `has_clip`)과 `is_playing`이 문서대로 값을 주는지, 특히 `fire` 직후 같은 흐름에서 읽어도 이미 바뀐 값을 주는지. 5장 `pc 3 3`, 마우스로 다른 씬을 튼 뒤 `pc 3`, Alt CC를 켜고 `pc 4 --mirror`(발사 한 번, 윗줄 `CC118=4 … (already on)`, 트랜스포트를 멈춘 상태에서도)로 확인한다. 값이 늦게 바뀌면 미러가 같은 씬을 한 번 더 발사한다. 퀀타이즈 중이면 같은 씬이 다시 대기할 뿐이지만, 퀀타이즈가 None이면 클립이 처음부터 다시 시작한다.
6. Freeze한 `.amxd`가 js 없이 다른 폴더에서도 도는지
7. Autostart(1.5초)와 로드 뒤 요청(0.5초)이 전시 세트 로드 시간(샘플 많을 때)에 충분한지. 요청의 답이 Autostart보다 늦으면 씬 1이 먼저 돌고 곧바로 패널의 씬으로 바뀐다.
8. 실제 포트 이름과 Windows 재열거 시 이름 변화, Live만 재시작했을 때 요청의 답으로 패널의 씬에서 시작하는지(3장 끝, 7장 크래시 복구 테스트)
9. 크래시 복구 대화상자의 실제 모습(7장)
10. 로드할 때 패치가 상태 줄을 `waiting for Live`로 되돌린 뒤 js가 `ready`로 바꾸는지. 테스트 후 저장한 `.amxd`를 js 없는 폴더에서 열어(Freeze 전) `waiting for Live`에 머무는지도 한 번 본다.
11. **CC 24–26 = 노브 위치**(패널 펌웨어, 2026-09-19. 호스트 테스트만 거침. 패널 쪽 확인 목록은 루트 `README.md` 8장 15번) — 세션이 붙을 때 PC 바로 뒤에 오는 CC 3개, 그리고 Live만 재시작했을 때 FlowLocal이 PC 뒤에 다시 보내는 CC 3개로 Live 파라미터가 패널 위치로 맞춰지는지. **Takeover None**(2장)에서 Live 쪽에서 바뀐 파라미터(마우스, 세트에 저장된 값)가 다음 CC에 한 번에 패널 위치로 가는지, 그 튐이 들을 만한지, 값이 같은 CC가 다시 와도 Live가 적용하는지. 대기 복귀 때 CC 64로 돌아가는 소리가 괜찮은지, 매핑 범위의 가운데가 시작 소리로 들을 만한지.
12. **발소리 노트**(패널 펌웨어, 2026-09-20. 호스트 테스트와 빌드만 거침. 패널에 굽지 않았다) — HEOEUNBIN에서 `midi_monitor.py`에 `NoteOn ch10 36`과 `38`이 번갈아 찍히는지, K3를 돌리면 빈도가 초당 3 × Speed로 따라오는지(끝 0.6 / 가운데 2.1 / 끝 7.2), 드럼 랙의 C1·D1이 울리는지, LED에서 발이 닿는 순간과 소리가 같이 오는지(몇 ms 어긋나면 4장 Track Delay). K4로 다른 패턴에 갔을 때와 패널이 잘 때 노트가 멈추고 내려가 있던 노트가 남지 않는지. 컨트롤 트랙(All Channels)에 발소리가 들어가도 씬이 발사되지 않는지.

개발 중 검증 명령 (`exhibition_2026` 폴더):

```bash
python -c "import json; json.load(open('ableton/pf-exhibit-scenes/pf_exhibit_scenes.maxpat', encoding='utf-8'))"
python tools/check_maxpat.py ableton/pf-exhibit-scenes/pf_exhibit_scenes.maxpat
node --check ableton/pf-exhibit-scenes/pf_exhibit_scenes.js
node tools/test_pf_exhibit_scenes.js      # ACORN_PATH=<acorn 패키지 폴더>를 주면 ES5 문법 검사도 함
```

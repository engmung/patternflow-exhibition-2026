# Patternflow Ensemble — 패널 여러 대를 함께

패널 8대, 10대, 그 이상을 허브 하나에 묶는다. 어느 패널이든 노브를 돌리면 다른 패널들이 따라오는데, 바로 따라오게 할 수도 있고 지도에 놓아 둔 자리의 거리만큼 늦게, 물결처럼 퍼지게 할 수도 있다. 패널마다 다른 패턴을 줄 수도 있다. 노트북은 USB로 허브에 붙어서 배치와 규칙을 정하고, 무슨 일이 일어나는지 본다.

2026년 전시의 구성(패널 → FlowLocal → 노트북)과 같은 모양이다. 허브는 그때의 FlowLocal 보드(ESP32-S3-DevKitC-1) 그대로 쓴다.

> **2026-11-11 첫 판, 하드웨어 없이 만들었다.** PC 테스트는 모두 통과하고 허브 펌웨어는 ESP32-S3용으로 컴파일된다. 패널 쪽 기능은 실제 Patternflow 소스에서 아직 빌드해 보지 못했고, 보드에 올려 본 것은 하나도 없다. 무엇이 확인됐고 무엇이 아닌지는 [8장](#8-아직-실기로-확인-안-된-것)에 있다.

## 1. 지금 바로 보기

보드 없이 된다. Chrome이나 Edge에서 [`console/index.html`](console/index.html)을 연다(파일을 끌어다 놓으면 된다).

가상 패널 10대가 뜬다. 허브와 패널 펌웨어에 들어가는 코드를 그대로 브라우저에서 돌리는 것이라, 여기서 보이는 동작이 보드가 할 동작이다. 지도의 패널을 눌러 고르고 오른쪽의 "손으로 돌리기"를 누르면 그 패널 앞에 선 사람이 된다. 왼쪽에서 K1~K4와 패턴을 각각 단독·동시·물결로 바꿔 본다. 패널을 끌어 자리를 옮기면 물결이 닿는 순서가 바뀐다. 손실 슬라이더를 올리거나 패널 전원을 껐다 켜도 결국 같은 상태로 모인다.

패널 그림은 패턴 번호와 노브 값이 보이도록 그린 것이고 실제 패턴은 아니다.

## 2. 구조

```
 패널 1 ─┐
 패널 2 ─┤  Wi-Fi, UDP 7400 (PFE/1)        ┌─ USB 잭: 시리얼 "Patternflow Ensemble" ─ 노트북의 콘솔
   …     ├──────────────────────── 허브 ───┤
 패널 N ─┘  ESP32-S3-DevKitC-1              ├─ COM 잭: 굽기, 부팅 로그 (콘솔도 붙을 수 있다)
                                            └─ Wi-Fi: http://192.168.66.1/ 에서 같은 콘솔 (전화기용)
```

**패널**은 Patternflow 펌웨어에 `ensemble` 기능을 얹은 것이다. 노브마다 위치 하나(0~1000)를 갖고, 전시 패널이 그랬듯 그 위치를 lane으로 패턴에 넘긴다. 패턴의 매개변수는 그 위치를 자기 범위에 옮긴 값이 된다. 클릭 수가 아니라 위치를 주고받기 때문에 패널끼리 어긋나지 않는다. 패널은 0.5초마다, 그리고 손이 움직일 때마다 자기 상태를 허브에 알리고, 허브가 말한 상태로 간다.

**손이 이긴다.** 노브마다 손이 움직인 횟수를 세고, 허브는 "이 값은 네 n번째 움직임까지 보고 정한 것"이라고 함께 보낸다. 횟수가 다르면 패널은 그 값을 버린다. 그래서 돌리는 도중에 허브가 옛 값으로 끌어당기는 일이 없다.

**허브**는 패널마다 "있어야 할 상태"(패턴 하나, 노브 넷)를 들고 있다. 채널은 다섯(K1, K2, K3, K4, 패턴)이고 채널마다 규칙이 하나다.

| 규칙 | 한 패널을 돌리면 |
|---|---|
| 단독 | 그 패널만 바뀐다 |
| 동시 | 묶인 패널이 모두 바로 바뀐다 |
| 물결 | 돌린 패널에서 시작해, 각 패널에 `거리 ÷ 속도`만큼 늦게 닿는다 |

거리는 콘솔 지도에 놓은 자리에서 나온다. 한 칸이 기준 간격이고, 속도 2칸/초면 한 칸 떨어진 패널이 0.5초 뒤에 따라온다. 두 사람이 양쪽 끝에서 돌리면 물결이 엇갈려 지나가고, 다 닿고 나면 모든 패널이 마지막에 돌린 값을 보여 준다.

K4는 두 가지로 쓴다. 패턴을 넘기는 손잡이(세 클릭에 한 패턴, 전시 때와 같다)이거나 네 번째 노브다. 콘솔에서 고른다.

주고받는 것은 언제나 상태 전체다. 패킷 하나를 잃어도 다음 것이 같은 내용을 가져오고, 전원이 나갔다 들어온 패널은 한 번에 지금 상태를 다시 받는다. 허브가 꺼졌다 켜지면 배치와 규칙은 플래시에서 돌아오고, 패널들은 보던 것을 그대로 보여 준다.

**같은 코드가 세 군데서 돈다.** [`core/`](core/)의 헤더들은 Arduino도 libc도 쓰지 않는다. 허브 펌웨어와 패널 펌웨어가 그대로 포함하고, PC 테스트가 그대로 컴파일하고, 콘솔의 데모가 WebAssembly로 그대로 돌린다.

통신 규약은 [`PROTOCOL.md`](PROTOCOL.md)에 있다.

## 3. 허브 굽기

보드는 ESP32-S3-DevKitC-1 한 장, 붙이는 것 없음. 전시 때 FlowLocal로 쓴 보드면 된다.

```bash
cd ensemble/hub
cp hub_secrets.example.h hub_secrets.h     # 핫스팟 이름과 비밀번호를 적는다 (커밋되지 않는다)
python -m platformio run -e hub
python -m platformio run -e hub -t upload --upload-port COMx    # COM 잭으로
```

- 플랫폼은 패널 펌웨어와 같은 `espressif32@7.0.1`이라, 패널을 빌드해 본 컴퓨터면 더 받을 것이 없다.
- 저장소 경로에 한글이 있으면 링커가 못 쓴다. 전시 때처럼 `PLATFORMIO_BUILD_DIR`을 영문 경로로 잡는다.
- **USB 잭**을 노트북에 꽂으면 시리얼 포트 `Patternflow Ensemble`이 생긴다. 콘솔이 붙는 곳이다. **COM 잭**은 굽기와 부팅 로그(115200)다.
- 켜고 12초 뒤에 핫스팟이 뜬다. 허브가 다시 켜질 때 앞서 붙어 있던 패널이 끊긴 것을 알아차릴 시간이고, 전시 때 FlowLocal에서 잰 값을 그대로 가져왔다(`HUB_AP_START_DELAY_MS`). USB와 콘솔은 그동안에도 된다.
- 설정은 [`hub/hub_config.h`](hub/hub_config.h)에 있다. 주소는 FlowLocal과 같은 `192.168.66.1`이다.

## 4. 패널 굽기

패널 펌웨어는 공개된 [Patternflow](https://github.com/engmung/Patternflow) 소스에 폴더 둘을 더한 것이다. 코어 파일은 하나도 고치지 않는다.

```bash
git clone https://github.com/engmung/Patternflow patternflow      # 이 저장소 안에 두면 .gitignore가 가려 준다
python ensemble/panel/install.py patternflow
cp firmware/patternflow_secrets.example.h patternflow/firmware/patternflow/patternflow_secrets.h
#   PF_WIFI_SSID / PF_WIFI_PASS 를 허브의 것과 같게 적는다
cd patternflow
PF_BUILD_DIR=$HOME/pf-build ./firmware/bundles/build.sh ensemble
```

`install.py`가 넣는 것은 `firmware/patternflow/features/ensemble/`(기능과 `core/`의 헤더 셋)와 `firmware/bundles/ensemble/`(빌드를 정하는 파일 둘)뿐이다. 나오는 이미지는 `$PF_BUILD_DIR/firmware/firmware.bin`이다.

- 이 빌드에는 `ensemble`만 들어 있다. OSC, MQTT, 네트워크 MIDI는 저마다 노브와 패턴을 움직이기 때문에, 함께 넣으려면 누가 이끄는지부터 정해야 한다.
- 전시용 패치(`firmware/panel-exhibit-2026.patch`)와는 따로다. 그 패치의 작가 패턴 여섯과 한 이미지에 넣는 것은 아직 해 보지 않았다.

### Wi-Fi로 올리기

USB는 필요 없다. 에디션을 바꿀 때와 같은 길이다. Patternflow 코어에는 `/update`가 있고, 이 빌드는 코어를 건드리지 않으니 들어갈 때도 나올 때도 그 길을 쓴다. 패널과 같은 Wi-Fi에 있는 컴퓨터에서 한다.

- 브라우저: `http://<패널 주소>/update`를 열고 `firmware.bin`을 끌어다 놓는다.
- 한 대, 빌드까지 한 번에: `./firmware/bundles/build.sh ensemble flash <패널 주소>`
- 여러 대: 주소를 한 줄에 하나씩 `panels.txt`에 적고

  ```bash
  BIN=$HOME/pf-build/firmware/firmware.bin
  while read p; do
    echo "== $p"
    curl -sS --max-time 180 -F "firmware=@$BIN" "http://$p/update?size=$(stat -c%s "$BIN")"; echo
  done < panels.txt
  ```

패널 주소는 IP(패널의 NETWORK 화면이나 공유기의 목록)를 쓰거나 `patternflow-a1b2.local`처럼 MAC 끝 네 자리가 붙은 이름을 쓴다. `patternflow.local`은 모든 패널이 함께 쓰는 이름이라 여러 대가 켜져 있으면 어느 패널이 답할지 모른다.

패턴, 저장된 Wi-Fi, 밝기, 노브 설정은 그대로 남는다. 파티션 표가 같고 이 기능은 플래시에 아무것도 쓰지 않는다.

**첫 한 대는 USB를 꽂을 수 있는 패널로 한다.** 이 이미지는 아직 보드에서 켜 본 적이 없다. 켜지다 멈추는 이미지는 `/update`까지 가지 못하니 Wi-Fi로는 되돌릴 수 없고, 그때는 USB로 다시 굽는다([patternflow.work/flash](https://patternflow.work/flash)). 한 대가 켜지고 허브에 붙는 것을 본 다음 나머지를 올린다.

### 허브에 붙이기

패널은 허브의 주소를 몰라도 된다. 같은 네트워크에 있기만 하면 전체에 대고 부르다가 허브가 답하는 순간부터 허브하고만 이야기한다. 그래서 할 일은 "같은 Wi-Fi에 있게 하는 것" 하나다.

| | 패널에서 할 일 |
|---|---|
| **패널이 이미 쓰는 Wi-Fi에 허브를 넣는다** (공유기 모드, `hub_secrets.h`의 `HUB_STA_SSID`) | 없음. 이미지를 올리면 다시 켜지면서 바로 붙는다. 작업실에서 만지고 디버깅할 때 가장 편하다. 노트북도 같은 Wi-Fi에 있으니 패널의 웹 콘솔과 허브의 콘솔(`http://pf-ensemble.local/`)이 함께 열리고 인터넷도 된다 |
| **전용 공유기를 작업실과 전시장에서 같이 쓴다** | 그 공유기를 패널에 한 번 저장해 두면 끝 |
| **허브의 핫스팟** | 허브의 Wi-Fi를 패널마다 저장한다 (아래) |

패널에 Wi-Fi를 더 저장하는 것도 Wi-Fi로 한다. 패널의 Wi-Fi 화면에서 넣거나, 한 줄로:

```bash
curl -X POST "http://<패널 주소>/api/wifi" --data-urlencode "ssid=PatternflowEnsemble" --data-urlencode "pass=<비밀번호>" -d connect=1
```

`connect=1`이면 그 자리에서 옮겨 가고, 다음부터도 그 네트워크를 먼저 찾는다. 빼면 저장만 한다. 패널은 네트워크를 다섯 개까지 기억하고, 먼저 찾던 것이 안 보이면 5초쯤마다 다음 것을 시도한다. 그래서 허브를 끄면 패널들은 저장돼 있던 작업실 Wi-Fi로 알아서 돌아온다.

`patternflow_secrets.h`에 허브의 Wi-Fi를 적어 빌드하는 방법은 **저장된 네트워크가 하나도 없는 패널**(막 지우고 구운 보드)에만 통한다. 하나라도 저장돼 있으면 코어는 컴파일된 것을 쓰지 않는다. 쓰던 패널에는 위의 방법을 쓴다.

### 원래 펌웨어로 되돌리기

같은 길로 나온다. 원래 쓰던 에디션의 `.bin`을 `/update`에 올리면 된다. 위의 반복문에서 `BIN`만 바꾸면 여러 대가 한 번에 돌아가고, 노트북이 인터넷과 패널에 함께 닿는 네트워크에 있으면 [patternflow.work/editions](https://patternflow.work/editions)의 한 번 누르기도 그대로 된다(패널마다 그 패널의 주소를 적는다).

- 패널 주소는 콘솔의 **주소 복사**가 모아 준다. 지금 허브에 붙어 있는 패널의 주소가 한 줄에 하나씩 복사되니 `panels.txt`에 붙여 넣는다.
- 패턴, Wi-Fi, 밝기는 그대로다. 패널에 저장해 둔 허브의 Wi-Fi는 목록에 남으니, 없애려면 패널의 Wi-Fi 화면에서 지운다(`curl -X DELETE "http://<패널 주소>/api/wifi?ssid=PatternflowEnsemble"`).
- 허브의 핫스팟에 붙어 있는 패널은 인터넷이 없는 네트워크에 있다. `.bin`을 미리 받아 두고 노트북을 그 핫스팟에 넣어 올리거나, 허브를 꺼서 패널이 작업실 Wi-Fi로 돌아온 뒤에 올린다.
- 허브 자체는 Wi-Fi로 굽지 않는다. 노트북에 USB로 꽂혀 있으니 그대로 굽는다.

## 5. 콘솔

[`console/index.html`](console/index.html)을 Chrome이나 Edge로 열고 위쪽에서 고른다.

| | |
|---|---|
| **데모** | 보드 없이 가상 패널로 |
| **USB** | 허브의 USB 잭(또는 COM 잭)에 꽂고 누른 뒤 포트를 고른다. Web Serial이라 Chrome·Edge에서만 된다 |
| **Wi-Fi** | 허브의 핫스팟에 들어간 기기에서. 전화기는 그냥 `http://192.168.66.1/`을 열면 허브가 같은 화면을 내준다 |

처음 설치할 때는 이 순서로 한다.

1. 패널을 켠다. 허브에 붙는 대로 지도에 한 줄로 나타나고 번호가 매겨진다.
2. **전체 번호 표시**를 누르면 패널마다 자기 번호를 LED에 띄운다. 그걸 보고 지도의 패널을 실제 자리로 끌어다 놓는다. 번호와 이름은 오른쪽에서 바꾼다.
3. 왼쪽에서 채널마다 단독·동시·물결을 고르고 물결 속도를 맞춘다.
4. 패널마다 다른 패턴을 주려면 패턴 규칙을 **단독**으로 두고, 패널을 골라 오른쪽에서 패턴을 정한다. "이 패널만"은 그 패널만, "여기서 물결로"와 "전체 동시에"는 모두에게 보낸다.
5. 마음에 드는 상태는 **장면**에 저장해 두고 불러온다. 고른 패널에서 물결로 불러올 수도 있다.

배치, 규칙, 장면은 허브의 플래시에 남는다. 노트북을 떼도 허브와 패널만으로 돈다.

아래 로그에는 허브가 겪는 일(패널이 붙음, 끊김, 다시 켜짐)이 찍힌다. "오가는 줄 모두 보기"를 켜면 주고받는 JSON이 그대로 보이고, 입력칸에 한 줄을 써서 직접 보낼 수도 있다. 패널을 고르면 신호 세기, fps, 잃은 보고 수 같은 진단값이 나온다. COM 잭에 시리얼 모니터를 붙여도 같은 줄이 보인다.

## 6. 몇 대까지

| 방식 | 패널 수 | |
|---|---|---|
| 허브의 핫스팟 (기본) | **8대까지** | 무선은 15대까지 받지만, 핫스팟의 DHCP 서버가 주소를 8개까지만 기억하고 아홉 번째가 오면 가장 오래된 것을 잊는다(`CONFIG_LWIP_DHCPS_MAX_STATION_NUM=8`, Arduino 코어에 들어 있는 값). 그러면 두 패널이 같은 주소를 받을 수 있다 |
| 공유기 모드 | **24대까지** | 허브와 패널이 모두 보통 Wi-Fi 공유기에 붙는다. 위의 한도가 없다. `hub_secrets.h`에서 `HUB_STA_SSID`를 켠다. 24는 허브가 기억하는 패널 수(`MAX_PANELS`)다 |

10대 이상이면 공유기 모드로 간다. 2.4 GHz 공유기 하나를 설치 전용으로 두고, 허브와 패널에 그 이름과 비밀번호를 적는다. 패널이 허브를 찾는 방식은 같아서 다른 것은 바꿀 필요가 없다. 콘솔은 그 네트워크의 `http://pf-ensemble.local/`에서도 열린다. 손님용 네트워크처럼 기기끼리 못 보게 막는 설정은 꺼야 한다.

## 7. 테스트

```bash
python ensemble/tests/run_tests.py
python ensemble/tests/run_tests.py --patternflow patternflow     # 받아 둔 Patternflow 소스와 훅 순서 대조
```

C++17 컴파일러가 있어야 한다(g++, clang++, 없으면 `python -m pip install ziglang`).

- **core**: 규약의 바이트, 패널 쪽 엔진, 허브를 가상 설치 통째로 돌린다. 동시와 물결의 도착 시간, 엇갈리는 물결, 패킷 30% 손실, 순서가 뒤바뀐 패킷, 패널 전원 재시작, 5초와 12초 Wi-Fi 끊김, 허브 재시작, 24대.
- **panel**: 패널 기능의 기기 쪽(소켓, 프레임 훅, 메뉴 잠금, 번호 표시)을 Patternflow 헤더의 대역(`tests/panel_stub/`)에 대고 컴파일해, 진짜 허브 코드와 맞물려 돌린다.
- **console**: 허브가 내주는 페이지(`hub/src/console_page.h`)가 `console/`과 같은지.

`core/`를 고치면 `python ensemble/tools/build_wasm.py`로 데모를 다시 만들고, `console/`을 고치면 `python ensemble/tools/build_console.py`로 허브의 페이지를 다시 만든다.

## 8. 아직 실기로 확인 안 된 것

2026-11-11 현재 **전부**다. 보드에 올려 본 것이 없다.

확인된 것:

- PC 테스트 통과(core 358, panel 56). 주소·정의되지 않은 동작 검사(ASan, UBSan)를 켠 채로.
- 허브 펌웨어가 `espressif32@7.0.1`에서 경고 없이 컴파일·링크된다(RAM 29%, 플래시 27%). 핫스팟 모드와 공유기 모드 둘 다.
- 콘솔이 헤드리스 Chromium에서 오류 없이 뜨고 데모가 돈다(데스크톱, 전화기 폭, 밝은·어두운 테마).
- 패널 기능의 훅 순서가 Patternflow v3.12.0의 `features/pf_feature.h`와 같다.

확인 안 된 것:

- **패널 펌웨어가 실제 Patternflow 트리에서 빌드되는지.** 대역 헤더로만 컴파일했다. `build.sh ensemble`을 처음 돌릴 때 include 경로나 이름에서 걸릴 수 있다. 코어에서 기대는 것은 `InputFrame`의 lane 필드, `btn1`~`btn4`의 `longPressFired`, `PatternflowSleep::isSleeping()`, `PatternflowUiText::drawLine`, `NUM_PATTERNS`, `dma_display`다.
- 허브: 핫스팟이 뜨는지, USB 시리얼이 Windows에서 잡히는지, Web Serial로 붙는지, COM 잭으로 붙을 때 허브가 재시작하지 않는지(DTR과 RTS를 내려 두게 했다), 플래시 저장.
- 패널의 lane이 패턴에서 전시 때처럼 느껴지는지, 다른 패널에서 온 값이 부드럽게 번지는지(`번짐` 80 ms).
- 실제 Wi-Fi에서 패널 8대의 지연과 손실. 허브 12초 침묵이 이 구성에서도 맞는 값인지.
- 9대째부터의 주소 충돌은 ESP-IDF 소스와 설정값을 읽고 쓴 것이다. 실제로 일으켜 보지는 않았다.
- 공유기 모드 전체.
- 패턴 번호는 패널마다 같은 패턴이 같은 순서로 깔려 있다고 본다. 다르게 깔린 패널이 섞이면 같은 번호가 다른 패턴이 된다.
- Wi-Fi로 올리고 되돌리는 길(4장)은 Patternflow의 문서와 소스(`docs/EDITIONS.md`, `docs/rest-api.md`, `src/core_web_update.h`, `src/core_wifi.h`)를 읽고 쓴 것이다. 이 이미지로 해 본 적은 없다.

들어 있지 않은 것:

- **소리.** 전시의 Ableton 연결(MIDI)은 여기 없다. 허브가 노브 값을 다 알고 있으니 USB에 MIDI를 더하는 길은 열려 있다.
- 전시 패널에 있던 Wi-Fi 자가 복구(연결은 됐는데 허브가 안 들릴 때 다시 붙기)는 코어를 고쳐야 해서 뺐다.
- 허브의 채널 자동 선택. 채널은 `HUB_AP_CHANNEL`(6)로 고정이다.
- 일정 시간 뒤 처음 상태로 돌아가기(전시의 3분 복귀).

## 9. 파일

| 경로 | |
|---|---|
| [`core/`](core/) | 규약(`pfe_proto.h`), 패널 쪽(`pfe_follower.h`), 허브 쪽(`pfe_conductor.h`), 콘솔 명령(`pfe_console.h`), 가상 설치(`pfe_sim.h`, `pfe_wasm.cpp`) |
| [`hub/`](hub/) | 허브 펌웨어. `src/main.cpp`는 Wi-Fi, UDP, 시리얼, 웹, 플래시를 `core/`에 잇는 배선이다 |
| [`panel/`](panel/) | 패널 기능과 번들, `install.py` |
| [`console/`](console/) | 콘솔. `pfe_sim.wasm.js`는 `core/`에서 만든 데모 |
| [`tests/`](tests/) | PC 테스트와 `run_tests.py` |
| [`tools/`](tools/) | `build_wasm.py`, `build_console.py` |
| [`PROTOCOL.md`](PROTOCOL.md) | 패널과 허브 사이의 바이트, 허브와 콘솔 사이의 줄 |

코드는 MIT.

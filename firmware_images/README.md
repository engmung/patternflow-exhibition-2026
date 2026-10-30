# 바로 굽는 펌웨어 이미지

빌드 환경 없이 이 폴더만으로 두 보드를 지금 상태로 되돌릴 수 있다. 다른 컴퓨터(노트북)에서 보드를 다시 구워야 할 때 쓴다.

**제일 쉬운 방법:** 레포 맨 위의 `flasher.html`을 Chrome이나 Edge로 연다(더블클릭, 또는 브라우저 창에 끌어다 놓기). 보드를 꽂고 버튼을 누른 뒤 뜨는 창에서 포트를 고르면 끝이다. 이 폴더의 이미지가 그 파일 안에 들어 있어서 인터넷도 Python도 필요 없다. 보드 MAC을 읽어 전시 패널·FlowLocal·개발용 보드를 구분하고, 엉뚱한 보드에는 굽지 않는다. FlowLocal은 오른쪽(COM) 잭으로도, 노트북에 꽂힌 왼쪽(USB) 잭으로도 굽는다(왼쪽 잭은 아래 "FlowLocal"의 버튼 순서가 먼저 필요하다). 직접 빌드한 `.bin`을 올리는 칸도 있다(연구용).

아래 명령은 같은 일을 명령줄로 하는 방법이다(브라우저가 안 될 때).

| 파일 | 보드 | 만든 커밋 | 비고 |
|---|---|---|---|
| `panel/firmware.bin` | 패널 DevKit | patternflow `exhibit-2026` `006e9c1` + 이 레포 `patterns/` (`7a8bae3`) | **2026-09-30 빌드**, 1,171,296 B, sha256 `502a3c11…`. `PatternflowLocal`에만 접속, 초대 호스트·MIDI 켬 고정, 자막 없음, 거절되면 1초 뒤 재초대, 죽은 세션 12초 안에 감지, Wi-Fi 자가 복구 30초, YUJIN 새 물 색. 2026-09-30 패널에 구웠다(COM10, 설정 유지) |
| `panel/bootloader.bin`, `panel/partitions.bin`, `panel/boot_app0.bin` | 패널 DevKit | 9/24와 같다(바뀌지 않음) | 전체 지우기로 처음부터 구울 때만 필요 |
| `flowlocal/firmware.factory.bin` | FlowLocal | flowlocal `exhibit-2026` `c6360dc` (펌웨어 태그 `flx-mon3`) | **2026-09-30 빌드**, 1,236,352 B, sha256 `4ecbcca0…`. `flx-mon2`에 더해: 패널 초대를 거절하지 않음(가장 오래 조용한 세션을 내보냄), 25초 조용한 세션 종료, 같은 초대 재전송에 같은 OK, 모니터의 채널 명령(0x13)과 점수(`cs=`). 2026-09-30 FlowLocal에 구웠다(USB 잭, 다운로드 모드, 굽고 RST) |
| `flowlocal/firmware.bin` | FlowLocal | 같은 빌드 | 1,170,816 B, sha256 `bcfe677e…`. 앱만. 보통은 factory를 쓴다 |

- 받은 뒤 파일이 온전한지: 이 폴더에서 `sha256sum -c SHA256SUMS`
- 두 이미지 모두 전시 핫스팟 비밀번호가 들어 있다. **이 레포는 비공개로 둔다.**

## 준비 (한 번)

```bash
pip install esptool
```

한글 Windows에서는 esptool 5.x가 진행 막대 글자 때문에 중간에 죽는다. 명령 앞에 `PYTHONUTF8=1 PYTHONIOENCODING=utf-8`을 붙인다(아래 명령에 붙어 있다).

**`--port`는 꼭 적는다.** 이 노트북에는 패널 DevKit과 FlowLocal이 같이 꽂혀 있다. 포트를 안 주면 esptool이 스스로 찾은 보드에 굽는다. 포트 번호는 장치 관리자(포트(COM & LPT))에서 확인한다.

## 패널 — 설정 남기고 앱만 (보통 이것)

DevKit을 패널 보드에서 빼서 DevKit의 **자기 USB 잭**으로 PC에 꽂는다. 버튼은 누르지 않아도 된다. 아래 `COM10`은 이 노트북 기준이다(예전 데스크탑에서는 `COM17`).

```bash
PYTHONUTF8=1 PYTHONIOENCODING=utf-8 python -m esptool --chip esp32s3 --port COM10 --baud 921600 \
  write-flash 0xe000 panel/boot_app0.bin 0x10000 panel/firmware.bin
```

밝기·자막 설정 같은 저장값은 그대로 남는다. `boot_app0.bin`을 빼지 않는다(무선 업데이트를 받은 적 있는 보드는 두 번째 슬롯으로 부팅해서 옛 앱이 뜬다). 2026-09-25에 이렇게 구웠다. 새 이미지는 패널에 저장된 다른 Wi-Fi와 `/midi` 호스트를 읽지 않으므로, 그것 때문에 지울 필요는 없다.

## 패널 — 전부 지우고 처음부터

```bash
PYTHONUTF8=1 PYTHONIOENCODING=utf-8 python -m esptool --chip esp32s3 --port COM10 erase-flash
PYTHONUTF8=1 PYTHONIOENCODING=utf-8 python -m esptool --chip esp32s3 --port COM10 --baud 921600 \
  write-flash 0x0 panel/bootloader.bin 0x8000 panel/partitions.bin 0xe000 panel/boot_app0.bin 0x10000 panel/firmware.bin
```

지우면 밝기 등 패널 설정이 초기값이 된다. 굽고 나서 웹 콘솔에서 다시 맞춘다.

## FlowLocal

잭이 둘이다. 어느 쪽이든 굽는 동안 MIDI 포트 `Patternflow Exhibit`가 사라지므로 Live와 모니터는 FlowLocal을 잃는다. 전시 중에는 하지 않는다.

- **왼쪽 잭(USB)** — 전시 때 노트북에 꽂는 그 잭 그대로 굽는다. 지금 설치에서는 이 잭만 쓸 수 있다. 평소 이 잭은 MIDI 장치라 굽기용 포트가 없으니, 먼저 다운로드 모드로 만든다: **BOOT를 누른 채 RST를 한 번 눌렀다 떼고, BOOT를 뗀다.** 장치 관리자에 새 COM 포트가 생긴다(2026-09-25 이 노트북에서는 `COM11`). 그 번호를 아래 `--port`에 넣는다.
- **오른쪽 잭(COM, CH343)** — 버튼 없이 된다. 이 설치에서는 이 잭에 꽂을 수 없다.

```bash
PYTHONUTF8=1 PYTHONIOENCODING=utf-8 python -m esptool --chip esp32s3 --port COM11 erase-flash
PYTHONUTF8=1 PYTHONIOENCODING=utf-8 python -m esptool --chip esp32s3 --port COM11 --baud 921600 \
  write-flash 0x0 flowlocal/firmware.factory.bin
```

- **왼쪽 잭으로 구웠으면 끝나고 RST를 한 번 누른다**(BOOT는 누르지 않는다). 버튼으로 들어간 다운로드 모드는 esptool이 끝에 `Hard resetting via RTS pin`을 찍고 재시작해도 그대로 남는다(2026-09-25에 겪음). RST를 누르면 새 펌웨어가 뜨고, 몇 초 뒤 노트북에 MIDI 포트가 다시 생긴다.
- 이미 전시 FlowLocal 펌웨어가 도는 보드는 `erase-flash`를 건너뛰고 `write-flash 0x0 flowlocal/firmware.factory.bin`만 해도 된다(2026-09-25에 이렇게 구웠다). 일반 FlowLocal을 쓰던 보드는 먼저 지운다(옛 AP 설정이 NVS에 남는다).

FlowLocal은 핫스팟 설정이 펌웨어에 박혀 있어서 지워도 잃는 게 없다. 켜지고 12초 뒤에 핫스팟이 뜬다(패널이 옛 연결을 놓게 일부러 기다린다). 그 12초의 끝에 주변 Wi-Fi를 훑어 채널 1·6·11 중 하나를 고른다.

## 이미지를 새로 만들었을 때

펌웨어를 고쳐 다시 빌드했으면 이 폴더의 파일도 바꾸고 `SHA256SUMS`를 다시 만든다(이 폴더에서 `sha256sum panel/*.bin flowlocal/*.bin > SHA256SUMS`). 이 노트북의 빌드 결과는 `C:/Users/user/pfx-exhibit-build/firmware/firmware.bin`(패널), `C:/Users/user/flx-pio/build/exhibit/firmware.bin`과 `firmware.factory.bin`(FlowLocal)이다. 그다음 `flasher.html`도 다시 만든다(레포 맨 위에서 `node tools/flasher/build.mjs`, 처음 한 번은 `cd tools/flasher && npm install`). 체크섬이 안 맞으면 만들기를 거부한다. 빌드 방법은 루트 `README.md` 4장과 `backups/README.md`.

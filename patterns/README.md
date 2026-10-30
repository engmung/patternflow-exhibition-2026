# 전시 패턴

패턴 하나에 폴더 하나. 번호는 K4 순서다(패널에서 1번이 켤 때 나오는 패턴).

| 폴더 | JS (Pattern Lab·시뮬레이터) | .h (패널 펌웨어) | 같이 있는 것 |
|---|---|---|---|
| `1_bakwan/` | `bakwan.js` | `bakwan.h` | |
| `2_choegyeongeun/` | `choegyeongeun.js` | `choegyeongeun.h` | `choegyeongeun_landscape.js` (참고용 가로 버전) |
| `3_hanyeonghoe/` | `hanyeonghoe.js` | `hanyeonghoe.h` | `hanyeonghoe.project.json` (레이어 원본) |
| `4_heoeunbin/` | `heoeunbin.js` | `heoeunbin.h` | `heoeunbin_landscape.js` (참고용 가로 버전) |
| `5_jeongeungyo/` | `jeongeungyo.js` | `jeongeungyo.h` | |
| `6_yujin/` | `yujin.js` | `yujin.h` | |

- **패널은 이 폴더의 `.h`를 그대로 컴파일한다.** 펌웨어 클론 안에 사본은 없다. 여기서 고치면 그게 곧 펌웨어다.
- `_original/`: 처음 받은 원본. `js/`는 2026-09-19에 붙여준 JS, `h/`는 그때 Pattern Lab에서 내보낸 .h. 비교용이라 고치지 않는다.
- `preview.html`: 전시 시뮬레이터. 브라우저로 열면 패턴을 돌려 보고 **JS 복사 / .h 복사** 버튼으로 바로 가져갈 수 있다.
- `sync.json`: JS와 .h가 같은 그림이라고 마지막으로 확인했을 때의 지문. 아래 "확인"이 쓴다.

## 복사해서 쓰기

- **Pattern Lab·커뮤니티에 올릴 때:** 폴더의 `<이름>.js`를 통째로 복사해 붙인다. HANYEONGHOE는 맨 끝 `// @stack` 줄까지 전부 붙여야 레이어가 살아난다.
- **.h가 필요할 때:** 같은 폴더의 `<이름>.h`를 쓴다.
- 파일을 여는 대신 `preview.html`의 복사 버튼을 써도 된다.

## 패턴을 고쳤을 때

1. **JS를 바꿨다**(Pattern Lab에서 고친 것을 붙여 넣었거나 직접 고쳤다) → 그 폴더의 `<이름>.js`를 덮어쓴다. HANYEONGHOE는 `hanyeonghoe.project.json`도 같이 바꿔야 한다(Pattern Lab에서 붙여 넣은 코드의 `@stack` 줄에 들어 있다. `tools/patterns/run.sh stack decode`로 꺼낸다).
2. **.h를 다시 만든다.** Claude에게 "`<이름>` .h 다시 만들어줘"라고 하면 된다. HANYEONGHOE는 생성기가 있다(`tools/patterns/hanyeonghoe_h/build.sh`, 레이어 코드가 바뀌면 손번역 부분을 고치라고 멈춘다).
3. **확인** (이 폴더의 상위, `exhibition_2026`에서):
   ```bash
   python tools/patterns/host/pfhost.py current <이름>        # .h가 JS와 같은 그림인지 (OK가 나와야 한다)
   python tools/patterns/check_sync.py --accept <이름>        # OK일 때만: 지금 상태를 기록
   node tools/patterns/preview.mjs                           # 시뮬레이터 다시 만들기
   ```
4. 패널 펌웨어 빌드와 굽기는 루트 `README.md` 4장. 굽기 전에 한 번:
   ```bash
   python tools/patterns/check_sync.py     # 전부 "in step"이어야 한다
   ```
   JS만 바꾸고 .h를 안 바꿨으면 여기서 `JS changed, .h not redone yet`이 나온다.
5. `git add patterns && git commit && git push`. 패턴만 바꿨으면 펌웨어 백업 번들은 다시 만들 필요 없다(.h가 이 레포에 있으니까).

## 알아둘 것

- `.js`, `.h`, `.json`은 어느 컴퓨터에서 받아도 줄바꿈이 LF로 유지된다(`.gitattributes`). 그래서 복사한 내용이 원본과 바이트까지 같다.
- 시뮬레이터와 렌더 도구, 호스트 대조(`pfhost.py`)는 메인 레포(`../랜딩페이지_opensorce/web`)의 Pattern Lab 런타임을 쓴다. 다른 컴퓨터에서는 메인 레포를 옆에 받고 `web/`에서 `npm install`을 한 번 해야 한다. 복사·붙여넣기만 할 거면 필요 없다(`preview.html`은 그대로 열린다).
- 노브·누르기·대기 복귀 규칙은 루트 `PATTERN_KNOBS.md`.

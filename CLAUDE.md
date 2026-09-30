# escape_from_blockov — 작업 규칙

WebGL 빌드 2.5D PvP 슈팅 게임(동시 50인) 클라이언트(`client/`)와 게임 서버(`server/`).

## 서버 작업 전 필독
- 서버는 자체 IOCP 컨텐츠 서버 라이브러리(NetLib) 위에 구현한다.
- **서버 코드를 보거나 수정하기 전에 `server/ContentEchoServer/CONTENT_SERVER_LIBRARY.md`를 먼저 읽을 것.**
  특히 5.2(송신 API 소유권), 5.3(Content 스레딩 규칙), 8장(패킷 크기 제약·알려진 이슈).
- 라이브러리(`NetLib/`, `Utils/`) 동작을 바꾸면 위 문서도 함께 갱신한다.
- 서버 소스는 CP949 + CRLF 인코딩. 편집 시 인코딩을 유지한다.

## 파일 삭제 규칙
- 파일 삭제는 허용하되 **반드시 휴지통으로** 보낸다(영구 삭제 금지).
  - 일반 파일(PowerShell에서 `Add-Type -AssemblyName Microsoft.VisualBasic` 후): `[Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($p, 'OnlyErrorDialogs', 'SendToRecycleBin')`
  - Unity 에셋: `AssetDatabase.MoveAssetToTrash(path)`

## 구성 (2026-09-30)
- 게임 명세: 프로젝트 문서 `claude/game-spec.md` (v0.9, 이 저장소의 `GAME_SPEC.md`는 사본). 프로토콜 v6
- 조작(v0.9): 카메라 줌 `[` `]`, 마우스 휠은 전체 맵(M) 줌 전용. 섹터 표기는 지도 좌표(`GameSession.SectorLabel`, 열 A~AD / 행 1~30, 위쪽이 1)
- 더미 강도: `server/DummyClient/dummy_config.txt`의 `weakness_min/max`, `reaction_base_ms`, `aim_error_max_deg` (명세 20.5)
- 실행: 저장소 최상단 `start_server.bat` [`build`] [`web`] / `stop_server.bat` (명세 18장). 게임 서버·게이트웨이·WebGL 웹서버를 한 번에 실행, `web`은 브라우저 열기
- 테스트 모드: `server/GameServer/game_config.txt`의 `test_mode: true` → 모든 플레이어를 섹터 (0,0)에 스폰
- 게임 서버: `server/GameServer/` (README 참고, `GameServer.sln` Release x64). 로직 테스트: `test/` 스텁 + Node 봇
- 더미 클라이언트: `server/DummyClient/` (C++ IOCP, `DummyClient.sln` Release x64, README 참고). 대규모 테스트 서버 설정: `server/GameServer/test/stress/`
- `ObstacleMap.h/.cpp`(서버)는 NetLib 의존 없이 유지할 것 — DummyClient가 함께 컴파일한다
- 게이트웨이: `server/gateway/index.js` (WS 8080 → TCP 10301)
- 엄폐물 맵: `map/obstacles.bmp` (1픽셀 = 1m, 검정 = 벽, 회색 = 낮은 엄폐물). 수정 후 서버 재시작 + Unity `Blockov/Map/Import Obstacles (default BMP)` + WebGL 재빌드
- 섹터: 50m × 30×30 = 월드 1500m (`SectorGrid.cs` / 서버 `ObstacleMap.h`의 `MapConst`). 방 정원 300
- 아이템(v0.8, 명세 19장): 슬롯 1 특수 총(샷건·저격총, 내구도) / 2 권총 / 3 붕대, Space 구르기, F 가방·에어드랍. 무기는 `weapons.txt`(15열: … jitterDeg durability slot). 수치는 `game_config.txt`(airdrop_*, bag_*, roll_*, bandage_* 등)
  - 클라는 구르기·붕대·상호작용 시간(0.25/3/2초, 2.5m, 1/2초)을 `GameSession` 상수로 가지고 있다. 서버 설정을 바꾸면 함께 바꿀 것
  - 서버 기능 테스트: `test/item` 설정(에어드랍 3초 주기) + `node test/item_test.js 127.0.0.1 10502`, 기본: `test/run` + `bot_test.js` (10501)
- 클라: `client/escape_from_blockov` — 씬 Title(0) → TestArena(1), 스크립트 `Assets/Scripts/Network`, `Assets/Scripts/Game`, `Assets/Scripts/Map`, 에디터 도구 `Assets/Editor`
- WebGL: `start_server.bat` → `http://localhost:8090/` (웹서버가 `/ws`로 게임 WS 중계). 외부 접속은 공유기 TCP 8090 포트포워딩(명세 18.4). 8080·10301은 외부에 열지 않음

## Git
- 저장소 최상위 하나로 관리(`main`). 변경 작업을 마치면 의미 단위로 커밋한다. 원격(push)은 설정하지 않음.
- `skills/`는 서브모듈(Unity-Technologies/skills). 업데이트: `git submodule update --remote skills`
- `.gitattributes`: 최상위는 `* -text`(CP949/CRLF 서버 소스·배치 파일을 바이트 그대로 보존). `client/escape_from_blockov/`는 Unity 템플릿 규칙(일부 바이너리는 Git LFS)이 우선.
- 커밋은 Windows git(`C:\Program Files\Git`)으로 한다. **Linux VM에서는 git을 실행하지 말 것**(`git status`조차 VM에서 지울 수 없는 `.git/index.lock`을 남겨 Windows git을 막는다. 남았다면 크기 0인 lock만 휴지통으로).
- 스크립트에서 git을 실행할 때 stdout/stderr를 동시에(비동기로) 읽거나 파일로 리다이렉트할 것. 순차로 ReadToEnd 하면 경고 출력이 파이프를 채워 git과 호출측이 서로 멈춘다.

## 작업 시 주의
- `start` 로 창을 띄우는 배치 파일을 출력 리다이렉트/ReadToEnd로 실행하지 말 것(자식이 파이프를 물고 있어 호출측이 멈춤).
- 서버를 스크립트에서 띄울 때 표준 출력은 파일로 리다이렉트(읽지 않는 파이프 금지).
- 런타임에 `CreatePrimitive` 등으로 만든 렌더러에는 반드시 `RuntimeMaterials.Apply()`(URP Lit 에셋)를 쓸 것. 기본 머티리얼은 WebGL 빌드에서 분홍색이 된다.
- 프로토콜을 바꾸면 `GAME_PROTOCOL_VERSION`/`NetConst.ProtocolVersion`과 `server/GameServer/test/*.js`의 버전·길이도 함께 갱신. DummyClient는 `GameProtocol.h`를 공유하지만 패킷 파싱(`Dummy.cpp`)은 따로 고쳐야 한다.
- 서버 코드에서 `near`/`far`/`min`/`max` 같은 이름을 지역 변수로 쓰지 말 것 (windows.h 매크로. 리눅스 스텁 빌드에서는 드러나지 않음).

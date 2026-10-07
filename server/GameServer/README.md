# GameServer

escape_from_blockov 게임 서버 (명세 `GAME_SPEC.md`). NetLib(`../ContentEchoServer/NetLib`) 위에서 동작한다.

## 빌드 / 실행
1. `GameServer.sln`을 Visual Studio 2022(v143)로 열고 **Release | x64** 빌드.
2. 작업 디렉터리는 이 폴더(`game_config.txt`, `weapons.txt`, 맵 `../../map/obstacles.bmp`). exe를 직접 실행하면 `game_config.txt`가 있는 위쪽 폴더로 자동으로 옮긴다.
3. 보통은 저장소 최상단 `start_server.bat`으로 서버·게이트웨이·웹서버를 함께 띄운다.
4. 콘솔 `Q` = 종료.
5. 크래시하면 `dumps/Dump_YYYYMMDD_HHMMSS_<pid>.dmp`(전체 메모리 덤프)와 게임 로그 `syslogs/game_*.log`의 `[crash]` 줄이 남는다. 같은 빌드의 `x64\Release\GameServer.pdb`로 Visual Studio에서 연다.

## 구성
| 파일 | 역할 |
|---|---|
| `GameServer.*` | NetLib_Server 상속. Entry 1개 + Battle N개 등록, 접속 시 Entry로 이동 |
| `EntryContent.*` | CS_ENTER_GAME 대기(10초), 버전·이름 검증, 방 예약(채우기 우선), GamePlayer 생성 후 방으로 이동 |
| `BattleContent.*` | 방 1개: 입장 시퀀스, 스폰, 이동 검증, 사격·피격(되감기) 검증, 사망·점수·랭킹, 섹터 시야, 아이템·가방·에어드랍(예고), 파괴 가능 엄폐물 상태, 타임아웃 |
| `GamePlayer.h` | 플레이어 상태, 위치 이력(64), 사격 기록(64), 위반 카운터 |
| `SectorMap.h` | 30x30 섹터(50m) 그리드, 반경 r 시야 조회 |
| `ObstacleMap.*` | 엄폐물 BMP(벽·낮은 엄폐물·파괴 가능 엄폐물) 로더, 이동·총알 판정. NetLib 의존 없음(DummyClient도 사용) |
| `GameData.*` | weapons.txt 로더 |
| `GameConfig.h` | game_config.txt 로더 (키 수 제한 없음) |
| `NetLibBridge.h` | NetLib include + 직렬화/수신 헬퍼 + 게임 로그. `GAME_STUB_NETLIB` 정의 시 리눅스 테스트 스텁 사용 |
| `test/` | 리눅스 스텁 NetLib + Node 봇 시나리오 테스트 (Windows 빌드에는 포함되지 않음) |

## 테스트 (`test/`, 서버를 각 폴더에서 실행)
| 폴더 / 포트 | 스크립트 |
|---|---|
| `test/run` 10501 | `node test/bot_test.js 127.0.0.1 10501` |
| `test/item` 10502 | `node test/item_test.js 127.0.0.1 10502` |
| `test/obs` 10502 | `node test/obstacle_test.js run 127.0.0.1 10502` |
| `test/cover` 10503 | `node test/cover_test.js run 127.0.0.1 10503` (맵: `node test/cover_test.js make test/cover/cover_map.bmp`) |
| `test/spawn` 10504 | `node test/cover_test.js spawn 127.0.0.1 10504` |
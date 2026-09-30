# GameServer

escape_from_blockov 게임 서버 (game-spec v0.4). NetLib(`../ContentEchoServer/NetLib`) 위에서 동작한다.

## 빌드 / 실행
1. `GameServer.sln`을 Visual Studio 2022(v143)로 열고 **Release | x64** 빌드.
2. 작업 디렉터리(디버거 기본값 = 이 폴더)에 `game_config.txt`, `weapons.txt`, `spawns.txt`가 있어야 한다.
3. 실행 후 `../gateway`에서 `node index.js` (WS 8080 → TCP 10301).
4. 콘솔 `Q` = 종료.

## 구성
| 파일 | 역할 |
|---|---|
| `GameServer.*` | NetLib_Server 상속. Entry 1개 + Battle N개 등록, 접속 시 Entry로 이동 |
| `EntryContent.*` | CS_ENTER_GAME 대기(10초), 버전·이름 검증, 방 예약(채우기 우선), GamePlayer 생성 후 방으로 이동 |
| `BattleContent.*` | 방 1개: 입장 시퀀스, 이동 검증, 사격·피격(되감기) 검증, 사망·점수·랭킹, 섹터 시야, 타임아웃 |
| `GamePlayer.h` | 플레이어 상태, 위치 이력(64), 사격 기록(64), 위반 카운터 |
| `SectorMap.h` | 50x50 섹터 그리드, 반경 r 시야 조회 |
| `GameData.*` | weapons.txt / spawns.txt 로더, 맵 상수 |
| `GameConfig.h` | game_config.txt 로더 (키 수 제한 없음) |
| `NetLibBridge.h` | NetLib include + 직렬화/수신 헬퍼. `GAME_STUB_NETLIB` 정의 시 리눅스 테스트 스텁 사용 |
| `test/` | 리눅스 스텁 NetLib + Node 봇 시나리오 테스트 (Windows 빌드에는 포함되지 않음) |

## 리눅스에서 로직 테스트
```
g++ -std=c++20 -DGAME_STUB_NETLIB -I. *.cpp test/StubNetLib.cpp -o gameserver -lpthread
# 테스트 설정: spawns.txt에 "10 10" 한 줄, enter_timeout_ms 2000
./gameserver &  node test/bot_test.js 127.0.0.1 10301
```

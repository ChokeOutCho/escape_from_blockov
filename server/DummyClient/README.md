# DummyClient — 스트레스 / 플레이 테스트용 더미 클라이언트

게임 서버(`server/GameServer`)에 **TCP로 직접** 접속하는 더미 플레이어를 원하는 수만큼 띄운다.
Windows IOCP 기반이라 한 프로세스로 수천 명을 처리한다.

## 빌드

`DummyClient.sln` → Release x64 (VS 2022). 서버와 같은 `../GameServer/ObstacleMap.cpp`, `GameProtocol.h`를 함께 컴파일한다.

```
MSBuild DummyClient.sln /p:Configuration=Release /p:Platform=x64
```

## 실행

작업 디렉터리는 `server/DummyClient` (설정 파일·맵 상대 경로 기준).

| 방법 | 설명 |
|---|---|
| `x64\Release\DummyClient.exe` | 인원 수를 입력받고 대시보드 표시 |
| `x64\Release\DummyClient.exe --count 1000 --duration 60` | 무인 실행: 60초 동안 5초마다 통계 한 줄 출력 후 종료 |

인자: `--count N`, `--duration 초`, `--server ip:port`, `--config 파일`, `--leave`(사망 시 퇴장), `--nofire`(사격 안 함)

### 대시보드 키

| 키 | 동작 |
|---|---|
| `C` | 인원 수 다시 입력 (퇴장 모드로 빠진 더미도 다시 채움) |
| `+` / `-` | 100명 늘리기 / 줄이기 |
| `M` | 사망 모드 전환: **재접속**(인원 유지) ↔ **퇴장**(죽으면 빠짐) |
| `F` | 사격 켜기/끄기 (끄면 배회만 → 이동 트래픽만 측정) |
| `Q` | 종료 (모든 연결 정리 후 요약 출력) |

## 더미 행동

1. 접속 → `CS_ENTER_GAME`(이름 `Dummy<번호>`) → 입장 후 핑 3회(200ms)로 서버 시각 동기화, 이후 2초마다 핑
2. **배회**: 무작위 방향으로 2~6초씩 이동(`sprint_chance` 확률로 달리기). 서버와 같은 `obstacles.bmp`로 벽·낮은 엄폐물을 피한다(0.25m 단위 충돌 검사)
3. **교전**: 시야(3x3 섹터) 안의 플레이어 중 교전 거리 이내이고 벽에 가리지 않은 가장 가까운 대상 → 멈추고 조준, 연사 간격 x1.1로 사격
   - 대상 속도로 탄 도착 위치를 예측(리드)해 쏘고, 탄 비행 시간 뒤 그 위치를 `CS_HIT_REPORT`로 보고 → 서버의 오토타게팅 되감기 검증을 그대로 거친다
   - `ViewTime` = 추정 서버 시각 - 편도 지연
4. **구르기** (프로토콜 v6, game-spec 19.10): 교전 중 쿨타임(3초)이 지나면 초당 30% 확률로 적의 옆 방향, 배회 중 쿨타임마다 10% 확률로 진행 방향으로 `CS_ROLL`. 도착점은 서버와 같은 계산(0.25m 단위, 엄폐물 앞 정지)이라 보정이 나지 않아야 한다. 붕대·아이템 획득·특수 총은 쓰지 않는다(권총만)
5. **사망**: 재접속 모드면 `reconnect_delay_ms` 뒤 다시 입장, 퇴장 모드면 빠진다
6. 대상에는 사람 플레이어와 다른 더미가 모두 포함된다
7. v6의 새 패킷(인벤토리·가방·에어드랍·체력 등)은 받기만 하고 무시한다

## 설정 (`dummy_config.txt`)

| 키 | 기본값 | 설명 |
|---|---|---|
| server_ip / server_port | 127.0.0.1 / 10301 | 게임 서버 (게이트웨이 아님) |
| count | 0 | 0이면 시작 시 입력 |
| max_dummies | 20000 | 한 프로세스 최대 수 |
| io_threads / logic_threads | 4 / 4 | IOCP 워커 / AI 틱 스레드 |
| tick_ms | 50 | AI 틱 간격 |
| connect_per_sec | 300 | 초당 신규 접속 수 (서버 accept 폭주 방지) |
| death_mode | reconnect | `reconnect` 또는 `leave` |
| reconnect_delay_ms | 2000 | 사망 후 재접속 대기 |
| fire | true | 사격 여부 |
| engage_range | 60 | 교전 거리 (무기 사거리-2 와 작은 값) |
| sprint_chance | 0.3 | 배회 중 달리기 비율 |
| name_prefix | Dummy | 이름 접두사 (번호 포함 12자 이내) |
| roll | true | 구르기 사용 |
| roll_combat_per_sec | 0.3 | 교전 중 쿨타임 후 초당 구르기 확률 (적의 옆 방향) |
| roll_wander_chance | 0.1 | 배회 중 쿨타임마다 구르기 확률 (진행 방향) |
| roll_cooldown_ms / roll_ms / roll_speed_mult | 3000 / 250 / 3.0 | 서버 `game_config.txt`와 같게 |
| obstacle_map | ../../map/obstacles.bmp | 서버와 같은 엄폐물 맵 |

## 비정상 이벤트 로그 (`logs/dummy_YYYYMMDD_HHMMSS.log`)

실행할 때마다 실행 폴더 아래 `logs/`에 새 파일을 만든다(1초마다 flush, `.gitignore`의 `*.log`로 제외). 대시보드 `[로그]` 줄과 무인 모드 `log a/b/c`에 건수가 나온다.

| 종류 | 기록 조건 |
|---|---|
| `DISCONNECT` | 게임 중(InGame) 우리가 끊지 않았는데 연결이 끊김. 사망 후 서버의 정상 종료(3초 뒤)와 인원 축소로 인한 종료는 제외 |
| `ENTER_FAIL` | 입장 중 끊김, 입장 거부(SERVER_FULL / VERSION_MISMATCH / INVALID_NAME), 입장 타임아웃(10초) |
| `CONNECT_FAIL` | TCP 접속 실패(서버 꺼짐, 포트 고갈 등) |

한 줄 형식:

```
2026-10-01 01:20:14.480 [DISCONNECT] #760 Dummy760 id=3 state=InGame reason="recv error" err=(64 지정된 네트워크 이름을 더 이상 사용할 수 없습니다) kick=0 connected=87ms lastRecv=8ms pos=(325.0,325.0) visible=0 sendQ=0B
```

| 필드 | 의미 |
|---|---|
| reason | `server closed connection (FIN)`(서버가 정상 종료), `recv error`/`send error`(RST 등 소켓 오류), `protocol error`, `send queue overflow`(서버가 수신하지 않음), `enter timeout`, `enter rejected: ...` |
| err | Windows/WSA 오류 코드와 설명 (64 = 상대가 연결을 끊음, 10054 = 연결 재설정 등) |
| kick | 끊기기 전에 받은 `SC_KICK` 사유 (1 타임아웃, 2 잘못된 패킷, 3 치트 의심, 4 서버 종료) |
| connected / lastRecv | 접속 후 경과 시간 / 마지막 패킷 수신 후 경과 시간 |
| pos / visible / sendQ | 마지막 위치, 시야 안 인원, 남은 송신 큐 |

## 대규모 테스트 시 서버 설정

기본 `game_config.txt`는 방 4 x 50명, `maxofsession` 1000이라 수천 명을 받을 수 없다.
`server/GameServer/test/stress/game_config.txt`(방 4 x 1250 = 5000명, 세션 6000)를 쓰려면 그 폴더에서 서버를 실행한다:

```
cd server\GameServer\test\stress
..\..\x64\Release\GameServer.exe
```

- 모두를 한 곳에 모아 최악의 밀집(시야 인원² 트래픽)을 보려면 그 설정에서 `"test_mode": true`
- 방 수를 늘리면 Content 바쁜 루프 스레드가 늘어나므로 `room_count + 1 <= workerTH_Pool_size - 2`를 지키고 정원(`room_capacity`)을 늘리는 편이 낫다
- 한 PC에서 한 서버 주소로 만들 수 있는 연결은 임시 포트 수(기본 약 16,000)가 한계. 더미는 abortive close로 끊어 TIME_WAIT를 남기지 않는다

## 대시보드 항목

| 줄 | 내용 |
|---|---|
| 상태 | 게임중 / 입장중 / 사망 / 접속중 / 종료중 / 대기 인원 |
| 접속 / 입장 | 누적 접속 성공·실패, 끊김(비정상 = 게임 중 서버가 끊음), 입장 OK·FULL·거부·타임아웃 |
| 수신 / 송신 | 전체 KB/s, pkt/s |
| RTT | 최근 1초 핑 평균·최대 (서버 Content 틱 대기 포함) |
| 전투 | 초당 사격·명중 보고·명중 확인(SC_DAMAGE)·사망·킬·구르기 |
| 검증 | 위치 보정(SC_POSITION_CORRECT), 킥 사유별 누적 — 0이 아니면 더미 로직 또는 서버 검증 문제 |
| 클라 | 더미 프로세스 CPU·메모리 (클라가 병목인지 확인) |

## 측정 기록 (2026-09-30, 로컬 1대: 서버·더미 같은 PC, stress 설정 방 4 x 1250)

| 시나리오 | 결과 |
|---|---|
| 200명 사격, 40초 | 접속·입장 875회(사망 후 재접속 포함) 실패 0, 사격 7,368 / 명중 보고 4,918 / 서버 확인 4,021 (82%), 위치 보정 0, 킥 0 |
| 3,000명 사격, 70초 | 접속·입장 21,010회 실패 0, 명중 확인 102,433, 사망 20,227, 위치 보정 0, 킥 0. 서버 송신 72k pkt/s (2.4 MB/s), 서버 프로세스 CPU 19.5%, 더미 CPU 4% · 87 MB |
| 5,000명 이동만(`--nofire`), 60초 | 5,000명 동시 게임중(방 4개 정원), 더미 수신 최대 630k pkt/s (19 MB/s, 스폰 밀집 시) → 분산 후 220k pkt/s, 서버 송신 195k pkt/s · CPU 23%, RTT 평균 48 ms, 위치 보정 0, 킥 0 |

참고: 스폰 지점이 50개뿐이라 수천 명이면 스폰마다 수십 명이 모여 곧바로 교전한다. 3,000명 사격 시 초당 약 300명이 죽어
`connect_per_sec`(300)가 재접속을 따라가지 못해 동시 게임중 인원이 약 800명에 머문다. 사격 상태로 동시 인원을 유지하려면
`connect_per_sec`를 올리거나 스폰 지점을 늘릴 것.

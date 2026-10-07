# escape_from_blockov 개발 분담 — 직접 구현한 부분과 AI를 사용한 부분

> **서버 라이브러리(NetLib), 컨텐츠 서버 라이브러리, 그리고 그 유틸리티 도구들은 개발자가 직접 설계·구현했습니다.**
> **AI(Claude)는 그 라이브러리 위에 올라가는 게임 컨텐츠와 주변 도구를 만드는 데에만 사용했습니다.**

---

## 1. 직접 구현 (개발자 본인)

`server/ContentEchoServer/` 아래의 라이브러리 본체·유틸리티·예제 서버는 이 게임 프로젝트를 시작하기 전부터 개발자가 직접 작성한 코드입니다. 게임 서버는 이 라이브러리를 그대로 가져다 쓰는 구조이며, 아키텍처·스레딩 모델·세션 수명 관리·송신 소유권 규칙 등 서버의 핵심 설계는 모두 이 라이브러리에서 나옵니다.

### 1.1 IOCP 서버 라이브러리 — `NetLib/`

| 구성 | 내용 |
|---|---|
| `NetLib_Server` | IOCP 생성, Accept / Worker / Monitor 스레드, 세션 배열·인덱스 풀, 송신 API(`SendPacket`, `SendPacketFast`, `SendPacketMulticast`, `SendPacketFastWithoutIOCount`), 세션 해제, Content 등록·이동 |
| `NetLib_Content` | **컨텐츠 서버 구조**: 고정 틱 Update 루프를 IOCP 위에서 돌리는 Content 단위, ENTER / LEAVE / RELEASE 메시지 큐, Content 간 세션 이동(`Move_Content`) 중 패킷 순서 보존 |
| `Session` | 소켓, recv 링버퍼, 컨텐츠 수신 링버퍼, lock-free 송신 큐, IOCount(참조 카운트 + Release 플래그), 64비트 세션 핸들(인덱스 + 고유 ID) |
| `Packet` / `SerializeBuffer` | 직렬화 버퍼, TLS 풀 기반 패킷, refCount, 네트워크 헤더 예약 |
| `NetLibraryProtocol`, `NetLibDefine`, `NetLib_Helper` | 와이어 헤더(NetHeader 5B), 경량 암호화 옵션, overlapped 타입 구분, 공용 상수·헬퍼 |
| `NetLib_Client` | 서버 간 통신용 단일 연결 IOCP 클라이언트 |

### 1.2 유틸리티 도구 — `Utils/`

| 도구 | 내용 |
|---|---|
| `TLSObjectPool` | 스레드별 청크 + 공용 저장소 구조의 TLS 오브젝트 풀 (패킷·플레이어 객체 할당) |
| `LockFreeQueue` / `LockFreeStack` / `LockFreePool` | 카운터 태깅으로 ABA를 막는 lock-free 자료구조 |
| `ObjectPool`, `RingBuffer` | 단일 스레드 풀, SPSC 바이트 링버퍼 |
| `SimpleEncoder` | 패킷 인코딩/디코딩 + 체크섬 |
| `Logger`, `Profiler` | 파일 로그, 스코프 프로파일러 |
| `Parser` | 설정 파일 파서 |
| `SystemMonitor` | PDH 기반 CPU·메모리·네트워크 모니터 |
| `CrashDump` | 처리되지 않은 예외 시 미니덤프 생성 |

### 1.3 예제 컨텐츠 서버 (Echo 서버)

`ContentServer`, `AuthContent`, `EchoContent`, `MonitorClient`, 설정 파일 등 — 라이브러리 사용법을 보여 주는 인증 → 에코 컨텐츠 서버와 모니터 서버 연동. 게임 서버는 이 예제의 구조(서버 클래스 → 입장 Content → 게임 Content)를 그대로 따릅니다.

---

<br><br><br>

## 2. AI 사용 (Claude)

라이브러리 **위에서 동작하는 게임 컨텐츠**와 개발·테스트용 주변 도구를 AI로 작성했습니다. 기획·요구 사항·결정은 개발자가 하고, AI는 그에 따라 명세 문서를 갱신하고 코드를 구현했습니다.

| 영역 | 위치 |
|---|---|
| 게임 서버 컨텐츠 | `server/GameServer/` — 입장·방 배정(EntryContent), 전투 방(BattleContent: 이동·사격 검증, 과거 위치 되감기 피격 판정, 점수·랭킹, 아이템·가방·에어드랍, 파괴 가능 엄폐물), 엄폐물 맵, 설정 |
| 게임 클라이언트 | `client/escape_from_blockov/` — Unity WebGL 클라이언트 전체(통신 모듈, 게임 로직, HUD, 맵 임포트 도구, 효과음·이펙트) |
| 주변 도구 | WS↔TCP 게이트웨이(`server/gateway`), 더미 클라이언트(`server/DummyClient`), 테스트 봇·리눅스 스텁(`server/GameServer/test`), 예시 맵 생성기(`map/`), 실행 배치 파일, WebGL 웹서버 |
| 문서 | 게임 명세(`GAME_SPEC.md`), 라이브러리 분석 문서(`CONTENT_SERVER_LIBRARY.md` — 기존 라이브러리 소스를 읽고 정리한 사용 가이드) |

---

## 3. 요약

- **핵심 서버 기술 — IOCP 네트워크 라이브러리, 컨텐츠 서버 구조, lock-free 자료구조·오브젝트 풀 등 유틸리티 — 는 개발자가 직접 구현했습니다.**
- AI는 그 위의 **게임 컨텐츠**(게임 서버 로직, Unity 클라이언트)와 테스트·운영 도구, 문서 작성에 사용했습니다.
- 라이브러리에 대한 AI의 수정은 불가능합니다.

#pragma once
////////////////////////////////////////////////////////////////////////
// escape_from_blockov 게임 프로토콜 (claude/game-spec.md 7장)
//  - 리틀 엔디안, pack(1), NetHeader(5B) 뒤 Payload = WORD Type + 본문
//  - C->S 3000~3099, S->C 3100~3199
//  - 페이로드 최대 PAYLOAD_LEN_DEFAULT(512)
//  - 클라이언트 대응 파일: client/.../Assets/Scripts/Network/NetProtocol.cs
////////////////////////////////////////////////////////////////////////
#include <cstdint>

const uint32_t GAME_PROTOCOL_VERSION = 7;   // v6: 아이템·구르기·에어드랍·가방, v7: 파괴 가능 엄폐물·에어드랍 예고·스폰 플래그
const int NAME_LEN = 12;                // WCHAR Name[12] (UTF-16LE, 24B)

// 접두사 PT_: windows.h의 SC_MOVE/SC_CLOSE 등(WM_SYSCOMMAND) 매크로와 충돌을 피하기 위함
enum en_GAME_PACKET_TYPE : uint16_t
{
	// C -> S
	PT_CS_ENTER_GAME = 3000,   // UINT32 ProtocolVersion, WCHAR Name[12]                                   (30B)
	PT_CS_MOVE = 3001,         // float PosX,PosZ,VelX,VelZ,AimAngle, UINT16 MoveSeq                       (24B)
	PT_CS_FIRE = 3002,         // UINT32 ShotSeq, BYTE WeaponID, float OX,OZ,DX,DZ, UINT32 ViewTimeMs, BYTE SpreadSeed, BYTE _r (29B)
	PT_CS_HIT_REPORT = 3003,   // BYTE Count, {UINT32 ShotSeq, BYTE Pellet, UINT32 TargetID(또는 0x80000000|CoverId), float HX,HZ}[n] (3+17n)
	PT_CS_PING = 3004,         // UINT32 ClientTimeMs                                                      (6B)
	PT_CS_HEARTBEAT = 3005,    // -                                                                        (2B)
	PT_CS_ROLL = 3006,         // float StartX,StartZ,DirX,DirZ                                            (18B)
	PT_CS_SWITCH_WEAPON = 3007,// BYTE Slot(1 특수 무기, 2 기본 무기)                                       (3B)
	PT_CS_USE_BANDAGE = 3008,  // -                                                                        (2B)
	PT_CS_OPEN_CONTAINER = 3009,// UINT32 ContainerId                                                      (6B)
	PT_CS_TAKE_ITEM = 3010,    // UINT32 ContainerId, BYTE Item(1 특수 무기, 3 붕대)                       (7B)

	// S -> C
	PT_SC_ENTER_GAME = 3100,
	PT_SC_WEAPON_DEFS = 3101,
	PT_SC_CREATE_CHARACTERS = 3102,
	PT_SC_DELETE_CHARACTERS = 3103,
	PT_SC_MOVE = 3104,
	PT_SC_POSITION_CORRECT = 3105,
	PT_SC_FIRE = 3106,
	PT_SC_DAMAGE = 3107,
	PT_SC_PLAYER_DIE = 3108,
	PT_SC_DEATH_RESULT = 3109,
	PT_SC_SCORE = 3110,
	PT_SC_RANKING_TOP3 = 3111,
	PT_SC_KICK = 3112,
	PT_SC_PONG = 3113,
	PT_SC_PLAYER_COUNT = 3114,  // UINT32 TotalPlayers (서버 전체 접속 인원)                            (6B)
	PT_SC_INVENTORY = 3115,     // BYTE Equipped, BYTE SpecialWeaponId, WORD Durability, BYTE Bandages     (7B)
	PT_SC_ROLL = 3116,          // UINT32 PlayerId, float StartX,StartZ,EndX,EndZ                          (22B)
	PT_SC_HP = 3117,            // UINT32 PlayerId, WORD Hp                                                (8B)
	PT_SC_CONTAINER_CREATE = 3118, // BYTE Count, {UINT32 Id, BYTE Type, float X,Z}[n]                     (3+13n)
	PT_SC_CONTAINER_DELETE = 3119, // BYTE Count, UINT32 Id[n]                                             (3+4n)
	PT_SC_CONTAINER_CONTENTS = 3120, // UINT32 Id, BYTE SpecialWeaponId, WORD Durability, BYTE Bandages    (10B)
	PT_SC_AIRDROP = 3121,       // UINT32 Id, float X,Z, BYTE SectorX,SectorY, BYTE IsNew                  (17B)
	PT_SC_COVER_HP = 3122,      // WORD CoverId, BYTE Hp                                                   (5B)
	PT_SC_COVER_STATE = 3123,   // BYTE Count, {WORD CoverId, BYTE Destroyed, UINT32 DestroyedAtMs, WORD RegenSec}[n] (3+9n)
	PT_SC_AIRDROP_FORECAST = 3124, // UINT32 DropAtMs, BYTE Count, {float X,Z}[n]                          (7+8n)
};

// 고정 길이 페이로드 크기 (Type 포함). 수신 검증에 사용
const int LEN_CS_ENTER_GAME = 30;
const int LEN_CS_MOVE = 24;
const int LEN_CS_FIRE = 29;
const int LEN_CS_HIT_REPORT_HEAD = 3;
const int LEN_HIT_ITEM = 17;
const int MAX_HIT_ITEMS = 29;           // 3 + 17*29 = 496 <= 512
const int LEN_CS_PING = 6;
const int LEN_CS_HEARTBEAT = 2;
const int LEN_CS_ROLL = 18;
const int LEN_CS_SWITCH_WEAPON = 3;
const int LEN_CS_USE_BANDAGE = 2;
const int LEN_CS_OPEN_CONTAINER = 6;
const int LEN_CS_TAKE_ITEM = 7;

// 목록형 패킷의 패킷당 최대 항목 수 (512B 한도)
const int MAX_CREATE_PER_PACKET = 9;    // 3 + 54*9 = 489
const int MAX_DELETE_PER_PACKET = 127;  // 3 + 4*127 = 511
const int MAX_WEAPON_DEFS_PER_PACKET = 14;   // 3 + 36*14 = 507 (v6 항목 36B)
const int MAX_CONTAINER_CREATE_PER_PACKET = 39;  // 3 + 13*39 = 510
const int MAX_CONTAINER_DELETE_PER_PACKET = 127;
const int MAX_COVER_STATE_PER_PACKET = 56;          // 3 + 9*56 = 507
const uint32_t HIT_TARGET_COVER = 0x80000000u;      // CS_HIT_REPORT TargetID 최상위 비트 = 파괴 가능 엄폐물
const uint8_t CREATE_FLAG_SPAWN = 0x01;             // SC_CREATE_CHARACTERS Flags: 방금 스폰
const int LEN_SC_ENTER_GAME = 65;      // v4 (MapHash, SprintMultiplier 포함)
const int LEN_SC_PLAYER_COUNT = 6;

enum en_ENTER_RESULT : uint8_t
{
	ENTER_OK = 0,
	ENTER_SERVER_FULL = 1,
	ENTER_VERSION_MISMATCH = 2,
	ENTER_INVALID_NAME = 3,
};

enum en_KICK_REASON : uint8_t
{
	KICK_TIMEOUT = 1,
	KICK_INVALID_PACKET = 2,
	KICK_CHEAT_SUSPECT = 3,
	KICK_SERVER_SHUTDOWN = 4,
};

// v6 아이템 슬롯 / 컨테이너 (game-spec 6)
enum en_SLOT : uint8_t
{
	SLOT_SPECIAL = 1,
	SLOT_PISTOL = 2,
	SLOT_BANDAGE = 3,
};

enum en_ITEM : uint8_t
{
	ITEM_SPECIAL_WEAPON = 1,
	ITEM_BANDAGE = 3,
};

enum en_CONTAINER_TYPE : uint8_t
{
	CONTAINER_BAG = 1,
	CONTAINER_AIRDROP = 2,
};

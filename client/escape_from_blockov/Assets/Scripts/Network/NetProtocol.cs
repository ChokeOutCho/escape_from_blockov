namespace Blockov.Net
{
    /// <summary>
    /// 서버 NetLib 와이어 규약 상수. 서버 NetLibraryProtocol.h / Packet.h / GameServer/GameProtocol.h 와 일치해야 한다.
    /// | Code(1) | Len(2) | RandKey(1) | CheckSum(1) | Payload(Len) |
    /// 암호화 미사용: RandKey/CheckSum = 0, Payload 평문.
    /// </summary>
    public static class NetConst
    {
        public const byte HeaderCode = 119;   // 0x77
        public const int HeaderSize = 5;
        public const int MaxPayload = 512;    // 서버 PAYLOAD_LEN_DEFAULT

        public const uint ProtocolVersion = 6;   // v3: SC_ENTER_GAME에 MapHash, v4: SprintMultiplier, v5: SC_PLAYER_COUNT, v6: 아이템·구르기·에어드랍·가방
        public const int NameLength = 12;     // WCHAR Name[12]
        public const int MaxHitItems = 29;    // CS_HIT_REPORT (3 + 17n <= 512)
    }

    /// <summary>패킷 타입. 서버 GameServer/GameProtocol.h 와 1:1.</summary>
    public enum PacketType : ushort
    {
        // C -> S
        CS_ENTER_GAME = 3000,
        CS_MOVE = 3001,
        CS_FIRE = 3002,
        CS_HIT_REPORT = 3003,
        CS_PING = 3004,
        CS_HEARTBEAT = 3005,
        CS_ROLL = 3006,             // float StartX, StartZ, DirX, DirZ
        CS_SWITCH_WEAPON = 3007,    // BYTE Slot (1 특수 무기, 2 권총)
        CS_USE_BANDAGE = 3008,
        CS_OPEN_CONTAINER = 3009,   // UINT32 ContainerId
        CS_TAKE_ITEM = 3010,        // UINT32 ContainerId, BYTE Item (1 특수 무기, 3 붕대)

        // S -> C
        SC_ENTER_GAME = 3100,
        SC_WEAPON_DEFS = 3101,
        SC_CREATE_CHARACTERS = 3102,
        SC_DELETE_CHARACTERS = 3103,
        SC_MOVE = 3104,
        SC_POSITION_CORRECT = 3105,
        SC_FIRE = 3106,
        SC_DAMAGE = 3107,
        SC_PLAYER_DIE = 3108,
        SC_DEATH_RESULT = 3109,
        SC_SCORE = 3110,
        SC_RANKING_TOP3 = 3111,
        SC_KICK = 3112,
        SC_PONG = 3113,
        SC_PLAYER_COUNT = 3114,     // UINT32 TotalPlayers (서버 전체 접속 인원, 접속·해제 시 방송)
        SC_INVENTORY = 3115,        // BYTE Equipped, BYTE SpecialWeaponId, WORD Durability, BYTE Bandages
        SC_ROLL = 3116,             // UINT32 PlayerId, float StartX, StartZ, EndX, EndZ
        SC_HP = 3117,               // UINT32 PlayerId, WORD Hp
        SC_CONTAINER_CREATE = 3118, // BYTE Count, {UINT32 Id, BYTE Type, float X, Z}[n]
        SC_CONTAINER_DELETE = 3119, // BYTE Count, UINT32 Id[n]
        SC_CONTAINER_CONTENTS = 3120, // UINT32 Id, BYTE SpecialWeaponId, WORD Durability, BYTE Bandages
        SC_AIRDROP = 3121,          // UINT32 Id, float X, Z, BYTE SectorX, SectorY, BYTE IsNew
    }

    public enum EnterResult : byte
    {
        Ok = 0,
        ServerFull = 1,
        VersionMismatch = 2,
        InvalidName = 3,
    }

    public enum KickReason : byte
    {
        None = 0,
        Timeout = 1,
        InvalidPacket = 2,
        CheatSuspect = 3,
        ServerShutdown = 4,
    }
}

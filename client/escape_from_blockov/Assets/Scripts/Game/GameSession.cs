using System.Collections.Generic;
using Blockov.Net;

namespace Blockov.Game
{
    /// <summary>서버 무기 정의 (SC_WEAPON_DEFS, game-spec 5.1)</summary>
    public sealed class WeaponDef
    {
        public byte Id;
        public ushort Damage;
        public float Range;
        public float ProjectileSpeed;
        public ushort FireIntervalMs;
        public float ProjectileRadius;
        public ushort MagazineSize;
        public ushort ReloadMs;
        public float SpreadDeg;
        public byte Pellets;
        public byte Pierce;
        /// <summary>발사 방향 무작위 흔들림 ±JitterDeg (v6)</summary>
        public float JitterDeg;
        /// <summary>특수 무기 최대 내구도(발사 횟수). 0 = 무한</summary>
        public ushort Durability;
        /// <summary>1 특수 무기, 2 기본 무기</summary>
        public byte Slot;
        public string Name;
    }

    public struct RankEntry
    {
        public byte Rank;
        public uint PlayerId;
        public string Name;
        public uint Score;
    }

    public sealed class DeathResult
    {
        public string KillerName;
        public uint FinalScore;
        public uint Kills;
        public uint SurvivalSec;
    }

    /// <summary>
    /// 씬 사이에 유지되는 현재 게임 정보 (Title → TestArena).
    /// SC_ENTER_GAME 수신 시 채워지고, 타이틀 복귀 시 초기화된다.
    /// </summary>
    public static class GameSession
    {
        public static string RequestedName = "";

        public static uint MyPlayerId;
        public static string MyName = "";
        public static byte RoomNo;
        public static float SpawnX, SpawnZ;
        public static ushort Hp, MaxHp;
        public static float MoveSpeed = 12f;
        public static float CharacterRadius = 0.5f;
        public static byte WeaponId;
        /// <summary>서버가 읽은 엄폐물 맵의 해시 (클라 ObstacleMap.Hash와 달라야 할 이유가 없음)</summary>
        public static uint MapHash;
        /// <summary>Shift 달리기 속도 배율 (서버 설정 sprint_multiplier)</summary>
        public static float SprintMultiplier = 1.2f;
        /// <summary>서버 전체 접속 인원 (SC_PLAYER_COUNT, 입장·퇴장 시 서버가 방송)</summary>
        public static int OnlineCount;

        public static uint Score;
        public static uint Kills;
        public static float EnterRealtime;

        public static readonly Dictionary<byte, WeaponDef> Weapons = new Dictionary<byte, WeaponDef>();
        public static readonly List<RankEntry> Top3 = new List<RankEntry>();
        public static DeathResult Death;

        /// <summary>타이틀에 표시할 메시지 (입장 실패, 연결 끊김 사유 등)</summary>
        public static string TitleMessage;

        public static bool InGame => NetworkManager.Instance != null && NetworkManager.Instance.CurrentState == NetworkManager.State.InGame;

        /// <summary>SC_ENTER_GAME(Result=OK) 본문 파싱. Type·Result는 이미 읽힌 상태.</summary>
        public static void ReadEnter(PacketReader r)
        {
            MyPlayerId = r.ReadUInt32();
            RoomNo = r.ReadByte();
            SpawnX = r.ReadFloat();
            SpawnZ = r.ReadFloat();
            Hp = r.ReadUInt16();
            MaxHp = r.ReadUInt16();
            MoveSpeed = r.ReadFloat();
            CharacterRadius = r.ReadFloat();
            WeaponId = r.ReadByte();
            uint serverTime = r.ReadUInt32();
            MyName = r.ReadName();
            MapHash = r.ReadUInt32();
            SprintMultiplier = r.ReadFloat();

            var net = NetworkManager.Instance;
            net.Clock.InitFromServerTime(serverTime, net.LocalTimeMsExact);

            Score = 0;
            Kills = 0;
            OnlineCount = 0;
            Death = null;
            Weapons.Clear();
            Top3.Clear();
            PistolWeaponId = WeaponId;
            Equipped = SlotPistol;
            SpecialWeaponId = 0;
            SpecialDurability = 0;
            Bandages = 0;
        }

        public static void ReadWeaponDefs(PacketReader r)
        {
            int count = r.ReadByte();
            for (int i = 0; i < count; i++)
            {
                var d = new WeaponDef
                {
                    Id = r.ReadByte(),
                    Damage = r.ReadUInt16(),
                    Range = r.ReadFloat(),
                    ProjectileSpeed = r.ReadFloat(),
                    FireIntervalMs = r.ReadUInt16(),
                    ProjectileRadius = r.ReadFloat(),
                    MagazineSize = r.ReadUInt16(),
                    ReloadMs = r.ReadUInt16(),
                    SpreadDeg = r.ReadFloat(),
                    Pellets = r.ReadByte(),
                    Pierce = r.ReadByte(),
                    JitterDeg = r.ReadFloat(),
                    Durability = r.ReadUInt16(),
                    Slot = r.ReadByte(),
                };
                r.Skip(2);
                d.Name = WeaponName(d.Id);
                Weapons[d.Id] = d;
            }
        }

        public static WeaponDef MyWeapon => Weapons.TryGetValue(WeaponId, out var w) ? w : null;

        ////////////////////////////////////////////////////////////////
        // 지도 좌표 (game-spec 3.4): 열 A..Z, AA..AD (sx), 행 1..30 (위쪽 = 북 = z 큰 쪽이 1)
        ////////////////////////////////////////////////////////////////
        public static int SectorCount => SectorGrid.DefaultSectorCount;

        public static string SectorColumn(int sx)
        {
            string s = "";
            int n = sx + 1;
            while (n > 0) { int m = (n - 1) % 26; s = (char)('A' + m) + s; n = (n - 1) / 26; }
            return s;
        }

        public static int SectorRow(int sy) => SectorCount - sy;

        public static string SectorLabel(int sx, int sy) => SectorColumn(sx) + SectorRow(sy);

        public static string SectorLabelAt(UnityEngine.Vector2 worldXZ)
        {
            int sx = UnityEngine.Mathf.Clamp(UnityEngine.Mathf.FloorToInt(worldXZ.x / SectorGrid.DefaultSectorSize), 0, SectorCount - 1);
            int sy = UnityEngine.Mathf.Clamp(UnityEngine.Mathf.FloorToInt(worldXZ.y / SectorGrid.DefaultSectorSize), 0, SectorCount - 1);
            return SectorLabel(sx, sy);
        }

        ////////////////////////////////////////////////////////////////
        // 구역 번호 (game-spec 3.4): 섹터 3x3 = 1구역, 10x10 = 1~100, 왼쪽 위(북서) 1부터 오른쪽으로, 행 단위로 아래로
        ////////////////////////////////////////////////////////////////
        public const int RegionSectors = 3;
        public static int RegionCount => (SectorCount + RegionSectors - 1) / RegionSectors;

        public static int RegionNumber(int sx, int sy)
        {
            int gx = UnityEngine.Mathf.Clamp(sx / RegionSectors, 0, RegionCount - 1);
            int gy = UnityEngine.Mathf.Clamp(sy / RegionSectors, 0, RegionCount - 1);
            return (RegionCount - 1 - gy) * RegionCount + gx + 1;
        }

        public static string RegionLabel(int sx, int sy) => $"구역 {RegionNumber(sx, sy)}";

        public static string RegionLabelAt(UnityEngine.Vector2 worldXZ)
        {
            int sx = UnityEngine.Mathf.Clamp(UnityEngine.Mathf.FloorToInt(worldXZ.x / SectorGrid.DefaultSectorSize), 0, SectorCount - 1);
            int sy = UnityEngine.Mathf.Clamp(UnityEngine.Mathf.FloorToInt(worldXZ.y / SectorGrid.DefaultSectorSize), 0, SectorCount - 1);
            return RegionLabel(sx, sy);
        }

        public static string WeaponName(byte id)
        {
            switch (id)
            {
                case 1: return "권총";
                case 2: return "샷건";
                case 3: return "저격총";
                default: return id == 0 ? "없음" : $"무기 {id}";
            }
        }

        ////////////////////////////////////////////////////////////////
        // v6 인벤토리 (SC_INVENTORY, game-spec 6.1)
        ////////////////////////////////////////////////////////////////
        public const byte SlotSpecial = 1, SlotPistol = 2, SlotBandage = 3;
        public const int MaxBandages = 5;
        public const float BandageSeconds = 2f;
        public const int BandageHeal = 30;            // 서버 bandage_heal (22.3)
        public const float BandageMoveMult = 0.5f;     // 붕대 사용 중 걷기 속도 배율 (21.4)
        public const float RollSeconds = 0.25f, RollSpeedMult = 3f, RollCooldown = 3f;
        public const float InteractRange = 2.5f, LootCloseRange = 3f;
        public const float BagOpenSeconds = 1f, AirdropOpenSeconds = 2f;

        public static byte Equipped = SlotPistol;
        public static byte SpecialWeaponId;
        public static ushort SpecialDurability;
        public static byte Bandages;
        public static byte PistolWeaponId = 1;

        public static void ReadInventory(PacketReader r)
        {
            Equipped = r.ReadByte();
            SpecialWeaponId = r.ReadByte();
            SpecialDurability = r.ReadUInt16();
            Bandages = r.ReadByte();
            if (Equipped == SlotSpecial && SpecialWeaponId == 0) Equipped = SlotPistol;
            WeaponId = Equipped == SlotSpecial ? SpecialWeaponId : PistolWeaponId;
        }

        /// <summary>산탄 i의 [0,1) 난수 (시드 공유 → 사수·관찰자 같은 각도, game-spec 5.3)</summary>
        public static float PelletRand(byte seed, int i)
        {
            uint h = (uint)(seed + 1) * 73856093u ^ (uint)(i + 1) * 19349663u;
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            return (h & 0xFFFF) / 65536f;
        }

        /// <summary>산탄 i의 방향 (기준 방향 dir, 전체 확산각 spreadDeg)</summary>
        public static UnityEngine.Vector2 PelletDir(UnityEngine.Vector2 dir, byte seed, int i, float spreadDeg)
        {
            if (spreadDeg <= 0) return dir;
            float off = (PelletRand(seed, i) - 0.5f) * spreadDeg * UnityEngine.Mathf.Deg2Rad;
            float c = UnityEngine.Mathf.Cos(off), s = UnityEngine.Mathf.Sin(off);
            return new UnityEngine.Vector2(dir.x * c - dir.y * s, dir.x * s + dir.y * c);
        }
    }
}

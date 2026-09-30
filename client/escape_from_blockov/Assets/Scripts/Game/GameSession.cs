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
            Death = null;
            Weapons.Clear();
            Top3.Clear();
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
                };
                r.Skip(3);
                Weapons[d.Id] = d;
            }
        }

        public static WeaponDef MyWeapon => Weapons.TryGetValue(WeaponId, out var w) ? w : null;
    }
}

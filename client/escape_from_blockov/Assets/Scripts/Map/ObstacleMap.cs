using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 런타임 엄폐물 격자 (1m x 1m, 1500 x 1500 = 섹터 50m x 30). 에디터 도구(Blockov/Map/Obstacle Map Importer)가
    /// map/obstacles.bmp에서 만든 Resources/Map/obstacle_map.bytes(병합된 사각형 목록)를 읽어 비트셋으로 복원한다.
    ///  - Wall: 이동·총알 차단,  Low: 이동만 차단(총알 통과)
    ///  - Dest: 파괴 가능 엄폐물 (game-spec 3.6). 멀쩡하면 이동·총알 차단, 파괴되면 통과
    /// 서버 GameServer/ObstacleMap.cpp와 같은 규칙·같은 해시(FNV-1a)·같은 엄폐물 번호를 사용한다.
    /// </summary>
    public static class ObstacleMap
    {
        public const int Size = SectorGrid.DefaultSectorSize * SectorGrid.DefaultSectorCount;   // 1500
        public const byte Empty = 0, Low = 1, Wall = 2, Dest = 3;
        public const ushort NoCover = 0xFFFF;
        public const string ResourcePath = "Map/obstacle_map";

        public struct RectCell
        {
            public byte Type;
            public int X, Z, W, H;
            public ushort Cover;     // Dest만: 엄폐물 id (그 외 NoCover)
        }

        static ulong[] _move;       // Low | Wall | Dest
        static ulong[] _bullet;     // Wall
        static ushort[] _coverOf;   // 셀 → 엄폐물 id + 1 (0 = 없음)
        static bool[] _destroyed;   // 엄폐물 id별 파괴 여부 (SC_COVER_STATE)
        static bool _loadTried;

        /// <summary>엄폐물 id별 최대 체력</summary>
        public static byte[] CoverMaxHp { get; private set; } = new byte[0];
        /// <summary>엄폐물 id별 사각형 목록 (CoverManager가 상자를 만든다)</summary>
        public static List<RectCell>[] CoverRects { get; private set; } = new List<RectCell>[0];
        public static int CoverCount => CoverMaxHp.Length;

        public static bool Loaded { get; private set; }
        public static uint Hash { get; private set; }
        public static List<RectCell> Rects { get; } = new List<RectCell>();

        public static void EnsureLoaded()
        {
            if (_loadTried) return;
            _loadTried = true;
            var ta = Resources.Load<TextAsset>(ResourcePath);
            if (ta == null)
            {
                Debug.LogWarning("[ObstacleMap] Resources/" + ResourcePath + ".bytes 없음 → 엄폐물 없이 실행");
                return;
            }
            try { Load(ta.bytes); }
            catch (Exception e) { Debug.LogException(e); }
        }

        public static void Load(byte[] data)
        {
            var rects = ReadRects(data, out uint hash, out byte[] coverHp);
            int words = Size * Size / 64;
            _move = new ulong[words];
            _bullet = new ulong[words];
            _coverOf = new ushort[Size * Size];
            CoverMaxHp = coverHp;
            _destroyed = new bool[coverHp.Length];
            CoverRects = new List<RectCell>[coverHp.Length];
            for (int i = 0; i < coverHp.Length; i++) CoverRects[i] = new List<RectCell>(1);
            Rects.Clear();
            foreach (var r in rects)
            {
                Rects.Add(r);
                bool dest = r.Type == Dest && r.Cover < coverHp.Length;
                if (dest) CoverRects[r.Cover].Add(r);
                for (int z = r.Z; z < r.Z + r.H; z++)
                    for (int x = r.X; x < r.X + r.W; x++)
                    {
                        long i = (long)z * Size + x;
                        ulong bit = 1UL << (int)(i & 63);
                        _move[i >> 6] |= bit;
                        if (r.Type == Wall) _bullet[i >> 6] |= bit;
                        if (dest) _coverOf[i] = (ushort)(r.Cover + 1);
                    }
            }
            Hash = hash;
            Loaded = true;
            Debug.Log($"[ObstacleMap] {rects.Count} rects, {coverHp.Length} covers, hash 0x{hash:X8}");
        }

        /// <summary>
        /// obstacle_map.bytes v2: "BKOM" u16 ver(2) u16 size u32 hash
        ///   i32 rectCount {u8 type, u16 x,z,w,h, u16 cover}[rectCount]   (cover: Dest의 엄폐물 id, 그 외 0xFFFF)
        ///   i32 coverCount {u8 maxHp}[coverCount]
        /// </summary>
        public static List<RectCell> ReadRects(byte[] data, out uint hash, out byte[] coverMaxHp)
        {
            using var br = new BinaryReader(new MemoryStream(data));
            if (new string(br.ReadChars(4)) != "BKOM") throw new InvalidDataException("obstacle_map: bad magic");
            ushort ver = br.ReadUInt16();
            ushort size = br.ReadUInt16();
            if (ver != 2 || size != Size) throw new InvalidDataException($"obstacle_map: unsupported ver {ver} size {size} (Blockov/Map 도구로 다시 가져오세요)");
            hash = br.ReadUInt32();
            int count = br.ReadInt32();
            var list = new List<RectCell>(count);
            for (int i = 0; i < count; i++)
                list.Add(new RectCell { Type = br.ReadByte(), X = br.ReadUInt16(), Z = br.ReadUInt16(), W = br.ReadUInt16(), H = br.ReadUInt16(), Cover = br.ReadUInt16() });
            int covers = br.ReadInt32();
            coverMaxHp = br.ReadBytes(covers);
            return list;
        }

        /// <summary>셀의 엄폐물 id (없으면 -1)</summary>
        public static int CoverAt(int x, int z)
        {
            if (_coverOf == null || x < 0 || z < 0 || x >= Size || z >= Size) return -1;
            return _coverOf[(long)z * Size + x] - 1;
        }

        public static bool IsDestroyed(int cover) => _destroyed != null && cover >= 0 && cover < _destroyed.Length && _destroyed[cover];

        public static void SetDestroyed(int cover, bool destroyed)
        {
            if (_destroyed != null && cover >= 0 && cover < _destroyed.Length) _destroyed[cover] = destroyed;
        }

        /// <summary>총알이 막히는 셀인가: 벽 또는 멀쩡한 파괴 가능 엄폐물</summary>
        static bool BulletBlockedAt(int x, int z, out int cover)
        {
            cover = -1;
            if (BlocksBullet(x, z)) return true;
            int c = CoverAt(x, z);
            if (c < 0 || IsDestroyed(c)) return false;
            cover = c;
            return true;
        }

        static bool Bit(ulong[] set, int x, int z)
        {
            if (x < 0 || z < 0 || x >= Size || z >= Size) return true;  // 월드 밖 = 막힘
            if (set == null) return false;
            long i = (long)z * Size + x;
            return (set[i >> 6] & (1UL << (int)(i & 63))) != 0;
        }

        /// <summary>이동 차단: 벽·낮은 엄폐물·멀쩡한 파괴 가능 엄폐물 (파괴된 엄폐물은 통과, game-spec 3.6)</summary>
        public static bool BlocksMove(int x, int z)
        {
            if (!Bit(_move, x, z)) return false;
            if (_destroyed == null || x < 0 || z < 0 || x >= Size || z >= Size) return true;
            int c = _coverOf[(long)z * Size + x] - 1;
            return c < 0 || !_destroyed[c];
        }
        public static bool BlocksBullet(int x, int z) => Bit(_bullet, x, z);

        /// <summary>원(x,z,r)이 이동 차단 셀과 겹치는가 (서버 CircleBlocked와 동일)</summary>
        public static bool CircleBlocked(float x, float z, float r)
        {
            if (!Loaded) return false;
            int x0 = Mathf.FloorToInt(x - r), x1 = Mathf.FloorToInt(x + r);
            int z0 = Mathf.FloorToInt(z - r), z1 = Mathf.FloorToInt(z + r);
            for (int cz = z0; cz <= z1; cz++)
                for (int cx = x0; cx <= x1; cx++)
                {
                    if (!BlocksMove(cx, cz)) continue;
                    float nx = Mathf.Clamp(x, cx, cx + 1), nz = Mathf.Clamp(z, cz, cz + 1);
                    float dx = x - nx, dz = z - nz;
                    if (dx * dx + dz * dz < r * r) return true;
                }
            return false;
        }

        /// <summary>
        /// 총알 선분 a→b가 처음 들어가는 차단 셀(벽 또는 멀쩡한 파괴 가능 엄폐물)까지의 비율 t(0~1). 막히지 않으면 2.
        /// </summary>
        public static float RaycastBullet(Vector2 a, Vector2 b) => RaycastBullet(a, b, out _);

        /// <summary>RaycastBullet + 막은 셀이 파괴 가능 엄폐물이면 그 id (벽이거나 막히지 않으면 -1)</summary>
        public static float RaycastBullet(Vector2 a, Vector2 b, out int cover)
        {
            cover = -1;
            if (!Loaded) return 2f;
            int cx = Mathf.FloorToInt(a.x), cz = Mathf.FloorToInt(a.y);
            int ex = Mathf.FloorToInt(b.x), ez = Mathf.FloorToInt(b.y);
            float dx = b.x - a.x, dz = b.y - a.y;
            int stepX = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
            int stepZ = dz > 0 ? 1 : (dz < 0 ? -1 : 0);
            const float INF = 1e30f;
            float tDeltaX = stepX != 0 ? Mathf.Abs(1f / dx) : INF;
            float tDeltaZ = stepZ != 0 ? Mathf.Abs(1f / dz) : INF;
            float tMaxX = stepX > 0 ? (cx + 1 - a.x) / dx : (stepX < 0 ? (a.x - cx) / -dx : INF);
            float tMaxZ = stepZ > 0 ? (cz + 1 - a.y) / dz : (stepZ < 0 ? (a.y - cz) / -dz : INF);
            float tEnter = 0f;
            for (int guard = 0; guard < 20000; guard++)
            {
                if (BulletBlockedAt(cx, cz, out cover)) return tEnter;
                if (cx == ex && cz == ez) return 2f;
                if (tMaxX < tMaxZ) { if (tMaxX > 1f) return 2f; tEnter = tMaxX; cx += stepX; tMaxX += tDeltaX; }
                else { if (tMaxZ > 1f) return 2f; tEnter = tMaxZ; cz += stepZ; tMaxZ += tDeltaZ; }
            }
            return tEnter;
        }

        /// <summary>전체 맵 미니맵 텍스처 (px x px). 한 픽셀 = Size/px m, 칸 안에 엄폐물이 있으면 표시</summary>
        public static Texture2D BuildMinimap(int px)
        {
            var tex = new Texture2D(px, px, TextureFormat.RGBA32, false) { filterMode = FilterMode.Point, wrapMode = TextureWrapMode.Clamp };
            var cols = new Color32[px * px];
            var ground = new Color32(62, 70, 58, 255);
            var gridLine = new Color32(74, 83, 69, 255);
            var low = new Color32(222, 205, 150, 255);
            var wall = new Color32(20, 22, 26, 255);
            var dest = new Color32(170, 60, 50, 255);
            float scale = (float)px / Size;
            // 섹터(50m)마다 격자선 (전체 맵에 섹터 번호를 함께 표시)
            var sectorOf = new int[px];
            for (int i = 0; i < px; i++) sectorOf[i] = Mathf.FloorToInt(i / scale / SectorGrid.DefaultSectorSize);
            for (int y = 0; y < px; y++)
                for (int x = 0; x < px; x++)
                {
                    bool line = (x > 0 && sectorOf[x] != sectorOf[x - 1]) || (y > 0 && sectorOf[y] != sectorOf[y - 1]);
                    cols[y * px + x] = line ? gridLine : ground;
                }
            foreach (var type in new[] { Low, Dest, Wall })
                foreach (var r in Rects)
                {
                    if (r.Type != type) continue;
                    int x0 = Mathf.FloorToInt(r.X * scale), x1 = Mathf.Max(x0, Mathf.FloorToInt((r.X + r.W - 1) * scale));
                    int z0 = Mathf.FloorToInt(r.Z * scale), z1 = Mathf.Max(z0, Mathf.FloorToInt((r.Z + r.H - 1) * scale));
                    for (int z = z0; z <= z1 && z < px; z++)
                        for (int x = x0; x <= x1 && x < px; x++)
                            cols[z * px + x] = type == Wall ? wall : type == Dest ? dest : low;
                }
            tex.SetPixels32(cols);
            tex.Apply(false, true);
            return tex;
        }
    }
}

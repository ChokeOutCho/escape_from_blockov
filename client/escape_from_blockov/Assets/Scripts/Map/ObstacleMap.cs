using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 런타임 엄폐물 격자 (1m x 1m, 6400 x 6400). 에디터 도구(Blockov/Map/Obstacle Map Importer)가
    /// map/obstacles.bmp에서 만든 Resources/Map/obstacle_map.bytes(병합된 사각형 목록)를 읽어 비트셋으로 복원한다.
    ///  - Wall: 이동·총알 차단,  Low: 이동만 차단(총알 통과)
    /// 서버 GameServer/GameData.cpp의 ObstacleMap과 같은 규칙·같은 해시(FNV-1a)를 사용한다.
    /// </summary>
    public static class ObstacleMap
    {
        public const int Size = 6400;
        public const byte Empty = 0, Low = 1, Wall = 2;
        public const string ResourcePath = "Map/obstacle_map";

        public struct RectCell
        {
            public byte Type;
            public int X, Z, W, H;
        }

        static ulong[] _move;       // Low | Wall
        static ulong[] _bullet;     // Wall
        static bool _loadTried;

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
            var rects = ReadRects(data, out uint hash);
            int words = Size * Size / 64;
            _move = new ulong[words];
            _bullet = new ulong[words];
            Rects.Clear();
            foreach (var r in rects)
            {
                Rects.Add(r);
                for (int z = r.Z; z < r.Z + r.H; z++)
                    for (int x = r.X; x < r.X + r.W; x++)
                    {
                        long i = (long)z * Size + x;
                        ulong bit = 1UL << (int)(i & 63);
                        _move[i >> 6] |= bit;
                        if (r.Type == Wall) _bullet[i >> 6] |= bit;
                    }
            }
            Hash = hash;
            Loaded = true;
            Debug.Log($"[ObstacleMap] {rects.Count} rects, hash 0x{hash:X8}");
        }

        /// <summary>obstacle_map.bytes: "BKOM" u16 ver u16 size u32 hash u32 count {u8 type, u16 x,z,w,h}[count]</summary>
        public static List<RectCell> ReadRects(byte[] data, out uint hash)
        {
            using var br = new BinaryReader(new MemoryStream(data));
            if (new string(br.ReadChars(4)) != "BKOM") throw new InvalidDataException("obstacle_map: bad magic");
            ushort ver = br.ReadUInt16();
            ushort size = br.ReadUInt16();
            if (ver != 1 || size != Size) throw new InvalidDataException($"obstacle_map: unsupported ver {ver} size {size}");
            hash = br.ReadUInt32();
            int count = br.ReadInt32();
            var list = new List<RectCell>(count);
            for (int i = 0; i < count; i++)
                list.Add(new RectCell { Type = br.ReadByte(), X = br.ReadUInt16(), Z = br.ReadUInt16(), W = br.ReadUInt16(), H = br.ReadUInt16() });
            return list;
        }

        static bool Bit(ulong[] set, int x, int z)
        {
            if (x < 0 || z < 0 || x >= Size || z >= Size) return true;  // 월드 밖 = 막힘
            if (set == null) return false;
            long i = (long)z * Size + x;
            return (set[i >> 6] & (1UL << (int)(i & 63))) != 0;
        }

        public static bool BlocksMove(int x, int z) => Bit(_move, x, z);
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
        /// 총알 선분 a→b가 처음 들어가는 벽(Wall) 셀까지의 비율 t(0~1). 막히지 않으면 2.
        /// </summary>
        public static float RaycastBullet(Vector2 a, Vector2 b)
        {
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
                if (BlocksBullet(cx, cz)) return tEnter;
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
            var low = new Color32(170, 150, 110, 255);
            var wall = new Color32(20, 22, 26, 255);
            float scale = (float)px / Size;
            int gridEvery = Mathf.Max(1, Mathf.RoundToInt(640 * scale));   // 10섹터(640m)마다 격자선
            for (int y = 0; y < px; y++)
                for (int x = 0; x < px; x++)
                    cols[y * px + x] = (x % gridEvery == 0 || y % gridEvery == 0) ? gridLine : ground;
            foreach (var type in new[] { Low, Wall })
                foreach (var r in Rects)
                {
                    if (r.Type != type) continue;
                    int x0 = Mathf.FloorToInt(r.X * scale), x1 = Mathf.Max(x0, Mathf.FloorToInt((r.X + r.W - 1) * scale));
                    int z0 = Mathf.FloorToInt(r.Z * scale), z1 = Mathf.Max(z0, Mathf.FloorToInt((r.Z + r.H - 1) * scale));
                    for (int z = z0; z <= z1 && z < px; z++)
                        for (int x = x0; x <= x1 && x < px; x++)
                            cols[z * px + x] = type == Wall ? wall : low;
                }
            tex.SetPixels32(cols);
            tex.Apply(false, true);
            return tex;
        }
    }
}

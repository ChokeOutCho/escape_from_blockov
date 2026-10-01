using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Blockov.Game;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.SceneManagement;

namespace Blockov.EditorTools
{
    /// <summary>
    /// 엄폐물 맵 도구 (메뉴: Blockov/Map/...)
    ///  1) BMP(1픽셀 = 1m, 검정 = 벽, 회색 = 낮은 엄폐물, 빨강 = 파괴 가능 엄폐물, 흰색 = 빈 곳)를 읽어
    ///  2) 같은 셀 값의 인접 픽셀을 사각형으로 병합 + 파괴 가능 엄폐물 번호 매기기 → Resources/Map/obstacle_map.bytes (런타임 충돌·미니맵·엄폐물용)
    ///  3) 벽·낮은 엄폐물은 256m 청크 단위 메시를 만들어 Assets/Map/Generated/ObstacleChunks.asset에 저장
    ///  4) TestArena 씬의 "Obstacles" 오브젝트를 새로 구성하고, "Covers"(CoverManager: 실행 시 파괴 가능 엄폐물 상자 생성)를 두고 저장
    /// 서버는 같은 BMP를 직접 읽는다(GameServer/ObstacleMap.cpp). 판정 규칙·해시·엄폐물 번호는 서버와 동일해야 한다 (game-spec 3.3).
    /// </summary>
    public sealed class ObstacleMapImporter : EditorWindow
    {
        const int Size = ObstacleMap.Size;
        const string ArenaScene = "Assets/Scenes/TestArena.unity";
        const string BytesPath = "Assets/Resources/Map/obstacle_map.bytes";
        const string MeshAssetPath = "Assets/Map/Generated/ObstacleChunks.asset";
        const string WallMatPath = "Assets/Materials/Arena/M_Obstacle_Wall.mat";
        const string LowMatPath = "Assets/Materials/Arena/M_Obstacle_Low.mat";
        const string PrefBmp = "Blockov.ObstacleBmpPath";

        string _bmpPath;
        float _wallHeight = 2.5f;
        float _lowHeight = 1.1f;
        int _chunkSize = 256;

        [MenuItem("Blockov/Map/Obstacle Map Importer...", priority = 1)]
        static void Open() => GetWindow<ObstacleMapImporter>("Obstacle Map");

        public static string DefaultBmpPath =>
            Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "..", "map", "obstacles.bmp"));

        void OnEnable()
        {
            _bmpPath = EditorPrefs.GetString(PrefBmp, DefaultBmpPath);
        }

        void OnGUI()
        {
            EditorGUILayout.HelpBox(
                "BMP 규칙: 1픽셀 = 1m×1m, 이미지 좌하단 = 월드 (0,0), 위쪽 = 북쪽(+Z)\n" +
                "빨강(R≥150, G·B≤100) = 파괴 가능 엄폐물: 체력 = 10 × (1 + (R−150)×9/105), 같은 색으로 이어진 칸 = 1개\n" +
                "검정(밝기<64) = 벽: 이동·총알 차단\n회색(64~223) = 낮은 엄폐물: 이동만 차단\n흰색(≥224) = 빈 곳\n" +
                "그림판에서 '24비트 비트맵'으로 저장하세요(16색으로 저장하면 빨강 단계가 사라짐). 서버도 같은 파일을 읽습니다.",
                MessageType.Info);
            EditorGUILayout.BeginHorizontal();
            _bmpPath = EditorGUILayout.TextField("BMP 파일", _bmpPath);
            if (GUILayout.Button("...", GUILayout.Width(30)))
            {
                var p = EditorUtility.OpenFilePanel("장애물 BMP", Path.GetDirectoryName(_bmpPath), "bmp");
                if (!string.IsNullOrEmpty(p)) _bmpPath = p;
            }
            EditorGUILayout.EndHorizontal();
            _wallHeight = EditorGUILayout.FloatField("벽 높이 (m)", _wallHeight);
            _lowHeight = EditorGUILayout.FloatField("낮은 엄폐물 높이 (m)", _lowHeight);
            _chunkSize = EditorGUILayout.IntPopup("청크 크기 (m)", _chunkSize, new[] { "128", "256", "512" }, new[] { 128, 256, 512 });

            EditorGUILayout.Space();
            if (GUILayout.Button("BMP 가져오기 → 씬 엄폐물 갱신", GUILayout.Height(32)))
            {
                EditorPrefs.SetString(PrefBmp, _bmpPath);
                Debug.Log(Import(_bmpPath, _wallHeight, _lowHeight, _chunkSize));
            }
        }

        [MenuItem("Blockov/Map/Import Obstacles (default BMP)", priority = 2)]
        public static void ImportDefault()
        {
            Debug.Log(Import(EditorPrefs.GetString(PrefBmp, DefaultBmpPath), 2.5f, 1.1f, 256));
        }


        ////////////////////////////////////////////////////////////////////
        // 가져오기
        ////////////////////////////////////////////////////////////////////
        public static string Import(string bmpPath, float wallHeight, float lowHeight, int chunkSize)
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            byte[] cells = ReadBmp(bmpPath, out int imgW, out int imgH);
            uint hash = Fnv1a(cells);
            var coverOf = BuildCovers(cells, out var coverHp);
            var rects = MergeRects(cells, coverOf);
            long wallCells = 0, lowCells = 0, destCells = 0;
            foreach (var c in cells) { if (c == ObstacleMap.Wall) wallCells++; else if (c == ObstacleMap.Low) lowCells++; else if ((c & 3) == ObstacleMap.Dest) destCells++; }

            // 1) 런타임 데이터
            Directory.CreateDirectory(Path.GetDirectoryName(BytesPath));
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write(Encoding.ASCII.GetBytes("BKOM"));
                bw.Write((ushort)2);
                bw.Write((ushort)Size);
                bw.Write(hash);
                bw.Write(rects.Count);
                foreach (var r in rects)
                {
                    bw.Write(r.Type); bw.Write((ushort)r.X); bw.Write((ushort)r.Z); bw.Write((ushort)r.W); bw.Write((ushort)r.H); bw.Write(r.Cover);
                }
                bw.Write(coverHp.Count);
                foreach (var hp in coverHp) bw.Write(hp);
                File.WriteAllBytes(BytesPath, ms.ToArray());
            }
            AssetDatabase.ImportAsset(BytesPath);

            // 2) 청크 메시
            var chunks = new Dictionary<Vector2Int, List<ObstacleMap.RectCell>>();
            foreach (var r in rects)
            {
                if (r.Type == ObstacleMap.Dest) continue;     // 파괴 가능 엄폐물은 CoverManager가 실행 시 만든다 (높이가 바뀜)
                var key = new Vector2Int(r.X / chunkSize, r.Z / chunkSize);
                if (!chunks.TryGetValue(key, out var list)) chunks[key] = list = new List<ObstacleMap.RectCell>();
                list.Add(r);
            }
            Directory.CreateDirectory(Path.GetDirectoryName(MeshAssetPath));
            if (AssetDatabase.LoadAssetAtPath<UnityEngine.Object>(MeshAssetPath) != null)
                AssetDatabase.MoveAssetToTrash(MeshAssetPath);   // 이전 메시는 휴지통으로
            var container = ScriptableObject.CreateInstance<ObstacleChunkSet>();
            AssetDatabase.CreateAsset(container, MeshAssetPath);
            var meshes = new Dictionary<Vector2Int, Mesh>();
            foreach (var kv in chunks)
            {
                var mesh = BuildChunkMesh(kv.Value, wallHeight, lowHeight);
                mesh.name = $"ObstacleChunk_{kv.Key.x}_{kv.Key.y}";
                AssetDatabase.AddObjectToAsset(mesh, container);
                meshes[kv.Key] = mesh;
            }
            AssetDatabase.SaveAssets();

            // 3) 씬
            var wallMat = GetOrCreateMaterial(WallMatPath, new Color(0.2f, 0.21f, 0.25f));
            var lowMat = GetOrCreateMaterial(LowMatPath, new Color(0.66f, 0.58f, 0.42f));
            var scene = EnsureArenaOpen();
            var root = GameObject.Find("Obstacles");
            if (root != null) DestroyImmediate(root);
            root = new GameObject("Obstacles");
            var info = root.AddComponent<ObstacleMapInfo>();
            info.SourceBmp = bmpPath;
            info.Hash = $"0x{hash:X8}";
            info.RectCount = rects.Count;
            info.WallCells = wallCells;
            info.LowCells = lowCells;
            info.ImportedAt = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss");
            foreach (var kv in meshes)
            {
                var go = new GameObject(kv.Value.name);
                go.transform.SetParent(root.transform, false);
                go.AddComponent<MeshFilter>().sharedMesh = kv.Value;
                var mr = go.AddComponent<MeshRenderer>();
                mr.sharedMaterials = new[] { wallMat, lowMat };
                mr.shadowCastingMode = ShadowCastingMode.On;
                GameObjectUtility.SetStaticEditorFlags(go, StaticEditorFlags.BatchingStatic | StaticEditorFlags.OccluderStatic | StaticEditorFlags.OccludeeStatic);
            }
            info.CoverCount = coverHp.Count;
            info.DestCells = destCells;

            // 파괴 가능 엄폐물 루트 (CoverManager가 실행 시 obstacle_map.bytes로 상자를 만든다)
            var covers = GameObject.Find("Covers");
            if (covers == null) covers = new GameObject("Covers");
            if (covers.GetComponent<CoverManager>() == null) covers.AddComponent<CoverManager>();

            // 고정 스폰 지점은 쓰지 않는다 (서버가 인원 분포로 정함, game-spec 3장)
            var spawnRoot = GameObject.Find("SpawnPoints");
            if (spawnRoot != null) DestroyImmediate(spawnRoot);

            EditorSceneManager.MarkSceneDirty(scene);
            EditorSceneManager.SaveScene(scene);

            return $"[ObstacleMap] {bmpPath} ({imgW}x{imgH}) → wall {wallCells}, low {lowCells}, destructible {destCells} cells ({coverHp.Count} covers), {rects.Count} rects, " +
                   $"{meshes.Count} chunks, hash 0x{hash:X8}, {sw.ElapsedMilliseconds}ms";
        }

        static Scene EnsureArenaOpen()
        {
            var active = EditorSceneManager.GetActiveScene();
            if (active.path == ArenaScene) return active;
            EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo();
            return EditorSceneManager.OpenScene(ArenaScene, OpenSceneMode.Single);
        }

        static Material GetOrCreateMaterial(string path, Color color)
        {
            var mat = AssetDatabase.LoadAssetAtPath<Material>(path);
            if (mat != null) return mat;
            var shader = Shader.Find("Universal Render Pipeline/Lit") ?? Shader.Find("Standard");
            mat = new Material(shader) { enableInstancing = true };
            mat.SetColor("_BaseColor", color);
            mat.SetColor("_Color", color);
            if (mat.HasProperty("_Smoothness")) mat.SetFloat("_Smoothness", 0.1f);
            AssetDatabase.CreateAsset(mat, path);
            return mat;
        }

        ////////////////////////////////////////////////////////////////////
        // BMP (서버 ObstacleMap::LoadBmp와 동일 규칙)
        ////////////////////////////////////////////////////////////////////
        /// <summary>셀 값: Empty 0, Low 1, Wall 2, Dest 3 + level*16 (level 1~10, 체력 = level*10)</summary>
        public static byte Classify(byte r, byte g, byte b)
        {
            if (r >= 150 && g <= 100 && b <= 100)
            {
                int level = 1 + (r - 150) * 9 / 105;
                return (byte)(ObstacleMap.Dest + level * 16);
            }
            int lum = (299 * r + 587 * g + 114 * b) / 1000;
            if (lum < 64) return ObstacleMap.Wall;
            if (lum < 224) return ObstacleMap.Low;
            return ObstacleMap.Empty;
        }

        public static byte[] ReadBmp(string path, out int width, out int height)
        {
            byte[] b = File.ReadAllBytes(path);
            if (b.Length < 54 || b[0] != 'B' || b[1] != 'M') throw new InvalidDataException("BMP 파일이 아닙니다: " + path);
            uint offBits = BitConverter.ToUInt32(b, 10);
            uint hdrSize = BitConverter.ToUInt32(b, 14);
            width = BitConverter.ToInt32(b, 18);
            height = BitConverter.ToInt32(b, 22);
            int bpp = BitConverter.ToUInt16(b, 28);
            uint compression = BitConverter.ToUInt32(b, 30);
            uint clrUsed = hdrSize >= 40 ? BitConverter.ToUInt32(b, 46) : 0;
            bool topDown = height < 0;
            if (topDown) height = -height;
            if (!(compression == 0 || (compression == 3 && bpp == 32)))
                throw new InvalidDataException("압축 BMP는 지원하지 않습니다 (24비트/16색/단색 비트맵으로 저장)");
            if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)
                throw new InvalidDataException("지원하지 않는 색 깊이: " + bpp);

            byte[] pal = null;
            if (bpp <= 8)
            {
                int n = clrUsed != 0 ? (int)clrUsed : 1 << bpp;
                pal = new byte[n];
                int po = 14 + (int)hdrSize;
                for (int i = 0; i < n; i++) pal[i] = Classify(b[po + i * 4 + 2], b[po + i * 4 + 1], b[po + i * 4]);
            }
            int stride = ((width * bpp + 31) / 32) * 4;
            var cells = new byte[(long)Size * Size];
            int useW = Math.Min(width, Size), useH = Math.Min(height, Size);
            for (int row = 0; row < height; row++)
            {
                int rowFromTop = topDown ? row : height - 1 - row;
                int z = height - 1 - rowFromTop;
                if (z >= useH) continue;
                long p = offBits + (long)stride * row;
                for (int x = 0; x < useW; x++)
                {
                    byte t;
                    switch (bpp)
                    {
                        case 1: { int idx = (b[p + (x >> 3)] >> (7 - (x & 7))) & 1; t = idx < pal.Length ? pal[idx] : (byte)0; break; }
                        case 4: { int v = b[p + (x >> 1)]; int idx = (x & 1) != 0 ? v & 0x0F : v >> 4; t = idx < pal.Length ? pal[idx] : (byte)0; break; }
                        case 8: { int idx = b[p + x]; t = idx < pal.Length ? pal[idx] : (byte)0; break; }
                        case 24: t = Classify(b[p + x * 3 + 2], b[p + x * 3 + 1], b[p + x * 3]); break;
                        default: t = Classify(b[p + x * 4 + 2], b[p + x * 4 + 1], b[p + x * 4]); break;
                    }
                    cells[(long)z * Size + x] = t;
                }
            }
            return cells;
        }

        public static uint Fnv1a(byte[] cells)
        {
            uint h = 2166136261u;
            for (long i = 0; i < cells.LongLength; i++) { h ^= cells[i]; h *= 16777619u; }
            return h;
        }

        /// <summary>
        /// 파괴 가능 엄폐물 번호: 같은 셀 값으로 상하좌우 이어진 칸 = 1개, z→x 순서로 처음 만나는 묶음부터 0,1,2... (서버 BuildCovers와 동일)
        /// 반환: 셀 → 엄폐물 id + 1 (0 = 없음), coverHp: id별 최대 체력
        /// </summary>
        public static ushort[] BuildCovers(byte[] cells, out List<byte> coverHp)
        {
            var coverOf = new ushort[cells.LongLength];
            coverHp = new List<byte>();
            var stack = new Stack<int>();
            for (int z = 0; z < Size; z++)
                for (int x = 0; x < Size; x++)
                {
                    int i = z * Size + x;
                    byte v = cells[i];
                    if ((v & 3) != ObstacleMap.Dest || coverOf[i] != 0) continue;
                    if (coverHp.Count >= 65000) throw new InvalidDataException("파괴 가능 엄폐물이 너무 많습니다 (최대 65000)");
                    ushort mark = (ushort)(coverHp.Count + 1);
                    coverHp.Add((byte)((v >> 4) * 10));
                    coverOf[i] = mark;
                    stack.Push(i);
                    while (stack.Count > 0)
                    {
                        int cur = stack.Pop();
                        int cx = cur % Size, cz = cur / Size;
                        if (cx + 1 < Size) Visit(cur + 1);
                        if (cx - 1 >= 0) Visit(cur - 1);
                        if (cz + 1 < Size) Visit(cur + Size);
                        if (cz - 1 >= 0) Visit(cur - Size);
                    }
                    void Visit(int j)
                    {
                        if (cells[j] != v || coverOf[j] != 0) return;
                        coverOf[j] = mark;
                        stack.Push(j);
                    }
                }
            return coverOf;
        }

        /// <summary>같은 셀 값의 인접 셀을 사각형으로 탐욕 병합 (행 방향 → 열 방향). Type = 종류(하위 2비트), Cover = 엄폐물 id</summary>
        public static List<ObstacleMap.RectCell> MergeRects(byte[] cells, ushort[] coverOf)
        {
            var done = new bool[cells.LongLength];
            var list = new List<ObstacleMap.RectCell>();
            for (int z = 0; z < Size; z++)
            {
                long row = (long)z * Size;
                for (int x = 0; x < Size; x++)
                {
                    long i = row + x;
                    byte t = cells[i];
                    if (t == 0 || done[i]) continue;
                    int w = 1;
                    while (x + w < Size && cells[i + w] == t && !done[i + w] && w < 65535) w++;
                    int h = 1;
                    while (z + h < Size && h < 65535)
                    {
                        long r2 = (long)(z + h) * Size + x;
                        bool ok = true;
                        for (int k = 0; k < w; k++)
                            if (cells[r2 + k] != t || done[r2 + k]) { ok = false; break; }
                        if (!ok) break;
                        h++;
                    }
                    for (int dz = 0; dz < h; dz++)
                        for (int dx = 0; dx < w; dx++)
                            done[(long)(z + dz) * Size + x + dx] = true;
                    byte type = (byte)(t & 3);
                    ushort cover = type == ObstacleMap.Dest ? (ushort)(coverOf[i] - 1) : ObstacleMap.NoCover;
                    list.Add(new ObstacleMap.RectCell { Type = type, X = x, Z = z, W = w, H = h, Cover = cover });
                    x += w - 1;
                }
            }
            return list;
        }

        static Mesh BuildChunkMesh(List<ObstacleMap.RectCell> rects, float wallH, float lowH)
        {
            var v = new List<Vector3>();
            var n = new List<Vector3>();
            var uv = new List<Vector2>();     // 윗면 = 월드 X·Z, 옆면 = 둘레 방향·높이 (1m = 1). 낮은 엄폐물 줄무늬 텍스처용
            var wallTris = new List<int>();
            var lowTris = new List<int>();
            Vector2 UV(Vector3 p, Vector3 normal)
            {
                if (normal == Vector3.up) return new Vector2(p.x, p.z);
                return new Vector2(Mathf.Abs(normal.x) > 0.5f ? p.z : p.x, p.y);
            }
            void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 normal, List<int> tris)
            {
                int s = v.Count;
                v.Add(a); v.Add(b); v.Add(c); v.Add(d);
                n.Add(normal); n.Add(normal); n.Add(normal); n.Add(normal);
                uv.Add(UV(a, normal)); uv.Add(UV(b, normal)); uv.Add(UV(c, normal)); uv.Add(UV(d, normal));
                tris.Add(s); tris.Add(s + 1); tris.Add(s + 2);
                tris.Add(s); tris.Add(s + 2); tris.Add(s + 3);
            }
            foreach (var r in rects)
            {
                var tris = r.Type == ObstacleMap.Wall ? wallTris : lowTris;
                float h = r.Type == ObstacleMap.Wall ? wallH : lowH;
                float x0 = r.X, x1 = r.X + r.W, z0 = r.Z, z1 = r.Z + r.H;
                Quad(new Vector3(x0, h, z0), new Vector3(x0, h, z1), new Vector3(x1, h, z1), new Vector3(x1, h, z0), Vector3.up, tris);          // 윗면
                Quad(new Vector3(x0, 0, z0), new Vector3(x0, h, z0), new Vector3(x1, h, z0), new Vector3(x1, 0, z0), Vector3.back, tris);        // 남
                Quad(new Vector3(x1, 0, z1), new Vector3(x1, h, z1), new Vector3(x0, h, z1), new Vector3(x0, 0, z1), Vector3.forward, tris);     // 북
                Quad(new Vector3(x0, 0, z1), new Vector3(x0, h, z1), new Vector3(x0, h, z0), new Vector3(x0, 0, z0), Vector3.left, tris);        // 서
                Quad(new Vector3(x1, 0, z0), new Vector3(x1, h, z0), new Vector3(x1, h, z1), new Vector3(x1, 0, z1), Vector3.right, tris);       // 동
            }
            var mesh = new Mesh { indexFormat = v.Count > 65000 ? IndexFormat.UInt32 : IndexFormat.UInt16 };
            mesh.SetVertices(v);
            mesh.SetNormals(n);
            mesh.SetUVs(0, uv);
            mesh.subMeshCount = 2;
            mesh.SetTriangles(wallTris, 0);
            mesh.SetTriangles(lowTris, 1);
            mesh.RecalculateBounds();
            mesh.UploadMeshData(false);
            return mesh;
        }
    }
}

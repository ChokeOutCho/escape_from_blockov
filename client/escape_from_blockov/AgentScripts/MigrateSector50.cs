using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// TestArena를 섹터 50m x 30x30 (월드 1500m)로 옮긴다 (2026-10-01).
//  - SectorGrid 50/30, 바닥 재생성, 외벽 4개 재배치
//  - 스폰: server/GameServer/spawns.txt(7x7) 기준으로 Spawn_NN_Sxx_yy 재배치 (남는 오브젝트 제거)
//  - 엄폐물: map/obstacles.bmp(1500x1500) 다시 가져오기
public static class MigrateSector50
{
    public static string Run()
    {
        var sb = new System.Text.StringBuilder();
        var scene = EditorSceneManager.OpenScene("Assets/Scenes/TestArena.unity", OpenSceneMode.Single);
        var grid = Object.FindAnyObjectByType<SectorGrid>();
        grid.sectorSize = 50; grid.sectorCountX = 30; grid.sectorCountY = 30;
        EditorUtility.SetDirty(grid);
        sb.AppendLine("SectorGrid -> 50m, 30x30");
        sb.AppendLine(Blockov.EditorTools.MapSetupTools.RebuildGround());

        // 외벽 (두께 2, 높이 10, 월드 바깥쪽)
        float W = grid.WorldSizeX;
        void Wall(string name, Vector3 pos, Vector3 scale)
        {
            var t = GameObject.Find(name)?.transform;
            if (t == null) { sb.AppendLine("missing " + name); return; }
            t.position = pos; t.localScale = scale;
            EditorUtility.SetDirty(t);
        }
        Wall("Wall_E", new Vector3(W + 1, 5, W / 2), new Vector3(2, 10, W));
        Wall("Wall_W", new Vector3(-1, 5, W / 2), new Vector3(2, 10, W));
        Wall("Wall_N", new Vector3(W / 2, 5, W + 1), new Vector3(W + 4, 10, 2));
        Wall("Wall_S", new Vector3(W / 2, 5, -1), new Vector3(W + 4, 10, 2));

        // 스폰
        var path = Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "..", "server", "GameServer", "spawns.txt"));
        var list = new System.Collections.Generic.List<Vector2Int>();
        foreach (var raw in File.ReadAllLines(path, System.Text.Encoding.GetEncoding(949)))
        {
            var line = raw.Split('#')[0].Trim();
            if (line.Length == 0) continue;
            var p = line.Split(new[] { ' ', '\t' }, System.StringSplitOptions.RemoveEmptyEntries);
            if (p.Length >= 2) list.Add(new Vector2Int(int.Parse(p[0]), int.Parse(p[1])));
        }
        var sp = GameObject.Find("SpawnPoints").transform;
        var children = new System.Collections.Generic.List<Transform>();
        foreach (Transform t in sp) children.Add(t);
        for (int i = 0; i < children.Count; i++)
        {
            var t = children[i];
            if (i >= list.Count) { Object.DestroyImmediate(t.gameObject); continue; }
            var s = list[i];
            t.name = $"Spawn_{i:00}_S{s.x:00}_{s.y:00}";
            t.position = new Vector3((s.x + 0.5f) * grid.sectorSize, t.position.y, (s.y + 0.5f) * grid.sectorSize);
            EditorUtility.SetDirty(t);
        }
        sb.AppendLine($"spawns: {System.Math.Min(children.Count, list.Count)} placed, {System.Math.Max(0, children.Count - list.Count)} removed");

        EditorSceneManager.MarkSceneDirty(scene);
        EditorSceneManager.SaveScene(scene);

        // 엄폐물 다시 가져오기 (씬 저장 포함)
        sb.AppendLine(Blockov.EditorTools.ObstacleMapImporter.Import(Blockov.EditorTools.ObstacleMapImporter.DefaultBmpPath, 2.5f, 0.9f, 256));
        return sb.ToString();
    }
}

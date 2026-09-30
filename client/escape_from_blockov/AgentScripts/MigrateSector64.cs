using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using System.Text.RegularExpressions;

public static class MigrateSector64
{
    public static string Run()
    {
        var sb = new System.Text.StringBuilder();
        var scene = EditorSceneManager.OpenScene("Assets/Scenes/TestArena.unity", OpenSceneMode.Single);
        var grid = Object.FindAnyObjectByType<SectorGrid>();
        int oldSize = grid.sectorSize;
        grid.sectorSize = 64; grid.sectorCountX = 100; grid.sectorCountY = 100;
        EditorUtility.SetDirty(grid);
        sb.AppendLine($"SectorGrid {oldSize} -> 64, 100x100");
        sb.AppendLine(Blockov.EditorTools.MapSetupTools.RebuildGround());

        var sp = GameObject.Find("SpawnPoints");
        int n = 0;
        var re = new Regex(@"^Spawn_(\d+)_S(\d+)_(\d+)$");
        foreach (Transform t in sp.transform)
        {
            var m = re.Match(t.name);
            if (!m.Success) { sb.AppendLine("skip " + t.name); continue; }
            int sx = int.Parse(m.Groups[2].Value), sy = int.Parse(m.Groups[3].Value);
            if (oldSize == 128) { sx *= 2; sy *= 2; }
            t.name = $"Spawn_{m.Groups[1].Value}_S{sx:00}_{sy:00}";
            t.position = new Vector3((sx + 0.5f) * 64f, t.position.y, (sy + 0.5f) * 64f);
            EditorUtility.SetDirty(t);
            n++;
        }
        sb.AppendLine($"spawns moved: {n}");
        EditorSceneManager.MarkSceneDirty(scene);
        EditorSceneManager.SaveScene(scene);
        return sb.ToString();
    }
}

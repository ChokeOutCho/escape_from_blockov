using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using System.Linq;

public static class SetupScenes
{
    public static string Run()
    {
        var log = new System.Text.StringBuilder();

        // 1) Title 씬
        var title = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var camGo = new GameObject("Main Camera");
        camGo.tag = "MainCamera";
        var cam = camGo.AddComponent<Camera>();
        cam.clearFlags = CameraClearFlags.SolidColor;
        cam.backgroundColor = new Color(0.07f, 0.08f, 0.11f);
        var net = new GameObject("NetworkManager");
        net.AddComponent<Blockov.Net.NetworkManager>();
        net.AddComponent<Blockov.Net.LatencyHUD>();
        new GameObject("Title").AddComponent<Blockov.Game.TitleController>();
        EditorSceneManager.SaveScene(title, "Assets/Scenes/Title.unity");
        log.AppendLine("Title saved");

        // 2) TestArena: NetworkManager 제거, GameController 추가
        var arena = EditorSceneManager.OpenScene("Assets/Scenes/TestArena.unity", OpenSceneMode.Single);
        foreach (var root in arena.GetRootGameObjects())
        {
            if (root.GetComponent<Blockov.Net.NetworkManager>() != null) { Object.DestroyImmediate(root); log.AppendLine("removed NetworkManager from arena"); }
        }
        if (Object.FindFirstObjectByType<Blockov.Game.GameController>() == null)
        {
            new GameObject("GameController").AddComponent<Blockov.Game.GameController>();
            log.AppendLine("added GameController");
        }
        var mainCam = Camera.main;
        if (mainCam != null && mainCam.GetComponent<Blockov.Game.CameraRig>() == null)
        {
            mainCam.gameObject.AddComponent<Blockov.Game.CameraRig>();
            log.AppendLine("added CameraRig to Main Camera");
        }
        EditorSceneManager.SaveScene(arena);

        // 3) 빌드 목록: Title(0), TestArena(1). SampleScene 제외(파일 유지)
        EditorBuildSettings.scenes = new[]
        {
            new EditorBuildSettingsScene("Assets/Scenes/Title.unity", true),
            new EditorBuildSettingsScene("Assets/Scenes/TestArena.unity", true),
        };
        log.AppendLine("build scenes: " + string.Join(", ", EditorBuildSettings.scenes.Select(s => s.path)));

        // 4) 플레이어 설정
        PlayerSettings.runInBackground = true;
        log.AppendLine("runInBackground=true");
        return log.ToString();
    }
}

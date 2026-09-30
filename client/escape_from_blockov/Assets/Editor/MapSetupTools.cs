using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace Blockov.EditorTools
{
    /// <summary>
    /// 맵 기반 도구 (메뉴: Blockov/Map/Rebuild Sector Ground)
    ///  - 섹터 크기(SectorGrid.sectorSize) 기준 체커 무늬 바닥을 한 장의 메시로 다시 만든다.
    ///    (섹터마다 오브젝트를 두던 방식은 64m 섹터에서 1만 개가 되어 WebGL에 부담)
    /// </summary>
    public static class MapSetupTools
    {
        const string TexPath = "Assets/Materials/Arena/T_GroundChecker.png";
        const string MatPath = "Assets/Materials/Arena/M_Ground_Checker.mat";

        [MenuItem("Blockov/Map/Rebuild Sector Ground", priority = 20)]
        public static void RebuildGroundMenu() => Debug.Log(RebuildGround());

        public static string RebuildGround()
        {
            var grid = Object.FindAnyObjectByType<SectorGrid>();
            if (grid == null) return "SectorGrid가 있는 씬(TestArena)을 여세요";
            int count = grid.sectorCountX;

            // 1) 체커 텍스처 (1텍셀 = 1섹터)
            var tex = new Texture2D(count, grid.sectorCountY, TextureFormat.RGBA32, false);
            var a = new Color(0.36f, 0.4f, 0.33f);
            var b = new Color(0.32f, 0.36f, 0.29f);
            for (int y = 0; y < grid.sectorCountY; y++)
                for (int x = 0; x < count; x++)
                    tex.SetPixel(x, y, ((x + y) & 1) == 0 ? a : b);
            tex.Apply();
            File.WriteAllBytes(TexPath, tex.EncodeToPNG());
            Object.DestroyImmediate(tex);
            AssetDatabase.ImportAsset(TexPath);
            var imp = (TextureImporter)AssetImporter.GetAtPath(TexPath);
            imp.filterMode = FilterMode.Point;
            imp.wrapMode = TextureWrapMode.Clamp;
            imp.mipmapEnabled = false;
            imp.textureCompression = TextureImporterCompression.Uncompressed;
            imp.npotScale = TextureImporterNPOTScale.None;
            imp.SaveAndReimport();
            var gtex = AssetDatabase.LoadAssetAtPath<Texture2D>(TexPath);

            // 2) 머티리얼
            var mat = AssetDatabase.LoadAssetAtPath<Material>(MatPath);
            if (mat == null)
            {
                mat = new Material(Shader.Find("Universal Render Pipeline/Lit"));
                AssetDatabase.CreateAsset(mat, MatPath);
            }
            mat.SetTexture("_BaseMap", gtex);
            mat.SetColor("_BaseColor", Color.white);
            mat.SetFloat("_Smoothness", 0.05f);
            EditorUtility.SetDirty(mat);

            // 3) 바닥 오브젝트: 기존 섹터별 오브젝트(Sectors) 제거 후 쿼드 1장
            var root = grid.transform;
            var old = root.Find("Sectors");
            int removed = 0;
            if (old != null) { removed = old.GetComponentsInChildren<Transform>(true).Length; Object.DestroyImmediate(old.gameObject); }
            var ground = root.Find("Ground");
            if (ground != null) Object.DestroyImmediate(ground.gameObject);
            var go = GameObject.CreatePrimitive(PrimitiveType.Quad);
            go.name = "Ground";
            Object.DestroyImmediate(go.GetComponent<Collider>());
            go.transform.SetParent(root, false);
            go.transform.position = new Vector3(grid.WorldSizeX * 0.5f, 0f, grid.WorldSizeY * 0.5f);
            go.transform.rotation = Quaternion.Euler(90, 0, 0);
            go.transform.localScale = new Vector3(grid.WorldSizeX, grid.WorldSizeY, 1);
            go.GetComponent<MeshRenderer>().sharedMaterial = mat;
            GameObjectUtility.SetStaticEditorFlags(go, StaticEditorFlags.BatchingStatic);

            EditorSceneManager.MarkSceneDirty(grid.gameObject.scene);
            return $"[Map] ground rebuilt: {count}x{grid.sectorCountY} sectors of {grid.sectorSize}m (removed {removed} old objects)";
        }
    }
}

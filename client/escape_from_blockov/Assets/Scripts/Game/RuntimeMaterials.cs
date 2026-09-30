using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 런타임에 만드는 오브젝트(캐릭터·총·탄)용 공용 머티리얼.
    /// GameObject.CreatePrimitive의 기본 머티리얼은 빌트인 Standard 셰이더라 URP 플레이어 빌드(WebGL)에서
    /// 셰이더가 빠져 분홍색으로 나온다 → Resources의 URP Lit 머티리얼 에셋을 대신 쓴다
    /// (에셋이 빌드에 포함되므로 셰이더·배리언트도 함께 포함됨). 색은 MaterialPropertyBlock(_BaseColor)으로 지정.
    /// </summary>
    public static class RuntimeMaterials
    {
        const string LitPath = "Materials/M_RuntimeLit";   // Assets/Resources/Materials/M_RuntimeLit.mat
        static Material _lit;

        public static Material Lit
        {
            get
            {
                if (_lit == null)
                {
                    _lit = Resources.Load<Material>(LitPath);
                    if (_lit == null)
                    {
                        Debug.LogError($"[RuntimeMaterials] Resources/{LitPath} 없음 → 분홍색으로 보일 수 있음");
                        var sh = Shader.Find("Universal Render Pipeline/Lit");
                        if (sh != null) _lit = new Material(sh);
                    }
                }
                return _lit;
            }
        }

        /// <summary>CreatePrimitive로 만든 오브젝트의 렌더러에 공용 Lit 머티리얼 지정</summary>
        public static void Apply(GameObject go)
        {
            var r = go.GetComponent<Renderer>();
            if (r != null && Lit != null) r.sharedMaterial = Lit;
        }
    }
}

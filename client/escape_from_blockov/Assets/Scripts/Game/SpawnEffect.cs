using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 스폰 이펙트 (game-spec 12.4): 흰색 반투명 빛 기둥이 내려앉으며 가늘어지고, 바닥 링이 퍼지며 약 1초 뒤 사라진다. 스폰 소리.
    /// 캐릭터가 기둥 안에서 보이도록 크기는 1.5배(기둥 지름 최대 2.1m, 링 최대 6m), 기둥 불투명도 최대 0.45.
    /// 반투명 머티리얼은 Resources/Materials/M_FxTransparent (URP Unlit, Transparent). 없으면 공용 Lit(불투명)으로 대신한다.
    /// </summary>
    public sealed class SpawnEffect : MonoBehaviour
    {
        const string MaterialPath = "Materials/M_FxTransparent";
        const float Life = 1.0f;
        static readonly Color FxColor = Color.white;
        const float Scale = 1.5f;
        static readonly int BaseColorId = Shader.PropertyToID("_BaseColor");
        static readonly int ColorId = Shader.PropertyToID("_Color");
        static Material _mat;

        Transform _pillar, _ring;
        Renderer _pillarR, _ringR;
        MaterialPropertyBlock _mpb;
        float _born;

        public static void Play(Vector2 worldXZ)
        {
            if (_mat == null)
            {
                _mat = Resources.Load<Material>(MaterialPath);
                if (_mat == null) _mat = RuntimeMaterials.Lit;
            }
            var go = new GameObject("SpawnEffect");
            go.transform.position = new Vector3(worldXZ.x, 0f, worldXZ.y);
            var fx = go.AddComponent<SpawnEffect>();
            fx._pillar = fx.Part(PrimitiveType.Cylinder, out fx._pillarR);
            fx._ring = fx.Part(PrimitiveType.Cylinder, out fx._ringR);
            fx._mpb = new MaterialPropertyBlock();
            fx._born = Time.time;
            fx.Apply(0f);
            SoundManager.PlayAt(SoundManager.Spawn, worldXZ);
        }

        Transform Part(PrimitiveType type, out Renderer rend)
        {
            var p = GameObject.CreatePrimitive(type);
            Destroy(p.GetComponent<Collider>());
            rend = p.GetComponent<Renderer>();
            rend.sharedMaterial = _mat;
            rend.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            rend.receiveShadows = false;
            p.transform.SetParent(transform, false);
            return p.transform;
        }

        void Update()
        {
            float t = (Time.time - _born) / Life;
            if (t >= 1f) { Destroy(gameObject); return; }
            Apply(t);
        }

        void Apply(float t)
        {
            // 기둥: 위(높이 8m)에서 내려와 0.3초 만에 바닥에 닿고, 이후 가늘어지며 사라짐
            float drop = Mathf.Clamp01(t / 0.3f);
            float height = Mathf.Lerp(1.5f, 8f, drop);
            float width = Mathf.Lerp(1.4f, 0.05f, Mathf.Clamp01((t - 0.25f) / 0.75f)) * Scale;
            float top = Mathf.Lerp(9.5f, 8f, drop);
            _pillar.localScale = new Vector3(width, height * 0.5f, width);       // 실린더 높이 = 2 × scale.y
            _pillar.localPosition = new Vector3(0f, top - height * 0.5f, 0f);
            // 바닥 링: 기둥이 닿은 뒤 퍼짐
            float ring = Mathf.Clamp01((t - 0.2f) / 0.8f);
            float d = Mathf.Lerp(0.6f, 4.0f, ring) * Scale;
            _ring.localScale = new Vector3(d, 0.02f, d);
            _ring.localPosition = new Vector3(0f, 0.03f, 0f);

            float a = t < 0.3f ? 1f : Mathf.Lerp(1f, 0f, (t - 0.3f) / 0.7f);
            SetColor(_pillarR, new Color(FxColor.r, FxColor.g, FxColor.b, a * 0.45f));
            SetColor(_ringR, new Color(FxColor.r, FxColor.g, FxColor.b, a * 0.8f * (1f - ring * 0.6f)));
        }

        void SetColor(Renderer r, Color c)
        {
            _mpb.SetColor(BaseColorId, c);
            _mpb.SetColor(ColorId, c);
            r.SetPropertyBlock(_mpb);
        }
    }
}

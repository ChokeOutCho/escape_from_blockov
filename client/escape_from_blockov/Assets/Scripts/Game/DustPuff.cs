using UnityEngine;

namespace Blockov.Game
{
    /// <summary>구르기 먼지 잔상 (game-spec 20.1): 회색 구가 0.5초 동안 커졌다가 작아지며 사라진다</summary>
    public sealed class DustPuff : MonoBehaviour
    {
        static readonly Color DustColor = new Color(0.62f, 0.58f, 0.5f);
        const float Life = 0.5f;
        float _born, _size;

        public static void Spawn(Vector3 feet, float size)
        {
            var go = GameObject.CreatePrimitive(PrimitiveType.Sphere);
            Destroy(go.GetComponent<Collider>());
            RuntimeMaterials.Apply(go);
            var r = go.GetComponent<Renderer>();
            r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            var mpb = new MaterialPropertyBlock();
            mpb.SetColor("_BaseColor", DustColor);
            mpb.SetColor("_Color", DustColor);
            r.SetPropertyBlock(mpb);
            go.name = "DustPuff";
            go.transform.position = feet + new Vector3(Random.Range(-0.2f, 0.2f), 0.25f, Random.Range(-0.2f, 0.2f));
            var d = go.AddComponent<DustPuff>();
            d._born = Time.time;
            d._size = size;
            go.transform.localScale = Vector3.one * size * 0.5f;
        }

        void Update()
        {
            float t = (Time.time - _born) / Life;
            if (t >= 1f) { Destroy(gameObject); return; }
            float s = _size * (t < 0.3f ? Mathf.Lerp(0.5f, 1f, t / 0.3f) : Mathf.Lerp(1f, 0.05f, (t - 0.3f) / 0.7f));
            transform.localScale = Vector3.one * s;
            transform.position += Vector3.up * Time.deltaTime * 0.6f;
        }
    }
}

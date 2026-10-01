using System.Collections.Generic;
using Blockov.Net;
using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 파괴 가능한 엄폐물 (game-spec 3.6, 씬 TestArena의 "Covers" 오브젝트).
    ///  - 시작 시 obstacle_map.bytes의 엄폐물마다 상자를 만든다 (멀쩡 2.0m, 반 블럭 1.0m, 재질 Resources/Materials/M_Cover)
    ///  - SC_COVER_HP: 체력 바(3초) / SC_COVER_STATE: 파괴·재생 → 높이·총알 판정(ObstacleMap.SetDestroyed)·재생 게이지
    ///  - 재생 게이지: 서버가 알려준 파괴 시각 + RegenSec 기준으로 남은 초를 1초 단위로 표시 (GameHUD가 그림)
    /// </summary>
    public sealed class CoverManager : MonoBehaviour
    {
        public const float IntactHeight = 2.0f, BrokenHeight = 1.0f;
        public const float HpBarSeconds = 3f;
        const string MaterialPath = "Materials/M_Cover";

        public static CoverManager Instance { get; private set; }

        public sealed class State
        {
            public int Id;
            public Vector2 Center;
            public byte Hp, MaxHp;
            public float LastHitTime = -100f;
            public bool Destroyed;
            public double DestroyedAtMs;    // 서버 시각
            public int RegenSec;
            public Transform View;
        }

        State[] _states = new State[0];
        readonly HashSet<int> _active = new HashSet<int>();   // 체력 바 또는 재생 게이지를 그릴 엄폐물
        static readonly int BaseColorId = Shader.PropertyToID("_BaseColor");
        static readonly int ColorId = Shader.PropertyToID("_Color");
        MaterialPropertyBlock _mpbIntact, _mpbBroken;

        /// <summary>화면에 표시할 엄폐물 (최근 피격 또는 파괴됨)</summary>
        public IEnumerable<State> Active
        {
            get { foreach (int id in _active) yield return _states[id]; }
        }

        void Awake() => Instance = this;

        void OnDestroy()
        {
            if (Instance == this) Instance = null;
        }

        void Start()
        {
            ObstacleMap.EnsureLoaded();
            Build();
        }

        void Build()
        {
            int n = ObstacleMap.CoverCount;
            _states = new State[n];
            var mat = Resources.Load<Material>(MaterialPath);
            if (mat == null)
            {
                Debug.LogWarning($"[CoverManager] Resources/{MaterialPath} 없음 → 공용 Lit 머티리얼 사용");
                mat = RuntimeMaterials.Lit;
            }
            _mpbIntact = new MaterialPropertyBlock();
            _mpbBroken = new MaterialPropertyBlock();
            var broken = new Color(0.36f, 0.2f, 0.16f);
            _mpbBroken.SetColor(BaseColorId, broken);
            _mpbBroken.SetColor(ColorId, broken);

            for (int id = 0; id < n; id++)
            {
                var rects = ObstacleMap.CoverRects[id];
                var root = new GameObject($"Cover_{id}").transform;
                root.SetParent(transform, false);
                Vector2 min = new Vector2(float.MaxValue, float.MaxValue), max = new Vector2(float.MinValue, float.MinValue);
                foreach (var r in rects)
                {
                    var box = GameObject.CreatePrimitive(PrimitiveType.Cube);
                    Destroy(box.GetComponent<Collider>());
                    box.GetComponent<Renderer>().sharedMaterial = mat;
                    box.transform.SetParent(root, false);
                    // 부모 y 스케일 = 높이 → 자식은 높이 1 기준 (아래 면이 지면)
                    box.transform.localPosition = new Vector3(r.X + r.W * 0.5f, 0.5f, r.Z + r.H * 0.5f);
                    box.transform.localScale = new Vector3(r.W - 0.04f, 1f, r.H - 0.04f);
                    min = Vector2.Min(min, new Vector2(r.X, r.Z));
                    max = Vector2.Max(max, new Vector2(r.X + r.W, r.Z + r.H));
                }
                root.localScale = new Vector3(1f, IntactHeight, 1f);
                _states[id] = new State
                {
                    Id = id,
                    Center = (min + max) * 0.5f,
                    Hp = ObstacleMap.CoverMaxHp[id],
                    MaxHp = ObstacleMap.CoverMaxHp[id],
                    View = root,
                };
            }
            Debug.Log($"[CoverManager] 파괴 가능 엄폐물 {n}개");
        }

        public State Get(int id) => id >= 0 && id < _states.Length ? _states[id] : null;

        /// <summary>SC_COVER_HP: WORD CoverId, BYTE Hp</summary>
        public void OnCoverHp(PacketReader r)
        {
            int id = r.ReadUInt16();
            byte hp = r.ReadByte();
            var s = Get(id);
            if (s == null) return;
            s.Hp = hp;
            s.LastHitTime = Time.time;
            _active.Add(id);
        }

        /// <summary>SC_COVER_STATE: BYTE Count, {WORD CoverId, BYTE Destroyed, UINT32 DestroyedAtMs, WORD RegenSec}[n]</summary>
        public void OnCoverState(PacketReader r)
        {
            int count = r.ReadByte();
            for (int i = 0; i < count; i++)
            {
                int id = r.ReadUInt16();
                bool destroyed = r.ReadByte() != 0;
                uint at = r.ReadUInt32();
                int regen = r.ReadUInt16();
                var s = Get(id);
                if (s == null) continue;
                bool changed = s.Destroyed != destroyed;
                s.Destroyed = destroyed;
                s.DestroyedAtMs = at;
                s.RegenSec = regen;
                s.Hp = destroyed ? (byte)0 : s.MaxHp;
                ObstacleMap.SetDestroyed(id, destroyed);
                SetVisual(s);
                if (destroyed) _active.Add(id);
                else { _active.Remove(id); s.LastHitTime = -100f; }
                if (changed && GameController.Instance != null)
                {
                    if (destroyed) SoundManager.PlayAt(SoundManager.CoverBreak, s.Center);
                    DustPuff.Burst(new Vector3(s.Center.x, 0.6f, s.Center.y), destroyed ? 6 : 3);
                }
            }
        }

        void SetVisual(State s)
        {
            if (s.View == null) return;
            s.View.localScale = new Vector3(1f, s.Destroyed ? BrokenHeight : IntactHeight, 1f);
            foreach (Transform c in s.View)
            {
                var rend = c.GetComponent<Renderer>();
                if (s.Destroyed) rend.SetPropertyBlock(_mpbBroken);
                else rend.SetPropertyBlock(null);
            }
        }

        void Update()
        {
            if (_active.Count == 0) return;
            List<int> done = null;
            foreach (int id in _active)
            {
                var s = _states[id];
                if (!s.Destroyed && Time.time - s.LastHitTime > HpBarSeconds) (done ??= new List<int>()).Add(id);
            }
            if (done != null) foreach (int id in done) _active.Remove(id);
        }

        /// <summary>재생까지 남은 초 (1초 단위, 0이면 재생 대기 중)</summary>
        public static int RemainingSeconds(State s)
        {
            var net = NetworkManager.Instance;
            if (net == null) return 0;
            double remainMs = s.DestroyedAtMs + s.RegenSec * 1000.0 - net.EstServerNow;
            return Mathf.Max(0, Mathf.CeilToInt((float)(remainMs / 1000.0)));
        }
    }
}

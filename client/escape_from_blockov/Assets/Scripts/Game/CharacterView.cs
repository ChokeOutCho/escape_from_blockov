using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 캐릭터 공통 표시: Capsule(높이 2, 반지름 0.5) + 조준 방향 표시, 이름·HP(HUD가 그림), 사망 연출.
    /// 판정은 X-Z 평면 원(반지름 CharacterRadius). 게임 로직 좌표는 (x, z), y는 표시용.
    /// </summary>
    public class CharacterView : MonoBehaviour
    {
        static readonly int BaseColorId = Shader.PropertyToID("_BaseColor");
        static readonly int ColorId = Shader.PropertyToID("_Color");

        public uint PlayerId { get; private set; }
        public string DisplayName { get; set; } = "";
        public ushort Hp { get; set; }
        public ushort MaxHp { get; set; } = 100;
        public byte WeaponId { get; set; }
        public bool IsLocal { get; private set; }
        public bool IsDying { get; private set; }
        public float AimAngle { get; protected set; }    // 도, +X 기준 반시계
        public float LastDamageTime { get; private set; } = -10f;

        public Vector2 PosXZ => new Vector2(transform.position.x, transform.position.z);

        Transform _pivot;       // 몸 중심(높이 1). 구르기 회전은 이 축을 돌린다
        Transform _body;
        Transform _aim;
        float _rollStart = -10f;
        Vector3 _rollAxis;
        int _rollPuffs;
        Renderer[] _renderers;
        MaterialPropertyBlock _mpb;
        Color _baseColor;
        float _dieStart;
        byte _shownWeapon = 255;
        float _gunLen = 0.9f;

        public void Init(uint id, string displayName, bool isLocal, float radius)
        {
            PlayerId = id;
            DisplayName = displayName;
            IsLocal = isLocal;
            name = (isLocal ? "Local_" : "Remote_") + id;

            _pivot = new GameObject("Pivot").transform;
            _pivot.SetParent(transform, false);
            _pivot.localPosition = new Vector3(0, 1f, 0);

            _body = GameObject.CreatePrimitive(PrimitiveType.Capsule).transform;
            _body.SetParent(_pivot, false);
            _body.localPosition = Vector3.zero;
            _body.localScale = new Vector3(radius * 2f, 1f, radius * 2f);   // capsule 기본 높이 2, 지름 1
            Destroy(_body.GetComponent<Collider>());
            RuntimeMaterials.Apply(_body.gameObject);

            _aim = GameObject.CreatePrimitive(PrimitiveType.Cube).transform;
            _aim.SetParent(_pivot, false);
            _aim.localScale = new Vector3(0.18f, 0.18f, 0.9f);
            Destroy(_aim.GetComponent<Collider>());
            RuntimeMaterials.Apply(_aim.gameObject);

            _renderers = GetComponentsInChildren<Renderer>();
            _mpb = new MaterialPropertyBlock();
            _baseColor = isLocal ? new Color(0.25f, 0.6f, 1f) : new Color(0.95f, 0.35f, 0.3f);
            ApplyColor(_baseColor);
            SetAim(0);
        }

        public void SetAim(float angleDeg)
        {
            AimAngle = angleDeg;
            if (_aim == null) return;
            float rad = angleDeg * Mathf.Deg2Rad;
            var dir = new Vector3(Mathf.Cos(rad), 0, Mathf.Sin(rad));
            _aim.localPosition = new Vector3(0, 0.3f, 0) + dir * (0.3f + _gunLen * 0.5f);
            _aim.localRotation = Quaternion.LookRotation(dir, Vector3.up);
        }

        public void SetPosition(float x, float z)
        {
            transform.position = new Vector3(x, 0, z);
        }

        /// <summary>구르기 연출 (game-spec 20.1): delay초 뒤부터 0.25초 동안 dir 방향 앞구르기 + 먼지 잔상</summary>
        public void PlayRoll(Vector2 dir, float delay = 0f)
        {
            if (dir.sqrMagnitude < 1e-4f) return;
            dir.Normalize();
            _rollStart = Time.time + delay;
            _rollAxis = Vector3.Cross(Vector3.up, new Vector3(dir.x, 0, dir.y));
            _rollPuffs = 0;
        }

        void UpdateRoll()
        {
            if (_pivot == null) return;
            float k = (Time.time - _rollStart) / GameSession.RollSeconds;
            if (k < 0f || k > 1.6f) { _pivot.localRotation = Quaternion.identity; return; }
            _pivot.localRotation = k >= 1f ? Quaternion.identity : Quaternion.AngleAxis(360f * k, _rollAxis);
            // 먼지 잔상: 0, 1/3, 2/3, 1 지점
            while (_rollPuffs < 4 && k >= _rollPuffs / 3f)
            {
                DustPuff.Spawn(transform.position, 1.1f + 0.2f * _rollPuffs);
                _rollPuffs++;
            }
        }

        public void OnDamaged(ushort newHp)
        {
            Hp = newHp;
            LastDamageTime = Time.time;
        }

        public void PlayDeath()
        {
            if (IsDying) return;
            IsDying = true;
            _dieStart = Time.time;
            Hp = 0;
        }

        // 들고 있는 총 모양 (1 권총 / 2 샷건 / 3 저격총)
        void UpdateWeaponVisual()
        {
            if (_shownWeapon == WeaponId || _aim == null) return;
            _shownWeapon = WeaponId;
            Vector3 s;
            switch (WeaponId)
            {
                case 2: s = new Vector3(0.3f, 0.24f, 1.1f); break;
                case 3: s = new Vector3(0.14f, 0.14f, 1.7f); break;
                default: s = new Vector3(0.18f, 0.18f, 0.9f); break;
            }
            _gunLen = s.z;
            _aim.localScale = s;
            SetAim(AimAngle);
        }

        protected virtual void LateUpdate()
        {
            if (_renderers == null) return;
            UpdateWeaponVisual();
            UpdateRoll();
            if (IsDying)
            {
                float t = (Time.time - _dieStart) / 0.6f;
                transform.localScale = Vector3.one * Mathf.Max(0.01f, 1f - t);
                ApplyColor(Color.Lerp(Color.white, _baseColor, t));
                if (t >= 1f && !IsLocal) Destroy(gameObject);
                return;
            }
            float flash = Mathf.Clamp01(1f - (Time.time - LastDamageTime) / 0.15f);
            ApplyColor(Color.Lerp(_baseColor, Color.white, flash));
        }

        void ApplyColor(Color c)
        {
            _mpb.SetColor(BaseColorId, c);
            _mpb.SetColor(ColorId, c);
            foreach (var r in _renderers)
                if (r != null) r.SetPropertyBlock(_mpb);
        }
    }
}

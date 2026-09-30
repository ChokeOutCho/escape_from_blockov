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

        Transform _body;
        Transform _aim;
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

            _body = GameObject.CreatePrimitive(PrimitiveType.Capsule).transform;
            _body.SetParent(transform, false);
            _body.localPosition = new Vector3(0, 1f, 0);
            _body.localScale = new Vector3(radius * 2f, 1f, radius * 2f);   // capsule 기본 높이 2, 지름 1
            Destroy(_body.GetComponent<Collider>());
            RuntimeMaterials.Apply(_body.gameObject);

            _aim = GameObject.CreatePrimitive(PrimitiveType.Cube).transform;
            _aim.SetParent(transform, false);
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
            _aim.localPosition = new Vector3(0, 1.3f, 0) + dir * (0.3f + _gunLen * 0.5f);
            _aim.localRotation = Quaternion.LookRotation(dir, Vector3.up);
        }

        public void SetPosition(float x, float z)
        {
            transform.position = new Vector3(x, 0, z);
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

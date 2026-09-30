using Blockov.Net;
using UnityEngine;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

namespace Blockov.Game
{
    /// <summary>
    /// 로컬 플레이어 조작 (game-spec 4, 11.2, 19): 클라 권위 이동(예측) + 서버 검증.
    ///  - WASD 8방향(정규화), Shift 누르는 동안 달리기(속도 x SprintMultiplier), 마우스 조준(지면 y=0), 좌클릭 사격(누르고 있으면 연사)
    ///  - 1/2 무기 전환(1 특수 총, 2 권총), 3 붕대(2초), Space 구르기(마우스 방향, 이동속도 x3, 0.25초, 쿨 3초)
    ///  - 조준선: 캐릭터 → 마우스 지면 (사거리 밖 구간은 어둡게)
    ///  - CS_MOVE: 이동 중이거나 조준 5° 이상 변화 → 100ms마다, 속도 변화 → 즉시(최소 50ms 간격). 구르는 동안은 보내지 않음
    ///  - CS_FIRE: 흔들림(±JitterDeg) 적용 후 방향 + ViewTimeMs + SpreadSeed, 로컬 탄 생성
    /// </summary>
    [RequireComponent(typeof(CharacterView))]
    public sealed class LocalPlayerController : MonoBehaviour
    {
        const float MoveSendInterval = 0.1f;
        const float MinSendInterval = 0.05f;
        const float AimSendThreshold = 5f;

        public CameraRig Rig;

        CharacterView _view;
        Vector2 _pos;
        Vector2 _vel;
        float _aim;

        ushort _moveSeq;
        uint _shotSeq;
        float _lastSendTime = -10f;
        Vector2 _lastSentVel;
        float _lastSentAim;
        float _nextFireTime;
        bool _forceSend;

        // 구르기
        float _rollStart = -10f, _rollReadyAt;
        Vector2 _rollFrom, _rollTo;

        // 붕대
        float _bandageStart = -10f;
        Vector2 _lastMoveDir;
        bool _usingBandage;

        // 조준선
        Transform _aimNear, _aimFar;
        MaterialPropertyBlock _mpb;

        public Vector2 Position => _pos;
        public bool IsSprinting { get; private set; }
        public bool InputEnabled { get; set; } = true;
        /// <summary>이번 프레임에 이동 입력(WASD)이 있는가 → F 상호작용 취소</summary>
        public bool HasMoveInput { get; private set; }
        public bool IsRolling => Time.time - _rollStart < GameSession.RollSeconds;
        /// <summary>구르기 쿨타임 남은 비율 (0 = 사용 가능)</summary>
        public float RollCooldown01 => Mathf.Clamp01((_rollReadyAt - Time.time) / GameSession.RollCooldown);
        /// <summary>붕대 사용 진행률 (사용 중이 아니면 -1)</summary>
        public float BandageProgress => _usingBandage ? Mathf.Clamp01((Time.time - _bandageStart) / GameSession.BandageSeconds) : -1f;

        void Awake()
        {
            _view = GetComponent<CharacterView>();
            _mpb = new MaterialPropertyBlock();
            _aimNear = MakeLine("AimLine", new Color(1f, 0.95f, 0.55f));
            _aimFar = MakeLine("AimLineOut", new Color(0.35f, 0.35f, 0.35f));
        }

        Transform MakeLine(string n, Color c)
        {
            var go = GameObject.CreatePrimitive(PrimitiveType.Cube);
            go.name = n;
            Destroy(go.GetComponent<Collider>());
            RuntimeMaterials.Apply(go);
            _mpb.SetColor("_BaseColor", c);
            _mpb.SetColor("_Color", c);
            go.GetComponent<Renderer>().SetPropertyBlock(_mpb);
            go.GetComponent<Renderer>().shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            go.SetActive(false);
            return go.transform;
        }

        void OnDestroy()
        {
            if (_aimNear != null) Destroy(_aimNear.gameObject);
            if (_aimFar != null) Destroy(_aimFar.gameObject);
        }

        public void Teleport(float x, float z)
        {
            _pos = new Vector2(x, z);
            _rollStart = -10f;      // 보정은 구르기를 끊는다
            _view.SetPosition(x, z);
            _forceSend = true;
        }

        /// <summary>서버 SC_INVENTORY 수신 후 (장착 무기가 바뀌었을 수 있음)</summary>
        public void OnInventory()
        {
            _view.WeaponId = GameSession.WeaponId;
        }

        /// <summary>서버 SC_HP(본인) 수신: 붕대 완료</summary>
        public void OnHealed()
        {
            _usingBandage = false;
        }

        void CancelBandage() => _usingBandage = false;

        void Update()
        {
            if (_view.IsDying || !GameSession.InGame)
            {
                ShowAimLine(false, default, default, 0);
                return;
            }
            float dt = Mathf.Min(Time.deltaTime, 0.1f);

            // 입력
            Vector2 input = Vector2.zero;
            bool fireHeld = false, sprint = false, rollPressed = false;
            int slotPressed = 0;
            Vector2 mouseScreen = Vector2.zero;
#if ENABLE_INPUT_SYSTEM
            var kb = Keyboard.current;
            var mouse = Mouse.current;
            if (InputEnabled && kb != null)
            {
                if (kb.wKey.isPressed) input.y += 1;
                if (kb.sKey.isPressed) input.y -= 1;
                if (kb.dKey.isPressed) input.x += 1;
                if (kb.aKey.isPressed) input.x -= 1;
                sprint = kb.shiftKey.isPressed;
                rollPressed = kb.spaceKey.wasPressedThisFrame;
                if (kb.digit1Key.wasPressedThisFrame || kb.numpad1Key.wasPressedThisFrame) slotPressed = 1;
                else if (kb.digit2Key.wasPressedThisFrame || kb.numpad2Key.wasPressedThisFrame) slotPressed = 2;
                else if (kb.digit3Key.wasPressedThisFrame || kb.numpad3Key.wasPressedThisFrame) slotPressed = 3;
            }
            if (mouse != null)
            {
                mouseScreen = mouse.position.ReadValue();
                var gcx = GameController.Instance;
                fireHeld = InputEnabled && mouse.leftButton.isPressed && !GameHUD.IsPointerOverUi(mouseScreen)
                           && !(gcx != null && gcx.ShowMinimap);     // 전체 맵이 열려 있으면 사격 안 함 (휠 줌·드래그)
            }
#endif
            HasMoveInput = input.sqrMagnitude > 0;

            // 조준 (구르는 중에도 갱신)
            Vector2 aimDir = new Vector2(Mathf.Cos(_aim * Mathf.Deg2Rad), Mathf.Sin(_aim * Mathf.Deg2Rad));
            Vector3 ground = default;
            bool hasGround = Rig != null && Rig.MouseGround(mouseScreen, out ground);
            if (hasGround)
            {
                var d = new Vector2(ground.x - _pos.x, ground.z - _pos.y);
                if (d.sqrMagnitude > 0.01f)
                {
                    aimDir = d.normalized;
                    _aim = Mathf.Repeat(Mathf.Atan2(aimDir.y, aimDir.x) * Mathf.Rad2Deg, 360f);
                }
            }

            if (slotPressed == 1 || slotPressed == 2) SwitchWeapon((byte)slotPressed);
            else if (slotPressed == 3) UseBandage();
            // 구르기 방향 = 키보드 이동 방향, 입력이 없으면 마지막 이동 방향(한 번도 안 움직였으면 조준 방향) (21.5)
            if (HasMoveInput) _lastMoveDir = input.normalized;
            if (rollPressed && !IsRolling && Time.time >= _rollReadyAt)
                StartRoll(HasMoveInput ? input.normalized : (_lastMoveDir.sqrMagnitude > 0.5f ? _lastMoveDir : aimDir));

            if (IsRolling)
            {
                float k = Mathf.Clamp01((Time.time - _rollStart) / GameSession.RollSeconds);
                _pos = Vector2.Lerp(_rollFrom, _rollTo, k);
                _vel = Vector2.zero;
                if (k >= 1f) { _rollStart = -10f; _forceSend = true; }
            }
            else
            {
                // 붕대 사용 중: 달리기 불가, 걷기 속도의 절반 (21.4)
                IsSprinting = sprint && HasMoveInput && !_usingBandage;
                float speed = GameSession.MoveSpeed * (_usingBandage ? GameSession.BandageMoveMult : (IsSprinting ? GameSession.SprintMultiplier : 1f));
                _vel = HasMoveInput ? input.normalized * speed : Vector2.zero;
                MoveWithCollision(_vel * dt);
            }
            _view.SetPosition(_pos.x, _pos.y);
            _view.SetAim(_aim);

            if (_usingBandage && Time.time - _bandageStart > GameSession.BandageSeconds + 1.5f) _usingBandage = false;   // 서버 응답 없음 (가득 참 등)

            if (!IsRolling) SendMoveIfNeeded();

            if (fireHeld && !IsRolling && Time.time >= _nextFireTime) Fire(aimDir);

            UpdateAimLine(hasGround, ground, aimDir);
        }

        ////////////////////////////////////////////////////////////////
        // 무기 전환 · 붕대 · 구르기
        ////////////////////////////////////////////////////////////////
        void SwitchWeapon(byte slot)
        {
            if (slot == GameSession.SlotSpecial && GameSession.SpecialWeaponId == 0) return;
            if (slot == GameSession.Equipped) return;
            CancelBandage();
            GameSession.Equipped = slot;
            GameSession.WeaponId = slot == GameSession.SlotSpecial ? GameSession.SpecialWeaponId : GameSession.PistolWeaponId;
            _view.WeaponId = GameSession.WeaponId;
            _nextFireTime = Mathf.Max(_nextFireTime, Time.time + 0.15f);
            NetworkManager.Instance.Send(new PacketWriter(PacketType.CS_SWITCH_WEAPON).WriteByte(slot));
        }

        void UseBandage()
        {
            if (_usingBandage || IsRolling || GameSession.Bandages == 0 || GameSession.Hp >= GameSession.MaxHp) return;
            _usingBandage = true;
            _bandageStart = Time.time;
            NetworkManager.Instance.Send(new PacketWriter(PacketType.CS_USE_BANDAGE));
        }

        void StartRoll(Vector2 dir)
        {
            if (dir.sqrMagnitude < 0.01f) return;
            dir.Normalize();
            CancelBandage();
            // 서버와 같은 계산: 0.25m 단위 직진, 엄폐물(반지름 그대로)에 닿으면 그 앞에서 멈춤
            float dist = GameSession.MoveSpeed * GameSession.RollSpeedMult * GameSession.RollSeconds;
            int steps = Mathf.Max(1, Mathf.CeilToInt(dist / 0.25f));
            float stepLen = dist / steps;
            Vector2 c = _pos;
            for (int i = 0; i < steps; i++)
            {
                var n = Clamp(c + dir * stepLen);
                if (ObstacleMap.CircleBlocked(n.x, n.y, GameSession.CharacterRadius)) break;
                c = n;
            }
            _rollFrom = _pos;
            _rollTo = c;
            _rollStart = Time.time;
            _rollReadyAt = Time.time + GameSession.RollCooldown;
            _view.PlayRoll(dir);
            NetworkManager.Instance.Send(new PacketWriter(PacketType.CS_ROLL)
                .WriteFloat(_pos.x).WriteFloat(_pos.y).WriteFloat(dir.x).WriteFloat(dir.y));
        }

        /// <summary>
        /// 엄폐물(벽·낮은 엄폐물) 충돌 이동: 0.25m 단위로 나눠 이동하고, 막히면 X/Z 축별로 미끄러진다.
        /// 서버는 경로 중심선과 도착 원(반지름 - 0.1)으로 검증하므로 클라가 반지름 그대로 막으면 보정이 나지 않는다.
        /// </summary>
        void MoveWithCollision(Vector2 delta)
        {
            float r = GameSession.CharacterRadius;
            int steps = Mathf.Max(1, Mathf.CeilToInt(delta.magnitude / 0.25f));
            Vector2 step = delta / steps;
            bool blockedX = false, blockedZ = false;
            for (int i = 0; i < steps; i++)
            {
                var full = Clamp(_pos + step);
                if (!ObstacleMap.CircleBlocked(full.x, full.y, r)) { _pos = full; continue; }
                var onlyX = Clamp(new Vector2(_pos.x + step.x, _pos.y));
                var onlyZ = Clamp(new Vector2(_pos.x, _pos.y + step.y));
                if (!blockedX && step.x != 0 && !ObstacleMap.CircleBlocked(onlyX.x, onlyX.y, r)) { _pos = onlyX; blockedZ = true; }
                else if (!blockedZ && step.y != 0 && !ObstacleMap.CircleBlocked(onlyZ.x, onlyZ.y, r)) { _pos = onlyZ; blockedX = true; }
                else break;
            }
            // 실제 이동 방향 기준으로 속도 보고 (벽에 막히면 0)
            if (blockedX) _vel.x = 0;
            if (blockedZ) _vel.y = 0;
        }

        // 이동 가능 영역: 외벽 두께 2 → [2, 월드 크기 - 2] (서버 MapConst::MinPos/MaxPos)
        const float MinPos = 2f, MaxPos = ObstacleMap.Size - 2f;
        static Vector2 Clamp(Vector2 p) => new Vector2(Mathf.Clamp(p.x, MinPos, MaxPos), Mathf.Clamp(p.y, MinPos, MaxPos));

        void SendMoveIfNeeded()
        {
            float now = Time.unscaledTime;
            float since = now - _lastSendTime;
            bool velChanged = (_vel - _lastSentVel).sqrMagnitude > 0.01f;
            bool moving = _vel.sqrMagnitude > 0;
            bool aimChanged = Mathf.Abs(Mathf.DeltaAngle(_aim, _lastSentAim)) >= AimSendThreshold;

            bool send = false;
            if ((velChanged || _forceSend) && since >= MinSendInterval) send = true;
            else if ((moving || aimChanged) && since >= MoveSendInterval) send = true;
            if (!send) return;

            _moveSeq++;
            NetworkManager.Instance.Send(new PacketWriter(PacketType.CS_MOVE)
                .WriteFloat(_pos.x).WriteFloat(_pos.y)
                .WriteFloat(_vel.x).WriteFloat(_vel.y)
                .WriteFloat(_aim)
                .WriteUInt16(_moveSeq));
            _lastSendTime = now;
            _lastSentVel = _vel;
            _lastSentAim = _aim;
            _forceSend = false;
        }

        void Fire(Vector2 aimDir)
        {
            var w = GameSession.MyWeapon;
            if (w == null) return;
            if (GameSession.Equipped == GameSession.SlotSpecial && w.Durability > 0 && GameSession.SpecialDurability == 0) return;
            CancelBandage();
            _nextFireTime = Time.time + w.FireIntervalMs / 1000f;
            _shotSeq++;
            var net = NetworkManager.Instance;
            uint viewTime = net.ViewTimeMs;

            // 흔들림 (19.2): 조준 방향 ± JitterDeg
            Vector2 dir = aimDir;
            if (w.JitterDeg > 0)
            {
                float j = Random.Range(-w.JitterDeg, w.JitterDeg) * Mathf.Deg2Rad;
                float c = Mathf.Cos(j), s = Mathf.Sin(j);
                dir = new Vector2(aimDir.x * c - aimDir.y * s, aimDir.x * s + aimDir.y * c);
            }
            byte seed = (byte)Random.Range(0, 256);

            net.Send(new PacketWriter(PacketType.CS_FIRE)
                .WriteUInt32(_shotSeq)
                .WriteByte(w.Id)
                .WriteFloat(_pos.x).WriteFloat(_pos.y)
                .WriteFloat(dir.x).WriteFloat(dir.y)
                .WriteUInt32(viewTime)
                .WriteByte(seed).WriteByte(0));

            for (byte i = 0; i < w.Pellets; i++)
                Projectile.Spawn(GameSession.MyPlayerId, _shotSeq, i, true, _pos, GameSession.PelletDir(dir, seed, i, w.SpreadDeg), w);

            // 특수 총 내구도 (서버와 같은 규칙: 0이 되면 사라지고 권총으로)
            if (GameSession.Equipped == GameSession.SlotSpecial && w.Durability > 0)
            {
                if (GameSession.SpecialDurability > 0) GameSession.SpecialDurability--;
                if (GameSession.SpecialDurability == 0)
                {
                    GameSession.SpecialWeaponId = 0;
                    GameSession.Equipped = GameSession.SlotPistol;
                    GameSession.WeaponId = GameSession.PistolWeaponId;
                    _view.WeaponId = GameSession.WeaponId;
                }
            }
        }

        ////////////////////////////////////////////////////////////////
        // 조준선
        ////////////////////////////////////////////////////////////////
        /// <summary>조준선 길이 (game-spec 20.2): 마우스와 무관하게 고정, 벽(Wall)에서 끊김</summary>
        public const float AimLineLength = 20f;

        void UpdateAimLine(bool hasGround, Vector3 ground, Vector2 aimDir)
        {
            if (GameSession.MyWeapon == null || !InputEnabled || aimDir.sqrMagnitude < 0.5f) { ShowAimLine(false, default, default, 0); return; }
            float len = AimLineLength;
            float t = ObstacleMap.RaycastBullet(_pos, _pos + aimDir * len);
            if (t <= 1f) len *= t;
            ShowAimLine(true, _pos, aimDir, len);
        }

        void ShowAimLine(bool show, Vector2 from, Vector2 dir, float len)
        {
            if (_aimNear == null) return;
            _aimFar.gameObject.SetActive(false);
            const float start = 0.6f;   // 캐릭터 몸 밖에서 시작
            if (!show || len < start + 0.05f) { _aimNear.gameObject.SetActive(false); return; }
            Place(_aimNear, from, dir, start, len, 0.07f);
        }

        static void Place(Transform t, Vector2 from, Vector2 dir, float a, float b, float width)
        {
            if (b - a < 0.05f) { t.gameObject.SetActive(false); return; }
            t.gameObject.SetActive(true);
            Vector2 mid = from + dir * ((a + b) * 0.5f);
            t.position = new Vector3(mid.x, 0.06f, mid.y);
            t.rotation = Quaternion.LookRotation(new Vector3(dir.x, 0, dir.y), Vector3.up);
            t.localScale = new Vector3(width, 0.02f, b - a);
        }
    }
}

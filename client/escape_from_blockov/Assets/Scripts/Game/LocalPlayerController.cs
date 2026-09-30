using Blockov.Net;
using UnityEngine;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

namespace Blockov.Game
{
    /// <summary>
    /// 로컬 플레이어 조작 (game-spec 4, 11.2): 클라 권위 이동(예측) + 서버 검증.
    ///  - WASD 8방향(정규화), Shift 누르는 동안 달리기(속도 x SprintMultiplier), 마우스 조준(지면 y=0), 좌클릭 사격(누르고 있으면 연사)
    ///  - CS_MOVE: 이동 중이거나 조준 5° 이상 변화 → 100ms마다, 속도 변화 → 즉시(최소 50ms 간격)
    ///  - CS_FIRE: 조준 방향 벡터 + ViewTimeMs, 로컬 탄 생성
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

        public Vector2 Position => _pos;
        public bool IsSprinting { get; private set; }
        public bool InputEnabled { get; set; } = true;

        void Awake()
        {
            _view = GetComponent<CharacterView>();
        }

        public void Teleport(float x, float z)
        {
            _pos = new Vector2(x, z);
            _view.SetPosition(x, z);
            _forceSend = true;
        }

        void Update()
        {
            if (_view.IsDying || !GameSession.InGame) return;
            float dt = Mathf.Min(Time.deltaTime, 0.1f);

            // 이동 입력
            Vector2 input = Vector2.zero;
            bool fireHeld = false;
            bool sprint = false;
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
            }
            if (mouse != null)
            {
                mouseScreen = mouse.position.ReadValue();
                fireHeld = InputEnabled && mouse.leftButton.isPressed;
            }
#endif
            IsSprinting = sprint && input.sqrMagnitude > 0;
            float speed = GameSession.MoveSpeed * (IsSprinting ? GameSession.SprintMultiplier : 1f);
            _vel = input.sqrMagnitude > 0 ? input.normalized * speed : Vector2.zero;
            MoveWithCollision(_vel * dt);
            _view.SetPosition(_pos.x, _pos.y);

            // 조준
            Vector2 aimDir = new Vector2(Mathf.Cos(_aim * Mathf.Deg2Rad), Mathf.Sin(_aim * Mathf.Deg2Rad));
            if (Rig != null && Rig.MouseGround(mouseScreen, out var ground))
            {
                var d = new Vector2(ground.x - _pos.x, ground.z - _pos.y);
                if (d.sqrMagnitude > 0.01f)
                {
                    aimDir = d.normalized;
                    _aim = Mathf.Repeat(Mathf.Atan2(aimDir.y, aimDir.x) * Mathf.Rad2Deg, 360f);
                }
            }
            _view.SetAim(_aim);

            SendMoveIfNeeded();

            if (fireHeld && Time.time >= _nextFireTime) Fire(aimDir);
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

        static Vector2 Clamp(Vector2 p) => new Vector2(Mathf.Clamp(p.x, 2f, 6398f), Mathf.Clamp(p.y, 2f, 6398f));

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

        void Fire(Vector2 dir)
        {
            var w = GameSession.MyWeapon;
            if (w == null) return;
            _nextFireTime = Time.time + w.FireIntervalMs / 1000f;
            _shotSeq++;
            var net = NetworkManager.Instance;
            uint viewTime = net.ViewTimeMs;

            net.Send(new PacketWriter(PacketType.CS_FIRE)
                .WriteUInt32(_shotSeq)
                .WriteByte(w.Id)
                .WriteFloat(_pos.x).WriteFloat(_pos.y)
                .WriteFloat(dir.x).WriteFloat(dir.y)
                .WriteUInt32(viewTime)
                .WriteUInt16(0));

            var rng = new System.Random((int)_shotSeq);
            for (byte i = 0; i < w.Pellets; i++)
            {
                Vector2 d = dir;
                if (w.SpreadDeg > 0)
                {
                    float off = ((float)rng.NextDouble() - 0.5f) * w.SpreadDeg * Mathf.Deg2Rad;
                    float c = Mathf.Cos(off), s = Mathf.Sin(off);
                    d = new Vector2(dir.x * c - dir.y * s, dir.x * s + dir.y * c);
                }
                Projectile.Spawn(GameSession.MyPlayerId, _shotSeq, i, true, _pos, d, w);
            }
        }
    }
}

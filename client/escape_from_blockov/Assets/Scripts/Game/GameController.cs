using System.Collections.Generic;
using Blockov.Net;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace Blockov.Game
{
    /// <summary>
    /// TestArena 씬의 게임 진행 (game-spec 7, 12.2).
    /// 서버 패킷을 받아 로컬/원격 캐릭터, 투사체, 점수·랭킹·사망을 처리한다.
    /// 씬에 GameController 오브젝트 하나만 두면 카메라·HUD는 런타임에 구성된다.
    /// </summary>
    public sealed class GameController : MonoBehaviour
    {
        public static GameController Instance { get; private set; }

        public struct KillFeed { public string Text; public float Time; }

        public CharacterView LocalView { get; private set; }
        public LocalPlayerController Local { get; private set; }
        public CameraRig Rig { get; private set; }
        public ContainerManager Containers { get; private set; }
        public CoverManager Covers { get; private set; }
        public IEnumerable<RemoteCharacter> RemoteCharacters => _remotes.Values;
        public IReadOnlyDictionary<uint, RemoteCharacter> Remotes => _remotes;
        public List<KillFeed> Feed { get; } = new List<KillFeed>();
        public float LastHitMarkerTime { get; private set; } = -10f;
        public bool ShowDeathResult => GameSession.Death != null;
        public bool LocalDead { get; private set; }
        public bool Disconnected { get; private set; }
        /// <summary>M 키로 전체 맵 표시 토글</summary>
        public bool ShowMinimap { get; set; }

        readonly Dictionary<uint, RemoteCharacter> _remotes = new Dictionary<uint, RemoteCharacter>();
        readonly List<Projectile> _observerShots = new List<Projectile>();
        readonly HitReporter _hits = new HitReporter();
        bool _leaving;

        void Awake()
        {
            Instance = this;
        }

        void Start()
        {
            var net = NetworkManager.Instance;
            if (net == null || !GameSession.InGame)
            {
                // 에디터에서 TestArena를 바로 실행한 경우 등 → 타이틀로
                SceneManager.LoadScene(0);
                return;
            }

            ObstacleMap.EnsureLoaded();

            // 로컬 플레이어
            var go = new GameObject("LocalPlayer");
            LocalView = go.AddComponent<CharacterView>();
            LocalView.Init(GameSession.MyPlayerId, GameSession.MyName, true, GameSession.CharacterRadius);
            LocalView.Hp = GameSession.Hp;
            LocalView.MaxHp = GameSession.MaxHp;
            LocalView.WeaponId = GameSession.WeaponId;
            Local = go.AddComponent<LocalPlayerController>();
            Local.Teleport(GameSession.SpawnX, GameSession.SpawnZ);

            // 카메라
            var cam = Camera.main;
            if (cam == null)
            {
                cam = new GameObject("Main Camera").AddComponent<Camera>();
                cam.tag = "MainCamera";
            }
            Rig = cam.GetComponent<CameraRig>();
            if (Rig == null) Rig = cam.gameObject.AddComponent<CameraRig>();
            Rig.Target = go.transform;
            Rig.SnapTo(go.transform.position);
            Local.Rig = Rig;

            Containers = GetComponent<ContainerManager>();
            if (Containers == null) Containers = gameObject.AddComponent<ContainerManager>();
            // 파괴 가능 엄폐물: 씬의 Covers(CoverManager). 없으면 만든다
            Covers = CoverManager.Instance != null ? CoverManager.Instance : FindAnyObjectByType<CoverManager>();
            if (Covers == null) Covers = new GameObject("Covers").AddComponent<CoverManager>();

            // 스폰 이펙트 (본인)
            SpawnEffect.Play(new Vector2(GameSession.SpawnX, GameSession.SpawnZ));
            if (GetComponent<GameHUD>() == null) gameObject.AddComponent<GameHUD>();

            net.PacketReceived += OnPacket;
            net.StateChanged += OnStateChanged;
            net.HoldDispatch = false;   // 씬 준비 완료 → 보류했던 패킷 처리 시작
        }

        void OnDestroy()
        {
            if (Instance == this) Instance = null;
            var net = NetworkManager.Instance;
            if (net == null) return;
            net.PacketReceived -= OnPacket;
            net.StateChanged -= OnStateChanged;
        }

        void Update()
        {
#if ENABLE_INPUT_SYSTEM
            var kb = UnityEngine.InputSystem.Keyboard.current;
            if (kb != null && kb.mKey.wasPressedThisFrame) ShowMinimap = !ShowMinimap;
            if (kb != null && kb.escapeKey.wasPressedThisFrame) ShowMinimap = false;   // Esc로 맵 닫기 (21.3)
#endif
            _hits.Update();
            _observerShots.RemoveAll(p => p == null);
            float now = Time.time;
            Feed.RemoveAll(f => now - f.Time > 6f);
        }

        void OnStateChanged(NetworkManager.State s)
        {
            if (s != NetworkManager.State.Disconnected) return;
            Disconnected = true;
            if (ShowDeathResult) return;   // 결과창은 사용자가 닫을 때까지 유지
            var net = NetworkManager.Instance;
            GameSession.TitleMessage = "서버와의 연결이 끊어졌습니다." +
                (net != null && !string.IsNullOrEmpty(net.LastError) ? $"\n({net.LastError})" : "");
            ReturnToTitle();
        }

        public void ReturnToTitle()
        {
            if (_leaving) return;
            _leaving = true;
            _hits.Flush();
            SceneManager.LoadScene(0);
        }

        // 로컬 탄이 원격 캐릭터에 맞음 (예측)
        public void OnLocalHit(Projectile p, RemoteCharacter target, Vector2 hitPoint)
        {
            _hits.Add(p.ShotSeq, p.Pellet, target.PlayerId, hitPoint);
            LastHitMarkerTime = Time.time;
        }

        // 로컬 탄이 파괴 가능 엄폐물에 맞음 → 서버에 보고 (판정은 서버, 체력은 SC_COVER_HP로)
        public void OnLocalCoverHit(Projectile p, int coverId, Vector2 hitPoint)
        {
            _hits.Add(p.ShotSeq, p.Pellet, NetConst.HitTargetCover | (uint)coverId, hitPoint);
            SoundManager.Play(SoundManager.CoverHit, 0.8f);
            DustPuff.Spawn(new Vector3(hitPoint.x, 0.6f, hitPoint.y), 0.6f);
        }

        public string NameOf(uint id)
        {
            if (id == GameSession.MyPlayerId) return GameSession.MyName;
            return _remotes.TryGetValue(id, out var r) && r != null ? r.DisplayName : "?";
        }

        ////////////////////////////////////////////////////////////////////
        // 패킷 처리
        ////////////////////////////////////////////////////////////////////
        void OnPacket(PacketReader r)
        {
            switch (r.Type)
            {
                case PacketType.SC_WEAPON_DEFS: GameSession.ReadWeaponDefs(r); break;
                case PacketType.SC_CREATE_CHARACTERS: OnCreate(r); break;
                case PacketType.SC_DELETE_CHARACTERS: OnDelete(r); break;
                case PacketType.SC_MOVE: OnMove(r); break;
                case PacketType.SC_POSITION_CORRECT: OnCorrect(r); break;
                case PacketType.SC_FIRE: OnFire(r); break;
                case PacketType.SC_DAMAGE: OnDamage(r); break;
                case PacketType.SC_PLAYER_DIE: OnDie(r); break;
                case PacketType.SC_DEATH_RESULT: OnDeathResult(r); break;
                case PacketType.SC_SCORE:
                    GameSession.Score = r.ReadUInt32();
                    GameSession.Kills = r.ReadUInt16();
                    break;
                case PacketType.SC_RANKING_TOP3: OnRanking(r); break;
                case PacketType.SC_PLAYER_COUNT: GameSession.OnlineCount = (int)r.ReadUInt32(); break;
                case PacketType.SC_INVENTORY:
                    GameSession.ReadInventory(r);
                    if (Local != null) Local.OnInventory();
                    break;
                case PacketType.SC_ROLL: OnRoll(r); break;
                case PacketType.SC_HP: OnHp(r); break;
                case PacketType.SC_CONTAINER_CREATE: if (Containers != null) Containers.OnCreate(r); break;
                case PacketType.SC_CONTAINER_DELETE: if (Containers != null) Containers.OnDelete(r); break;
                case PacketType.SC_CONTAINER_CONTENTS: if (Containers != null) Containers.OnContents(r); break;
                case PacketType.SC_AIRDROP: if (Containers != null) Containers.OnAirdrop(r); break;
                case PacketType.SC_AIRDROP_FORECAST: if (Containers != null) Containers.OnForecast(r); break;
                case PacketType.SC_COVER_HP: if (Covers != null) Covers.OnCoverHp(r); break;
                case PacketType.SC_COVER_STATE: if (Covers != null) Covers.OnCoverState(r); break;
                case PacketType.SC_KICK:
                    // NetworkManager가 사유를 LastError에 기록. 곧 서버가 끊는다.
                    break;
            }
        }

        void OnCreate(PacketReader r)
        {
            int count = r.ReadByte();
            double now = NetworkManager.Instance.EstServerNow;
            for (int i = 0; i < count; i++)
            {
                uint id = r.ReadUInt32();
                string name = r.ReadName();
                float x = r.ReadFloat(), z = r.ReadFloat(), vx = r.ReadFloat(), vz = r.ReadFloat(), aim = r.ReadFloat();
                ushort hp = r.ReadUInt16(), maxHp = r.ReadUInt16();
                byte weapon = r.ReadByte();
                byte flags = r.ReadByte();
                if (id == GameSession.MyPlayerId) continue;
                if ((flags & NetConst.CreateFlagSpawn) != 0) SpawnEffect.Play(new Vector2(x, z));   // 방금 스폰한 플레이어

                if (!_remotes.TryGetValue(id, out var rc) || rc == null)
                {
                    var go = new GameObject();
                    rc = go.AddComponent<RemoteCharacter>();
                    rc.Init(id, name, false, GameSession.CharacterRadius);
                    rc.SetPosition(x, z);
                    rc.SetAim(aim);
                    _remotes[id] = rc;
                }
                rc.DisplayName = name;
                rc.Hp = hp;
                rc.MaxHp = maxHp;
                rc.WeaponId = weapon;
                // 생성 시점의 상태를 현재 시각과 렌더 시각 양쪽에 두어 곧바로 보이게 한다
                rc.AddSnapshot(now - ServerClock.InterpDelayMs, x, z, vx, vz, aim);
                rc.AddSnapshot(now, x, z, vx, vz, aim);
            }
        }

        void OnDelete(PacketReader r)
        {
            int count = r.ReadByte();
            for (int i = 0; i < count; i++)
            {
                uint id = r.ReadUInt32();
                if (_remotes.TryGetValue(id, out var rc))
                {
                    if (rc != null) Destroy(rc.gameObject);
                    _remotes.Remove(id);
                }
            }
        }

        void OnMove(PacketReader r)
        {
            uint id = r.ReadUInt32();
            float x = r.ReadFloat(), z = r.ReadFloat(), vx = r.ReadFloat(), vz = r.ReadFloat(), aim = r.ReadFloat();
            if (_remotes.TryGetValue(id, out var rc) && rc != null)
                rc.AddSnapshot(NetworkManager.Instance.EstServerNow, x, z, vx, vz, aim);
        }

        void OnCorrect(PacketReader r)
        {
            float x = r.ReadFloat(), z = r.ReadFloat();
            r.ReadUInt16();
            if (Local != null && !LocalDead) Local.Teleport(x, z);
        }

        void OnFire(PacketReader r)
        {
            uint shooter = r.ReadUInt32();
            uint seq = r.ReadUInt32();
            byte weaponId = r.ReadByte();
            float ox = r.ReadFloat(), oz = r.ReadFloat(), dx = r.ReadFloat(), dz = r.ReadFloat();
            byte seed = r.ReadByte();
            if (!GameSession.Weapons.TryGetValue(weaponId, out var w)) return;
            if (_remotes.TryGetValue(shooter, out var src) && src != null) src.WeaponId = weaponId;

            var dir = new Vector2(dx, dz);
            SoundManager.PlayAt(SoundManager.FireClip(w), new Vector2(ox, oz));
            for (byte i = 0; i < w.Pellets; i++)
                _observerShots.Add(Projectile.Spawn(shooter, seq, i, false, new Vector2(ox, oz), GameSession.PelletDir(dir, seed, i, w.SpreadDeg), w));
        }

        // 원격 구르기: 시작 → 도착(0.25초) 스냅샷 (game-spec 4.2)
        void OnRoll(PacketReader r)
        {
            uint id = r.ReadUInt32();
            float sx = r.ReadFloat(), sz = r.ReadFloat(), ex = r.ReadFloat(), ez = r.ReadFloat();
            if (!_remotes.TryGetValue(id, out var rc) || rc == null) return;
            double now = NetworkManager.Instance.EstServerNow;
            float aim = rc.AimAngle;
            rc.AddSnapshot(now, sx, sz, 0, 0, aim);
            rc.AddSnapshot(now + GameSession.RollSeconds * 1000.0, ex, ez, 0, 0, aim);
            rc.PlayRoll(new Vector2(ex - sx, ez - sz), (float)(ServerClock.InterpDelayMs / 1000.0));   // 보간 지연 뒤 화면에서 구름
        }

        // 체력 변경 (붕대 회복)
        void OnHp(PacketReader r)
        {
            uint id = r.ReadUInt32();
            ushort hp = r.ReadUInt16();
            if (id == GameSession.MyPlayerId)
            {
                GameSession.Hp = hp;
                if (LocalView != null) LocalView.Hp = hp;
                if (Local != null) Local.OnHealed();
            }
            else if (_remotes.TryGetValue(id, out var rc) && rc != null)
            {
                rc.Hp = hp;
            }
        }

        void OnDamage(PacketReader r)
        {
            uint attacker = r.ReadUInt32();
            uint victim = r.ReadUInt32();
            uint seq = r.ReadUInt32();
            r.ReadUInt16();     // damage
            ushort hp = r.ReadUInt16();

            if (victim == GameSession.MyPlayerId)
            {
                GameSession.Hp = hp;
                if (LocalView != null) LocalView.OnDamaged(hp);
                SoundManager.Play(SoundManager.Hurt);
            }
            else if (_remotes.TryGetValue(victim, out var rc) && rc != null)
            {
                rc.OnDamaged(hp);
            }
            if (attacker == GameSession.MyPlayerId && victim != GameSession.MyPlayerId) SoundManager.Play(SoundManager.Hit);

            // 해당 사격의 관찰자 탄 제거 (연출)
            for (int i = _observerShots.Count - 1; i >= 0; i--)
            {
                var p = _observerShots[i];
                if (p != null && p.ShooterId == attacker && p.ShotSeq == seq)
                {
                    Destroy(p.gameObject);
                    _observerShots.RemoveAt(i);
                }
            }
        }

        void OnDie(PacketReader r)
        {
            uint victim = r.ReadUInt32();
            uint killer = r.ReadUInt32();
            Feed.Add(new KillFeed { Text = $"{NameOf(killer)}  ▶  {NameOf(victim)}", Time = Time.time });
            if (killer == GameSession.MyPlayerId && victim != GameSession.MyPlayerId) SoundManager.Play(SoundManager.Kill);
            if (Feed.Count > 5) Feed.RemoveAt(0);

            if (victim == GameSession.MyPlayerId)
            {
                LocalDead = true;
                GameSession.Hp = 0;
                if (LocalView != null) LocalView.PlayDeath();
                if (Local != null) Local.InputEnabled = false;
                _hits.Flush();
            }
            else if (_remotes.TryGetValue(victim, out var rc))
            {
                if (rc != null) rc.PlayDeath();     // 연출 후 스스로 제거
                _remotes.Remove(victim);
            }
        }

        void OnDeathResult(PacketReader r)
        {
            GameSession.Death = new DeathResult
            {
                KillerName = r.ReadName(),
                FinalScore = r.ReadUInt32(),
                Kills = r.ReadUInt32(),
                SurvivalSec = r.ReadUInt32(),
            };
            LocalDead = true;
            if (Local != null) Local.InputEnabled = false;
        }

        void OnRanking(PacketReader r)
        {
            GameSession.Top3.Clear();
            int count = r.ReadByte();
            for (int i = 0; i < count; i++)
            {
                GameSession.Top3.Add(new RankEntry
                {
                    Rank = r.ReadByte(),
                    PlayerId = r.ReadUInt32(),
                    Name = r.ReadName(),
                    Score = r.ReadUInt32(),
                });
            }
        }
    }
}

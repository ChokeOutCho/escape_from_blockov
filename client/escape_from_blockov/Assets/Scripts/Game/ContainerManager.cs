using System.Collections.Generic;
using Blockov.Net;
using UnityEngine;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

namespace Blockov.Game
{
    /// <summary>
    /// 가방·에어드랍 (game-spec 19.5~19.7).
    ///  - SC_CONTAINER_CREATE/DELETE: 시야(3x3) 안 가방, 에어드랍 제거
    ///  - SC_AIRDROP: 방 전체 에어드랍 (IsNew=1이면 상단 공지)
    ///  - F 상호작용: 2.5m 이내 가장 가까운 대상 → F를 누르는 동안 원형 게이지(가방 1초 / 에어드랍 2초), 이동 입력·F 떼기로 취소
    ///    → CS_OPEN_CONTAINER → SC_CONTAINER_CONTENTS 수신 시 루팅 창(획득 버튼), 3m 넘게 멀어지면 닫힘
    /// </summary>
    public sealed class ContainerManager : MonoBehaviour
    {
        public const byte TypeBag = 1, TypeAirdrop = 2;
        public const byte ItemSpecial = 1, ItemBandage = 3;

        public sealed class Info
        {
            public uint Id;
            public byte Type;
            public Vector2 Pos;
            public int SectorX, SectorY;
            public GameObject View;
            public float Spawned;
        }

        public struct Contents
        {
            public uint Id;
            public byte SpecialWeaponId;
            public ushort Durability;
            public byte Bandages;
        }

        public struct Notice { public string Text; public float Time; }

        readonly Dictionary<uint, Info> _items = new Dictionary<uint, Info>();

        public IEnumerable<Info> All => _items.Values;
        public IEnumerable<Info> Airdrops { get { foreach (var i in _items.Values) if (i.Type == TypeAirdrop) yield return i; } }

        /// <summary>F 대상 (2.5m 이내 가장 가까운 것, 없으면 null)</summary>
        public Info Target { get; private set; }
        /// <summary>F 누르는 중 진행률 (0~1, 누르지 않으면 -1)</summary>
        public float HoldProgress { get; private set; } = -1f;
        /// <summary>열린 루팅 창 (null = 닫힘)</summary>
        public Info OpenContainer { get; private set; }
        public Contents OpenContents { get; private set; }
        public bool WaitingOpen { get; private set; }
        public Notice LastNotice { get; private set; }

        GameController _gc;
        float _holdStart = -1f;
        uint _holdId;
        float _waitSince;
        bool _needRelease;      // 열기 요청/창 닫기 후 F를 한 번 떼야 다시 게이지 시작

        void Awake() => _gc = GetComponent<GameController>();

        void OnDestroy()
        {
            foreach (var i in _items.Values) if (i.View != null) Destroy(i.View);
            _items.Clear();
        }

        ////////////////////////////////////////////////////////////////
        // 패킷
        ////////////////////////////////////////////////////////////////
        public void OnCreate(PacketReader r)
        {
            int n = r.ReadByte();
            for (int i = 0; i < n; i++)
            {
                uint id = r.ReadUInt32();
                byte type = r.ReadByte();
                float x = r.ReadFloat(), z = r.ReadFloat();
                Add(id, type, x, z);
            }
        }

        public void OnDelete(PacketReader r)
        {
            int n = r.ReadByte();
            for (int i = 0; i < n; i++) Remove(r.ReadUInt32());
        }

        public void OnAirdrop(PacketReader r)
        {
            uint id = r.ReadUInt32();
            float x = r.ReadFloat(), z = r.ReadFloat();
            byte sx = r.ReadByte(), sy = r.ReadByte();
            bool isNew = r.ReadByte() != 0;
            var info = Add(id, TypeAirdrop, x, z);
            info.SectorX = sx; info.SectorY = sy;
            if (isNew) LastNotice = new Notice { Text = $"에어드랍 투하!  섹터 ({sx},{sy})", Time = Time.time };
        }

        public void OnContents(PacketReader r)
        {
            var c = new Contents { Id = r.ReadUInt32(), SpecialWeaponId = r.ReadByte(), Durability = r.ReadUInt16(), Bandages = r.ReadByte() };
            if (!_items.TryGetValue(c.Id, out var info)) return;
            // 기다리던 열기 응답이거나, 이미 열어 둔 창의 갱신
            if ((WaitingOpen && _holdId == c.Id) || (OpenContainer != null && OpenContainer.Id == c.Id))
            {
                WaitingOpen = false;
                OpenContainer = info;
                OpenContents = c;
            }
        }

        Info Add(uint id, byte type, float x, float z)
        {
            if (_items.TryGetValue(id, out var info)) return info;
            info = new Info
            {
                Id = id, Type = type, Pos = new Vector2(x, z),
                SectorX = Mathf.FloorToInt(x / SectorGrid.DefaultSectorSize), SectorY = Mathf.FloorToInt(z / SectorGrid.DefaultSectorSize),
                Spawned = Time.time,
            };
            info.View = BuildView(info);
            _items[id] = info;
            return info;
        }

        void Remove(uint id)
        {
            if (!_items.TryGetValue(id, out var info)) return;
            if (info.View != null) Destroy(info.View);
            _items.Remove(id);
            if (OpenContainer == info) OpenContainer = null;
            if (_holdId == id) { _holdStart = -1f; WaitingOpen = false; }
        }

        static GameObject BuildView(Info info)
        {
            var root = new GameObject(info.Type == TypeBag ? $"Bag_{info.Id}" : $"Airdrop_{info.Id}");
            root.transform.position = new Vector3(info.Pos.x, 0, info.Pos.y);
            var mpb = new MaterialPropertyBlock();
            void Part(PrimitiveType pt, Vector3 pos, Vector3 scale, Color c)
            {
                var go = GameObject.CreatePrimitive(pt);
                Object.Destroy(go.GetComponent<Collider>());
                RuntimeMaterials.Apply(go);
                go.transform.SetParent(root.transform, false);
                go.transform.localPosition = pos;
                go.transform.localScale = scale;
                mpb.SetColor("_BaseColor", c);
                mpb.SetColor("_Color", c);
                go.GetComponent<Renderer>().SetPropertyBlock(mpb);
            }
            if (info.Type == TypeBag)
            {
                var brown = new Color(0.45f, 0.3f, 0.15f);
                Part(PrimitiveType.Cube, new Vector3(0, 0.3f, 0), new Vector3(0.9f, 0.6f, 0.6f), brown);
                Part(PrimitiveType.Cube, new Vector3(0, 0.65f, 0), new Vector3(0.5f, 0.12f, 0.12f), new Color(0.3f, 0.2f, 0.1f));
            }
            else
            {
                var crate = new Color(0.2f, 0.45f, 0.95f);
                Part(PrimitiveType.Cube, new Vector3(0, 0.7f, 0), new Vector3(1.6f, 1.4f, 1.6f), crate);
                Part(PrimitiveType.Cube, new Vector3(0, 0.7f, 0), new Vector3(1.65f, 0.25f, 1.65f), new Color(1f, 0.85f, 0.2f));
                // 멀리서도 보이는 신호 기둥
                Part(PrimitiveType.Cylinder, new Vector3(0, 6f, 0), new Vector3(0.25f, 5f, 0.25f), new Color(1f, 0.35f, 0.2f));
            }
            return root;
        }

        ////////////////////////////////////////////////////////////////
        // 상호작용
        ////////////////////////////////////////////////////////////////
        void Update()
        {
            // 에어드랍 기둥 깜빡임 / 가방 위아래
            foreach (var i in _items.Values)
                if (i.View != null && i.Type == TypeBag)
                    i.View.transform.position = new Vector3(i.Pos.x, 0.05f + Mathf.Sin((Time.time - i.Spawned) * 3f) * 0.05f, i.Pos.y);

            var local = _gc != null ? _gc.Local : null;
            if (local == null || _gc.LocalDead)
            {
                Target = null; HoldProgress = -1f; OpenContainer = null; WaitingOpen = false;
                return;
            }
            Vector2 me = local.Position;

            // 루팅 창 자동 닫기 (3m 초과)
            if (OpenContainer != null && Vector2.Distance(me, OpenContainer.Pos) > GameSession.LootCloseRange) OpenContainer = null;
            if (WaitingOpen && Time.time - _waitSince > 1.5f) WaitingOpen = false;   // 서버 거부(이동 등)

            // 대상 선택
            Info best = null;
            float bestD = GameSession.InteractRange;
            foreach (var i in _items.Values)
            {
                float d = Vector2.Distance(me, i.Pos);
                if (d <= bestD) { bestD = d; best = i; }
            }
            Target = best;

            bool fDown = false, fHeld = false, esc = false;
#if ENABLE_INPUT_SYSTEM
            var kb = Keyboard.current;
            if (kb != null && local.InputEnabled)
            {
                fDown = kb.fKey.wasPressedThisFrame;
                fHeld = kb.fKey.isPressed;
                esc = kb.escapeKey.wasPressedThisFrame;
            }
#endif
            if (esc) OpenContainer = null;
            if (!fHeld) _needRelease = false;

            // 창이 열려 있으면 F로 닫기
            if (OpenContainer != null && fDown)
            {
                OpenContainer = null;
                _holdStart = -1f;
                HoldProgress = -1f;
                _needRelease = true;
                return;
            }

            if (Target == null || OpenContainer != null || WaitingOpen) { _holdStart = -1f; HoldProgress = -1f; return; }

            // 누르는 동안 게이지 (대상에 다가가며 누르고 있던 경우도 시작)
            if ((fDown || (fHeld && _holdStart < 0)) && !_needRelease) { _holdStart = Time.time; _holdId = Target.Id; }
            bool cancel = !fHeld || local.HasMoveInput || local.IsRolling || _holdId != Target.Id;
            if (_holdStart < 0 || cancel) { _holdStart = -1f; HoldProgress = -1f; return; }

            float need = Target.Type == TypeBag ? GameSession.BagOpenSeconds : GameSession.AirdropOpenSeconds;
            HoldProgress = Mathf.Clamp01((Time.time - _holdStart) / need);
            if (HoldProgress >= 1f)
            {
                NetworkManager.Instance.Send(new PacketWriter(PacketType.CS_OPEN_CONTAINER).WriteUInt32(Target.Id));
                WaitingOpen = true;
                _needRelease = true;
                _waitSince = Time.time;
                _holdStart = -1f;
                HoldProgress = -1f;
            }
        }

        /// <summary>루팅 창 [획득] 버튼</summary>
        public void Take(byte item)
        {
            if (OpenContainer == null) return;
            NetworkManager.Instance.Send(new PacketWriter(PacketType.CS_TAKE_ITEM).WriteUInt32(OpenContainer.Id).WriteByte(item));
        }

        public void CloseLoot() => OpenContainer = null;
    }
}

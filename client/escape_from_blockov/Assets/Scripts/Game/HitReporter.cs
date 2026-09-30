using System.Collections.Generic;
using Blockov.Net;
using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 로컬 탄 피격 보고 버퍼 (game-spec 6.2, 7.3 CS_HIT_REPORT).
    /// 첫 항목 후 100ms가 지나거나 29건(512B 한도)이 차면 한 패킷으로 보낸다.
    /// </summary>
    public sealed class HitReporter
    {
        struct Item
        {
            public uint ShotSeq;
            public byte Pellet;
            public uint TargetId;
            public float X, Z;
        }

        const float FlushIntervalSec = 0.1f;

        readonly List<Item> _items = new List<Item>(NetConst.MaxHitItems);
        float _firstTime;

        public void Add(uint shotSeq, byte pellet, uint targetId, Vector2 hit)
        {
            if (_items.Count == 0) _firstTime = Time.unscaledTime;
            _items.Add(new Item { ShotSeq = shotSeq, Pellet = pellet, TargetId = targetId, X = hit.x, Z = hit.y });
            if (_items.Count >= NetConst.MaxHitItems) Flush();
        }

        public void Update()
        {
            if (_items.Count > 0 && Time.unscaledTime - _firstTime >= FlushIntervalSec) Flush();
        }

        public void Flush()
        {
            if (_items.Count == 0) return;
            var net = NetworkManager.Instance;
            if (net != null)
            {
                var w = new PacketWriter(PacketType.CS_HIT_REPORT).WriteByte((byte)_items.Count);
                foreach (var it in _items)
                    w.WriteUInt32(it.ShotSeq).WriteByte(it.Pellet).WriteUInt32(it.TargetId).WriteFloat(it.X).WriteFloat(it.Z);
                net.Send(w);
            }
            _items.Clear();
        }
    }
}

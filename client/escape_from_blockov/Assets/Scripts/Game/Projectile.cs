using System.Collections.Generic;
using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 투사체 (game-spec 7.2).
    ///  - 로컬 탄(내가 쏜 탄): 매 프레임 이동 + 원격 캐릭터(화면에 그려진 위치)와 원-선분 충돌 → HitReporter에 보고, 히트 마커(예측)
    ///    멀쩡한 파괴 가능 엄폐물에 닿으면 멈추고 엄폐물 피격으로 보고 (game-spec 3.6)
    ///  - 관찰자 탄(SC_FIRE): 이동·연출만. 사거리 도달·벽/엄폐물 충돌 또는 해당 ShotSeq의 SC_DAMAGE 수신 시 제거
    /// </summary>
    public sealed class Projectile : MonoBehaviour
    {
        static readonly int BaseColorId = Shader.PropertyToID("_BaseColor");
        static readonly int ColorId = Shader.PropertyToID("_Color");

        public uint ShooterId { get; private set; }
        public uint ShotSeq { get; private set; }
        public byte Pellet { get; private set; }
        public bool IsLocal { get; private set; }

        Vector2 _pos, _dir;
        float _speed, _range, _radius, _traveled;
        int _hitsLeft;
        readonly HashSet<uint> _hit = new HashSet<uint>();

        public static Projectile Spawn(uint shooterId, uint shotSeq, byte pellet, bool isLocal,
                                       Vector2 origin, Vector2 dir, WeaponDef w)
        {
            var go = GameObject.CreatePrimitive(PrimitiveType.Sphere);
            Destroy(go.GetComponent<Collider>());
            RuntimeMaterials.Apply(go);
            go.name = $"Projectile_{shooterId}_{shotSeq}_{pellet}";
            float visual = Mathf.Max(0.35f, w.ProjectileRadius * 2f);
            go.transform.localScale = Vector3.one * visual;
            var p = go.AddComponent<Projectile>();
            p.ShooterId = shooterId;
            p.ShotSeq = shotSeq;
            p.Pellet = pellet;
            p.IsLocal = isLocal;
            p._pos = origin;
            p._dir = dir.normalized;
            p._speed = w.ProjectileSpeed;
            p._range = w.Range;
            p._radius = w.ProjectileRadius;
            p._hitsLeft = 1 + w.Pierce;

            var mpb = new MaterialPropertyBlock();
            var c = isLocal ? new Color(1f, 0.92f, 0.3f) : new Color(1f, 0.55f, 0.2f);
            mpb.SetColor(BaseColorId, c);
            mpb.SetColor(ColorId, c);
            go.GetComponent<Renderer>().SetPropertyBlock(mpb);
            p.Place();
            return p;
        }

        void Place() => transform.position = new Vector3(_pos.x, 1.2f, _pos.y);

        void Update()
        {
            float step = _speed * Time.deltaTime;
            if (_traveled + step > _range) step = _range - _traveled;
            Vector2 from = _pos;
            Vector2 to = _pos + _dir * step;

            // 벽·멀쩡한 파괴 가능 엄폐물에 막히는 지점까지만 진행 (낮은 엄폐물·반 블럭은 통과)
            float wallT = ObstacleMap.RaycastBullet(from, to, out int cover);
            bool hitWall = wallT <= 1f;
            if (hitWall) to = from + (to - from) * wallT;

            if (IsLocal && GameController.Instance != null)
            {
                float hitDist = GameSession.CharacterRadius + _radius;
                foreach (var target in GameController.Instance.RemoteCharacters)
                {
                    if (target == null || target.IsDying || _hit.Contains(target.PlayerId)) continue;
                    Vector2 c = target.PosXZ;
                    Vector2 seg = to - from;
                    float len2 = seg.sqrMagnitude;
                    float t = len2 > 1e-6f ? Mathf.Clamp01(Vector2.Dot(c - from, seg) / len2) : 0f;
                    Vector2 closest = from + seg * t;
                    if ((c - closest).sqrMagnitude <= hitDist * hitDist)
                    {
                        _hit.Add(target.PlayerId);
                        GameController.Instance.OnLocalHit(this, target, closest);
                        if (--_hitsLeft <= 0)
                        {
                            Destroy(gameObject);
                            return;
                        }
                    }
                }
            }

            _pos = to;
            _traveled += hitWall ? step * wallT : step;
            Place();
            if (hitWall && cover >= 0 && IsLocal && GameController.Instance != null)
                GameController.Instance.OnLocalCoverHit(this, cover, to);
            if (hitWall || _traveled >= _range - 1e-3f) Destroy(gameObject);
        }
    }
}

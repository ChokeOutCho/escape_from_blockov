using System.Collections.Generic;
using Blockov.Net;
using UnityEngine;

namespace Blockov.Game
{
    /// <summary>
    /// 원격 캐릭터 (game-spec 12.2): 스냅샷(수신 시 EstServerNow 기준 시각)을 버퍼에 쌓고
    /// 렌더 시각 EstServerNow - 100ms로 보간한다. 스냅샷이 부족하면 속도로 최대 200ms 외삽.
    /// 화면에 그려진 위치 = 서버 시각 (EstServerNow - InterpDelay) 의 위치 → CS_FIRE의 ViewTime과 일치.
    /// </summary>
    public sealed class RemoteCharacter : CharacterView
    {
        struct Snapshot
        {
            public double T;
            public Vector2 Pos;
            public Vector2 Vel;
            public float Aim;
        }

        const int MaxSnapshots = 32;
        const double MaxExtrapolateMs = 200;

        readonly List<Snapshot> _snaps = new List<Snapshot>(MaxSnapshots);

        public void AddSnapshot(double serverTime, float x, float z, float vx, float vz, float aim)
        {
            // 순서 보장 (같은 시각이면 최신으로 교체)
            if (_snaps.Count > 0 && serverTime <= _snaps[_snaps.Count - 1].T)
                serverTime = _snaps[_snaps.Count - 1].T + 0.001;
            _snaps.Add(new Snapshot { T = serverTime, Pos = new Vector2(x, z), Vel = new Vector2(vx, vz), Aim = aim });
            if (_snaps.Count > MaxSnapshots) _snaps.RemoveAt(0);
        }

        /// <summary>렌더 시각의 위치 (보간/외삽)</summary>
        public Vector2 Sample(double renderTime, out float aim)
        {
            aim = AimAngle;
            int n = _snaps.Count;
            if (n == 0) return PosXZ;

            if (renderTime <= _snaps[0].T)
            {
                aim = _snaps[0].Aim;
                return _snaps[0].Pos;
            }
            var last = _snaps[n - 1];
            if (renderTime >= last.T)
            {
                double dt = System.Math.Min(renderTime - last.T, MaxExtrapolateMs) / 1000.0;
                aim = last.Aim;
                return last.Pos + last.Vel * (float)dt;
            }
            for (int i = n - 2; i >= 0; i--)
            {
                var a = _snaps[i];
                if (renderTime >= a.T)
                {
                    var b = _snaps[i + 1];
                    float k = (float)((renderTime - a.T) / (b.T - a.T));
                    aim = Mathf.LerpAngle(a.Aim, b.Aim, k);
                    return Vector2.Lerp(a.Pos, b.Pos, k);
                }
            }
            return _snaps[0].Pos;
        }

        void Update()
        {
            if (IsDying) return;
            var net = NetworkManager.Instance;
            if (net == null) return;
            double renderTime = net.EstServerNow - ServerClock.InterpDelayMs;
            var p = Sample(renderTime, out float aim);
            SetPosition(p.x, p.y);
            SetAim(aim);

            // 오래된 스냅샷 정리 (렌더 시각보다 1초 이상 이전, 보간용으로 최소 2개 유지)
            while (_snaps.Count > 2 && _snaps[1].T < renderTime - 1000) _snaps.RemoveAt(0);
        }
    }
}

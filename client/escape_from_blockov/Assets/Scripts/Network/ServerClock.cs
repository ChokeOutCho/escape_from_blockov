using System;

namespace Blockov.Net
{
    /// <summary>
    /// 서버 시각 추정 (game-spec 9.2).
    ///  - SC_PONG마다 rtt = now - ClientTimeMs, offsetSample = ServerTimeMs + rtt/2 - now
    ///  - 최근 8개 중 RTT가 가장 작은 샘플의 offset을 목표로 삼고, 초당 50ms 이내로 완만히 보정
    ///  - EstServerNow = localNow + offset,  ViewTimeMs = EstServerNow - InterpDelay
    /// </summary>
    public sealed class ServerClock
    {
        public const int InterpDelayMs = 100;
        const int Window = 8;
        const double MaxSlewPerSec = 50.0;
        const double SnapThresholdMs = 1000.0;

        readonly int[] _rtt = new int[Window];
        readonly double[] _offset = new double[Window];
        int _count, _index;

        double _currentOffset;
        double _targetOffset;
        bool _initialized;
        double _lastAdvanceLocal;

        public bool IsSynced => _count > 0;
        public double OffsetMs => _currentOffset;

        public void Reset()
        {
            _count = _index = 0;
            _initialized = false;
            _currentOffset = _targetOffset = 0;
        }

        /// <summary>SC_ENTER_GAME의 ServerTimeMs로 초기값 설정 (RTT 모름 → 편도 0 가정)</summary>
        public void InitFromServerTime(uint serverTimeMs, double localNowMs)
        {
            _currentOffset = _targetOffset = serverTimeMs - localNowMs;
            _initialized = true;
            _lastAdvanceLocal = localNowMs;
        }

        public void AddSample(int rttMs, uint serverTimeMs, double localNowMs)
        {
            double sample = serverTimeMs + rttMs / 2.0 - localNowMs;
            _rtt[_index] = rttMs;
            _offset[_index] = sample;
            _index = (_index + 1) % Window;
            if (_count < Window) _count++;

            int best = 0;
            for (int i = 1; i < _count; i++)
                if (_rtt[i] < _rtt[best]) best = i;
            _targetOffset = _offset[best];

            if (!_initialized || _count == 1 || Math.Abs(_targetOffset - _currentOffset) > SnapThresholdMs)
            {
                _currentOffset = _targetOffset;
                _initialized = true;
            }
            _lastAdvanceLocal = localNowMs;
        }

        /// <summary>매 프레임 호출: offset을 목표로 초당 50ms 이내로 이동</summary>
        public void Advance(double localNowMs)
        {
            double dt = Math.Max(0, (localNowMs - _lastAdvanceLocal) / 1000.0);
            _lastAdvanceLocal = localNowMs;
            double maxStep = MaxSlewPerSec * dt;
            double diff = _targetOffset - _currentOffset;
            if (Math.Abs(diff) <= maxStep) _currentOffset = _targetOffset;
            else _currentOffset += Math.Sign(diff) * maxStep;
        }

        public double EstServerNow(double localNowMs) => localNowMs + _currentOffset;

        /// <summary>화면에 그려지는 원격 캐릭터의 서버 시각 (CS_FIRE ViewTimeMs)</summary>
        public uint ViewTimeMs(double localNowMs) => (uint)(long)(EstServerNow(localNowMs) - InterpDelayMs);
    }
}

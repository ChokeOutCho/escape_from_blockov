using System;
using System.Diagnostics;
using UnityEngine;
using Debug = UnityEngine.Debug;

namespace Blockov.Net
{
    /// <summary>
    /// 서버 연결 관리 싱글톤 (DontDestroyOnLoad, Title 씬에 배치).
    /// 흐름: Connect(name) → WS 연결 → CS_ENTER_GAME → SC_ENTER_GAME(OK) → InGame
    ///       InGame 중 CS_PING 2초마다(RTT·시각 동기화), CS_HEARTBEAT 60초마다(백그라운드 타이머).
    /// 모든 수신 처리는 Update()에서 메인 스레드로 수행한다.
    /// 씬 전환 중에는 HoldDispatch = true로 수신 패킷을 보류한다(순서 보존).
    /// </summary>
    [DefaultExecutionOrder(-100)]
    public sealed class NetworkManager : MonoBehaviour
    {
        public enum State { Disconnected, Connecting, Entering, InGame }

        public static NetworkManager Instance { get; private set; }

        [Header("Connection")]
        [Tooltip("개발: 게이트웨이 ws://<host>:8080/  서비스: wss://<domain>/game\nWebGL은 페이지 URL의 ?server= 값이 우선")]
        [SerializeField] string serverUrl = "ws://127.0.0.1:8080/";

        [Header("Latency / Heartbeat")]
        [SerializeField] float pingIntervalSec = 2f;
        [Tooltip("서버 하트비트 타임아웃(3분)보다 충분히 짧게")]
        [SerializeField] int heartbeatIntervalMs = 60000;
        [SerializeField] int maxEventsPerFrame = 256;

        const int RttWindow = 8;
        const int InitialPingBurst = 3;
        const float InitialPingBurstInterval = 0.2f;

        public State CurrentState { get; private set; } = State.Disconnected;
        public string ServerUrl => serverUrl;
        public string LastError { get; private set; }
        public EnterResult LastEnterResult { get; private set; }
        public KickReason LastKickReason { get; private set; }

        public int LastRttMs { get; private set; } = -1;
        public float AvgRttMs { get; private set; } = -1;
        public int MinRttMs { get; private set; } = -1;

        public ServerClock Clock { get; } = new ServerClock();

        /// <summary>true면 수신 패킷 디스패치를 보류한다 (씬 로딩 중)</summary>
        public bool HoldDispatch { get; set; }

        /// <summary>NetworkManager가 직접 처리하지 않는 패킷. 게임 로직이 구독한다.</summary>
        public event Action<PacketReader> PacketReceived;
        /// <summary>SC_ENTER_GAME 수신 (결과 무관). 호출 시점에 HoldDispatch = true로 바뀐다.</summary>
        public event Action<PacketReader, EnterResult> EnterGameReceived;
        public event Action<State> StateChanged;

        IWebSocket _ws;
        readonly PacketAssembler _assembler = new PacketAssembler();
        readonly Stopwatch _clock = Stopwatch.StartNew();
        readonly int[] _rttSamples = new int[RttWindow];
        int _rttCount, _rttIndex;
        float _nextPingTime;
        int _pingBurstLeft;
        string _pendingName;

        public uint LocalTimeMs => (uint)_clock.ElapsedMilliseconds;
        public double LocalTimeMsExact => _clock.Elapsed.TotalMilliseconds;
        public double EstServerNow => Clock.EstServerNow(LocalTimeMsExact);
        public uint ViewTimeMs => Clock.ViewTimeMs(LocalTimeMsExact);

        void Awake()
        {
            if (Instance != null && Instance != this) { Destroy(gameObject); return; }
            Instance = this;
            DontDestroyOnLoad(gameObject);
            ApplyUrlOverride();
        }

        void ApplyUrlOverride()
        {
            // WebGL: 1) 페이지 URL의 ?server=ws://1.2.3.4:8080/ 가 있으면 그 주소
            //        2) 없으면 페이지를 준 서버의 /ws (serve_webgl.js가 게이트웨이로 중계)
            //           http://host:8090/ → ws://host:8090/ws, https://domain/ → wss://domain/ws
            string url = Application.absoluteURL;
            if (string.IsNullOrEmpty(url)) return;
            int q = url.IndexOf('?');
            if (q >= 0)
            {
                foreach (var part in url.Substring(q + 1).Split('&'))
                {
                    int eq = part.IndexOf('=');
                    if (eq > 0 && part.Substring(0, eq) == "server")
                    {
                        serverUrl = Uri.UnescapeDataString(part.Substring(eq + 1));
                        Debug.Log($"[Net] server url override: {serverUrl}");
                        return;
                    }
                }
            }
#if UNITY_WEBGL && !UNITY_EDITOR
            if (Uri.TryCreate(url, UriKind.Absolute, out var page) && (page.Scheme == "http" || page.Scheme == "https"))
            {
                serverUrl = (page.Scheme == "https" ? "wss://" : "ws://") + page.Authority + "/ws";
                Debug.Log($"[Net] server url from page: {serverUrl}");
            }
#endif
        }

        public void Connect(string playerName)
        {
            if (CurrentState != State.Disconnected) return;
            _ws?.Dispose();
            _ws = WebSocketFactory.Create();
            _assembler.Reset();
            ResetRtt();
            Clock.Reset();
            HoldDispatch = false;
            LastError = null;
            LastKickReason = KickReason.None;
            LastEnterResult = EnterResult.Ok;
            _pendingName = playerName ?? "";
            SetState(State.Connecting);
            Debug.Log($"[Net] Connecting {serverUrl}");
            _ws.Connect(serverUrl);
        }

        public void Disconnect(string reason = "client disconnect")
        {
            if (_ws == null) return;
            _ws.Close();
            OnClosed(reason);
        }

        public void Send(PacketWriter writer)
        {
            if (_ws == null || CurrentState != State.InGame) return;
            _ws.Send(writer.ToPacket());
        }

        void Update()
        {
            Clock.Advance(LocalTimeMsExact);
            if (_ws == null) return;

            for (int i = 0; i < maxEventsPerFrame && _ws != null && !HoldDispatch; i++)
            {
                // 보류 해제 직후 어셈블러에 남은 패킷부터 처리
                if (DrainAssembler()) break;
                if (_ws == null || !_ws.TryDequeue(out var ev)) break;
                switch (ev.Type)
                {
                    case WsEventType.Open: OnOpened(); break;
                    case WsEventType.Message: _assembler.Append(ev.Data); break;
                    case WsEventType.Close: OnClosed(ev.Reason); break;
                }
            }
            if (_ws != null && !HoldDispatch) DrainAssembler();

            if (_ws != null && CurrentState == State.InGame && !HoldDispatch && Time.unscaledTime >= _nextPingTime)
                SendPing();
        }

        /// <summary>어셈블러의 완성 패킷을 처리. 보류/종료로 중단되면 true.</summary>
        bool DrainAssembler()
        {
            while (_ws != null && !HoldDispatch && _assembler.TryPop(out var payload))
            {
                try
                {
                    Dispatch(new PacketReader(payload));
                }
                catch (Exception e)
                {
                    Debug.LogException(e);
                    Disconnect("invalid packet: " + e.Message);
                    return true;
                }
            }
            if (_ws != null && _assembler.Error != null)
            {
                Disconnect(_assembler.Error);
                return true;
            }
            return _ws == null || HoldDispatch;
        }

        void OnOpened()
        {
            Debug.Log("[Net] Connected. Enter game...");
            SetState(State.Entering);
            var w = new PacketWriter(PacketType.CS_ENTER_GAME)
                .WriteUInt32(NetConst.ProtocolVersion)
                .WriteName(_pendingName);
            _ws.Send(w.ToPacket());
        }

        void Dispatch(PacketReader r)
        {
            switch (r.Type)
            {
                case PacketType.SC_ENTER_GAME:
                {
                    var result = (EnterResult)r.ReadByte();
                    LastEnterResult = result;
                    if (result == EnterResult.Ok)
                    {
                        SetState(State.InGame);
                        _ws.SetKeepAlive(new PacketWriter(PacketType.CS_HEARTBEAT).ToPacket(), heartbeatIntervalMs);
                        _pingBurstLeft = InitialPingBurst;
                        _nextPingTime = 0;
                        HoldDispatch = true;    // 게임 씬이 준비될 때까지 이후 패킷 보류
                    }
                    else
                    {
                        LastError = "입장 실패: " + result;
                    }
                    EnterGameReceived?.Invoke(r, result);
                    break;
                }
                case PacketType.SC_PONG:
                {
                    uint clientTime = r.ReadUInt32();
                    uint serverTime = r.ReadUInt32();
                    double now = LocalTimeMsExact;
                    int rtt = (int)(LocalTimeMs - clientTime);
                    if (rtt >= 0)
                    {
                        AddRtt(rtt);
                        Clock.AddSample(rtt, serverTime, now);
                    }
                    break;
                }
                case PacketType.SC_KICK:
                {
                    LastKickReason = (KickReason)r.ReadByte();
                    LastError = "서버가 연결을 종료함: " + LastKickReason;
                    PacketReceived?.Invoke(r);
                    break;
                }
                default:
                    PacketReceived?.Invoke(r);
                    break;
            }
        }

        void SendPing()
        {
            _ws.Send(new PacketWriter(PacketType.CS_PING).WriteUInt32(LocalTimeMs).ToPacket());
            if (_pingBurstLeft > 0)
            {
                _pingBurstLeft--;
                _nextPingTime = Time.unscaledTime + InitialPingBurstInterval;
            }
            else
            {
                _nextPingTime = Time.unscaledTime + pingIntervalSec;
            }
        }

        void AddRtt(int rtt)
        {
            LastRttMs = rtt;
            MinRttMs = MinRttMs < 0 ? rtt : Math.Min(MinRttMs, rtt);
            _rttSamples[_rttIndex] = rtt;
            _rttIndex = (_rttIndex + 1) % RttWindow;
            if (_rttCount < RttWindow) _rttCount++;
            long sum = 0;
            for (int i = 0; i < _rttCount; i++) sum += _rttSamples[i];
            AvgRttMs = (float)sum / _rttCount;
        }

        void ResetRtt()
        {
            LastRttMs = -1;
            AvgRttMs = -1;
            MinRttMs = -1;
            _rttCount = 0;
            _rttIndex = 0;
        }

        void OnClosed(string reason)
        {
            if (_ws == null) return;
            Debug.Log($"[Net] Disconnected: {reason}");
            if (LastError == null) LastError = reason;
            _ws.Dispose();
            _ws = null;
            HoldDispatch = false;
            SetState(State.Disconnected);
        }

        void SetState(State s)
        {
            if (CurrentState == s) return;
            CurrentState = s;
            StateChanged?.Invoke(s);
        }

        void OnDestroy()
        {
            if (Instance == this) Instance = null;
            _ws?.Dispose();
            _ws = null;
        }
    }
}

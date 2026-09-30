using System;

namespace Blockov.Net
{
    public enum WsEventType
    {
        Open = 1,
        Message = 2,
        Close = 4,
    }

    public struct WsEvent
    {
        public WsEventType Type;
        public byte[] Data;     // Message: 수신 바이트
        public string Reason;   // Close: 사유
    }

    /// <summary>
    /// 플랫폼별 WebSocket 백엔드. 이벤트는 내부 큐에 쌓이고 메인 스레드에서 TryDequeue로 소비한다.
    /// WebGL = JsWebSocket(jslib), Editor/Standalone = DotNetWebSocket(ClientWebSocket).
    /// </summary>
    public interface IWebSocket : IDisposable
    {
        void Connect(string url);
        /// <summary>바이너리 메시지 1개 송신 (패킷 1개). 연결 전/후에는 무시.</summary>
        void Send(byte[] data);
        void Close();
        /// <summary>
        /// packet을 intervalMs마다 자동 송신한다(하트비트). 메인 루프가 멈춰도 동작한다
        /// (WebGL: 브라우저 setInterval, Standalone: 스레드 타이머). intervalMs &lt;= 0이면 해제.
        /// </summary>
        void SetKeepAlive(byte[] packet, int intervalMs);
        bool TryDequeue(out WsEvent ev);
    }

    public static class WebSocketFactory
    {
        public static IWebSocket Create()
        {
#if UNITY_WEBGL && !UNITY_EDITOR
            return new JsWebSocket();
#else
            return new DotNetWebSocket();
#endif
        }
    }
}

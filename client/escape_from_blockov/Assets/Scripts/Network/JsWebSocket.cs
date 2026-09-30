#if UNITY_WEBGL && !UNITY_EDITOR
using System.Runtime.InteropServices;
using System.Text;

namespace Blockov.Net
{
    /// <summary>
    /// WebGL용 WebSocket 백엔드. 브라우저 WebSocket(Assets/Plugins/WebGL/BlockovWebSocket.jslib)을 사용한다.
    /// JS가 이벤트를 큐에 쌓고 C#이 매 프레임 폴링한다 (콜백 대신 폴링 → 처리 경로를 DotNet 백엔드와 통일).
    /// 하트비트는 JS setInterval로 보내므로 탭이 백그라운드여서 Unity 루프가 멈춰도 전송된다.
    /// </summary>
    public sealed class JsWebSocket : IWebSocket
    {
        [DllImport("__Internal")] static extern int BlockovWS_Create(string url);
        [DllImport("__Internal")] static extern int BlockovWS_Send(int id, byte[] data, int length);
        [DllImport("__Internal")] static extern void BlockovWS_SetKeepAlive(int id, byte[] data, int length, int intervalMs);
        [DllImport("__Internal")] static extern int BlockovWS_PeekEventType(int id);
        [DllImport("__Internal")] static extern int BlockovWS_PeekEventLength(int id);
        [DllImport("__Internal")] static extern int BlockovWS_PopEvent(int id, byte[] buffer, int length);
        [DllImport("__Internal")] static extern void BlockovWS_Close(int id);
        [DllImport("__Internal")] static extern void BlockovWS_Free(int id);

        int _id;

        public void Connect(string url) => _id = BlockovWS_Create(url);

        public void Send(byte[] data)
        {
            if (_id != 0 && data != null && data.Length > 0)
                BlockovWS_Send(_id, data, data.Length);
        }

        public void SetKeepAlive(byte[] packet, int intervalMs)
        {
            if (_id == 0) return;
            if (packet == null || intervalMs <= 0)
                BlockovWS_SetKeepAlive(_id, new byte[1], 0, 0);
            else
                BlockovWS_SetKeepAlive(_id, packet, packet.Length, intervalMs);
        }

        public bool TryDequeue(out WsEvent ev)
        {
            ev = default;
            if (_id == 0) return false;
            int type = BlockovWS_PeekEventType(_id);
            if (type == 0) return false;

            int len = BlockovWS_PeekEventLength(_id);
            var buf = new byte[len > 0 ? len : 1];
            BlockovWS_PopEvent(_id, buf, len);

            ev.Type = (WsEventType)type;
            if (ev.Type == WsEventType.Message)
                ev.Data = len > 0 ? buf : new byte[0];
            else if (ev.Type == WsEventType.Close)
                ev.Reason = len > 0 ? Encoding.UTF8.GetString(buf, 0, len) : "closed";
            return true;
        }

        public void Close()
        {
            if (_id != 0) BlockovWS_Close(_id);
        }

        public void Dispose()
        {
            if (_id == 0) return;
            BlockovWS_Close(_id);
            BlockovWS_Free(_id);
            _id = 0;
        }
    }
}
#endif

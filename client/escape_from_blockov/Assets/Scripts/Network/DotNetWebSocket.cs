#if !UNITY_WEBGL || UNITY_EDITOR
using System;
using System.Collections.Concurrent;
using System.IO;
using System.Net.WebSockets;
using System.Threading;
using System.Threading.Tasks;

namespace Blockov.Net
{
    /// <summary>
    /// Editor/Standalone용 WebSocket 백엔드 (System.Net.WebSockets.ClientWebSocket).
    /// 수신은 백그라운드 루프, 송신은 큐 + 단일 송신 루프(동시 SendAsync 금지 규칙 준수).
    /// </summary>
    public sealed class DotNetWebSocket : IWebSocket
    {
        readonly ConcurrentQueue<WsEvent> _events = new ConcurrentQueue<WsEvent>();
        readonly ConcurrentQueue<byte[]> _sendQueue = new ConcurrentQueue<byte[]>();
        readonly SemaphoreSlim _sendSignal = new SemaphoreSlim(0);
        ClientWebSocket _ws;
        CancellationTokenSource _cts;
        Timer _keepAlive;
        int _closeRaised;

        public void Connect(string url)
        {
            _ws = new ClientWebSocket();
            _cts = new CancellationTokenSource();
            _ = RunAsync(new Uri(url), _cts.Token);
        }

        async Task RunAsync(Uri uri, CancellationToken ct)
        {
            try
            {
                await _ws.ConnectAsync(uri, ct).ConfigureAwait(false);
                _events.Enqueue(new WsEvent { Type = WsEventType.Open });
                _ = SendLoopAsync(ct);

                var buf = new byte[8192];
                using var ms = new MemoryStream();
                while (!ct.IsCancellationRequested && _ws.State == WebSocketState.Open)
                {
                    var r = await _ws.ReceiveAsync(new ArraySegment<byte>(buf), ct).ConfigureAwait(false);
                    if (r.MessageType == WebSocketMessageType.Close)
                    {
                        RaiseClose($"closed by server ({r.CloseStatus} {r.CloseStatusDescription})");
                        return;
                    }
                    if (r.MessageType == WebSocketMessageType.Text)
                    {
                        RaiseClose("text frame not allowed");
                        return;
                    }
                    ms.Write(buf, 0, r.Count);
                    if (r.EndOfMessage)
                    {
                        _events.Enqueue(new WsEvent { Type = WsEventType.Message, Data = ms.ToArray() });
                        ms.SetLength(0);
                    }
                }
                RaiseClose("socket state " + _ws.State);
            }
            catch (Exception e)
            {
                RaiseClose(ct.IsCancellationRequested ? "client close" : e.Message);
            }
        }

        async Task SendLoopAsync(CancellationToken ct)
        {
            try
            {
                while (!ct.IsCancellationRequested)
                {
                    await _sendSignal.WaitAsync(ct).ConfigureAwait(false);
                    while (_sendQueue.TryDequeue(out var data))
                        await _ws.SendAsync(new ArraySegment<byte>(data), WebSocketMessageType.Binary, true, ct).ConfigureAwait(false);
                }
            }
            catch (Exception e)
            {
                RaiseClose(ct.IsCancellationRequested ? "client close" : "send failed: " + e.Message);
            }
        }

        public void Send(byte[] data)
        {
            if (_ws == null || _ws.State != WebSocketState.Open) return;
            _sendQueue.Enqueue(data);
            _sendSignal.Release();
        }

        public void SetKeepAlive(byte[] packet, int intervalMs)
        {
            _keepAlive?.Dispose();
            _keepAlive = null;
            if (packet == null || intervalMs <= 0) return;
            _keepAlive = new Timer(_ => Send(packet), null, intervalMs, intervalMs);
        }

        void RaiseClose(string reason)
        {
            if (Interlocked.Exchange(ref _closeRaised, 1) != 0) return;
            _keepAlive?.Dispose();
            _events.Enqueue(new WsEvent { Type = WsEventType.Close, Reason = reason });
        }

        public bool TryDequeue(out WsEvent ev) => _events.TryDequeue(out ev);

        public void Close()
        {
            _keepAlive?.Dispose();
            _keepAlive = null;
            if (_ws == null) return;
            try
            {
                if (_ws.State == WebSocketState.Open)
                    _ = _ws.CloseAsync(WebSocketCloseStatus.NormalClosure, "client close", CancellationToken.None)
                           .ContinueWith(_ => _cts?.Cancel());
                else
                    _cts?.Cancel();
            }
            catch { _cts?.Cancel(); }
        }

        public void Dispose()
        {
            Close();
            _ws?.Dispose();
        }
    }
}
#endif

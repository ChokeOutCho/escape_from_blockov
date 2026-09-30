using System;

namespace Blockov.Net
{
    /// <summary>
    /// WebSocket 메시지 경계를 신뢰하지 않고 바이트 스트림으로 재조립해 NetHeader 패킷 단위로 꺼낸다.
    /// (게이트웨이는 TCP 청크를 그대로 WS 메시지로 보내므로 패킷이 쪼개지거나 붙어서 올 수 있다)
    /// </summary>
    public sealed class PacketAssembler
    {
        byte[] _buf = new byte[8192];
        int _start;
        int _end;

        /// <summary>프로토콜 위반 시 설정된다. 설정되면 연결을 끊어야 한다.</summary>
        public string Error { get; private set; }

        public void Reset()
        {
            _start = _end = 0;
            Error = null;
        }

        public void Append(byte[] data)
        {
            if (data == null || data.Length == 0) return;
            if (_end + data.Length > _buf.Length)
            {
                int used = _end - _start;
                if (used + data.Length > _buf.Length)
                {
                    var bigger = new byte[Math.Max(_buf.Length * 2, used + data.Length)];
                    Buffer.BlockCopy(_buf, _start, bigger, 0, used);
                    _buf = bigger;
                }
                else
                {
                    Buffer.BlockCopy(_buf, _start, _buf, 0, used);
                }
                _start = 0;
                _end = used;
            }
            Buffer.BlockCopy(data, 0, _buf, _end, data.Length);
            _end += data.Length;
        }

        /// <summary>완성된 패킷 1개의 페이로드(WORD Type부터)를 꺼낸다.</summary>
        public bool TryPop(out byte[] payload)
        {
            payload = null;
            if (Error != null) return false;

            int used = _end - _start;
            if (used < NetConst.HeaderSize) return false;

            byte code = _buf[_start];
            int len = _buf[_start + 1] | (_buf[_start + 2] << 8);
            if (code != NetConst.HeaderCode) { Error = $"Wrong header code {code}"; return false; }
            if (len > NetConst.MaxPayload) { Error = $"Wrong header len {len}"; return false; }
            if (len < 2) { Error = $"Payload too short {len}"; return false; }
            if (used < NetConst.HeaderSize + len) return false;

            payload = new byte[len];
            Buffer.BlockCopy(_buf, _start + NetConst.HeaderSize, payload, 0, len);
            _start += NetConst.HeaderSize + len;
            if (_start == _end) _start = _end = 0;
            return true;
        }
    }
}

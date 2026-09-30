using System;
using System.Buffers.Binary;

namespace Blockov.Net
{
    /// <summary>
    /// 페이로드를 리틀 엔디안으로 직렬화하고 NetHeader를 붙여 송신 바이트를 만든다.
    /// 페이로드가 NetConst.MaxPayload를 넘으면 예외 (서버처럼 무음 실패하지 않게).
    /// </summary>
    public sealed class PacketWriter
    {
        readonly byte[] _buf = new byte[NetConst.HeaderSize + NetConst.MaxPayload];
        int _pos = NetConst.HeaderSize;

        public PacketWriter(PacketType type) => WriteUInt16((ushort)type);

        public int PayloadSize => _pos - NetConst.HeaderSize;

        Span<byte> Take(int n)
        {
            if (_pos + n > _buf.Length)
                throw new InvalidOperationException($"Packet payload exceeds {NetConst.MaxPayload} bytes");
            var span = new Span<byte>(_buf, _pos, n);
            _pos += n;
            return span;
        }

        public PacketWriter WriteByte(byte v) { Take(1)[0] = v; return this; }
        public PacketWriter WriteUInt16(ushort v) { BinaryPrimitives.WriteUInt16LittleEndian(Take(2), v); return this; }
        public PacketWriter WriteInt32(int v) { BinaryPrimitives.WriteInt32LittleEndian(Take(4), v); return this; }
        public PacketWriter WriteUInt32(uint v) { BinaryPrimitives.WriteUInt32LittleEndian(Take(4), v); return this; }
        public PacketWriter WriteInt64(long v) { BinaryPrimitives.WriteInt64LittleEndian(Take(8), v); return this; }
        public PacketWriter WriteFloat(float v) { BinaryPrimitives.WriteInt32LittleEndian(Take(4), BitConverter.SingleToInt32Bits(v)); return this; }

        /// <summary>고정 길이 필드. src가 짧으면 0으로 채우고 길면 자른다.</summary>
        public PacketWriter WriteFixedBytes(byte[] src, int fixedLength)
        {
            var dst = Take(fixedLength);
            dst.Clear();
            if (src != null)
                new ReadOnlySpan<byte>(src, 0, Math.Min(src.Length, fixedLength)).CopyTo(dst);
            return this;
        }

        /// <summary>WCHAR Name[12]: UTF-16LE 12자로 자르고 남는 칸은 0.</summary>
        public PacketWriter WriteName(string name)
        {
            name ??= "";
            for (int i = 0; i < NetConst.NameLength; i++)
                WriteUInt16(i < name.Length ? name[i] : (ushort)0);
            return this;
        }

        /// <summary>NetHeader(5B) + 페이로드. WebSocket 메시지 1개 = 패킷 1개로 보낸다.</summary>
        public byte[] ToPacket()
        {
            int len = PayloadSize;
            _buf[0] = NetConst.HeaderCode;
            BinaryPrimitives.WriteUInt16LittleEndian(new Span<byte>(_buf, 1, 2), (ushort)len);
            _buf[3] = 0; // RandKey (암호화 미사용)
            _buf[4] = 0; // CheckSum (암호화 미사용)
            var result = new byte[_pos];
            Buffer.BlockCopy(_buf, 0, result, 0, _pos);
            return result;
        }
    }
}

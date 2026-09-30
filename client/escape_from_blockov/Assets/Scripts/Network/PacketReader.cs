using System;
using System.Buffers.Binary;

namespace Blockov.Net
{
    /// <summary>수신 페이로드(WORD Type 포함) 리틀 엔디안 읽기. 범위를 넘으면 예외.</summary>
    public sealed class PacketReader
    {
        readonly byte[] _buf;
        int _pos;

        public PacketReader(byte[] payload)
        {
            _buf = payload;
            Type = (PacketType)ReadUInt16();
        }

        public PacketType Type { get; }
        public int Remaining => _buf.Length - _pos;

        ReadOnlySpan<byte> Take(int n)
        {
            if (_pos + n > _buf.Length)
                throw new FormatException($"Packet {Type} too short (need {n}, remaining {Remaining})");
            var span = new ReadOnlySpan<byte>(_buf, _pos, n);
            _pos += n;
            return span;
        }

        public byte ReadByte() => Take(1)[0];
        public ushort ReadUInt16() => BinaryPrimitives.ReadUInt16LittleEndian(Take(2));
        public int ReadInt32() => BinaryPrimitives.ReadInt32LittleEndian(Take(4));
        public uint ReadUInt32() => BinaryPrimitives.ReadUInt32LittleEndian(Take(4));
        public long ReadInt64() => BinaryPrimitives.ReadInt64LittleEndian(Take(8));
        public float ReadFloat() => BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(Take(4)));

        /// <summary>WCHAR Name[12] (UTF-16LE, 0에서 종료)</summary>
        public string ReadName()
        {
            var chars = new char[NetConst.NameLength];
            int n = 0;
            bool ended = false;
            for (int i = 0; i < NetConst.NameLength; i++)
            {
                ushort c = ReadUInt16();
                if (c == 0) ended = true;
                if (!ended) chars[n++] = (char)c;
            }
            return new string(chars, 0, n);
        }

        public void Skip(int n) => Take(n);
    }
}

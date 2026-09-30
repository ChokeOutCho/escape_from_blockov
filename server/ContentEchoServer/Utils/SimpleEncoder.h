#pragma once

// 인코딩 범위 = CheckSum(1) + Payload(최대 PAYLOAD_LEN_DEFAULT=512). Packet.h와 함께 변경할 것
#define MAX_PACKET_SIZE (512 + 1)

class SimpleEncoder
{
public:
    SimpleEncoder()
        : m_original(nullptr), m_encoded(nullptr), m_bufferSize(0), m_K(0), m_RK(0)
    {}
    /// <summary>
    /// 같은 버퍼로 지정하면 버퍼 자체의 값을 바꿔버린다.
    /// </summary>
    /// <param name="original"></param>
    /// <param name="encoded"></param>
    /// <param name="bufferSize"></param>
    __inline void SetBuffers(unsigned char* original, unsigned char* encoded, int bufferSize)
    {
        m_original = original;
        m_encoded = encoded;
        m_bufferSize = bufferSize;
    }

    __inline void SetKeys(unsigned char fixedKey, unsigned char randomKey)
    {
        m_K = fixedKey;
        m_RK = randomKey;
    }

    __inline void Encode()
    {
        unsigned char prevP = 0;
        unsigned char temp[MAX_PACKET_SIZE];

        for (int i = 0; i < m_bufferSize; ++i)
        {
            unsigned char p = m_original[i] ^ (i == 0 ? m_RK + 1
                : prevP + m_RK + (unsigned char)(i + 1));
            prevP = p;
            temp[i] = p ^ (i == 0 ? m_K + 1
                : temp[i - 1] + m_K + (unsigned char)(i + 1));
        }

        // 결과를 한 번에 복사
        memcpy(m_encoded, temp, m_bufferSize);
    }

    __inline void Decode()
    {
        unsigned char prevP = 0;
        unsigned char temp[MAX_PACKET_SIZE];

        for (int i = 0; i < m_bufferSize; ++i)
        {
            unsigned char p = m_encoded[i] ^ (i == 0 ? m_K + 1
                : m_encoded[i - 1] + m_K + (unsigned char)(i + 1));
            temp[i] = p ^ (i == 0 ? m_RK + 1
                : prevP + m_RK + (unsigned char)(i + 1));
            prevP = p;
        }

        // 결과를 한 번에 복사
        memcpy(m_original, temp, m_bufferSize);
    }

    __forceinline unsigned char CalculateChecksum(const unsigned char* buf, int len) const
    {
        unsigned int sum = 0;
        for (int i = 0; i < len; ++i)
        {
            sum += buf[i];
        }
        return (unsigned char)sum; // % 256 대신 캐스팅만
    }

    unsigned char GetK() const { return m_K; }
    unsigned char GetRK() const { return m_RK; }

private:
    unsigned char* m_original;
    unsigned char* m_encoded;
    int m_bufferSize;
    unsigned char m_K;
    unsigned char m_RK;
};
#pragma once
#include <iostream>
#include "Windows.h"
class NetLib_Helper
{
public:
	static void IPToWstring(unsigned long inIP, WCHAR* outSTR, int len)
	{
		unsigned char b1 = (inIP >> 24) & 0xFF;
		unsigned char b2 = (inIP >> 16) & 0xFF;
		unsigned char b3 = (inIP >> 8) & 0xFF;
		unsigned char b4 = (inIP) & 0xFF;
		swprintf(outSTR, len, L"%u.%u.%u.%u", b1, b2, b3, b4);
	}

	static void IPPortToWstring(unsigned long inIP, unsigned short inPort, WCHAR* outSTR, int len)
	{
		unsigned char b1 = (inIP >> 24) & 0xFF;
		unsigned char b2 = (inIP >> 16) & 0xFF;
		unsigned char b3 = (inIP >> 8) & 0xFF;
		unsigned char b4 = (inIP) & 0xFF;
		swprintf(outSTR, len, L"%u.%u.%u.%u:%u", b1, b2, b3, b4, inPort);
	}

	// 난수 생성 경량화 (xorshift32)
	__forceinline static unsigned int FastRand()
	{
		static thread_local unsigned int state = (unsigned int)timeGetTime();
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}
};
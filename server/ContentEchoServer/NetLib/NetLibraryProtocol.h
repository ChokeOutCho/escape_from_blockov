#pragma once
#pragma pack(push, 1)
//Code(1byte) - Len(2byte) - RandKey(1byte) - CheckSum(1byte) - Payload(Len byte)
struct NetHeader
{
	unsigned char Code;
	unsigned short Len;
	unsigned char RandKey;
	unsigned char CheckSum;
};
#pragma pack(pop)

struct Opt_Encryption
{
	char Header_Code = 0x77;
	char Fixed_Key = 0x32;
};

struct ContentMSG
{
	// type
	// ENTER	=	0
	// LEAVE	=	1
	// RELEASE	=	2
	short type;
	void* session;
};

struct ContentQueueHeader
{
	short Len;
};
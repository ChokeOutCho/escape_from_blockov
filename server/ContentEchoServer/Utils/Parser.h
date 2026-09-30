#pragma once
#include <fstream>
#include <string>

#define MAX_PAIRS 10
#define MAX_KEY_LEN 32
#define MAX_VAL_LEN 64

class Parser
{
private:
	struct Pair
	{
		char key[MAX_KEY_LEN];
		char value[MAX_VAL_LEN];
	};

	Pair pairs[MAX_PAIRS];
	int count;

public:
#pragma warning(push)
#pragma warning(disable : 26495)
	Parser() : count(0) {}
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable : 4996)
	int GetInt(const char* key) const
	{
		const char* val = GetString(key);
		return val ? atoi(val) : 0;
	}

	bool HasKey(const char* key) const
	{
		for (int i = 0; i < count; ++i)
		{
			if (strcmp(pairs[i].key, key) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool GetBool(const char* key) const
	{
		const char* val = GetString(key);
		if (!val) return false;
		return strcmp(val, "true") == 0 || strcmp(val, "1") == 0;
	}

	float GetFloat(const char* key) const
	{
		const char* val = GetString(key);
		return val ? static_cast<float>(atof(val)) : 0.0f;
	}

	double GetDouble(const char* key) const
	{
		const char* val = GetString(key);
		return val ? atof(val) : 0.0;
	}

	long GetLong(const char* key) const
	{
		const char* val = GetString(key);
		return val ? atol(val) : 0;
	}
	void parse(const char* json)
	{
		count = 0;
		const char* ptr = json;

		while (*ptr && count < MAX_PAIRS)
		{
			// 키 시작
			while (*ptr && *ptr != '"') ptr++;
			if (!*ptr) break;

			const char* key_start = ptr + 1;
			const char* key_end = strchr(key_start, '"');
			if (!key_end) break;

			__int64 key_len = key_end - key_start;
			if (key_len >= MAX_KEY_LEN) key_len = MAX_KEY_LEN - 1;
			strncpy(pairs[count].key, key_start, key_len);
			pairs[count].key[key_len] = '\0';

			// 콜론 찾기
			ptr = key_end + 1;
			while (*ptr && *ptr != ':') ptr++;
			if (!*ptr) break;
			ptr++; // 콜론 다음으로 이동

			// 공백 건너뛰기
			while (*ptr == ' ' || *ptr == '\n' || *ptr == '\r') ptr++;

			// 값 처리
			const char* val_start;
			const char* val_end;

			if (*ptr == '"')
			{
				val_start = ptr + 1;
				val_end = strchr(val_start, '"');
				if (!val_end) break;

				__int64 val_len = val_end - val_start;
				if (val_len >= MAX_VAL_LEN) val_len = MAX_VAL_LEN - 1;
				strncpy(pairs[count].value, val_start, val_len);
				pairs[count].value[val_len] = '\0';

				ptr = val_end + 1;
			}
			else
			{
				val_start = ptr;
				val_end = val_start;
				while (*val_end && *val_end != ',' && *val_end != '}' && *val_end != '\n') val_end++;

				__int64 val_len = val_end - val_start;
				if (val_len >= MAX_VAL_LEN) val_len = MAX_VAL_LEN - 1;
				strncpy(pairs[count].value, val_start, val_len);
				pairs[count].value[val_len] = '\0';

				ptr = val_end;
			}

			count++;

			// 다음 키로 이동
			while (*ptr && (*ptr == ',' || *ptr == ' ' || *ptr == '\n' || *ptr == '\r')) ptr++;
		}

	}
#pragma warning(pop)
	bool loadFromFile(const char* filename)
	{
		std::ifstream file(filename);
		if (!file.is_open())
		{
			return false;
		}

		// 전체 파일 내용을 하나의 문자열로 읽기
		std::string content;
		std::string line;
		while (std::getline(file, line))
		{
			content += line;
		}
		file.close();

		// 파싱
		parse(content.c_str());
		return true;
	}

	const char* GetString(const char* key) const
	{
		for (int i = 0; i < count; ++i)
		{
			if (strcmp(pairs[i].key, key) == 0)
			{
				return pairs[i].value;
			}
		}
		return NULL;
	}

	bool CopyString(const char* key, char* buffer, size_t bufferSize) const
	{
		for (int i = 0; i < count; ++i)
		{
			if (strcmp(pairs[i].key, key) == 0)
			{
				// 문자열 길이 확인 후 복사
				size_t valueLen = strlen(pairs[i].value);
				if (bufferSize == 0) return false;
#pragma warning(push)
#pragma warning(disable : 4996)
				// 최대 bufferSize - 1만큼 복사하고 null-terminate
				strncpy(buffer, pairs[i].value, bufferSize - 1);
#pragma warning(pop)
				buffer[bufferSize - 1] = '\0';
				return true;
			}
		}
		return false; // key를 찾지 못한 경우
	}




};
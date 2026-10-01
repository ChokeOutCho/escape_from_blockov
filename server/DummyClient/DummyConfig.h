#pragma once
////////////////////////////////////////////////////////////////////////
// dummy_config.txt 로드. "key": value 형식 (GameServer/game_config.txt 와 같은 파서)
////////////////////////////////////////////////////////////////////////
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>

struct DummyConfig
{
	// 접속 대상 (게임 서버 TCP 직접)
	std::string serverIp = "127.0.0.1";
	int serverPort = 10301;

	// 규모 / 스레드
	int count = 0;                  // 0이면 시작할 때 입력받는다
	int maxDummies = 20000;         // 한 프로세스 최대 더미 수 (한 PC에서 서버 1곳에 약 16,000 연결이 한계: 임시 포트 수)
	int ioThreads = 4;              // IOCP 워커
	int logicThreads = 4;           // 더미 AI 틱 스레드
	int tickMs = 50;                // AI 틱 간격
	int connectPerSec = 300;        // 초당 신규 접속 수 (서버 accept 폭주 방지)

	// 사망 처리: true = 자동 재접속(인원 유지), false = 그대로 퇴장
	bool reconnectOnDeath = true;
	int reconnectDelayMs = 2000;

	// 행동
	bool fire = true;               // 사격 여부
	float engageRange = 60.0f;      // 교전 거리 (무기 사거리 - 2 와 작은 값 사용)
	float sprintChance = 0.3f;      // 배회 중 달리기 비율
	// 구르기 (game-spec 17.5): 교전 중 쿨타임 후 초당 확률(적의 옆 방향), 배회 중 쿨타임마다 확률(진행 방향)
	bool roll = true;
	// 더미 강도 (game-spec 17.5): 더미마다 약함 w를 [weaknessMin, weaknessMax]에서 균등 무작위로 1회 뽑아
	// 반응 지연 = reactionBaseMs × w, 연사 간격 × (1 + w), 조준 오차 ±aimErrorMaxDeg × w, 리드 사격 확률 1 - w
	float weaknessMin = 0.0f;
	float weaknessMax = 1.0f;
	int reactionBaseMs = 300;
	float aimErrorMaxDeg = 8.0f;
	float rollCombatPerSec = 0.3f;
	float rollWanderChance = 0.1f;
	int rollCooldownMs = 3000;      // 서버 roll_cooldown_ms
	int rollMs = 250;               // 서버 roll_ms
	float rollSpeedMult = 3.0f;     // 서버 roll_speed_mult
	std::string namePrefix = "Dummy";   // 닉네임 목록이 없을 때 이름 = 접두사 + 번호 (12자 이내)
	std::string namesFile = "dummy_names.txt";   // 닉네임 목록 (UTF-8, 한 줄에 하나)
	float lootRange = 50.0f;            // 에어드랍 인식 거리 (교전 중이 아닐 때 특수 무기를 가지러 감)
	bool loot = true;
	int airdropOpenMs = 2000;           // 서버 airdrop_open_ms

	// 서버와 같은 엄폐물 맵 (없으면 엄폐물 무시하고 이동 → 위치 보정이 늘어난다)
	std::string obstacleMap = "../../map/obstacles.bmp";

	bool Load(const char* path)
	{
		std::ifstream f(path);
		if (!f) return false;
		std::stringstream ss;
		ss << f.rdbuf();
		std::string s = ss.str();

		size_t i = 0;
		while ((i = s.find('"', i)) != std::string::npos)
		{
			size_t e = s.find('"', i + 1);
			if (e == std::string::npos) break;
			std::string key = s.substr(i + 1, e - i - 1);
			size_t c = s.find(':', e);
			if (c == std::string::npos) break;
			size_t v = c + 1;
			while (v < s.size() && isspace((unsigned char)s[v])) v++;
			std::string val;
			if (v < s.size() && s[v] == '"')
			{
				size_t ve = s.find('"', v + 1);
				if (ve == std::string::npos) break;
				val = s.substr(v + 1, ve - v - 1);
				i = ve + 1;
			}
			else
			{
				size_t ve = v;
				while (ve < s.size() && s[ve] != ',' && s[ve] != '}' && s[ve] != '\n' && s[ve] != '\r') ve++;
				val = s.substr(v, ve - v);
				while (!val.empty() && isspace((unsigned char)val.back())) val.pop_back();
				i = ve;
			}
			Apply(key, val);
		}
		return true;
	}

private:
	static bool ToBool(const std::string& v) { return v == "true" || v == "1"; }

	void Apply(const std::string& k, const std::string& v)
	{
		int n = atoi(v.c_str());
		float fl = (float)atof(v.c_str());
		if (k == "server_ip") serverIp = v;
		else if (k == "server_port") serverPort = n;
		else if (k == "count") count = n;
		else if (k == "max_dummies") maxDummies = n;
		else if (k == "io_threads") ioThreads = n;
		else if (k == "logic_threads") logicThreads = n;
		else if (k == "tick_ms") tickMs = n;
		else if (k == "connect_per_sec") connectPerSec = n;
		else if (k == "death_mode") reconnectOnDeath = (v != "leave");
		else if (k == "reconnect_delay_ms") reconnectDelayMs = n;
		else if (k == "fire") fire = ToBool(v);
		else if (k == "engage_range") engageRange = fl;
		else if (k == "sprint_chance") sprintChance = fl;
		else if (k == "weakness_min") weaknessMin = fl;
		else if (k == "weakness_max") weaknessMax = fl;
		else if (k == "reaction_base_ms") reactionBaseMs = n;
		else if (k == "aim_error_max_deg") aimErrorMaxDeg = fl;
		else if (k == "roll") roll = (v == "true" || v == "1");
		else if (k == "roll_combat_per_sec") rollCombatPerSec = fl;
		else if (k == "roll_wander_chance") rollWanderChance = fl;
		else if (k == "roll_cooldown_ms") rollCooldownMs = n;
		else if (k == "roll_ms") rollMs = n;
		else if (k == "roll_speed_mult") rollSpeedMult = fl;
		else if (k == "name_prefix") namePrefix = v;
		else if (k == "names_file") namesFile = v;
		else if (k == "loot_range") lootRange = fl;
		else if (k == "loot") loot = (v == "true" || v == "1");
		else if (k == "airdrop_open_ms") airdropOpenMs = n;
		else if (k == "obstacle_map") obstacleMap = v;
	}
};

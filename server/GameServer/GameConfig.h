#pragma once
////////////////////////////////////////////////////////////////////////
// game_config.txt 로드 (game-spec 10.6). "key": value 형식(JSON 유사), 키 수 제한 없음.
// 없는 키는 기본값 유지.
////////////////////////////////////////////////////////////////////////
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>

struct GameConfig
{
	// 네트워크 (echo_config.txt와 동일 의미)
	int port = 10301;
	int workerThreads = 8;
	int concurrentThreads = 8;
	int maxSessions = 1000;
	int sendBuf = 1000;
	bool zeroCopy = true;

	// 게임
	int roomCount = 4;
	int roomCapacity = 50;
	int battleTickMs = 33;
	int entryTickMs = 50;
	int viewSectorRadius = 1;
	int enterTimeoutMs = 10000;
	int heartbeatTimeoutMs = 180000;
	int deathDisconnectMs = 3000;
	float moveSpeed = 12.0f;
	float characterRadius = 0.5f;
	int maxHp = 100;
	int maxRewindMs = 500;
	float hitTolerance = 1.0f;
	int defaultWeaponId = 1;

	std::string weaponsFile = "weapons.txt";
	std::string obstacleMapFile = "../../map/obstacles.bmp";
	float sprintMultiplier = 1.2f;     // Shift 달리기 속도 배율 (클라에 SC_ENTER_GAME으로 전달)

	// v6 아이템·구르기·에어드랍·가방 (game-spec 6)
	int airdropIntervalMs = 60000;    // 회차 주기 (빈 방에 첫 입장 시 즉시 1회차 후 이 주기로)
	int airdropNoticeMs = 30000;      // 투하 예고: 이 시간 전에 위치를 정해 방 전체에 공개 (주기보다 길면 주기만큼)
	int coverRegenMs = 30000;         // 파괴된 엄폐물 재생 시간
	int airdropMax = 4;               // 방당 최대 (인원 기준 상한의 상한)
	int airdropPlayersPer = 30;       // 방 인원 N명당 1개 (올림, 최소 1)
	int bagLifetimeMs = 30000;
	int startBandages = 2;
	int maxBandages = 5;
	int bandageHeal = 30;
	int bandageMs = 2000;
	int rollMs = 250;
	float rollSpeedMult = 3.0f;
	int rollCooldownMs = 3000;
	float interactRange = 2.5f;
	int bagOpenMs = 1000;
	int airdropOpenMs = 2000;

	// 테스트 모드: 모든 플레이어를 한 섹터(test_spawn_sector_x/y, 기본 0,0)에 스폰
	bool testMode = false;
	int testSpawnSectorX = 0;
	int testSpawnSectorY = 0;
	int testSpawnRadius = -1;         // 테스트 모드 스폰 범위: -1 = 섹터 전체, 0 = 섹터 중심, >0 = 중심에서 반경 (m)

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
		if (k == "port") port = n;
		else if (k == "workerTH_Pool_size") workerThreads = n;
		else if (k == "concurrentTH_size") concurrentThreads = n;
		else if (k == "maxofsession") maxSessions = n;
		else if (k == "sendbuf") sendBuf = n;
		else if (k == "zerocopy") zeroCopy = ToBool(v);
		else if (k == "room_count") roomCount = n;
		else if (k == "room_capacity") roomCapacity = n;
		else if (k == "battle_tick_ms") battleTickMs = n;
		else if (k == "entry_tick_ms") entryTickMs = n;
		else if (k == "view_sector_radius") viewSectorRadius = n;
		else if (k == "enter_timeout_ms") enterTimeoutMs = n;
		else if (k == "heartbeat_timeout_ms") heartbeatTimeoutMs = n;
		else if (k == "death_disconnect_ms") deathDisconnectMs = n;
		else if (k == "move_speed") moveSpeed = fl;
		else if (k == "character_radius") characterRadius = fl;
		else if (k == "max_hp") maxHp = n;
		else if (k == "max_rewind_ms") maxRewindMs = n;
		else if (k == "hit_tolerance") hitTolerance = fl;
		else if (k == "default_weapon_id") defaultWeaponId = n;
		else if (k == "weapons_file") weaponsFile = v;
		else if (k == "obstacle_map") obstacleMapFile = v;
		else if (k == "sprint_multiplier") sprintMultiplier = fl;
		else if (k == "airdrop_interval_ms") airdropIntervalMs = n;
		else if (k == "airdrop_notice_ms") airdropNoticeMs = n;
		else if (k == "cover_regen_ms") coverRegenMs = n;
		else if (k == "airdrop_max") airdropMax = n;
		else if (k == "airdrop_players_per") airdropPlayersPer = n;
		else if (k == "bag_lifetime_ms") bagLifetimeMs = n;
		else if (k == "start_bandages") startBandages = n;
		else if (k == "max_bandages") maxBandages = n;
		else if (k == "bandage_heal") bandageHeal = n;
		else if (k == "bandage_ms") bandageMs = n;
		else if (k == "roll_ms") rollMs = n;
		else if (k == "roll_speed_mult") rollSpeedMult = fl;
		else if (k == "roll_cooldown_ms") rollCooldownMs = n;
		else if (k == "interact_range") interactRange = fl;
		else if (k == "bag_open_ms") bagOpenMs = n;
		else if (k == "airdrop_open_ms") airdropOpenMs = n;
		else if (k == "test_mode") testMode = ToBool(v);
		else if (k == "test_spawn_sector_x") testSpawnSectorX = n;
		else if (k == "test_spawn_sector_y") testSpawnSectorY = n;
		else if (k == "test_spawn_radius") testSpawnRadius = n;
	}
};

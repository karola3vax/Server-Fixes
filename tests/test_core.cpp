// Gate tests for src/core: built and run by scripts/build-linux.sh before the plugin, no SDK needed.

#include "core/config.h"
#include "core/pattern.h"
#include "core/rules.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{

int g_failures = 0;
int g_checks = 0;

#define CHECK(condition)                                                                       \
	do                                                                                     \
	{                                                                                      \
		++g_checks;                                                                    \
		if (!(condition))                                                              \
		{                                                                              \
			++g_failures;                                                          \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);       \
		}                                                                              \
	} while (0)

void Config()
{
	const sf::Config config = sf::ParseConfig(
		"// comment\n"
		"slow_animation.enable 1\n"
		"slow_animation.reload_interval 900   // trailing comment\n"
		"\n"
		"  beam_crash.enable   0\n"
		"steam_ban.whitelist \"76561198000000001, 76561198000000002\"\n"
		"webhook.url https://example.com/a//b\n"
		"badline\n"
		"team_limit.enable\n"
		".enable 1\n");
	CHECK(config.fixes.size() == 4);
	CHECK(sf::GetBool(config.fixes.at("slow_animation"), "enable", false));
	CHECK(sf::GetNumber(config.fixes.at("slow_animation"), "reload_interval", 1800, 60, 86400) == 900);
	CHECK(!sf::GetBool(config.fixes.at("beam_crash"), "enable", true));
	CHECK(sf::GetString(config.fixes.at("steam_ban"), "whitelist", "") == "76561198000000001, 76561198000000002");
	CHECK(sf::GetString(config.fixes.at("webhook"), "url", "") == "https://example.com/a//b");
	CHECK(config.errors.size() == 3); // badline, a key with no value, an empty fix name
	CHECK(config.fixes.count("team_limit") == 0);

	const sf::Options options{{"n", "abc"}, {"low", "5"}, {"b", "maybe"}};
	CHECK(sf::GetNumber(options, "n", 1800, 60, 86400) == 1800);    // not a number: fallback
	CHECK(sf::GetNumber(options, "low", 1800, 60, 86400) == 60);    // clamped
	CHECK(sf::GetNumber(options, "missing", 5, 1, 60) == 5);
	CHECK(sf::GetBool(options, "b", true));                         // unknown word: fallback
	CHECK(!sf::GetBool(options, "missing", false));
}

void Patterns()
{
	CHECK(!sf::ParsePattern(""));
	CHECK(!sf::ParsePattern("? ??"));
	CHECK(!sf::ParsePattern("4G"));
	CHECK(!sf::ParsePattern("488"));
	const auto pattern = sf::ParsePattern("55 48 ? 89 e5");
	CHECK(pattern && pattern->size() == 5 && (*pattern)[2] == -1 && (*pattern)[4] == 0xE5);

	const std::uint8_t code[] = {0x90, 0x55, 0x48, 0x01, 0x89, 0xE5, 0xC3, 0x55, 0x48, 0x02, 0x89, 0xE5};
	const auto two = sf::FindPattern(code, sizeof(code), *pattern);
	CHECK(two.size() == 2 && two[0] == 1 && two[1] == 7);
	CHECK(sf::FindPattern(code, sizeof(code), *pattern, 1).size() == 1);
	const auto tail = sf::ParsePattern("89 E5");
	CHECK(sf::FindPattern(code, sizeof(code), *tail, 5).size() == 2); // a match ending at the last byte
	const auto leading = sf::ParsePattern("? 48 ? 89");
	const auto found = sf::FindPattern(code, sizeof(code), *leading, 5);
	CHECK(found.size() == 2 && found[0] == 1);
	CHECK(sf::FindPattern(code, 3, *pattern).empty()); // buffer shorter than the pattern
	CHECK(sf::FindPattern(nullptr, 10, *pattern).empty());

	// A function another plugin hooked: KHook's jump replaced the first 6 bytes (seen live on CBeam::SetBeamOrigin).
	const auto beam = sf::ParsePattern("55 48 89 E5 41 54 49 89 F4 53 48 89 FB 48 83 EC ? 0F 1F 80");
	const std::uint8_t hooked[] = {0xE9, 0x00, 0x97, 0x77, 0xFE, 0x90, 0x49, 0x89, 0xF4, 0x53, 0x48, 0x89, 0xFB,
		0x48, 0x83, 0xEC, 0x10, 0x0F, 0x1F, 0x80, 0x00};
	CHECK(sf::FindPattern(hooked, sizeof(hooked), *beam).empty());
	const auto behind = sf::BehindHookJump(*beam);
	CHECK(behind && sf::FindPattern(hooked, sizeof(hooked), *behind).size() == 1);
	CHECK(!sf::BehindHookJump(*sf::ParsePattern("48 8D 35 ? ? ? ? 89 C2 48 63 46"))); // too little left to be safe
	CHECK(!sf::BehindHookJump(*sf::ParsePattern("75 ? 48 8B 03")));
}

void Players()
{
	CHECK(sf::HumanPlayers(3, 0, true) == 3);  // the bug: controllers read 0 with players on
	CHECK(sf::HumanPlayers(0, 3, true) == 3);  // still connecting, no Steam account yet
	CHECK(sf::HumanPlayers(0, 0, true) == 0);  // really empty: the only case that reloads
	CHECK(sf::HumanPlayers(2, -1, true) == 2);
	CHECK(sf::HumanPlayers(0, 0, false) == -1); // no entity system: unknown, never reload
	CHECK(sf::HumanPlayers(4, 0, false) == 4);
}

void Accounts()
{
	CHECK(sf::AccountIdFromSteamId64("76561198353131845") == 392866117u);
	CHECK(!sf::AccountIdFromSteamId64("76561197960265728"));  // account 0
	CHECK(!sf::AccountIdFromSteamId64("90071996842377216"));  // universe 1, type 4 (anonymous game server)
	CHECK(!sf::AccountIdFromSteamId64("STEAM_0:1:1"));
	CHECK(!sf::AccountIdFromSteamId64("7656119835313184x"));
	std::vector<std::string_view> rejected;
	const auto accounts = sf::ParseAccountList("76561198353131845, bad 76561198000000001,", &rejected);
	CHECK(accounts.size() == 2 && rejected.size() == 1 && rejected[0] == "bad");
}

void TeamLimit()
{
	CHECK(sf::RaisedLimit(10, 24) == 24);
	CHECK(sf::RaisedLimit(30, 24) == 30);
	CHECK(sf::RaisedLimit(10, 0) == 10);  // the broken maxClients read: leave the game's value
	CHECK(sf::RaisedLimit(10, 200) == 10);
}

void Voice()
{
	sf::VoiceMessage good;
	good.has_audio = good.has_xuid = good.format_valid = true;
	good.xuid = good.sender = 76561198353131845ull;
	good.packets = good.offsets = 3;
	good.voice_bytes = 300;
	good.message_bytes = 340;
	CHECK(sf::CheckVoiceMessage(good) == sf::VoiceVerdict::Pass);
	CHECK(sf::VoiceCost(good) == 352);

	auto spoofed = good;
	spoofed.xuid = 76561198000000001ull;
	CHECK(sf::CheckVoiceMessage(spoofed) == sf::VoiceVerdict::Malformed);
	auto empty = good;
	empty.voice_bytes = 0;
	CHECK(sf::CheckVoiceMessage(empty) == sf::VoiceVerdict::Malformed);
	auto huge = good;
	huge.message_bytes = 20000;
	CHECK(sf::CheckVoiceMessage(huge) == sf::VoiceVerdict::TooBig);
	auto many = good;
	many.packets = 65;
	CHECK(sf::CheckVoiceMessage(many) == sf::VoiceVerdict::TooBig);

	sf::VoiceBudget budget;
	const auto start = sf::VoiceBudget::Clock::now();
	int taken = 0;
	for (int i = 0; i < 300; ++i)
		taken += budget.Take(start, 100, 1) ? 1 : 0;
	CHECK(taken == 192);                                                  // message cap per second
	CHECK(budget.Take(start + std::chrono::milliseconds(1001), 100, 1)); // the window rolled over
	sf::VoiceBudget bytes;
	CHECK(bytes.Take(start, 200 * 1024, 1));
	CHECK(!bytes.Take(start, 100 * 1024, 1));                             // 256 KiB per second
	sf::VoiceBudget packets;
	CHECK(packets.Take(start, 10, 1000));
	CHECK(!packets.Take(start, 10, 100));                                 // 1024 packets per second
}

} // namespace

int main()
{
	Config();
	Patterns();
	Players();
	Accounts();
	TeamLimit();
	Voice();
	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

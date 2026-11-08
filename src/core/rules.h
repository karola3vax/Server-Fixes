#pragma once

// The decisions the fixes make, apart from the game's types, so tests/test_core.cpp checks them directly.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <vector>

namespace sf
{

// slow_animation: humans on the server from two independent counts, or -1 when neither can be trusted.
// GameFixes counted controllers only, below a CGlobalVars::maxClients its build read as 0, and reloaded
// the map under players every half hour. The higher count wins; with no entity system only the Steam
// count is left, and an empty answer from it alone is not trusted.
int HumanPlayers(int steam_accounts, int controllers, bool entity_system_ready);

// steam_ban whitelist: the Steam account id of a SteamID64 for an individual account in the public
// universe ("76561198000000000"), or nullopt.
std::optional<std::uint32_t> AccountIdFromSteamId64(std::string_view text);
// A comma or space separated list; invalid entries are returned in `rejected`.
std::vector<std::uint32_t> ParseAccountList(std::string_view text, std::vector<std::string_view> *rejected);

// team_limit: a team cap below maxplayers is raised to it; a broken maxplayers (outside 1..64) changes nothing.
int RaisedLimit(int current, int max_players);

// voice_flood: what a clc_VoiceData message carries, read off the protobuf by the fix.
struct VoiceMessage
{
	bool has_audio = false;
	bool has_xuid = false;
	std::uint64_t xuid = 0;
	std::uint64_t sender = 0; // the client's own SteamID64
	bool format_valid = false;
	std::uint32_t offsets = 0;
	std::uint32_t packets = 0;
	std::size_t voice_bytes = 0;
	std::size_t message_bytes = 0;
};

enum class VoiceVerdict
{
	Pass,
	Malformed,
	TooBig,
	OverBudget,
};

// Per-message limits: 16 KiB of voice data, 16 KiB serialized, 64 packets.
VoiceVerdict CheckVoiceMessage(const VoiceMessage &message);
// What a passing message costs against the budget: its size plus 4 bytes per packet.
std::size_t VoiceCost(const VoiceMessage &message);

// One client's rolling one-second budget: 192 messages, 256 KiB of cost, 1024 packets.
class VoiceBudget
{
public:
	using Clock = std::chrono::steady_clock;
	bool Take(Clock::time_point now, std::size_t cost, std::uint32_t packets);
	void Reset() { *this = VoiceBudget(); }

private:
	struct Work
	{
		Clock::time_point time;
		std::size_t cost;
		std::uint32_t packets;
	};
	std::deque<Work> recent_;
	std::size_t cost_ = 0;
	std::uint32_t packets_ = 0;
};

} // namespace sf

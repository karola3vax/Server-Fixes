#pragma once

#include "fix.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace sf
{

std::unique_ptr<Fix> MakeSlowAnimation();
std::unique_ptr<Fix> MakeBeamCrash();
std::unique_ptr<Fix> MakeInputActivatorCrash();
std::unique_ptr<Fix> MakeTeamLimit();
std::unique_ptr<Fix> MakeServerListPlayers();
std::unique_ptr<Fix> MakeSteamBan();
std::unique_ptr<Fix> MakeSvCheats();
std::unique_ptr<Fix> MakeVoiceFlood();
std::unique_ptr<Fix> MakeDemoRecord();
std::unique_ptr<Fix> MakeHammerId();
std::unique_ptr<Fix> MakeAbstractEntity();
std::unique_ptr<Fix> MakeWaterJump();
std::unique_ptr<Fix> MakeRampbug();
std::unique_ptr<Fix> MakeBlockNavLookup();

// The names server_fixes.cfg knows, in load order; CreateFixes() follows the same order.
inline const char *const kFixNames[] = {
	"slow_animation", "beam_crash", "input_activator_crash", "team_limit", "server_list_players",
	"steam_ban", "sv_cheats", "voice_flood", "demo_record", "hammer_id", "abstract_entity", "water_jump",
	"rampbug", "block_nav_lookup",
};

inline bool IsFixName(const std::string &name)
{
	for (const char *known : kFixNames)
		if (name == known)
			return true;
	return false;
}

inline std::vector<std::unique_ptr<Fix>> CreateFixes()
{
	std::vector<std::unique_ptr<Fix>> fixes;
	fixes.push_back(MakeSlowAnimation());
	fixes.push_back(MakeBeamCrash());
	fixes.push_back(MakeInputActivatorCrash());
	fixes.push_back(MakeTeamLimit());
	fixes.push_back(MakeServerListPlayers());
	fixes.push_back(MakeSteamBan());
	fixes.push_back(MakeSvCheats());
	fixes.push_back(MakeVoiceFlood());
	fixes.push_back(MakeDemoRecord());
	fixes.push_back(MakeHammerId());
	fixes.push_back(MakeAbstractEntity());
	fixes.push_back(MakeWaterJump());
	fixes.push_back(MakeRampbug());
	fixes.push_back(MakeBlockNavLookup());
	return fixes;
}

} // namespace sf

// curtime is a 32-bit float. After a day or two on one map it has lost enough precision that animation,
// movement and lag compensation go sluggish, and the game never resets it. Every reload_interval seconds,
// when nobody is on the server, the current map is reloaded; the rest of mp_timelimit carries over.
//
// GameFixes did the same, but counted players only from controllers below CGlobalVars::maxClients, which
// its build read as 0: it found the server empty at every check and reloaded the map under the players
// every half hour. Here two counts that don't share that read must both say empty (core/rules.h).

#include "all.h"
#include "core/rules.h"
#include "players.h"

#include <engine2/ivengineserver2.h>
#include <steam/steamclientpublic.h>
#include <tier0/convar.h>

#include <chrono>
#include <cstdio>

namespace sf
{

namespace
{

using Clock = std::chrono::steady_clock;

class SlowAnimation final : public Fix
{
public:
	const char *Name() const override { return "slow_animation"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		if (!players_.Load(context.schema, error))
			return false;
		interval_ = std::chrono::seconds(static_cast<long>(GetNumber(context.options, "reload_interval", 1800, 60, 7 * 86400)));
		next_check_ = Clock::now() + interval_;
		Log("checking for an empty server every %ld s", static_cast<long>(interval_.count()));
		return true;
	}

	void OnStartupServer(GameSessionConfiguration_t &, const char *map) override
	{
		std::snprintf(map_, sizeof(map_), "%s", map);
		map_start_ = Clock::now();
		next_check_ = map_start_ + interval_;
		restore_timelimit_ = pending_timelimit_ >= 0.0f;
	}

	void OnGameFrame() override
	{
		// The map's own cfg runs after StartupServer and would undo it; the first frame is past that.
		if (restore_timelimit_)
		{
			restore_timelimit_ = false;
			ConVarRefAbstract timelimit("mp_timelimit");
			if (timelimit.IsValidRef())
			{
				const float value = pending_timelimit_ > 0.0f ? pending_timelimit_ : 0.1f;
				Log("restoring the rest of the timelimit: mp_timelimit %.1f", value);
				timelimit.SetFloat(value);
			}
			pending_timelimit_ = -1.0f;
		}

		const auto now = Clock::now();
		if (now < next_check_)
			return;
		next_check_ = now + interval_;
		if (!map_[0])
		{
			Log("no reload: loaded mid-map, waiting for the next map start");
			return;
		}

		int steam = 0, controllers = 0;
		for (int slot = 0; slot < 64; ++slot)
		{
			const CSteamID *id = g_pEngineServer->GetClientSteamID(CPlayerSlot(slot));
			if (id && id->BIndividualAccount())
				++steam;
			CEntityInstance *controller = Players::Controller(slot);
			if (controller && players_.Connected(controller) && !players_.Bot(controller) && !players_.Tv(controller))
				++controllers;
		}
		const int humans = HumanPlayers(steam, controllers, Players::EntitySystemReady());
		if (humans != 0)
		{
			Log("no reload: %d player(s) (Steam %d, controllers %d) on %s", humans, steam, controllers, map_);
			return;
		}

		ConVarRefAbstract timelimit("mp_timelimit");
		const float limit = timelimit.IsValidRef() ? timelimit.GetFloat() : 0.0f;
		if (limit > 0.0f)
			pending_timelimit_ = limit - std::chrono::duration<float, std::ratio<60>>(now - map_start_).count();
		if (g_pEngineServer->IsMapValid(map_))
		{
			Log("server empty, reloading %s", map_);
			g_pEngineServer->ChangeLevel(map_, nullptr);
		}
		else
		{
			// Not a stock map: a workshop one, which only changes level by its id.
			char command[320];
			std::snprintf(command, sizeof(command), "ds_workshop_changelevel %s", map_);
			Log("server empty, %s", command);
			g_pEngineServer->ServerCommand(command);
		}
	}

private:
	Players players_;
	std::chrono::seconds interval_{1800};
	Clock::time_point next_check_{};
	Clock::time_point map_start_{};
	char map_[256] = "";
	float pending_timelimit_ = -1.0f;
	bool restore_timelimit_ = false;
};

} // namespace

std::unique_ptr<Fix> MakeSlowAnimation()
{
	return std::make_unique<SlowAnimation>();
}

} // namespace sf

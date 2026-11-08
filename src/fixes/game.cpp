// Fixes to game rules and entities.
//
// team_limit: a join is refused as "team full" once a team has as many players as the smaller of
// CCSGameRules' m_iMaxNum<team> and m_iSpawnPointCount_<team>. The game sets the first at level init and
// recounts the second whenever a spawn point changes (maps do that mid-round), so every frame both are
// raised to maxplayers when below it. Players beyond the map's spawn points share them.
//
// sv_cheats: turning sv_cheats off leaves anyone in noclip flying. The moment it flips to 0, every such
// pawn goes back to walking.
//
// hammer_id: SetSchemaHammerUniqueId only writes m_sUniqueHammerID behind a jnz, so most entities never
// get one. The jnz becomes a jmp, and goes back on unload. (CS2Fixes 61937f7, as carried by GameFixes_mm.)

#include "all.h"
#include "core/rules.h"
#include "players.h"
#include "schema.h"
#include "signatures.h"

#include <const.h>
#include <engine2/ivengineserver2.h>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <gameentitysystem.h>
#include <tier0/convar.h>
#include <tier0/icvar.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace sf
{

namespace
{

class TeamLimit final : public Fix
{
public:
	const char *Name() const override { return "team_limit"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		return Offset(context.schema, "CCSGameRulesProxy", "m_pGameRules", rules_, error)
			&& Offset(context.schema, "CCSGameRules", "m_iMaxNumTerrorists", max_t_, error)
			&& Offset(context.schema, "CCSGameRules", "m_iMaxNumCTs", max_ct_, error)
			&& Offset(context.schema, "CCSGameRules", "m_iSpawnPointCount_Terrorist", spawns_t_, error)
			&& Offset(context.schema, "CCSGameRules", "m_iSpawnPointCount_CT", spawns_ct_, error);
	}

	void Unload() override { game_rules_ = nullptr; }

	void OnStartupServer(GameSessionConfiguration_t &, const char *) override { game_rules_ = nullptr; } // new map, new rules

	void OnGameFrame() override
	{
		void *rules = GameRules();
		CGlobalVars *globals = g_pEngineServer->GetServerGlobals();
		if (!rules || !globals)
			return;
		// None of these are networked; a plain write is enough.
		for (std::ptrdiff_t field : {max_t_, max_ct_, spawns_t_, spawns_ct_})
		{
			auto &limit = schema::Field<std::int32_t>(rules, field);
			limit = RaisedLimit(limit, globals->maxClients);
		}
	}

private:
	void *GameRules()
	{
		if (game_rules_)
			return game_rules_;
		CGameEntitySystem *entities = GameEntitySystem();
		if (!entities)
			return nullptr;
		for (CEntityIdentity *identity = entities->m_EntityList.m_pFirstActiveEntity; identity; identity = identity->m_pNext)
		{
			if (!identity->m_pInstance || std::strcmp(identity->GetClassname(), "cs_gamerules") != 0)
				continue;
			game_rules_ = schema::Field<void *>(identity->m_pInstance, rules_);
			break;
		}
		return game_rules_;
	}

	void *game_rules_ = nullptr;
	std::ptrdiff_t rules_ = -1, max_t_ = -1, max_ct_ = -1, spawns_t_ = -1, spawns_ct_ = -1;
};

class SvCheats final : public Fix
{
public:
	const char *Name() const override { return "sv_cheats"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		if (!players_.Load(context.schema, error)
			|| !Offset(context.schema, "CBaseEntity", "m_MoveType", move_type_, error)
			|| !Offset(context.schema, "CBaseEntity", "m_nActualMoveType", actual_move_type_, error))
			return false;
		instance_ = this;
		g_pCVar->InstallGlobalChangeCallback(&SvCheats::OnConVarChanged);
		installed_ = true;
		Log("watching sv_cheats");
		return true;
	}

	void Unload() override
	{
		if (installed_)
			g_pCVar->RemoveGlobalChangeCallback(&SvCheats::OnConVarChanged);
		installed_ = false;
		instance_ = nullptr;
	}

private:

	// ICvar takes one plain function for every convar; the fix is reached through instance_.
	static void OnConVarChanged(ConVarRefAbstract *convar, CSplitScreenSlot, const char *new_value, const char *, ConVarUserInfoSet_t *)
	{
		if (instance_ && convar && new_value && std::strcmp(convar->GetName(), "sv_cheats") == 0 && std::atoi(new_value) == 0)
			instance_->CheatsOff();
	}

	void CheatsOff()
	{
		int fixed = 0;
		for (int slot = 0; slot < 64; ++slot)
		{
			CEntityInstance *controller = Players::Controller(slot);
			CEntityInstance *pawn = controller && players_.Connected(controller) ? players_.Pawn(controller) : nullptr;
			if (!pawn)
				continue;
			auto &move = schema::Field<std::uint8_t>(pawn, move_type_);
			auto &actual = schema::Field<std::uint8_t>(pawn, actual_move_type_);
			if (move != MOVETYPE_NOCLIP && actual != MOVETYPE_NOCLIP)
				continue;
			move = MOVETYPE_WALK;
			actual = MOVETYPE_WALK;
			NetworkStateChangedData data(static_cast<uint32>(move_type_));
			pawn->NetworkStateChanged(data);
			++fixed;
		}
		if (fixed)
			Log("sv_cheats went off, %d pawn(s) taken out of noclip", fixed);
	}

	static inline SvCheats *instance_ = nullptr;
	Players players_;
	std::ptrdiff_t move_type_ = -1, actual_move_type_ = -1;
	bool installed_ = false;
};

class HammerId final : public Fix
{
public:
	const char *Name() const override { return "hammer_id"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		const auto jnz = context.server.FindUnique(sig::kSigHammerIdJnz, error);
		if (!jnz)
		{
			error = "SetSchemaHammerUniqueId: " + error;
			return false;
		}
		if (*reinterpret_cast<const std::uint8_t *>(jnz) != kJnz)
		{
			error = "SetSchemaHammerUniqueId: the patched byte is not a jnz";
			return false;
		}
		if (!WriteCode(jnz, &kJmp, 1))
		{
			error = "SetSchemaHammerUniqueId: the code page could not be made writable";
			return false;
		}
		patched_ = jnz;
		Log("patched SetSchemaHammerUniqueId at %p (jnz -> jmp)", reinterpret_cast<void *>(jnz));
		return true;
	}

	void Unload() override
	{
		if (patched_)
			WriteCode(patched_, &kJnz, 1);
		patched_ = 0;
	}

private:
	static constexpr std::uint8_t kJnz = 0x75;
	static constexpr std::uint8_t kJmp = 0xEB;
	std::uintptr_t patched_ = 0;
};

} // namespace

std::unique_ptr<Fix> MakeTeamLimit()
{
	return std::make_unique<TeamLimit>();
}

std::unique_ptr<Fix> MakeSvCheats()
{
	return std::make_unique<SvCheats>();
}

std::unique_ptr<Fix> MakeHammerId()
{
	return std::make_unique<HammerId>();
}

} // namespace sf

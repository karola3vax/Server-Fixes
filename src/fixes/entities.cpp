// abstract_entity: the entity system allocates any registered class by classname but only constructs one its
// schema allows (CONSTRUCT_ALLOWED, not abstract); for any other it calls into the zeroed object and crashes on
// a null vtable. Old map ports still carry such classnames (weapon_cs_base in game_player_equip, above all).
// Creating one from keyvalues (map spawn, game_player_equip, CreateEntityByName) is refused the way an unknown
// classname is: no entity. (After GameFixes_mm.)
//
// water_jump: touching the bottom of a body of water, the CategorizePosition after WaterMove sees ground and
// treats the player as standing on it, so a jump out of the water no longer takes. That one CategorizePosition
// is skipped. (After GameFixes_mm.)

#include "all.h"
#include "players.h"
#include "schema.h"
#include "signatures.h"

#include <entity2/entityclass.h>
#include <entity2/entityinstance.h>
#include <entity2/entitykeyvalues.h>
#include <entity2/entitysystem.h>
#include <gameentitysystem.h>
#include <schemalib/schemaclassinfo_data.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_set>

namespace sf
{

namespace
{

class AbstractEntity final : public Fix
{
public:
	const char *Name() const override { return "abstract_entity"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		const auto create = context.server.FindFunction(sig::kFnCreateEntityFromKeyValues, error);
		if (!create)
		{
			error = "CEntitySystem::CreateEntityFromKeyValues: " + error;
			return false;
		}
		hook_ = std::make_unique<Hook>(this, &AbstractEntity::CreatePre, nullptr);
		hook_->Configure(reinterpret_cast<void *>(create));
		Log("hooked CEntitySystem::CreateEntityFromKeyValues (%p)", reinterpret_cast<void *>(create));
		return true;
	}

	void Unload() override
	{
		hook_.reset();
		logged_.clear();
	}

private:
	using Hook = KHook::Member<CEntitySystem, CEntityInstance *, CEntityKeyValues *, uint32>;

	// What the entity system checks before it runs a class's constructor; the primary binding has the flags.
	static bool Constructible(const CSchemaClassInfo *info)
	{
		if (info && info->m_pPrimaryBinding)
			info = info->m_pPrimaryBinding;
		if (!info)
			return true; // nothing to go by: left to the game
		constexpr uint32 kMask = SCHEMA_CF1_IS_ABSTRACT | SCHEMA_CF1_CONSTRUCT_ALLOWED | SCHEMA_CF1_CONSTRUCT_DISALLOWED;
		return (info->m_nClassFlags1 & kMask) == SCHEMA_CF1_CONSTRUCT_ALLOWED;
	}

	KHook::Return<CEntityInstance *> CreatePre(CEntitySystem *system, CEntityKeyValues *keys, uint32)
	{
		const char *classname = system && keys ? keys->GetString("classname", nullptr) : nullptr;
		if (!classname || !*classname)
			return {KHook::Action::Ignore, nullptr};
		CEntityClass *entity_class = system->FindClassByDesignName(classname);
		if (!entity_class || !entity_class->m_pClassInfo || Constructible(entity_class->m_pClassInfo->m_pSchemaBinding))
			return {KHook::Action::Ignore, nullptr};
		if (logged_.insert(classname).second)
		{
			const char *cpp = entity_class->m_pClassInfo->m_pszCPPClassname;
			Log("refused to create %s (%s cannot be constructed; the game would crash on it)", classname, cpp ? cpp : "?");
		}
		return {KHook::Action::Supersede, nullptr};
	}

	std::unique_ptr<Hook> hook_;
	std::unordered_set<std::string> logged_;
};

class WaterJump final : public Fix
{
public:
	const char *Name() const override { return "water_jump"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		if (!Offset(context.schema, "CPlayerPawnComponent", "__m_pChainEntity", pawn_, error)
			|| !Offset(context.schema, "CBasePlayerPawn", "m_hController", controller_, error))
			return false;
		const auto water_move = context.server.FindFunction(sig::kFnWaterMove, error);
		const auto categorize = water_move ? context.server.FindFunction(sig::kFnCategorizePosition, error) : 0;
		if (!water_move || !categorize)
		{
			error = (water_move ? "CategorizePosition: " : "WaterMove: ") + error;
			return false;
		}
		water_move_ = std::make_unique<WaterMoveHook>(this, &WaterJump::WaterMovePre, nullptr);
		categorize_ = std::make_unique<CategorizeHook>(this, &WaterJump::CategorizePre, nullptr);
		water_move_->Configure(reinterpret_cast<void *>(water_move));
		categorize_->Configure(reinterpret_cast<void *>(categorize));
		Log("hooked WaterMove (%p) and CategorizePosition (%p)", reinterpret_cast<void *>(water_move), reinterpret_cast<void *>(categorize));
		return true;
	}

	void Unload() override
	{
		water_move_.reset();
		categorize_.reset();
	}

	void OnStartupServer(GameSessionConfiguration_t &, const char *) override { skip_.fill(false); }

private:
	// The movement services are not entities; void is enough for the hook's class.
	struct MovementServices;
	using WaterMoveHook = KHook::Member<MovementServices, void, void *>;
	using CategorizeHook = KHook::Member<MovementServices, void, void *, bool>;

	// The player slot a movement service belongs to, or -1.
	int Slot(MovementServices *services) const
	{
		void *pawn = services ? schema::Field<void *>(services, pawn_) : nullptr;
		CGameEntitySystem *entities = GameEntitySystem();
		CEntityInstance *controller = pawn && entities ? entities->GetEntityInstance(schema::Field<CEntityHandle>(pawn, controller_)) : nullptr;
		if (!controller)
			return -1;
		const int slot = controller->GetEntityIndex().Get() - 1; // controllers sit at entity index 1..64
		return slot >= 0 && slot < static_cast<int>(skip_.size()) ? slot : -1;
	}

	KHook::Return<void> WaterMovePre(MovementServices *services, void *)
	{
		if (const int slot = Slot(services); slot >= 0)
			skip_[slot] = true;
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> CategorizePre(MovementServices *services, void *, bool)
	{
		const int slot = Slot(services);
		if (slot < 0 || !skip_[slot])
			return {KHook::Action::Ignore};
		skip_[slot] = false;
		return {KHook::Action::Supersede};
	}

	std::unique_ptr<WaterMoveHook> water_move_;
	std::unique_ptr<CategorizeHook> categorize_;
	std::array<bool, 64> skip_{};
	std::ptrdiff_t pawn_ = -1, controller_ = -1;
};

} // namespace

std::unique_ptr<Fix> MakeAbstractEntity()
{
	return std::make_unique<AbstractEntity>();
}

std::unique_ptr<Fix> MakeWaterJump()
{
	return std::make_unique<WaterJump>();
}

} // namespace sf

#include "players.h"

#include "schema.h"

#include <const.h>
#include <entity2/entityidentity.h>
#include <gameentitysystem.h>

namespace sf
{

namespace
{

constexpr std::uint32_t kPlayerConnected = 0; // PlayerConnectedState::PlayerConnected

} // namespace

bool Players::Load(CSchemaSystemTypeScope *schema, std::string &error)
{
	return Offset(schema, "CBasePlayerController", "m_iConnected", connected_, error)
		&& Offset(schema, "CBaseEntity", "m_fFlags", flags_, error)
		&& Offset(schema, "CBasePlayerController", "m_bIsHLTV", tv_, error)
		&& Offset(schema, "CBasePlayerController", "m_hPawn", pawn_, error)
		&& Offset(schema, "CBasePlayerController", "m_iszPlayerName", name_, error);
}

bool Players::EntitySystemReady()
{
	return GameEntitySystem() != nullptr;
}

CEntityInstance *Players::Controller(int slot)
{
	CGameEntitySystem *entities = GameEntitySystem();
	return entities ? entities->GetPlayerController(CPlayerSlot(slot)) : nullptr;
}

bool Players::Connected(CEntityInstance *controller) const
{
	return schema::Field<std::uint32_t>(controller, connected_) == kPlayerConnected;
}

bool Players::Bot(CEntityInstance *controller) const
{
	return (schema::Field<std::uint32_t>(controller, flags_) & FL_FAKECLIENT) != 0;
}

bool Players::Tv(CEntityInstance *controller) const
{
	return schema::Field<bool>(controller, tv_);
}

CEntityInstance *Players::Pawn(CEntityInstance *controller) const
{
	CGameEntitySystem *entities = GameEntitySystem();
	return entities ? entities->GetEntityInstance(schema::Field<CEntityHandle>(controller, pawn_)) : nullptr;
}

const char *Players::Name(CEntityInstance *controller) const
{
	return &schema::Field<char>(controller, name_);
}

} // namespace sf

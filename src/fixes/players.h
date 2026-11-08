#pragma once

// Player controllers for every slot, read through schema offsets. Shared by the fixes that walk players.

#include "fix.h"

#include <entity2/entityinstance.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace sf
{

class Players
{
public:
	bool Load(CSchemaSystemTypeScope *schema, std::string &error);

	// The controller of a slot (0..63), or nullptr; null too while the entity system is not up.
	static CEntityInstance *Controller(int slot);
	static bool EntitySystemReady();

	bool Connected(CEntityInstance *controller) const;   // m_iConnected == PlayerConnected
	bool Bot(CEntityInstance *controller) const;         // FL_FAKECLIENT in m_fFlags
	bool Tv(CEntityInstance *controller) const;          // m_bIsHLTV
	CEntityInstance *Pawn(CEntityInstance *controller) const; // m_hPawn
	const char *Name(CEntityInstance *controller) const; // m_iszPlayerName

	std::ptrdiff_t connected_ = -1, flags_ = -1, tv_ = -1, pawn_ = -1, name_ = -1;
};

} // namespace sf

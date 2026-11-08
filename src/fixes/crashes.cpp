// Two ways a map can crash the server.
//
// beam_crash: CBeam::SetBeamOrigin and SetBeamEndPos loop forever on a beam that has no parent. Pre skips
// the game code for those, Post does the one assignment it would have made; parented beams are untouched.
// (CS2Fixes 0686a80, as carried by GameFixes_mm.)
//
// input_activator_crash: a TestActivator input sent to a filter_* entity with no activator (a map output
// wired straight to a filter) is dereferenced without a check. Those calls are dropped.

#include "all.h"
#include "schema.h"
#include "signatures.h"

#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <mathlib/vector.h>
#include <tier0/utlsymbollarge.h>

#include <memory>
#include <strings.h>

namespace sf
{

namespace
{

class BeamCrash final : public Fix
{
public:
	const char *Name() const override { return "beam_crash"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		if (!Offset(context.schema, "CBaseEntity", "m_CBodyComponent", body_, error)
			|| !Offset(context.schema, "CBodyComponent", "m_pSceneNode", scene_node_, error)
			|| !Offset(context.schema, "CGameSceneNode", "m_pParent", parent_, error)
			|| !Offset(context.schema, "CGameSceneNode", "m_vecAbsOrigin", abs_origin_, error)
			|| !Offset(context.schema, "CBeam", "m_vecEndPos", end_pos_, error))
			return false;
		const auto origin = context.server.FindUnique(sig::kSigBeamSetOrigin, error);
		const auto end = origin ? context.server.FindUnique(sig::kSigBeamSetEndPos, error) : 0;
		if (!origin || !end)
		{
			error = "CBeam::SetBeamOrigin / SetBeamEndPos: " + error;
			return false;
		}
		set_origin_ = std::make_unique<Hook>(this, &BeamCrash::OriginPre, &BeamCrash::OriginPost);
		set_end_ = std::make_unique<Hook>(this, &BeamCrash::EndPre, &BeamCrash::EndPost);
		set_origin_->Configure(reinterpret_cast<void *>(origin));
		set_end_->Configure(reinterpret_cast<void *>(end));
		Log("hooked CBeam::SetBeamOrigin (%p) and SetBeamEndPos (%p)", reinterpret_cast<void *>(origin), reinterpret_cast<void *>(end));
		return true;
	}

	void Unload() override
	{
		set_origin_.reset();
		set_end_.reset();
	}

private:
	using Hook = KHook::Member<CEntityInstance, void, const Vector *>;

	void *SceneNode(CEntityInstance *beam) const
	{
		void *body = schema::Field<void *>(beam, body_);
		return body ? schema::Field<void *>(body, scene_node_) : nullptr;
	}

	// Game code only for a parented beam; an unparented one would loop forever, so Post finishes it.
	bool Parented(CEntityInstance *beam) const
	{
		void *node = SceneNode(beam);
		return node && schema::Field<void *>(node, parent_);
	}

	KHook::Return<void> OriginPre(CEntityInstance *beam, const Vector *)
	{
		return {Parented(beam) ? KHook::Action::Ignore : KHook::Action::Supersede};
	}

	KHook::Return<void> OriginPost(CEntityInstance *beam, const Vector *position)
	{
		void *node = SceneNode(beam);
		if (node && position && !schema::Field<void *>(node, parent_))
			schema::Field<Vector>(node, abs_origin_) = *position;
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> EndPre(CEntityInstance *beam, const Vector *)
	{
		return {Parented(beam) ? KHook::Action::Ignore : KHook::Action::Supersede};
	}

	KHook::Return<void> EndPost(CEntityInstance *beam, const Vector *position)
	{
		if (position && !Parented(beam))
			schema::Field<Vector>(beam, end_pos_) = *position;
		return {KHook::Action::Ignore};
	}

	std::unique_ptr<Hook> set_origin_, set_end_;
	std::ptrdiff_t body_ = -1, scene_node_ = -1, parent_ = -1, abs_origin_ = -1, end_pos_ = -1;
};

class InputActivatorCrash final : public Fix
{
public:
	const char *Name() const override { return "input_activator_crash"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		const auto accept = context.server.FindUnique(sig::kSigAcceptInput, error);
		if (!accept)
		{
			error = "CEntityIdentity::AcceptInput: " + error;
			return false;
		}
		accept_input_ = std::make_unique<Hook>(this, &InputActivatorCrash::AcceptInputPre, nullptr);
		accept_input_->Configure(reinterpret_cast<void *>(accept));
		Log("hooked CEntityIdentity::AcceptInput (%p)", reinterpret_cast<void *>(accept));
		return true;
	}

	void Unload() override { accept_input_.reset(); }

private:
	using Hook = KHook::Member<CEntityIdentity, bool, CUtlSymbolLarge *, CEntityInstance *, CEntityInstance *, void *, void *, void *>;

	KHook::Return<bool> AcceptInputPre(CEntityIdentity *identity, CUtlSymbolLarge *input, CEntityInstance *activator,
		CEntityInstance *, void *, void *, void *)
	{
		// A player who left took their pawn along; TestActivator on a filter would read the missing activator.
		if (!activator && identity && input && strncasecmp(identity->GetClassname(), "filter_", 7) == 0
			&& strcasecmp(input->String(), "TestActivator") == 0)
			return {KHook::Action::Supersede, true};
		return {KHook::Action::Ignore, false};
	}

	std::unique_ptr<Hook> accept_input_;
};

} // namespace

std::unique_ptr<Fix> MakeBeamCrash()
{
	return std::make_unique<BeamCrash>();
}

std::unique_ptr<Fix> MakeInputActivatorCrash()
{
	return std::make_unique<InputActivatorCrash>();
}

} // namespace sf

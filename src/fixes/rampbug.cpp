// rampbug: surf ramps. A movement trace that stops a hair short of a ramp, or lands on its edge, hands
// TryPlayerMove a plane the player never really hit, and the player is flung off. Every trace of the pass is
// redone from a small offset along the last plane the player was known to be on; CategorizePosition gets the
// same treatment on the way down a ramp, and landing on a slope keeps its speed. It lessens rampbugs, it does
// not end them. CS2KZ's fix, as carried by CS2Fixes-RampbugFix and then GameFixes_mm (GPL-3.0), whose logic
// this follows line for line.
//
// The game's own TryPlayerMove now pushes the end point off a single hit plane (Valve's own mitigation), so
// putting our walk back over the game's ("tryplayermove_override") is off by default; the walk still runs,
// for the last valid plane CategorizePosition works with. What a player was on is forgotten when the game
// moves them outside the pass (a teleport).
//
// Only useful on surf / kz maps, and it adds traces to every player's movement; server_fixes.cfg ships it off.

// The SDK's CGameTrace declares a destructor, so copying one is "deprecated"; the fix copies traces just as the game does.
#pragma GCC diagnostic ignored "-Wdeprecated-copy"

#include "all.h"
#include "movedata.h"
#include "schema.h"
#include "signatures.h"

#include <const.h>
#include <engine2/ivengineserver2.h>
#include <entity2/entityinstance.h>
#include <gameentitysystem.h>
#include <gametrace.h>
#include <mathlib/mathlib.h>
#include <mathlib/ray.h>

#include <array>
#include <cfloat>
#include <cmath>
#include <memory>

namespace sf
{

namespace
{

// The server's CNavPhysicsInterface, reached through its vtable by RTTI name. Same declaration order as
// cs2kz-metamod and GameFixes_mm; nothing is instantiated, the vtable pointer is the whole "object".
class INavPhysicsInterface
{
public:
	virtual ~INavPhysicsInterface() = 0;
	virtual void Nav_TraceLine(const Vector &start, const Vector &end, CEntityInstance *ignore, uint64 interacts_with, uint8 collision_group, uint8 object_set_mask, CGameTrace *trace) = 0;
	virtual void Nav_TraceLine(const Vector &start, const Vector &end, CTraceFilter *filter, CGameTrace *trace) = 0;
	virtual void Nav_TraceShape(const Ray_t &ray, const Vector &start, const Vector &end, CEntityInstance *ignore, uint64 interacts_with, uint8 collision_group, uint8 object_set_mask, CGameTrace *trace) = 0;
	virtual void Nav_TraceShape(const Ray_t &ray, const Vector &start, const Vector &end, CTraceFilter *filter, CGameTrace *trace) = 0;
};

constexpr int kMaxBumps = 4;
constexpr float kRampPierceDistance = 0.0625f;
constexpr float kRampBugThreshold = 0.98f;
constexpr float kRampBugVelocityThreshold = 0.95f;
constexpr float kNewRampThreshold = 0.95f;

struct Bounds
{
	Vector mins;
	Vector maxs;
};

void ClipVelocity(const Vector &in, const Vector &normal, Vector &out)
{
	float backoff = -((in.x * normal.x) + ((normal.z * in.z) + (in.y * normal.y)));
	backoff = fmaxf(backoff, 0.0f) + 0.03125f;
	out = normal * backoff + in;
}

class Rampbug final : public Fix
{
public:
	const char *Name() const override { return "rampbug"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		auto *s = context.schema;
		if (!Offset(s, "CPlayerPawnComponent", "__m_pChainEntity", f_.pawn, error)
			|| !Offset(s, "CCSPlayer_MovementServices", "m_bDucked", f_.ducked, error)
			|| !Offset(s, "CCSPlayer_MovementServices", "m_nGameCodeHasMovedPlayerAfterCommand", f_.game_moves, error)
			|| !Offset(s, "CBasePlayerPawn", "m_hController", f_.controller, error)
			|| !Offset(s, "CBasePlayerPawn", "m_pMovementServices", f_.movement, error)
			|| !Offset(s, "CBaseEntity", "m_fFlags", f_.flags, error)
			|| !Offset(s, "CBaseEntity", "m_MoveType", f_.move_type, error)
			|| !Offset(s, "CBaseEntity", "m_vecAbsVelocity", f_.abs_velocity, error)
			|| !Offset(s, "CBaseEntity", "m_pCollision", f_.collision, error)
			|| !Offset(s, "CBaseEntity", "m_hOwnerEntity", f_.owner, error)
			|| !Offset(s, "CBaseEntity", "m_hGroundEntity", f_.ground, error)
			|| !Offset(s, "CBaseEntity", "m_CBodyComponent", f_.body, error)
			|| !Offset(s, "CBodyComponent", "m_pSceneNode", f_.scene_node, error)
			|| !Offset(s, "CGameSceneNode", "m_vecAbsOrigin", f_.abs_origin, error)
			|| !Offset(s, "CCollisionProperty", "m_collisionAttribute", f_.attribute, error)
			|| !Offset(s, "VPhysicsCollisionAttribute_t", "m_nHierarchyId", f_.hierarchy, error)
			|| !Offset(s, "VPhysicsCollisionAttribute_t", "m_nInteractsWith", f_.interacts_with, error))
			return false;
		override_ = GetBool(context.options, "tryplayermove_override", false);
		nav_vtable_ = reinterpret_cast<void *>(context.server.FindVTable("CNavPhysicsInterface", error));
		if (!nav_vtable_)
			return false;
		const auto process = context.server.FindFunction(sig::kFnProcessMovement, error);
		const auto try_move = process ? context.server.FindFunction(sig::kFnTryPlayerMove, error) : 0;
		const auto categorize = try_move ? context.server.FindFunction(sig::kFnCategorizePosition, error) : 0;
		if (!process || !try_move || !categorize)
		{
			error = std::string(!process ? "ProcessMovement: " : !try_move ? "TryPlayerMove: " : "CategorizePosition: ") + error;
			return false;
		}
		for (State &state : states_)
			state.Reset();
		process_ = std::make_unique<ProcessHook>(this, &Rampbug::ProcessPre, &Rampbug::ProcessPost);
		try_move_ = std::make_unique<TryMoveHook>(this, &Rampbug::TryMovePre, &Rampbug::TryMovePost);
		categorize_ = std::make_unique<CategorizeHook>(this, &Rampbug::CategorizePre, nullptr);
		process_->Configure(reinterpret_cast<void *>(process));
		try_move_->Configure(reinterpret_cast<void *>(try_move));
		categorize_->Configure(reinterpret_cast<void *>(categorize));
		Log("hooked ProcessMovement (%p), TryPlayerMove (%p) and CategorizePosition (%p); CNavPhysicsInterface vtable at %p; TryPlayerMove override %s",
			reinterpret_cast<void *>(process), reinterpret_cast<void *>(try_move), reinterpret_cast<void *>(categorize), nav_vtable_, override_ ? "on" : "off");
		return true;
	}

	void Unload() override
	{
		process_.reset();
		try_move_.reset();
		categorize_.reset();
		nav_vtable_ = nullptr;
	}

	void OnStartupServer(GameSessionConfiguration_t &, const char *) override
	{
		for (State &state : states_)
			state.Reset();
	}

private:
	struct MovementServices;
	using ProcessHook = KHook::Member<MovementServices, void, void *>;
	using TryMoveHook = KHook::Member<MovementServices, void, CMoveData *, Vector *, trace_t *, bool *>;
	using CategorizeHook = KHook::Member<MovementServices, void, CMoveData *, bool>;

	// What one player's movement pass left behind, from one call to the next.
	struct State
	{
		CMoveData *move = nullptr; // the pass in flight, null outside ProcessMovement
		bool processing = false;
		bool previous_on_ground = false;
		int game_moves = 0;        // m_nGameCodeHasMovedPlayerAfterCommand last seen
		bool did_tpm = false;      // TryPlayerMove ran during this pass
		bool override_tpm = false; // our own trace result should replace the game's
		Vector tpm_velocity;
		Vector tpm_origin;
		Vector last_valid_plane;   // vec3_origin when there is none
		Vector takeoff_velocity;
		Vector landing_origin;
		Vector landing_velocity;

		void Reset()
		{
			move = nullptr;
			processing = false;
			previous_on_ground = false;
			game_moves = 0;
			ForgetSurface();
		}

		// Forgets what the player was on, keeping the pass in flight.
		void ForgetSurface()
		{
			did_tpm = false;
			override_tpm = false;
			tpm_velocity = vec3_invalid;
			tpm_origin = vec3_invalid;
			last_valid_plane = vec3_origin;
			takeoff_velocity = vec3_origin;
			landing_origin = vec3_origin;
			landing_velocity = vec3_origin;
		}
	};

	struct Fields
	{
		std::ptrdiff_t pawn = -1, ducked = -1, game_moves = -1, controller = -1, movement = -1, flags = -1, move_type = -1,
			abs_velocity = -1, collision = -1, owner = -1, ground = -1, body = -1, scene_node = -1, abs_origin = -1,
			attribute = -1, hierarchy = -1, interacts_with = -1;
	};

	template <typename T>
	T &At(void *object, std::ptrdiff_t offset) const { return schema::Field<T>(object, offset); }

	bool OnGround(CEntityInstance *pawn) const { return (At<std::uint32_t>(pawn, f_.flags) & FL_ONGROUND) != 0; }

	Vector &SceneOrigin(CEntityInstance *pawn) const
	{
		return At<Vector>(At<void *>(At<void *>(pawn, f_.body), f_.scene_node), f_.abs_origin);
	}

	// The movement pass's own filter: the player and what it owns pass through, everything the player collides with counts.
	CTraceFilter MovementFilter(CEntityInstance *pawn) const
	{
		void *collision = At<void *>(pawn, f_.collision);
		auto *attribute = &At<std::uint8_t>(collision, f_.attribute);
		CGameEntitySystem *entities = GameEntitySystem();
		CTraceFilter filter(pawn, entities ? entities->GetEntityInstance(At<CEntityHandle>(pawn, f_.owner)) : nullptr,
			At<std::uint16_t>(attribute, f_.hierarchy), At<std::uint64_t>(attribute, f_.interacts_with), COLLISION_GROUP_PLAYER_MOVEMENT, true);
		filter.EnableInteractsAsLayer(LAYER_INDEX_CONTENTS_PLAYER);
		filter.m_nObjectSetMask = RNQUERY_OBJECTS_ALL;
		filter.m_bHitSolid = true;
		filter.m_bHitSolidRequiresGenerateContacts = true;
		filter.m_bHitTrigger = false;
		filter.m_bShouldIgnoreDisabledPairs = true;
		filter.m_bIgnoreIfBothInteractWithHitboxes = false;
		filter.m_bForceHitEverything = false;
		filter.m_unk101 = true;
		return filter;
	}

	// The player box, traced through CNavPhysicsInterface::Nav_TraceShape as cs2kz does; the interface has no
	// state, so a pointer to its vtable pointer stands in for the object.
	void TraceBox(const Vector &start, const Vector &end, const Bounds &bounds, CTraceFilter *filter, trace_t &trace)
	{
		auto *nav = reinterpret_cast<INavPhysicsInterface *>(&nav_vtable_);
		nav->Nav_TraceShape(Ray_t(bounds.mins, bounds.maxs), start, end, filter, &trace);
	}

	Bounds PlayerBounds(MovementServices *services) const
	{
		return {Vector(-16.0f, -16.0f, 0.0f), Vector(16.0f, 16.0f, At<bool>(services, f_.ducked) ? 54.0f : 72.0f)};
	}

	// The state of the player a movement service belongs to, or null.
	State *GetState(MovementServices *services, CEntityInstance **pawn_out)
	{
		auto *pawn = services ? At<CEntityInstance *>(services, f_.pawn) : nullptr;
		CGameEntitySystem *entities = GameEntitySystem();
		CEntityInstance *controller = pawn && entities ? entities->GetEntityInstance(At<CEntityHandle>(pawn, f_.controller)) : nullptr;
		if (!controller)
			return nullptr;
		const int slot = controller->GetEntityIndex().Get() - 1; // controllers sit at entity index 1..64
		if (slot < 0 || slot >= static_cast<int>(states_.size()))
			return nullptr;
		*pawn_out = pawn;
		return &states_[slot];
	}

	// Inside a pass the move data is where the player is: the game writes the scene node's origin only after each
	// subtick step, so reading that one would trace from where the player was before this step's move.
	Vector Origin(const State &state, CEntityInstance *pawn) const
	{
		return state.processing && state.move ? state.move->m_vecAbsOrigin : SceneOrigin(pawn);
	}

	Vector Velocity(const State &state, CEntityInstance *pawn) const
	{
		return state.processing && state.move ? state.move->m_vecVelocity : At<Vector>(pawn, f_.abs_velocity);
	}

	void SetOrigin(State &state, CEntityInstance *pawn, const Vector &origin) const
	{
		if (state.processing && state.move)
			state.move->m_vecAbsOrigin = origin;
		else
			SceneOrigin(pawn) = origin;
	}

	void SetVelocity(State &state, CEntityInstance *pawn, const Vector &velocity) const
	{
		if (state.processing && state.move)
			state.move->m_vecVelocity = velocity;
		else
			At<Vector>(pawn, f_.abs_velocity) = velocity;
	}

	// Landing on a slope: the game clips the landing velocity against the slope and loses speed the player should keep.
	void ApplySlopeFix(State &state, CEntityInstance *pawn)
	{
		if (!state.processing || !state.move)
			return;
		const Bounds bounds = PlayerBounds(At<MovementServices *>(pawn, f_.movement));
		CTraceFilter filter = MovementFilter(pawn);
		Vector ground = state.move->m_vecAbsOrigin;
		ground.z -= 2.0f;
		trace_t trace;
		TraceBox(state.move->m_vecAbsOrigin, ground, bounds, &filter, trace);
		if (trace.m_bStartInSolid || trace.m_flFraction == 1.0f || trace.m_vHitNormal.z < 0.7f || trace.m_vHitNormal.z >= 1.0f)
			return;
		Vector velocity;
		const float backoff = DotProduct(state.landing_velocity, trace.m_vHitNormal);
		for (int i = 0; i < 3; i++)
			velocity[i] = state.landing_velocity[i] - trace.m_vHitNormal[i] * backoff;
		const float adjust = DotProduct(velocity, trace.m_vHitNormal);
		if (adjust < 0.0f)
			velocity -= trace.m_vHitNormal * adjust;
		if (velocity.Length2D() >= state.landing_velocity.Length2D())
		{
			state.move->m_vecVelocity.x = velocity.x;
			state.move->m_vecVelocity.y = velocity.y;
			state.landing_velocity.x = velocity.x;
			state.landing_velocity.y = velocity.y;
		}
	}

	bool ValidTrace(trace_t &trace, const Bounds &bounds, CTraceFilter *filter)
	{
		if (trace.m_bStartInSolid)
			return false;
		// Hit something, but no plane to show for it.
		if (trace.m_flFraction < 1.0f && fabsf(trace.m_vHitNormal.x) < FLT_EPSILON && fabsf(trace.m_vHitNormal.y) < FLT_EPSILON
			&& fabsf(trace.m_vHitNormal.z) < FLT_EPSILON)
			return false;
		// A deformed plane.
		if (fabsf(trace.m_vHitNormal.x) > 1.0f || fabsf(trace.m_vHitNormal.y) > 1.0f || fabsf(trace.m_vHitNormal.z) > 1.0f)
			return false;
		// An unswept trace and a backward one, to be sure.
		trace_t stuck;
		TraceBox(trace.m_vEndPos, trace.m_vEndPos, bounds, filter, stuck);
		if (stuck.m_bStartInSolid || stuck.m_flFraction < 1.0f - FLT_EPSILON)
			return false;
		// Since the Call to Arms update a trace can hit in one direction only, so the backward fraction is not checked.
		TraceBox(trace.m_vEndPos, trace.m_vStartPos, bounds, filter, stuck);
		return !stuck.m_bStartInSolid;
	}

	KHook::Return<void> ProcessPre(MovementServices *services, void *move)
	{
		CEntityInstance *pawn = nullptr;
		State *state = GetState(services, &pawn);
		if (!state)
			return {KHook::Action::Ignore};
		// Moved by game code since the last pass (a teleport): the surface it was on is not where it is now.
		const int game_moves = At<std::int32_t>(services, f_.game_moves);
		if (game_moves != state->game_moves)
		{
			state->game_moves = game_moves;
			state->ForgetSurface();
			state->previous_on_ground = OnGround(pawn);
		}
		state->move = static_cast<CMoveData *>(move);
		state->did_tpm = false;
		state->processing = true;
		const bool on_ground = OnGround(pawn);
		if (!state->previous_on_ground && on_ground)
		{
			state->landing_origin = Origin(*state, pawn);
			state->landing_velocity = Velocity(*state, pawn);
			ApplySlopeFix(*state, pawn);
		}
		else if (state->previous_on_ground && !on_ground)
		{
			state->takeoff_velocity = Velocity(*state, pawn);
		}
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> ProcessPost(MovementServices *services, void *)
	{
		CEntityInstance *pawn = nullptr;
		State *state = GetState(services, &pawn);
		if (!state)
			return {KHook::Action::Ignore};
		if (!state->did_tpm)
			state->last_valid_plane = vec3_origin;
		state->processing = false;
		state->move = nullptr;
		state->previous_on_ground = OnGround(pawn);
		return {KHook::Action::Ignore};
	}

	// The pass's own walk along the ramp, redone with every trace nudged off the last valid plane, so a trace that
	// stops a hair short of the ramp (or on its edge) cannot hand the game a plane the player never hit.
	KHook::Return<void> TryMovePre(MovementServices *services, CMoveData *, Vector *first_dest, trace_t *first_trace, bool *)
	{
		CEntityInstance *pawn = nullptr;
		State *found = GetState(services, &pawn);
		if (!found)
			return {KHook::Action::Ignore};
		State &state = *found;
		state.override_tpm = false;
		state.did_tpm = true;
		const CGlobalVars *globals = g_pEngineServer->GetServerGlobals();
		if (!globals)
			return {KHook::Action::Ignore};
		float time_left = globals->frametime;
		Vector start = Origin(state, pawn), velocity = Velocity(state, pawn), end;
		if (velocity.Length() == 0.0f)
			return {KHook::Action::Ignore};
		const Vector primal_velocity = velocity;
		bool valid_plane = false;
		float all_fraction = 0.0f;
		trace_t pm;
		Vector planes[5];
		unsigned num_planes = 0;
		trace_t pierce;
		const Bounds bounds = PlayerBounds(services);
		CTraceFilter filter = MovementFilter(pawn);
		bool potentially_stuck = false;
		for (int bump = 0; bump < kMaxBumps; bump++)
		{
			// Assume the whole way from the origin to the end point is free.
			VectorMA(start, time_left, velocity, end);
			if (first_dest && end == *first_dest)
			{
				pm = *first_trace;
			}
			else
			{
				TraceBox(start, end, bounds, &filter, pm);
				if (end == start)
					continue;
				if (ValidTrace(pm, bounds, &filter) && pm.m_flFraction == 1.0f)
					break; // nothing in the way
				if (state.last_valid_plane.Length() > FLT_EPSILON
					&& (!ValidTrace(pm, bounds, &filter) || pm.m_vHitNormal.Dot(state.last_valid_plane) < kRampBugThreshold
						|| (potentially_stuck && pm.m_flFraction == 0.0f)))
				{
					// A plane that would change the velocity a lot: make sure it is a real one.
					Vector offset_direction;
					const float offsets[] = {0.0f, -1.0f, 1.0f};
					bool success = false;
					for (unsigned i = 0; i < 3 && !success; i++)
					{
						for (unsigned j = 0; j < 3 && !success; j++)
						{
							for (unsigned k = 0; k < 3 && !success; k++)
							{
								if (i == 0 && j == 0 && k == 0)
								{
									offset_direction = state.last_valid_plane;
								}
								else
								{
									offset_direction = Vector(offsets[i], offsets[j], offsets[k]);
									if (state.last_valid_plane.Dot(offset_direction) <= 0.0f)
										continue;
									trace_t test;
									TraceBox(start + offset_direction * kRampPierceDistance, start, bounds, &filter, test);
									if (!ValidTrace(test, bounds, &filter))
										continue;
								}
								bool good_trace = false;
								bool hit_new_plane = false;
								for (float ratio = 0.25f; ratio <= 1.0f; ratio += 0.25f)
								{
									TraceBox(start + offset_direction * kRampPierceDistance * ratio, end + offset_direction * kRampPierceDistance * ratio,
										bounds, &filter, pierce);
									if (!ValidTrace(pierce, bounds, &filter))
										continue;
									// Until a similar plane is hit.
									valid_plane = pierce.m_flFraction < 1.0f && pierce.m_flFraction > 0.1f
										&& pierce.m_vHitNormal.Dot(state.last_valid_plane) >= kRampBugThreshold;
									hit_new_plane = pm.m_vHitNormal.Dot(pierce.m_vHitNormal) < kNewRampThreshold
										&& state.last_valid_plane.Dot(pierce.m_vHitNormal) > kNewRampThreshold;
									good_trace = CloseEnough(pierce.m_flFraction, 1.0f, FLT_EPSILON) || valid_plane;
									if (good_trace)
										break;
								}
								if (good_trace || hit_new_plane)
								{
									// Back to the original end point, for its normal.
									trace_t test;
									TraceBox(pierce.m_vEndPos, end, bounds, &filter, test);
									pm = pierce;
									pm.m_vStartPos = start;
									pm.m_flFraction = clamp((pierce.m_vEndPos - pierce.m_vStartPos).Length() / (end - start).Length(), 0.0f, 1.0f);
									pm.m_vEndPos = test.m_vEndPos;
									if (pierce.m_vHitNormal.Length() > 0.0f)
									{
										pm.m_vHitNormal = pierce.m_vHitNormal;
										state.last_valid_plane = pierce.m_vHitNormal;
									}
									else
									{
										pm.m_vHitNormal = test.m_vHitNormal;
										state.last_valid_plane = test.m_vHitNormal;
									}
									success = true;
									state.override_tpm = true;
								}
							}
						}
					}
				}
				if (pm.m_vHitNormal.Length() > 0.99f)
					state.last_valid_plane = pm.m_vHitNormal;
				potentially_stuck = pm.m_flFraction == 0.0f;
			}
			if (pm.m_flFraction * velocity.Length() > 0.03125f || pm.m_flFraction > 0.03125f)
			{
				all_fraction += pm.m_flFraction;
				start = pm.m_vEndPos;
				num_planes = 0;
			}
			if (all_fraction == 1.0f)
				break;
			time_left -= globals->frametime * pm.m_flFraction;
			if (num_planes >= 5 || (pm.m_vHitNormal.z >= 0.7f && velocity.Length2D() < 1.0f))
			{
				velocity = vec3_origin;
				break;
			}
			planes[num_planes] = pm.m_vHitNormal;
			num_planes++;
			CGameEntitySystem *entities = GameEntitySystem();
			if (num_planes == 1 && At<std::uint8_t>(pawn, f_.move_type) == MOVETYPE_WALK && entities
				&& entities->GetEntityInstance(At<CEntityHandle>(pawn, f_.ground)) == nullptr)
			{
				ClipVelocity(velocity, planes[0], velocity);
			}
			else
			{
				unsigned i, j;
				for (i = 0; i < num_planes; i++)
				{
					ClipVelocity(velocity, planes[i], velocity);
					for (j = 0; j < num_planes; j++)
						if (j != i && velocity.Dot(planes[j]) < 0)
							break; // moving against this plane now
					if (j == num_planes)
						break; // no clip needed
				}
				if (i == num_planes)
				{
					// Along the crease.
					if (num_planes != 2)
					{
						velocity = vec3_origin;
						break;
					}
					Vector dir;
					CrossProduct(planes[0], planes[1], dir);
					dir.NormalizeInPlace();
					const float d = dir.Dot(velocity);
					VectorScale(dir, d, velocity);
					if (velocity.Dot(primal_velocity) <= 0)
					{
						velocity = vec3_origin;
						break;
					}
				}
			}
		}
		state.tpm_origin = pm.m_vEndPos;
		state.tpm_velocity = velocity;
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> TryMovePost(MovementServices *services, CMoveData *, Vector *, trace_t *, bool *)
	{
		CEntityInstance *pawn = nullptr;
		State *state = GetState(services, &pawn);
		// Our walk ending in a standstill is no result to put back.
		if (!state || !override_ || !state->override_tpm || state->tpm_velocity == vec3_invalid || state->tpm_velocity.LengthSqr() < 1.0f)
			return {KHook::Action::Ignore};
		const Vector velocity = Velocity(*state, pawn);
		// The game took the player somewhere our own walk did not: put ours back.
		const bool heavily_modified = state->tpm_velocity.Normalized().Dot(velocity.Normalized()) < kRampBugThreshold
			|| (state->tpm_velocity.Length() > 50.0f && velocity.Length() / state->tpm_velocity.Length() < kRampBugVelocityThreshold);
		if (heavily_modified && state->tpm_origin != vec3_invalid && state->tpm_velocity != vec3_invalid)
		{
			SetOrigin(*state, pawn, state->tpm_origin);
			SetVelocity(*state, pawn, state->tpm_velocity);
		}
		return {KHook::Action::Ignore};
	}

	// Going down a ramp fast, the ground check can find a standable plane the player is not really on; nudge them
	// onto the last valid plane first.
	KHook::Return<void> CategorizePre(MovementServices *services, CMoveData *, bool stay_on_ground)
	{
		CEntityInstance *pawn = nullptr;
		State *found = GetState(services, &pawn);
		if (!found)
			return {KHook::Action::Ignore};
		State &state = *found;
		// Already colliding with a standable, valid plane: nothing to check.
		if (stay_on_ground || state.last_valid_plane.Length() < 0.000001f || state.last_valid_plane.z > 0.7f)
			return {KHook::Action::Ignore};
		// Only worth it going down at some speed.
		if (Velocity(state, pawn).z > -64.0f)
			return {KHook::Action::Ignore};
		const Bounds bounds = PlayerBounds(services);
		CTraceFilter filter = MovementFilter(pawn);
		trace_t trace;
		Vector origin = Origin(state, pawn), ground_origin = origin;
		ground_origin.z -= 2.0f;
		TraceBox(origin, ground_origin, bounds, &filter, trace);
		if (trace.m_flFraction == 1.0f)
			return {KHook::Action::Ignore};
		// Something the player could actually stand on?
		if (trace.m_flFraction < 0.95f && trace.m_vHitNormal.z > 0.7f && state.last_valid_plane.Dot(trace.m_vHitNormal) < kRampBugThreshold)
		{
			origin += state.last_valid_plane * 0.0625f;
			ground_origin = origin;
			ground_origin.z -= 2.0f;
			TraceBox(origin, ground_origin, bounds, &filter, trace);
			if (trace.m_bStartInSolid)
				return {KHook::Action::Ignore};
			if (trace.m_flFraction == 1.0f || state.last_valid_plane.Dot(trace.m_vHitNormal) >= kRampBugThreshold)
				SetOrigin(state, pawn, origin);
		}
		return {KHook::Action::Ignore};
	}

	Fields f_;
	bool override_ = false;
	void *nav_vtable_ = nullptr;
	std::array<State, 64> states_;
	std::unique_ptr<ProcessHook> process_;
	std::unique_ptr<TryMoveHook> try_move_;
	std::unique_ptr<CategorizeHook> categorize_;
};

} // namespace

std::unique_ptr<Fix> MakeRampbug()
{
	return std::make_unique<Rampbug>();
}

} // namespace sf

// block_nav_lookup: CNavMesh::GetNearestNavArea finds the navigation area nearest a point, for bots. On a
// server without bots its work is wasted, so it answers "none" without looking. It breaks bot navigation;
// only for servers that run no bots. (After CS2Fixes' cs2f_block_nav_lookup.)
//
// "measure_only 1" leaves the game alone and logs, once a minute, how often the lookup ran and how long it
// took, so the saving can be seen before anything is blocked.

#include "all.h"
#include "signatures.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <memory>

namespace sf
{

namespace
{

using Clock = std::chrono::steady_clock;

class BlockNavLookup final : public Fix
{
public:
	const char *Name() const override { return "block_nav_lookup"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		measure_only_ = GetBool(context.options, "measure_only", false);
		const auto lookup = context.server.FindFunction(sig::kFnGetNearestNavArea, error);
		if (!lookup)
		{
			error = "CNavMesh::GetNearestNavArea: " + error;
			return false;
		}
		hook_ = std::make_unique<Hook>(this, &BlockNavLookup::LookupPre, &BlockNavLookup::LookupPost);
		hook_->Configure(reinterpret_cast<void *>(lookup));
		window_start_ = Clock::now();
		Log("hooked CNavMesh::GetNearestNavArea (%p), %s", reinterpret_cast<void *>(lookup),
			measure_only_ ? "measuring only, nothing blocked" : "blocking every lookup");
		return true;
	}

	void Unload() override { hook_.reset(); }

	void OnGameFrame() override
	{
		const auto now = Clock::now();
		if (now - window_start_ < std::chrono::minutes(1))
			return;
		if (calls_)
			Log("last minute: %llu lookup(s)%s", static_cast<unsigned long long>(calls_), measure_only_ ? Took().c_str() : ", all blocked");
		calls_ = 0;
		spent_ = Clock::duration::zero();
		window_start_ = now;
	}

private:
	struct NavMesh;
	using Hook = KHook::Member<NavMesh, void *, float *, unsigned int *, unsigned int, std::int64_t, float, std::int64_t>;

	std::string Took() const
	{
		char text[96];
		std::snprintf(text, sizeof(text), ", %.3f ms in total", std::chrono::duration<double, std::milli>(spent_).count());
		return text;
	}

	KHook::Return<void *> LookupPre(NavMesh *, float *, unsigned int *, unsigned int, std::int64_t, float, std::int64_t)
	{
		++calls_;
		if (!measure_only_)
			return {KHook::Action::Supersede, nullptr};
		started_ = Clock::now();
		return {KHook::Action::Ignore, nullptr};
	}

	KHook::Return<void *> LookupPost(NavMesh *, float *, unsigned int *, unsigned int, std::int64_t, float, std::int64_t)
	{
		// Game thread only, and the lookup does not call itself, so one start time is enough.
		if (measure_only_)
			spent_ += Clock::now() - started_;
		return {KHook::Action::Ignore, nullptr};
	}

	std::unique_ptr<Hook> hook_;
	bool measure_only_ = false;
	std::uint64_t calls_ = 0;
	Clock::time_point started_{}, window_start_{};
	Clock::duration spent_ = Clock::duration::zero();
};

} // namespace

std::unique_ptr<Fix> MakeBlockNavLookup()
{
	return std::make_unique<BlockNavLookup>();
}

} // namespace sf

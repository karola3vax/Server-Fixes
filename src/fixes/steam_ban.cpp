// The engine's per-frame ban and competitive-cooldown kick pass (GameSystem_Think_CheckSteamBan) walks
// CCSGameRules::sm_mapGcBanInformation and kicks every connected account it finds there. Pre takes the
// whitelisted accounts out of the map, and below sv_kick_players_with_cooldown 2 the competitive cooldowns
// the pass would still act on; with clear_after_pass, Post empties the map, so a stale entry never reaches
// the next player to join. (CS2Fixes c82d21a, as carried by GameFixes_mm, plus the whitelist.)

#include "all.h"
#include "core/rules.h"
#include "signatures.h"

#include <tier0/convar.h>
#include <tier0/utlmap.h>

#include <memory>
#include <unordered_set>
#include <vector>

namespace sf
{

namespace
{

// The engine's value type in the map, by layout.
struct GcBanInformation
{
	uint32 reason;
	double unknown;
	double expiration;
	uint32 account;
};

// The third argument is uint32 on purpose, not a comparator, to match the engine's layout. Only
// structural access (FOR_EACH_MAP, Key, Element, RemoveAt, RemoveAll) is valid on it, never Find or Insert.
using BanMap = CUtlOrderedMap<uint32, GcBanInformation, uint32>;

class SteamBan final : public Fix
{
public:
	const char *Name() const override { return "steam_ban"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		std::vector<std::string_view> rejected;
		const std::string list = GetString(context.options, "whitelist", "");
		for (std::uint32_t account : ParseAccountList(list, &rejected))
			whitelist_.insert(account);
		for (std::string_view entry : rejected)
			Log("ignoring whitelist entry \"%.*s\": not a SteamID64", static_cast<int>(entry.size()), entry.data());
		clear_after_pass_ = GetBool(context.options, "clear_after_pass", true);

		const auto reference = context.server.FindUnique(sig::kSigGcBanMapRef, error);
		const auto pass = reference ? context.server.FindUnique(sig::kSigCheckSteamBan, error) : 0;
		if (!reference || !pass)
		{
			error = (reference ? "GameSystem_Think_CheckSteamBan: " : "sm_mapGcBanInformation: ") + error;
			return false;
		}
		bans_ = reinterpret_cast<BanMap *>(ResolveRelative(reference, 3, 7)); // lea rsi, [rip + map]
		hook_ = std::make_unique<Hook>(this, &SteamBan::PassPre, &SteamBan::PassPost);
		hook_->Configure(reinterpret_cast<void *>(pass));
		Log("hooked GameSystem_Think_CheckSteamBan (%p), ban map at %p, %zu whitelisted, clear after pass %s",
			reinterpret_cast<void *>(pass), static_cast<void *>(bans_), whitelist_.size(), clear_after_pass_ ? "on" : "off");
		return true;
	}

	void Unload() override
	{
		hook_.reset();
		bans_ = nullptr;
	}

private:
	using Hook = KHook::Function<void>;

	KHook::Return<void> PassPre()
	{
		if (!bans_ || bans_->Count() <= 0)
			return {KHook::Action::Ignore};
		// Below sv_kick_players_with_cooldown 2 the pass still acts on competitive cooldowns (reasons 20, 22, 23).
		ConVarRefAbstract kick_cooldowns("sv_kick_players_with_cooldown");
		const bool drop_cooldowns = kick_cooldowns.IsValidRef() && kick_cooldowns.GetInt() < 2;
		// Collected first, removed after: the tree must not change mid-walk.
		std::vector<int> remove;
		FOR_EACH_MAP(*bans_, i)
		{
			const uint32 reason = bans_->Element(i).reason;
			if (whitelist_.count(bans_->Key(i)) || (drop_cooldowns && (reason == 20 || reason == 22 || reason == 23)))
				remove.push_back(i);
		}
		for (int i : remove)
			bans_->RemoveAt(i);
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> PassPost()
	{
		// Whoever the pass wanted kicked is gone by now; the rest is stale.
		if (clear_after_pass_ && bans_ && bans_->Count() > 0)
			bans_->RemoveAll();
		return {KHook::Action::Ignore};
	}

	std::unordered_set<std::uint32_t> whitelist_;
	bool clear_after_pass_ = true;
	BanMap *bans_ = nullptr;
	std::unique_ptr<Hook> hook_;
};

} // namespace

std::unique_ptr<Fix> MakeSteamBan()
{
	return std::make_unique<SteamBan>();
}

} // namespace sf

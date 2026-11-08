// Fixes on the engine's client side (libengine2).
//
// server_list_players: the server browser lists a server's players from what it reported to Steam, and
// CS2 reports nothing. Every update_interval seconds each player's SteamID, name and score go to
// ISteamGameServer::BUpdateUserData. (Source2ZE/ServerListPlayersFix, as carried by GameFixes_mm.)
//
// voice_flood: malformed or oversized clc_VoiceData is dropped in CServerSideClient::ProcessVoiceData
// before the engine handles it, and each client is held to a rolling one-second budget (core/rules.h).
// Nobody is kicked; a dropped message is just never played.
//
// demo_record: with tv_enable 1 the GOTV client is never let go: its Disconnect is skipped and the map's
// maxplayers gets one more slot at StartupServer, so the demo keeps recording across match restarts.

#include "all.h"
#include "core/rules.h"
#include "players.h"

#include <engine2/gamesessionconfiguration.h>
#include <engine2/ivengineserver2.h>
#include <engine2/isource2server.h>
#include <engine2/serversideclientbase.h>
#include <steam/isteamgameserver.h>
#include <tier0/convar.h>

#include <dlfcn.h>

#include <array>
#include <chrono>
#include <cstdint>

namespace sf
{

namespace
{

using Clock = std::chrono::steady_clock;

// A stand-in object for KHook's AddGlobal: it only ever reads the first pointer, the vtable, so the hook
// covers every client sharing it even though there is no client to hand over at load.
struct VTableHolder
{
	void *vtable = nullptr;
};

class ServerListPlayers final : public Fix
{
public:
	const char *Name() const override { return "server_list_players"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		if (!players_.Load(context.schema, error))
			return false;
		// Steam's flat API accessor, from the libsteam_api the game already loaded.
		void *steam = dlopen("libsteam_api.so", RTLD_NOW | RTLD_NOLOAD);
		accessor_ = steam ? reinterpret_cast<Accessor>(dlsym(steam, "SteamAPI_SteamGameServer_v015")) : nullptr;
		if (steam)
			dlclose(steam);
		if (!accessor_)
		{
			error = "libsteam_api.so has no SteamAPI_SteamGameServer_v015";
			return false;
		}
		interval_ = std::chrono::seconds(static_cast<long>(GetNumber(context.options, "update_interval", 5, 1, 600)));
		Log("sending players to the server browser every %ld s", static_cast<long>(interval_.count()));
		return true;
	}

	void OnGameFrame() override
	{
		const auto now = Clock::now();
		if (now < next_)
			return;
		next_ = now + interval_;
		ISteamGameServer *steam = accessor_ ? accessor_() : nullptr; // null until the server is logged in to Steam
		if (!steam)
			return;
		for (int slot = 0; slot < 64; ++slot)
		{
			const CSteamID *id = g_pEngineServer->GetClientSteamID(CPlayerSlot(slot));
			CEntityInstance *controller = id && id->BIndividualAccount() ? Players::Controller(slot) : nullptr;
			if (controller)
				steam->BUpdateUserData(*id, players_.Name(controller), static_cast<uint32>(g_pSource2GameClients->GetPlayerScore(CPlayerSlot(slot))));
		}
	}

private:
	using Accessor = ISteamGameServer *(*)();
	Accessor accessor_ = nullptr;
	Players players_;
	std::chrono::seconds interval_{5};
	Clock::time_point next_{};
};

class VoiceFlood final : public Fix
{
public:
	const char *Name() const override { return "voice_flood"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		holder_.vtable = reinterpret_cast<void *>(context.engine.FindVTable("CServerSideClient", error));
		if (!holder_.vtable)
			return false;
		hook_ = std::make_unique<Hook>(&CServerSideClientBase::ProcessVoiceData, this, &VoiceFlood::ProcessVoiceDataPre, nullptr);
		hook_->AddGlobal(reinterpret_cast<CServerSideClientBase *>(&holder_));
		Log("hooked CServerSideClient::ProcessVoiceData on vtable %p", holder_.vtable);
		return true;
	}

	void Unload() override
	{
		if (hook_ && holder_.vtable)
			hook_->RemoveGlobal(reinterpret_cast<CServerSideClientBase *>(&holder_));
		hook_.reset();
		holder_.vtable = nullptr;
	}

	void OnStartupServer(GameSessionConfiguration_t &, const char *) override
	{
		for (VoiceBudget &budget : budgets_)
			budget.Reset();
		if (dropped_)
			Log("%llu voice message(s) dropped on the last map", static_cast<unsigned long long>(dropped_));
		dropped_ = 0;
		logged_first_ = false;
	}

private:
	using Hook = KHook::Virtual<CServerSideClientBase, bool, const CNetMessagePB<CCLCMsg_VoiceData> &>;

	KHook::Return<bool> ProcessVoiceDataPre(CServerSideClientBase *client, const CNetMessagePB<CCLCMsg_VoiceData> &message)
	{
		if (!ShouldDrop(client, message))
			return {KHook::Action::Ignore, true};
		++dropped_;
		if (!logged_first_)
		{
			logged_first_ = true;
			Log("dropped a voice message from slot %d", client->GetPlayerSlot().Get());
		}
		// true, so the engine doesn't count the client as broken; the message just never plays.
		return {KHook::Action::Supersede, true};
	}

	bool ShouldDrop(CServerSideClientBase *client, const CCLCMsg_VoiceData &message)
	{
		// Bots and clients still connecting are left to the engine.
		if (!client || client->IsFakeClient() || !client->IsActive())
			return false;
		const int slot = client->GetPlayerSlot().Get();
		if (slot < 0 || slot >= static_cast<int>(budgets_.size()))
			return true;
		VoiceMessage summary;
		summary.has_audio = message.has_audio();
		summary.has_xuid = message.has_xuid();
		summary.xuid = message.xuid();
		summary.sender = client->GetClientSteamID().ConvertToUint64();
		if (summary.has_audio)
		{
			const CMsgVoiceAudio &audio = message.audio();
			summary.format_valid = VoiceDataFormat_t_IsValid(audio.format());
			summary.offsets = static_cast<std::uint32_t>(audio.packet_offsets_size());
			summary.packets = audio.num_packets();
			summary.voice_bytes = audio.voice_data().size();
		}
		summary.message_bytes = message.ByteSizeLong();
		if (CheckVoiceMessage(summary) != VoiceVerdict::Pass)
			return true;
		return !budgets_[slot].Take(Clock::now(), VoiceCost(summary), std::max(summary.offsets, summary.packets));
	}

	VTableHolder holder_;
	std::unique_ptr<Hook> hook_;
	std::array<VoiceBudget, 64> budgets_;
	std::uint64_t dropped_ = 0;
	bool logged_first_ = false;
};

class DemoRecord final : public Fix
{
public:
	const char *Name() const override { return "demo_record"; }

	bool Load(const FixContext &context, std::string &error) override
	{
		holder_.vtable = reinterpret_cast<void *>(context.engine.FindVTable("CServerSideClient", error));
		if (!holder_.vtable)
			return false;
		hook_ = std::make_unique<Hook>(&CServerSideClientBase::Disconnect, this, &DemoRecord::DisconnectPre, nullptr);
		hook_->AddGlobal(reinterpret_cast<CServerSideClientBase *>(&holder_));
		Log("hooked CServerSideClient::Disconnect on vtable %p, tv_enable is %s", holder_.vtable, TvEnabled() ? "on" : "off");
		return true;
	}

	void Unload() override
	{
		if (hook_ && holder_.vtable)
			hook_->RemoveGlobal(reinterpret_cast<CServerSideClientBase *>(&holder_));
		hook_.reset();
		holder_.vtable = nullptr;
	}

	void OnStartupServer(GameSessionConfiguration_t &config, const char *) override
	{
		if (!TvEnabled())
			return;
		// The engine reads max_clients after this Pre hook, so GOTV's slot comes on top of the player count.
		Log("GOTV is on, maxplayers %u -> %u for its slot", config.max_clients(), config.max_clients() + 1);
		config.set_max_clients(config.max_clients() + 1);
	}

private:
	using Hook = KHook::Virtual<CServerSideClientBase, void, ENetworkDisconnectionReason, const char *>;

	static bool TvEnabled()
	{
		ConVarRefAbstract tv_enable("tv_enable");
		return tv_enable.IsValidRef() && tv_enable.GetBool();
	}

	KHook::Return<void> DisconnectPre(CServerSideClientBase *client, ENetworkDisconnectionReason, const char *)
	{
		if (client && client->IsHLTV() && TvEnabled())
		{
			Log("GOTV is on, keeping the GOTV client connected");
			return {KHook::Action::Supersede};
		}
		return {KHook::Action::Ignore};
	}

	VTableHolder holder_;
	std::unique_ptr<Hook> hook_;
};

} // namespace

std::unique_ptr<Fix> MakeServerListPlayers()
{
	return std::make_unique<ServerListPlayers>();
}

std::unique_ptr<Fix> MakeVoiceFlood()
{
	return std::make_unique<VoiceFlood>();
}

std::unique_ptr<Fix> MakeDemoRecord()
{
	return std::make_unique<DemoRecord>();
}

} // namespace sf

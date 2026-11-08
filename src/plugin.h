#pragma once

#include "fixes/fix.h"
#include "module.h"

#include <ISmmPlugin.h>
#include <engine2/gamesessionconfiguration.h>
#include <engine2/inetworkserverservice.h>
#include <engine2/isource2server.h>

#include <memory>
#include <vector>

class ServerFixes final : public ISmmPlugin
{
public:
	ServerFixes();

	bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late) override;
	bool Unload(char *error, size_t maxlen) override;

	const char *GetAuthor() override { return "karola3vax"; }
	const char *GetName() override { return "Server Fixes"; }
	const char *GetDescription() override { return "CS2 server fixes, each switched on in server_fixes.cfg"; }
	const char *GetURL() override { return ""; }
	const char *GetLicense() override { return "GPL-3.0"; }
	const char *GetVersion() override { return SERVER_FIXES_VERSION; }
	const char *GetDate() override { return __DATE__; }
	const char *GetLogTag() override { return "SERVER_FIXES"; }

private:
	KHook::Return<void> OnGameFrame(ISource2Server *, bool simulating, bool first_tick, bool last_tick);
	KHook::Return<void> OnStartupServer(INetworkServerService *, const GameSessionConfiguration_t &config,
		ISource2WorldSession *session, const char *map);

	bool LoadFixes(char *error, size_t maxlen);

	KHook::Virtual<ISource2Server, void, bool, bool, bool> game_frame_;
	KHook::Virtual<INetworkServerService, void, const GameSessionConfiguration_t &, ISource2WorldSession *, const char *> startup_server_;
	sf::Module server_module_;
	sf::Module engine_module_;
	std::vector<std::unique_ptr<sf::Fix>> fixes_;
	bool hooks_added_ = false;
};

extern ServerFixes g_ServerFixes;

PLUGIN_GLOBALVARS();

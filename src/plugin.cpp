#include "plugin.h"

#include "fixes/all.h"
#include "schema.h"
#include "signatures.h"

#include <engine2/igameresourceservice.h>
#include <engine2/ivengineserver2.h>
#include <interfaces/interfaces.h>
#include <schemasystem/schemasystem.h>
#include <tier0/buffer_string.h>
#include <tier0/convar.h>
#include <tier0/icvar.h>
#include <IPluginManager.h>

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <fstream>
#include <sstream>

ServerFixes g_ServerFixes;
PLUGIN_EXPOSE(ServerFixes, g_ServerFixes);

namespace
{

// Server console (and rcon) only: a player's client could otherwise switch fixes off.
bool FromServerConsole(const CCommandContext &context, const CCommand &args)
{
	if (context.GetPlayerSlot().Get() == -1)
		return true;
	META_CONPRINTF("[SERVER_FIXES] %s is for the server console only\n", args.Arg(0));
	return false;
}

void ReloadCommand(const CCommandContext &context, const CCommand &args)
{
	if (FromServerConsole(context, args))
		g_ServerFixes.Reload();
}

void StatusCommand(const CCommandContext &context, const CCommand &args)
{
	if (FromServerConsole(context, args))
		g_ServerFixes.PrintStatus();
}

} // namespace

ServerFixes::ServerFixes()
	: game_frame_(&ISource2Server::GameFrame, this, nullptr, &ServerFixes::OnGameFrame),
	  // Pre: demo_record changes the session's maxplayers before the engine reads it.
	  startup_server_(&INetworkServerService::StartupServer, this, &ServerFixes::OnStartupServer, nullptr)
{
}

bool ServerFixes::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	GET_V_IFACE_ANY(GetServerFactory, g_pSource2Server, ISource2Server, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameClients, ISource2GameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngineServer, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);

	// Any pointer into a library finds it; an interface's vtable lives in its library's read-only data.
	std::string why;
	if (!server_module_.Load(*reinterpret_cast<void **>(g_pSource2Server), why)
		|| !engine_module_.Load(*reinterpret_cast<void **>(g_pEngineServer), why))
	{
		std::snprintf(error, maxlen, "game libraries: %s", why.c_str());
		return false;
	}
	sf::Config config;
	if (!ReadConfig(config, why))
	{
		std::snprintf(error, maxlen, "%s", why.c_str());
		return false;
	}
	CheckSignatures(false);
	LoadFixes(config);

	game_frame_.Add(g_pSource2Server);
	startup_server_.Add(g_pNetworkServerService);
	hooks_added_ = true;
	// Created here, not with CON_COMMAND_F: ConVar_Unregister empties the SDK's list of static commands,
	// and after `meta unload` + `meta load` the dynamic linker can hand back the same library image, whose
	// static constructors do not run again. Commands made at Load are registered on every load.
	ConVar_Register(FCVAR_RELEASE | FCVAR_GAMEDLL);
	reload_command_ = std::make_unique<ConCommand>("server_fixes_reload", ReloadCommand,
		"Server Fixes: read server_fixes.cfg again and reload the plugin (server console only)", FCVAR_RELEASE | FCVAR_GAMEDLL);
	status_command_ = std::make_unique<ConCommand>("server_fixes_status", StatusCommand,
		"Server Fixes: the fixes on and every signature (server console only)", FCVAR_RELEASE | FCVAR_GAMEDLL);
	META_CONPRINTF("[SERVER_FIXES] Server Fixes %s loaded%s, %zu fix(es) on.\n", SERVER_FIXES_VERSION, late ? " late" : "", fixes_.size());
	return true;
}

bool ServerFixes::ReadConfig(sf::Config &config, std::string &error) const
{
	CBufferStringN<256> game_dir;
	g_pEngineServer->GetGameDir(game_dir);
	const std::string path = std::string(game_dir.Get()) + "/addons/server_fixes/server_fixes.cfg";
	std::ifstream file(path);
	if (!file)
	{
		error = "cannot read " + path;
		return false;
	}
	std::stringstream text;
	text << file.rdbuf();
	config = sf::ParseConfig(text.str());
	return true;
}

void ServerFixes::LoadFixes(const sf::Config &config)
{
	for (const std::string &line : config.errors)
		META_CONPRINTF("[SERVER_FIXES] server_fixes.cfg %s\n", line.c_str());

	CSchemaSystemTypeScope *scope = schema::ServerScope(g_pSchemaSystem);
	const sf::Options none;
	for (auto &fix : sf::CreateFixes())
	{
		const auto block = config.fixes.find(fix->Name());
		const sf::Options &options = block == config.fixes.end() ? none : block->second;
		if (!sf::GetBool(options, "enable", false))
		{
			META_CONPRINTF("[SERVER_FIXES] %s: off\n", fix->Name());
			fix->Unload();
			continue;
		}
		std::string why;
		if (!fix->Load({server_module_, engine_module_, scope, options}, why))
		{
			META_CONPRINTF("[SERVER_FIXES] %s: NOT loaded, %s\n", fix->Name(), why.c_str());
			fix->Unload();
			continue;
		}
		fixes_.push_back(std::move(fix));
	}
	for (const auto &[name, options] : config.fixes)
		if (!sf::IsFixName(name))
			META_CONPRINTF("[SERVER_FIXES] server_fixes.cfg: no fix is called \"%s\"\n", name.c_str());
}

void ServerFixes::UnloadFixes()
{
	for (auto &fix : fixes_)
		fix->Unload();
	fixes_.clear();
}

void ServerFixes::CheckSignatures(bool verbose) const
{
	std::size_t found = 0;
	for (const auto &signature : sf::sig::kAll)
	{
		std::string why, unused;
		std::uintptr_t address = signature.function ? server_module_.FindFunction(signature.bytes, why)
			: server_module_.FindUnique(signature.bytes, why);
		const bool patched = !address && signature.patched && (address = server_module_.FindUnique(signature.patched, unused)) != 0;
		if (address)
		{
			++found;
			if (verbose)
				META_CONPRINTF("[SERVER_FIXES]   ok      %s (%p)%s\n", signature.name, reinterpret_cast<void *>(address),
					patched ? ", patched by us" : "");
		}
		else
			META_CONPRINTF("[SERVER_FIXES]   FAILED  %s: %s\n", signature.name, why.c_str());
	}
	const std::size_t total = std::size(sf::sig::kAll);
	META_CONPRINTF("[SERVER_FIXES] signatures: %zu of %zu found in libserver.so%s\n", found, total,
		found == total ? "" : "; the fixes that need the failed ones stay off until src/signatures.h is updated");
}

void ServerFixes::Reload()
{
	sf::Config config;
	std::string why;
	if (!ReadConfig(config, why))
	{
		META_CONPRINTF("[SERVER_FIXES] reload: %s; the running fixes are kept\n", why.c_str());
		return;
	}
	// Metamod unloads and loads the plugin, the path every plugin swap takes. Unloading the fixes and
	// loading them again in place would leave hook ids that are already gone in Metamod's list of the
	// plugin's hooks; its Unloader erases from that set while it walks it, skips some, and never closes
	// the library again, so the next `meta load` would bring the old code back.
	auto *plugins = static_cast<SourceMM::ISmmPluginManager *>(g_SMAPI->MetaFactory(MMIFACE_PLMANAGER, nullptr, nullptr));
	const char *file = nullptr;
	if (!plugins || !plugins->Query(g_PLID, &file, nullptr, nullptr) || !file || !*file)
	{
		META_CONPRINTF("[SERVER_FIXES] reload: Metamod does not say which file this plugin is; use meta unload and meta load\n");
		return;
	}
	char command[512];
	std::snprintf(command, sizeof(command), "meta unload %d; meta load \"%s\"\n", static_cast<int>(g_PLID), file);
	META_CONPRINTF("[SERVER_FIXES] reload: %s", command);
	g_pEngineServer->ServerCommand(command); // queued: runs after this command returns, not inside the plugin
}

void ServerFixes::PrintStatus()
{
	META_CONPRINTF("[SERVER_FIXES] Server Fixes %s, %zu fix(es) on:", SERVER_FIXES_VERSION, fixes_.size());
	for (const auto &fix : fixes_)
		META_CONPRINTF(" %s", fix->Name());
	META_CONPRINTF("\n");
	CheckSignatures(true);
}

bool ServerFixes::Unload(char *, size_t)
{
	// Metamod also unloads a plugin whose Load failed, before its hooks were added.
	if (hooks_added_)
	{
		game_frame_.Remove(g_pSource2Server);
		startup_server_.Remove(g_pNetworkServerService);
		reload_command_.reset(); // ~ConCommand unregisters it
		status_command_.reset();
		ConVar_Unregister();
		hooks_added_ = false;
	}
	UnloadFixes();
	return true;
}

KHook::Return<void> ServerFixes::OnGameFrame(ISource2Server *, bool, bool, bool)
{
	for (auto &fix : fixes_)
		fix->OnGameFrame();
	return {KHook::Action::Ignore};
}

KHook::Return<void> ServerFixes::OnStartupServer(INetworkServerService *, const GameSessionConfiguration_t &config,
	ISource2WorldSession *, const char *map)
{
	for (auto &fix : fixes_)
		fix->OnStartupServer(const_cast<GameSessionConfiguration_t &>(config), map ? map : "");
	return {KHook::Action::Ignore};
}

namespace sf
{

void Fix::Log(const char *format, ...) const
{
	char message[1024];
	va_list args;
	va_start(args, format);
	std::vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	META_CONPRINTF("[SERVER_FIXES] %s: %s\n", Name(), message);
}

bool Offset(CSchemaSystemTypeScope *scope, const char *class_name, const char *field, std::ptrdiff_t &offset, std::string &error)
{
	offset = schema::FieldOffset(scope, class_name, field);
	if (offset >= 0)
		return true;
	error = std::string("CS2's schema has no ") + class_name + "::" + field;
	return false;
}

} // namespace sf

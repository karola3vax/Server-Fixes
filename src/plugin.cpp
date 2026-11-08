#include "plugin.h"

#include "fixes/all.h"
#include "schema.h"

#include <engine2/igameresourceservice.h>
#include <engine2/ivengineserver2.h>
#include <interfaces/interfaces.h>
#include <schemasystem/schemasystem.h>
#include <tier0/buffer_string.h>
#include <tier0/icvar.h>

#include <cstdio>
#include <fstream>
#include <sstream>

ServerFixes g_ServerFixes;
PLUGIN_EXPOSE(ServerFixes, g_ServerFixes);

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
	if (!LoadFixes(error, maxlen))
		return false;

	game_frame_.Add(g_pSource2Server);
	startup_server_.Add(g_pNetworkServerService);
	hooks_added_ = true;
	META_CONPRINTF("[SERVER_FIXES] Server Fixes %s loaded%s, %zu fix(es) on.\n", SERVER_FIXES_VERSION, late ? " late" : "", fixes_.size());
	return true;
}

bool ServerFixes::LoadFixes(char *error, size_t maxlen)
{
	CBufferStringN<256> game_dir;
	g_pEngineServer->GetGameDir(game_dir);
	const std::string path = std::string(game_dir.Get()) + "/addons/server_fixes/server_fixes.cfg";
	std::ifstream file(path);
	if (!file)
	{
		std::snprintf(error, maxlen, "cannot read %s", path.c_str());
		return false;
	}
	std::stringstream text;
	text << file.rdbuf();
	const sf::Config config = sf::ParseConfig(text.str());
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
	return true;
}

bool ServerFixes::Unload(char *, size_t)
{
	// Metamod also unloads a plugin whose Load failed, before its hooks were added.
	if (hooks_added_)
	{
		game_frame_.Remove(g_pSource2Server);
		startup_server_.Remove(g_pNetworkServerService);
		hooks_added_ = false;
	}
	for (auto &fix : fixes_)
		fix->Unload();
	fixes_.clear();
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

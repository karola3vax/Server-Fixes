#pragma once

// What every fix looks like to the plugin. A fix is created at load, configured from its block in
// server_fixes.cfg, and only loaded with "<fix>.enable 1". Load() returns false with a reason to leave
// just that fix off; the plugin calls Unload() for it either way.

#include "core/config.h"
#include "module.h"

#include <khook.hpp>

#include <cstdarg>
#include <string>

class CSchemaSystemTypeScope;
class GameSessionConfiguration_t;

namespace sf
{

struct FixContext
{
	const Module &server;
	const Module &engine;
	CSchemaSystemTypeScope *schema;
	const Options &options;
};

class Fix
{
public:
	virtual ~Fix() = default;
	virtual const char *Name() const = 0;
	virtual bool Load(const FixContext &context, std::string &error) = 0;
	virtual void Unload() {}
	// Every server frame (GameFrame post, simulating or not).
	virtual void OnGameFrame() {}
	// A new map; Pre, so the session config can still be changed.
	virtual void OnStartupServer(GameSessionConfiguration_t &/*config*/, const char */*map*/) {}

protected:
	// "[SERVER_FIXES] <fix>: <message>" in the server console.
	void Log(const char *format, ...) const __attribute__((format(printf, 2, 3)));
};

// The schema offset of class::field, or false with `error` naming it.
bool Offset(CSchemaSystemTypeScope *schema, const char *class_name, const char *field, std::ptrdiff_t &offset, std::string &error);

} // namespace sf

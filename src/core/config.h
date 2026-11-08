#pragma once

// server_fixes.cfg: one "<fix>.<option> <value>" per line, "//" comments. Kept free of SDK headers so
// tests/test_core.cpp can build it with a plain compiler.

#include <map>
#include <string>
#include <vector>

namespace sf
{

// One fix's options, option -> value.
using Options = std::map<std::string, std::string>;

struct Config
{
	std::map<std::string, Options> fixes; // fix name -> its options
	std::vector<std::string> errors;      // "line 7: ..." for every line that was skipped
};

Config ParseConfig(const std::string &text);

bool GetBool(const Options &options, const std::string &key, bool fallback);
// fallback when the value is missing or not a number; the result is clamped to [min, max].
double GetNumber(const Options &options, const std::string &key, double fallback, double min, double max);
std::string GetString(const Options &options, const std::string &key, const std::string &fallback);

} // namespace sf

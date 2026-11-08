#include "config.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace sf
{

namespace
{

std::string Trim(const std::string &text)
{
	auto space = [](unsigned char c) { return std::isspace(c) != 0; };
	auto begin = std::find_if_not(text.begin(), text.end(), space);
	auto end = std::find_if_not(text.rbegin(), text.rend(), space).base();
	return begin < end ? std::string(begin, end) : std::string();
}

// "//" starts a comment only at the line start or after whitespace, so a value such as a URL keeps its slashes.
std::string StripComment(const std::string &line)
{
	for (std::size_t i = 0; i + 1 < line.size(); ++i)
		if (line[i] == '/' && line[i + 1] == '/' && (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1]))))
			return line.substr(0, i);
	return line;
}

std::string Unquote(const std::string &value)
{
	if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
		return value.substr(1, value.size() - 2);
	return value;
}

} // namespace

Config ParseConfig(const std::string &text)
{
	Config config;
	std::istringstream stream(text);
	std::string raw;
	for (int number = 1; std::getline(stream, raw); ++number)
	{
		const std::string line = Trim(StripComment(raw));
		if (line.empty())
			continue;
		const auto gap = line.find_first_of(" \t");
		const std::string key = line.substr(0, gap);
		const std::string value = gap == std::string::npos ? std::string() : Unquote(Trim(line.substr(gap)));
		const auto dot = key.find('.');
		if (dot == std::string::npos || dot == 0 || dot + 1 == key.size())
		{
			config.errors.push_back("line " + std::to_string(number) + ": expected <fix>.<option> <value>, got \"" + line + "\"");
			continue;
		}
		if (gap == std::string::npos)
		{
			config.errors.push_back("line " + std::to_string(number) + ": " + key + " has no value");
			continue;
		}
		config.fixes[key.substr(0, dot)][key.substr(dot + 1)] = value;
	}
	return config;
}

bool GetBool(const Options &options, const std::string &key, bool fallback)
{
	const auto found = options.find(key);
	if (found == options.end())
		return fallback;
	const std::string &value = found->second;
	if (value == "1" || value == "true" || value == "on" || value == "yes")
		return true;
	if (value == "0" || value == "false" || value == "off" || value == "no")
		return false;
	return fallback;
}

double GetNumber(const Options &options, const std::string &key, double fallback, double min, double max)
{
	double value = fallback;
	const auto found = options.find(key);
	if (found != options.end() && !found->second.empty())
	{
		char *end = nullptr;
		errno = 0;
		const double parsed = std::strtod(found->second.c_str(), &end);
		if (errno == 0 && end && *end == '\0' && std::isfinite(parsed))
			value = parsed;
	}
	return std::clamp(value, min, max);
}

std::string GetString(const Options &options, const std::string &key, const std::string &fallback)
{
	const auto found = options.find(key);
	return found == options.end() ? fallback : found->second;
}

} // namespace sf

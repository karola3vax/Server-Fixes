#include "pattern.h"

#include <cctype>
#include <cstring>

namespace sf
{

std::optional<Pattern> ParsePattern(std::string_view text)
{
	Pattern pattern;
	bool concrete = false;
	std::size_t i = 0;
	while (i < text.size())
	{
		if (std::isspace(static_cast<unsigned char>(text[i])))
		{
			++i;
			continue;
		}
		std::size_t end = i;
		while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])))
			++end;
		const std::string_view token = text.substr(i, end - i);
		i = end;
		if (token == "?" || token == "??")
		{
			pattern.push_back(-1);
			continue;
		}
		if (token.size() != 2 || !std::isxdigit(static_cast<unsigned char>(token[0]))
			|| !std::isxdigit(static_cast<unsigned char>(token[1])))
			return std::nullopt;
		auto nibble = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::toupper(c) - 'A' + 10); };
		pattern.push_back(nibble(token[0]) * 16 + nibble(token[1]));
		concrete = true;
	}
	if (pattern.empty() || !concrete)
		return std::nullopt;
	return pattern;
}

std::vector<std::size_t> FindPattern(const std::uint8_t *data, std::size_t size, const Pattern &pattern, std::size_t limit)
{
	std::vector<std::size_t> found;
	if (!data || pattern.empty() || size < pattern.size() || limit == 0)
		return found;
	// Anchor on the first concrete byte with memchr, then check the rest.
	std::size_t anchor = 0;
	while (pattern[anchor] < 0)
		++anchor;
	const auto first = static_cast<std::uint8_t>(pattern[anchor]);
	const std::size_t last_start = size - pattern.size();
	std::size_t start = 0;
	while (start <= last_start)
	{
		const void *hit = std::memchr(data + start + anchor, first, last_start - start + 1);
		if (!hit)
			break;
		const std::size_t candidate = static_cast<const std::uint8_t *>(hit) - data - anchor;
		bool match = true;
		for (std::size_t j = 0; j < pattern.size(); ++j)
			if (pattern[j] >= 0 && data[candidate + j] != pattern[j])
			{
				match = false;
				break;
			}
		if (match)
		{
			found.push_back(candidate);
			if (found.size() >= limit)
				break;
		}
		start = candidate + 1;
	}
	return found;
}

} // namespace sf

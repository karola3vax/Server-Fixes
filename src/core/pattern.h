#pragma once

// Byte patterns ("55 48 89 E5 ? 8B") and a scanner over a plain buffer. SDK-free for tests/test_core.cpp.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace sf
{

// -1 is a wildcard byte ("?" or "??").
using Pattern = std::vector<int>;

// nullopt for an empty pattern, a bad token, or one made only of wildcards.
std::optional<Pattern> ParsePattern(std::string_view text);

// The same pattern for a function another hook already starts with a jump (KHook writes E9 <rel32> 90, and
// keeps it after that plugin unloads): its first 6 bytes become wildcards. nullopt when too little is left.
std::optional<Pattern> BehindHookJump(const Pattern &pattern);

// Offsets of the matches, at most `limit`: 2 is enough to tell a unique match from an ambiguous one.
std::vector<std::size_t> FindPattern(const std::uint8_t *data, std::size_t size, const Pattern &pattern, std::size_t limit = 2);

} // namespace sf

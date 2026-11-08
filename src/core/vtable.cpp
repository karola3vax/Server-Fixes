#include "vtable.h"

#include <cstring>
#include <string>

namespace sf
{

namespace
{

// Addresses of the 8-byte-aligned qwords equal to `value`, at most `limit`.
std::vector<std::uintptr_t> FindQwords(const std::vector<DataSpan> &spans, std::uintptr_t value, std::size_t limit)
{
	std::vector<std::uintptr_t> found;
	for (const DataSpan &span : spans)
	{
		const std::uintptr_t start = (span.start + 7) & ~std::uintptr_t(7);
		for (std::uintptr_t at = start; at + 8 <= span.end; at += 8)
		{
			std::uintptr_t qword;
			std::memcpy(&qword, reinterpret_cast<const void *>(at), sizeof(qword));
			if (qword != value)
				continue;
			found.push_back(at);
			if (found.size() >= limit)
				return found;
		}
	}
	return found;
}

bool Inside(const std::vector<DataSpan> &spans, std::uintptr_t address, std::size_t size)
{
	for (const DataSpan &span : spans)
		if (address >= span.start && address + size <= span.end)
			return true;
	return false;
}

} // namespace

std::vector<std::uintptr_t> FindVTables(const std::vector<DataSpan> &spans, const char *name)
{
	const std::string mangled = std::to_string(std::strlen(name)) + name;
	std::vector<std::uintptr_t> names;
	for (const DataSpan &span : spans)
	{
		const auto *data = reinterpret_cast<const char *>(span.start);
		const std::size_t size = span.end - span.start;
		std::size_t at = 1;
		while (at + mangled.size() < size)
		{
			const void *hit = std::memchr(data + at, mangled[0], size - mangled.size() - at);
			if (!hit)
				break;
			at = static_cast<const char *>(hit) - data;
			// A whole string: starts after a terminator and ends with one ("13Foo" is not "3Foo").
			if (data[at - 1] == '\0' && data[at + mangled.size()] == '\0' && std::memcmp(data + at, mangled.data(), mangled.size()) == 0)
				names.push_back(span.start + at);
			++at;
		}
	}
	std::vector<std::uintptr_t> vtables;
	for (std::uintptr_t text : names)
		for (std::uintptr_t name_slot : FindQwords(spans, text, 8))
		{
			if (name_slot < 8)
				continue;
			const std::uintptr_t typeinfo = name_slot - 8; // { vptr, name }
			for (std::uintptr_t slot : FindQwords(spans, typeinfo, 16))
			{
				// Read offset-to-top only where it is mapped: a slot at a span's start has nothing before it.
				if (!Inside(spans, slot - 8, 8))
					continue;
				std::intptr_t offset_to_top;
				std::memcpy(&offset_to_top, reinterpret_cast<const void *>(slot - 8), sizeof(offset_to_top));
				if (offset_to_top == 0) // the primary vtable; a secondary base's has a negative offset
					vtables.push_back(slot + 8);
			}
		}
	return vtables;
}

} // namespace sf

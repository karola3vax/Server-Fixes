#pragma once

// Finding a class's vtable through its Itanium RTTI, over plain memory. SDK-free for tests/test_core.cpp;
// Module::FindVTable runs it over a library's data segments.

#include <cstdint>
#include <vector>

namespace sf
{

// A readable, non-code address range [start, end).
struct DataSpan
{
	std::uintptr_t start;
	std::uintptr_t end;
};

// The primary vtables (offset-to-top 0) of class `name`, found from its typeinfo name string ("3Foo" for
// Foo) through its typeinfo { vptr, name } to the vtable { offset-to-top, typeinfo, functions... }. The
// result points at the first function, like an object's vptr. One entry means found; none or several mean
// not found.
std::vector<std::uintptr_t> FindVTables(const std::vector<DataSpan> &spans, const char *name);

} // namespace sf

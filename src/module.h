#pragma once

// A loaded game library (libserver.so, libengine2.so): its mapped segments, signature scans over its code,
// and a vtable found by its RTTI name. Linux only, like the servers this plugin is for.

#include "core/pattern.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf
{

class Module
{
public:
	// The library that contains `address` (any pointer into it, such as an interface's vtable).
	bool Load(const void *address, std::string &error);

	const std::string &Name() const { return name_; }

	// The one match of `signature` in the code, or 0 with `error` set (none found, or more than one).
	std::uintptr_t FindUnique(const char *signature, std::string &error) const;

	// The vtable of class `name` (address point, the first virtual function slot), or 0 with `error` set.
	// Itanium ABI: the typeinfo name string, the typeinfo object pointing at it, and the one primary
	// vtable (offset-to-top 0) pointing at the typeinfo.
	std::uintptr_t FindVTable(const char *name, std::string &error) const;

private:
	struct Range
	{
		std::uintptr_t start;
		std::uintptr_t end;
		bool code;
	};

	std::vector<std::uintptr_t> FindQwords(std::uintptr_t value, std::size_t limit) const;

	std::string name_;
	std::vector<Range> ranges_;
};

// x86-64 rip-relative operand: the address an instruction at `instruction` refers to, with the 32-bit
// displacement at `offset` and the next instruction `length` bytes on.
std::uintptr_t ResolveRelative(std::uintptr_t instruction, int offset, int length);

// Overwrites code bytes; the page is made writable only for the copy.
bool WriteCode(std::uintptr_t address, const void *bytes, std::size_t size);

} // namespace sf

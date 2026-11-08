#include "module.h"

#include "core/vtable.h"

#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>

namespace sf
{

namespace
{

struct Search
{
	std::uintptr_t base;
	std::vector<std::uintptr_t> starts, ends;
	std::vector<bool> code;
};

int CollectSegments(dl_phdr_info *info, std::size_t, void *data)
{
	auto *search = static_cast<Search *>(data);
	if (info->dlpi_addr != search->base)
		return 0;
	for (int i = 0; i < info->dlpi_phnum; ++i)
	{
		const ElfW(Phdr) &header = info->dlpi_phdr[i];
		if (header.p_type != PT_LOAD || header.p_memsz == 0)
			continue;
		search->starts.push_back(info->dlpi_addr + header.p_vaddr);
		search->ends.push_back(info->dlpi_addr + header.p_vaddr + header.p_memsz);
		search->code.push_back((header.p_flags & PF_X) != 0);
	}
	return 1;
}

} // namespace

bool Module::Load(const void *address, std::string &error)
{
	Dl_info info{};
	if (!address || !dladdr(address, &info) || !info.dli_fbase)
	{
		error = "no library contains that address";
		return false;
	}
	Search search{reinterpret_cast<std::uintptr_t>(info.dli_fbase), {}, {}, {}};
	dl_iterate_phdr(CollectSegments, &search);
	if (search.starts.empty())
	{
		error = std::string("no loaded segments for ") + (info.dli_fname ? info.dli_fname : "?");
		return false;
	}
	name_ = info.dli_fname ? info.dli_fname : "?";
	if (const auto slash = name_.rfind('/'); slash != std::string::npos)
		name_ = name_.substr(slash + 1);
	ranges_.clear();
	for (std::size_t i = 0; i < search.starts.size(); ++i)
		ranges_.push_back({search.starts[i], search.ends[i], search.code[i]});
	return true;
}

std::vector<std::uintptr_t> Module::Scan(const Pattern &bytes) const
{
	std::vector<std::uintptr_t> found;
	for (const Range &range : ranges_)
	{
		if (!range.code)
			continue;
		for (std::size_t offset : FindPattern(reinterpret_cast<const std::uint8_t *>(range.start), range.end - range.start, bytes, 2))
			found.push_back(range.start + offset);
		if (found.size() > 1)
			break;
	}
	return found;
}

std::uintptr_t Module::FindUnique(const char *signature, std::string &error) const
{
	return Find(signature, false, error);
}

std::uintptr_t Module::FindFunction(const char *signature, std::string &error) const
{
	return Find(signature, true, error);
}

std::uintptr_t Module::Find(const char *signature, bool function, std::string &error) const
{
	const auto pattern = ParsePattern(signature);
	if (!pattern)
	{
		error = "bad signature";
		return 0;
	}
	std::vector<std::uintptr_t> found = Scan(*pattern);
	// Another plugin may already hook the function (its jump replaced the first bytes). Still only one match.
	if (function && found.empty())
		if (const auto hooked = BehindHookJump(*pattern))
			found = Scan(*hooked);
	if (found.size() != 1)
	{
		error = found.empty() ? "signature not found in " + name_ : "signature matches more than once in " + name_;
		return 0;
	}
	return found[0];
}

std::uintptr_t Module::FindVTable(const char *name, std::string &error) const
{
	std::vector<DataSpan> data;
	for (const Range &range : ranges_)
		if (!range.code)
			data.push_back({range.start, range.end});
	const std::vector<std::uintptr_t> vtables = FindVTables(data, name);
	if (vtables.size() != 1)
	{
		error = std::string(vtables.empty() ? "no vtable for " : "more than one vtable for ") + name + " in " + name_;
		return 0;
	}
	return vtables[0];
}

std::uintptr_t ResolveRelative(std::uintptr_t instruction, int offset, int length)
{
	std::int32_t displacement = 0;
	std::memcpy(&displacement, reinterpret_cast<const void *>(instruction + offset), sizeof(displacement));
	return instruction + length + displacement;
}

bool WriteCode(std::uintptr_t address, const void *bytes, std::size_t size)
{
	const auto page = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
	const std::uintptr_t first = address & ~(page - 1);
	const std::uintptr_t last = (address + size + page - 1) & ~(page - 1);
	auto *start = reinterpret_cast<void *>(first);
	if (mprotect(start, last - first, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
		return false;
	std::memcpy(reinterpret_cast<void *>(address), bytes, size);
	// Code pages are read + execute; that is what they go back to.
	return mprotect(start, last - first, PROT_READ | PROT_EXEC) == 0;
}

} // namespace sf

#pragma once

// Field offsets come from CS2's own schema system at load, so they survive CS2 updates.

#include <schemalib/schemaclassinfo.h>
#include <schemasystem/schemasystem.h>

#include <cstddef>
#include <cstring>

namespace schema
{

inline CSchemaSystemTypeScope *ServerScope(ISchemaSystem *system)
{
#ifdef _WIN32
	return system->FindTypeScopeForModule("server.dll");
#else
	return system->FindTypeScopeForModule("libserver.so");
#endif
}

// -1 when CS2 no longer has the field.
inline std::ptrdiff_t FieldOffset(CSchemaSystemTypeScope *scope, const char *class_name, const char *field)
{
	const CSchemaClassInfo *info = scope ? scope->FindDeclaredClass(class_name).Get() : nullptr;
	for (int i = 0; info && i < info->m_nNumFields; ++i)
		if (info->m_pFields[i].m_pName && std::strcmp(info->m_pFields[i].m_pName, field) == 0)
			return info->m_pFields[i].m_nSingleInheritanceOffset;
	return -1;
}

template <typename T>
T &Field(void *object, std::ptrdiff_t offset)
{
	return *reinterpret_cast<T *>(static_cast<std::byte *>(object) + offset);
}

} // namespace schema

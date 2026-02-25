#ifndef TREE_SCHEMA_INCLUDE_GUARD
#define TREE_SCHEMA_INCLUDE_GUARD

#include "types.hpp"
#include "range.hpp"

enum class TreeSchemaNodeKind : u8
{
	INVALID = 0,
	Container,
	Integer,
	String,
	Boolean,
	Path,
};

struct TreeSchemaNode;

struct TreeSchemaContainerAttach
{
	Range<TreeSchemaNode> children;
};

struct TreeSchemaIntegerAttach
{
	s64 min;

	s64 max;
};

struct TreeSchemaNode
{
	TreeSchemaNodeKind kind;

	u32 target_offset;

	const char8* name;

	const char8* helptext;

	union
	{
		TreeSchemaContainerAttach container;

		TreeSchemaIntegerAttach integer;
	};

	constexpr TreeSchemaNode(TreeSchemaNodeKind kind, u32 target_offset, const char8* name, const char8* helptext) noexcept :
		kind{ kind },
		target_offset{ target_offset },
		name{ name },
		helptext{ helptext },
		container{}
	{}

	constexpr TreeSchemaNode(TreeSchemaContainerAttach attach, u32 target_offset, const char8* name, const char8* helptext) noexcept :
		kind{ TreeSchemaNodeKind::Container },
		target_offset{ target_offset },
		name{ name },
		helptext{ helptext },
		container{ attach }
	{}

	constexpr TreeSchemaNode(TreeSchemaIntegerAttach attach, u32 target_offset, const char8* name, const char8* helptext) noexcept :
		kind{ TreeSchemaNodeKind::Integer },
		target_offset{ target_offset },
		name{ name },
		helptext{ helptext },
		integer{ attach }
	{}
};

#endif // TREE_SCHEMA_INCLUDE_GUARD

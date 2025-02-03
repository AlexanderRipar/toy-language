#ifndef AST2_ATTACH_INCLUDE_GUARD
#define AST2_ATTACH_INCLUDE_GUARD

#include "infra/common.hpp"
#include "ast2.hpp"
#include "pass_data.hpp"

namespace a2
{
	struct ValIntegerData
	{
		static constexpr Tag TAG = Tag::ValInteger;

		u64 value;
	};

	struct ValFloatData
	{
		static constexpr Tag TAG = Tag::ValFloat;

		f64 value;
	};

	struct ValCharData
	{
		static constexpr Tag TAG = Tag::ValChar;

		u32 codepoint;
	};

	struct ValIdentifierData
	{
		static constexpr Tag TAG = Tag::ValIdentifer;

		IdentifierId identifier_id;
	};

	struct ValStringData
	{
		static constexpr Tag TAG = Tag::ValString;

		IdentifierId string_id;
	};

	struct DefinitionData
	{
		static constexpr Tag TAG = Tag::Definition;

		IdentifierId identifier_id;
	};

	struct BlockData
	{
		static constexpr Tag TAG = Tag::Block;

		u32 namespace_index;
	};

	struct FileData
	{
		static constexpr Tag TAG = Tag::File;

		BlockData root_block;

		IdentifierId filename_id;
	};
}

#endif // AST2_ATTACH_INCLUDE_GUARD

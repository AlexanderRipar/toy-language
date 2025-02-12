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

		#pragma pack(push)
		#pragma pack(4)
		u64 value;
		#pragma pack(pop)
	};

	struct ValFloatData
	{
		static constexpr Tag TAG = Tag::ValFloat;

		#pragma pack(push)
		#pragma pack(4)
		f64 value;
		#pragma pack(pop)
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

		TypeId type_id;

		ValueId value_id;
	};

	struct BlockData
	{
		static constexpr Tag TAG = Tag::Block;

		u32 definition_count;

		ScopeId scope_id;
	};

	struct FileData
	{
		static constexpr Tag TAG = Tag::File;

		BlockData root_block;

		IdentifierId filename_id;
	};

	struct BuiltinData
	{
		static constexpr Tag TAG = Tag::Builtin;

		using BuiltinSignature = void (*) (Interpreter*);

		#pragma pack(push)
		#pragma pack(4)
		BuiltinSignature function;
		#pragma pack(pop)
	};

	struct FuncData
	{
		static constexpr Tag TAG = Tag::Func;

		TypeId signature_type_id;

		TypeId return_type_id;
	};
}

#endif // AST2_ATTACH_INCLUDE_GUARD

#ifndef AST2_ATTACH_INCLUDE_GUARD
#define AST2_ATTACH_INCLUDE_GUARD

#include "../infra/common.hpp"
#include "pass_data.hpp"

struct ValIntegerData
{
	static constexpr AstTag TAG = AstTag::ValInteger;

	#pragma pack(push)
	#pragma pack(4)
	u64 value;
	#pragma pack(pop)
};

struct ValFloatData
{
	static constexpr AstTag TAG = AstTag::ValFloat;

	#pragma pack(push)
	#pragma pack(4)
	f64 value;
	#pragma pack(pop)
};

struct ValCharData
{
	static constexpr AstTag TAG = AstTag::ValChar;

	u32 codepoint;
};

struct ValIdentifierData
{
	static constexpr AstTag TAG = AstTag::ValIdentifer;

	IdentifierId identifier_id;
};

struct ValStringData
{
	static constexpr AstTag TAG = AstTag::ValString;

	IdentifierId string_id;
};

struct DefinitionData
{
	static constexpr AstTag TAG = AstTag::Definition;

	IdentifierId identifier_id;

	TypeId type_id;

	ValueId value_id;
};

struct BlockData
{
	static constexpr AstTag TAG = AstTag::Block;

	u32 definition_count;

	ScopeId scope_id;
};

struct FileData
{
	static constexpr AstTag TAG = AstTag::File;

	BlockData root_block;
};

struct FuncData
{
	static constexpr AstTag TAG = AstTag::Func;

	TypeId signature_type_id;

	TypeId return_type_id;

	ScopeId scope_id;
};

#endif // AST2_ATTACH_INCLUDE_GUARD

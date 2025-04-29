#ifndef AST_ATTACH_INCLUDE_GUARD
#define AST_ATTACH_INCLUDE_GUARD

#include "../infra/common.hpp"
#include "pass_data.hpp"

struct AstLitIntegerData
{
	static constexpr AstTag TAG = AstTag::LitInteger;

	#pragma pack(push)
	#pragma pack(4)
	CompIntegerValue value;
	#pragma pack(pop)
};

struct AstLitFloatData
{
	static constexpr AstTag TAG = AstTag::LitFloat;

	#pragma pack(push)
	#pragma pack(4)
	CompFloatValue value;
	#pragma pack(pop)
};

struct AstLitCharData
{
	static constexpr AstTag TAG = AstTag::LitChar;

	u32 codepoint;
};

struct AstIdentifierData
{
	static constexpr AstTag TAG = AstTag::Identifer;

	IdentifierId identifier_id;
};

struct ValStringData
{
	static constexpr AstTag TAG = AstTag::LitString;

	IdentifierId string_id;
};

struct DefinitionData
{
	static constexpr AstTag TAG = AstTag::Definition;

	IdentifierId identifier_id;
};

struct BlockData
{
	static constexpr AstTag TAG = AstTag::Block;

	TypeId scope_type_id;
};

#endif // AST_ATTACH_INCLUDE_GUARD

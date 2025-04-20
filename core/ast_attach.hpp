#ifndef AST_ATTACH_INCLUDE_GUARD
#define AST_ATTACH_INCLUDE_GUARD

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
};

#endif // AST_ATTACH_INCLUDE_GUARD

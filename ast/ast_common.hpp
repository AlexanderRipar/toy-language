#ifndef AST_COMMON_INCLUDE_GUARD
#define AST_COMMON_INCLUDE_GUARD

#include "../common.hpp"

namespace ast
{
	enum class NodeType : u8
	{
		INVALID = 0,
		Program,
		CompositeInitializer,
		ArrayInitializer,
		Where,
		Definition,
		Block,
		If,
		For,
		ForEach,
		Switch,
		Case,
		Func,
		Catch,
		ValIdentifer,
		ValInteger,
		ValFloat,
		ValChar,
		ValString,
		Call,
		UOpTypeTailArray,
		UOpTypeSlice,
		UOpTypeMultiPtr,
		UOpTypeOptMultiPtr,
		UOpTry,
		UOpDefer,
		UOpAddr,
		UOpDeref,
		UOpBitNot,
		UOpLogNot,
		UOpTypeOptPtr,
		UOpTypeVar,
		OpImpliedMember,
		UOpTypePtr,
		UOpNegate,
		UOpPos,
		OpAdd,
		OpSub,
		OpMul,
		OpDiv,
		OpAddTC,
		OpSubTC,
		OpMulTC,
		OpMod,
		OpBitAnd,
		OpBitOr,
		OpBitXor,
		OpShiftL,
		OpShiftR,
		OpLogAnd,
		OpLogOr,
		OpMember,
		OpCmpLT,
		OpCmpGT,
		OpCmpLE,
		OpCmpGE,
		OpCmpNE,
		OpCmpEQ,
		OpSet,
		OpSetAdd,
		OpSetSub,
		OpSetMul,
		OpSetDiv,
		OpSetAddTC,
		OpSetSubTC,
		OpSetMulTC,
		OpSetMod,
		OpSetBitAnd,
		OpSetBitOr,
		OpSetBitXor,
		OpSetShiftL,
		OpSetShiftR,
		OpTypeArray,
		OpArrayIndex,
	};

	static constexpr const char8* const NODE_TYPE_NAMES[] = {
		"[unknown]",
		"Program",
		"CompositeInitializer",
		"ArrayInitializer",
		"Where",
		"Definition",
		"Block",
		"If",
		"For",
		"ForEach",
		"Switch",
		"Case",
		"Func",
		"Catch",
		"ValIdentifer",
		"ValInteger",
		"ValFloat",
		"ValChar",
		"ValString",
		"Call",
		"UOpTypeTailArray",
		"UOpTypeSlice",
		"UOpTypeMultiPtr",
		"UOpTypeOptMultiPtr",
		"UOpTry",
		"UOpDefer",
		"UOpAddr",
		"UOpDeref",
		"UOpBitNot",
		"UOpLogNot",
		"UOpTypeOptPtr",
		"UOpTypeVar",
		"OpImpliedMember",
		"UOpTypePtr",
		"UOpNegate",
		"UOpPos",
		"OpAdd",
		"OpSub",
		"OpMul",
		"OpDiv",
		"OpAddTC",
		"OpSubTC",
		"OpMulTC",
		"OpMod",
		"OpBitAnd",
		"OpBitOr",
		"OpBitXor",
		"OpShiftL",
		"OpShiftR",
		"OpLogAnd",
		"OpLogOr",
		"OpMember",
		"OpCmpLT",
		"OpCmpGT",
		"OpCmpLE",
		"OpCmpGE",
		"OpCmpNE",
		"OpCmpEQ",
		"OpSet",
		"OpSetAdd",
		"OpSetSub",
		"OpSetMul",
		"OpSetDiv",
		"OpSetAddTC",
		"OpSetSubTC",
		"OpSetMulTC",
		"OpSetMod",
		"OpSetBitAnd",
		"OpSetBitOr",
		"OpSetBitXor",
		"OpSetShiftL",
		"OpSetShiftR",
		"OpTypeArray",
		"OpArrayIndex",
	};

	static constexpr const char8* node_type_name(NodeType type)
	{
		if (static_cast<u8>(type) < array_count(NODE_TYPE_NAMES))
			return NODE_TYPE_NAMES[static_cast<u8>(type)];

		return NODE_TYPE_NAMES[0];
	}
}

#endif // AST_COMMON_INCLUDE_GUARD

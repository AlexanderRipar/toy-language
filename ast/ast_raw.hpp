#ifndef AST_RAW_INCLUDE_GUARD
#define AST_RAW_INCLUDE_GUARD

#include "../common.hpp"

namespace ast::raw
{
	enum class Type : u8
	{
		INVALID = 0,
		Program,
		CompositeInitializer,
		ArrayInitializer,
		Wildcard,
		Where,
		Expects,
		Ensures,
		Definition,
		Block,
		If,
		For,
		ForEach,
		Switch,
		Case,
		Func,
		Trait,
		Impl,
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

	enum class Flag : u8
	{
		EMPTY                = 0,

		Definition_IsPub     = 0x01,
		Definition_IsMut     = 0x02,
		Definition_IsGlobal  = 0x04,
		Definition_IsAuto    = 0x08,
		Definition_IsUse     = 0x10,
		Definition_HasType   = 0x20,

		If_HasWhere          = 0x01,
		If_HasElse           = 0x02,

		For_HasWhere         = 0x01,
		For_HasStep          = 0x02,
		For_HasFinally       = 0x04,

		ForEach_HasWhere     = 0x01,
		ForEach_HasIndex     = 0x02,
		ForEach_HasFinally   = 0x04,

		Switch_HasWhere      = 0x01,

		Func_HasExpects      = 0x01,
		Func_HasEnsures      = 0x02,
		Func_IsProc          = 0x04,
		Func_HasReturnType   = 0x08,
		Func_HasBody         = 0x10,

		Trait_HasExpects     = 0x01,

		Impl_HasExpects      = 0x01,

		Catch_HasDefinition  = 0x01,
	};

	Flag operator|(Flag lhs, Flag rhs) noexcept
	{
		return static_cast<Flag>(static_cast<u8>(lhs) | static_cast<u8>(rhs));
	}

	Flag operator&(Flag lhs, Flag rhs) noexcept
	{
		return static_cast<Flag>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
	}

	Flag& operator|=(Flag& lhs, Flag rhs) noexcept
	{
		lhs = lhs | rhs;

		return lhs;
	}

	Flag& operator&=(Flag& lhs, Flag rhs) noexcept
	{
		lhs = lhs & rhs;

		return lhs;
	}

	struct NodeHeader
	{
		Type type;

		u8 data_dwords : 2;

		u8 flags : 6;

		u16 child_count;

		u32 next_sibling_offset;
	};

	struct Tree
	{
	private:

		NodeHeader* const m_begin;

		const u32 m_length;

	public:

		Tree(NodeHeader* begin, u32 length) noexcept :
			m_begin{ begin },
			m_length{ length }
		{}

		Range<NodeHeader> raw_nodes() const noexcept
		{
			return Range{ m_begin, m_length };
		}

		const NodeHeader* root() const noexcept
		{
			return m_begin;
		}
	};

	static constexpr inline const char8* const NODE_TYPE_NAMES[] = {
		"[unknown]",
		"Program",
		"CompositeInitializer",
		"ArrayInitializer",
		"Wildcard",
		"Where",
		"Expects",
		"Ensures",
		"Definition",
		"Block",
		"If",
		"For",
		"ForEach",
		"Switch",
		"Case",
		"Func",
		"Trait",
		"Impl",
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

	static constexpr inline const char8* type_name(Type type)
	{
		if (static_cast<u8>(type) < array_count(NODE_TYPE_NAMES))
			return NODE_TYPE_NAMES[static_cast<u8>(type)];

		return NODE_TYPE_NAMES[0];
	}
}

#endif // AST_RAW_INCLUDE_GUARD

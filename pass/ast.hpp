#ifndef AST_INCLUDE_GUARD
#define AST_INCLUDE_GUARD

#include <cstring>

#include "../infra/common.hpp"
#include "../infra/range.hpp"

namespace ast
{
	enum class Tag : u8
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
		UOpEval,
		UOpTry,
		UOpDefer,
		UOpAddr,
		UOpDeref,
		UOpBitNot,
		UOpLogNot,
		UOpTypeOptPtr,
		UOpTypeVar,
		UOpImpliedMember,
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
		MAX,
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

		If_HasWhere          = 0x20,
		If_HasElse           = 0x01,

		For_HasWhere         = 0x20,
		For_HasStep          = 0x01,
		For_HasFinally       = 0x02,

		ForEach_HasWhere     = 0x20,
		ForEach_HasIndex     = 0x01,
		ForEach_HasFinally   = 0x02,

		Switch_HasWhere      = 0x20,

		Func_HasExpects      = 0x01,
		Func_HasEnsures      = 0x02,
		Func_IsProc          = 0x04,
		Func_HasReturnType   = 0x08,
		Func_HasBody         = 0x10,

		Trait_HasExpects     = 0x01,

		Impl_HasExpects      = 0x01,

		Catch_HasDefinition  = 0x01,
	};

	inline Flag operator|(Flag lhs, Flag rhs) noexcept
	{
		return static_cast<Flag>(static_cast<u8>(lhs) | static_cast<u8>(rhs));
	}

	inline Flag operator&(Flag lhs, Flag rhs) noexcept
	{
		return static_cast<Flag>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
	}

	inline Flag& operator|=(Flag& lhs, Flag rhs) noexcept
	{
		lhs = lhs | rhs;

		return lhs;
	}

	inline Flag& operator&=(Flag& lhs, Flag rhs) noexcept
	{
		lhs = lhs & rhs;

		return lhs;
	}

	struct Node
	{
		static constexpr u8 FLAGS_BITS = 6;

		static constexpr u8 DATA_DWORDS_BITS = 2;

		static constexpr u8 TYPE_TAG_BITS = 5;

		static constexpr u8 TYPE_INDEX_BITS = 27;

		Tag tag;

		u8 flags : FLAGS_BITS;

		u8 data_dwords : DATA_DWORDS_BITS;

		u16 child_count;

		u32 next_sibling_offset;

		u32 type_index;

		ast::Node* next_sibling() noexcept
		{
			return reinterpret_cast<ast::Node*>(reinterpret_cast<u32*>(this) + next_sibling_offset);
		}

		const ast::Node* next_sibling() const noexcept
		{
			return reinterpret_cast<const ast::Node*>(reinterpret_cast<const u32*>(this) + next_sibling_offset);
		}

		ast::Node* first_child() noexcept
		{
			return reinterpret_cast<ast::Node*>(reinterpret_cast<u32*>(this + 1) + data_dwords);
		}

		const ast::Node* first_child() const noexcept
		{
			return reinterpret_cast<const ast::Node*>(reinterpret_cast<const u32*>(this + 1) + data_dwords);
		}

		template<typename T>
		T* data() noexcept
		{
			static_assert(alignof(T) <= alignof(Node));

			ASSERT_OR_IGNORE(data_dwords * sizeof(u32) >= sizeof(T));

			return reinterpret_cast<T*>(this + 1);
		}

		template<typename T>
		const T* data() const noexcept
		{
			static_assert(alignof(T) <= alignof(Node));

			ASSERT_OR_IGNORE(data_dwords * sizeof(u32) >= sizeof(T));

			return reinterpret_cast<const T*>(this + 1);
		}
	};

	static_assert(sizeof(Node) == 12);

	namespace data
	{
		struct Program
		{
			static constexpr Tag TAG = Tag::Program;

			u32 symbol_table_index;
		};

		struct Definition
		{
			static constexpr Tag TAG = Tag::Definition;

			u32 identifier_index;
		};

		struct ValIdentifier
		{
			static constexpr Tag TAG = Tag::ValIdentifer;

			u32 identifier_index;
		};

		struct ValString
		{
			static constexpr Tag TAG = Tag::ValString;

			u32 string_index;
		};

		struct ValInteger
		{
			static constexpr Tag TAG = Tag::ValInteger;

			u32 halves[2];

			u64 get() const noexcept
			{
				u64 value;
				
				memcpy(&value, halves, sizeof(value));

				return value;
			}

			void set(u64 value) noexcept
			{
				memcpy(halves, &value, sizeof(value));
			}
		};

		struct ValFloat
		{
			static constexpr Tag TAG = Tag::ValFloat;

			u32 halves[2];

			f64 get() const noexcept
			{
				f64 value;
				
				memcpy(&value, halves, sizeof(value));

				return value;
			}

			void set(f64 value) noexcept
			{
				memcpy(halves, &value, sizeof(value));
			}
		};

		struct ValChar
		{
			static constexpr Tag TAG = Tag::ValChar;

			u32 codepoint;
		};
	}

	struct Tree
	{
	private:

		Node* const m_begin;

		const u32 m_length;

	public:

		Tree(Node* begin, u32 length) noexcept :
			m_begin{ begin },
			m_length{ length }
		{}

		Range<Node> raw_nodes() const noexcept
		{
			return Range{ m_begin, m_length };
		}

		Node* root() noexcept
		{
			return m_begin;
		}

		const Node* root() const noexcept
		{
			return m_begin;
		}
	};

	constexpr inline const char8* const NODE_TYPE_NAMES[] = {
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
		"UOpEval",
		"UOpTry",
		"UOpDefer",
		"UOpAddr",
		"UOpDeref",
		"UOpBitNot",
		"UOpLogNot",
		"UOpTypeOptPtr",
		"UOpTypeVar",
		"UOpImpliedMember",
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

	constexpr inline const char8* tag_name(Tag type)
	{
		if (static_cast<u8>(type) < array_count(NODE_TYPE_NAMES))
			return NODE_TYPE_NAMES[static_cast<u8>(type)];

		return NODE_TYPE_NAMES[0];
	}

	inline bool has_flag(const Node* node, Flag wanted) noexcept
	{
		return (static_cast<Flag>(node->flags) & wanted) == wanted;
	}
}

#endif // AST_INCLUDE_GUARD

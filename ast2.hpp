#ifndef AST2_INCLUDE_GUARD
#define AST2_INCLUDE_GUARD

#include "infra/common.hpp"
#include "infra/container.hpp"
#include "infra/optptr.hpp"

struct AstPool;

namespace a2
{
	static constexpr s32 MAX_TREE_DEPTH = 128;

	enum class AstTag : u8
	{
		INVALID = 0,
		Builtin,
		File,
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
		Return,
		Leave,
		Yield,
		ParameterList,
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

	enum class AstFlag : u8
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

		Type_IsMut           = 0x02,
	};

	inline AstFlag operator|(AstFlag lhs, AstFlag rhs) noexcept
	{
		return static_cast<AstFlag>(static_cast<u8>(lhs) | static_cast<u8>(rhs));
	}

	inline AstFlag operator&(AstFlag lhs, AstFlag rhs) noexcept
	{
		return static_cast<AstFlag>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
	}

	inline AstFlag& operator|=(AstFlag& lhs, AstFlag rhs) noexcept
	{
		lhs = lhs | rhs;

		return lhs;
	}

	inline AstFlag& operator&=(AstFlag& lhs, AstFlag rhs) noexcept
	{
		lhs = lhs & rhs;

		return lhs;
	}

	struct AstNode
	{
		static constexpr u8 FLAG_LAST_SIBLING  = 0x01;
		static constexpr u8 FLAG_FIRST_SIBLING = 0x02;
		static constexpr u8 FLAG_NO_CHILDREN   = 0x04;

		AstTag tag;

		AstFlag flags;

		u8 data_dwords;

		u8 internal_flags;

		u32 next_sibling_offset;
	};



	static inline AstNode* apply_offset_(AstNode* node, ureg offset) noexcept
	{
		static_assert(sizeof(AstNode) % sizeof(u32) == 0 && alignof(AstNode) % sizeof(u32) == 0);

		return reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(node) + offset);
	}

	static inline bool has_children(const AstNode* node) noexcept
	{
		return (node->internal_flags & AstNode::FLAG_NO_CHILDREN) == 0;
	}

	static inline bool has_next_sibling(const AstNode* node) noexcept
	{
		return (node->internal_flags & AstNode::FLAG_LAST_SIBLING) == 0;
	}

	static inline bool has_flag(AstNode* node, AstFlag flag) noexcept
	{
		return (static_cast<u8>(node->flags) & static_cast<u8>(flag)) != 0;
	}

	static inline AstNode* next_sibling_of(AstNode* node) noexcept
	{
		ASSERT_OR_IGNORE(has_next_sibling(node));

		return apply_offset_(node, node->next_sibling_offset);
	}

	static inline AstNode* first_child_of(AstNode* node) noexcept
	{
		ASSERT_OR_IGNORE(has_children(node));

		return apply_offset_(node, node->data_dwords);
	}

	template<typename T>
	static inline T* attachment_of(AstNode* node) noexcept
	{
		ASSERT_OR_IGNORE(T::TAG == node->tag);

		ASSERT_OR_IGNORE(sizeof(T) + sizeof(AstNode) == node->data_dwords * sizeof(u32));

		return reinterpret_cast<T*>(node + 1);
	}

	template<typename T>
	static inline const T* attachment_of(const AstNode* node) noexcept
	{
		ASSERT_OR_IGNORE(T::TAG == node->tag);

		ASSERT_OR_IGNORE(sizeof(T) + sizeof(AstNode) == node->data_dwords * sizeof(u32));

		return reinterpret_cast<const T*>(node + 1);
	}



	struct IterationResult
	{
		AstNode* node;

		u32 depth;
	};

	static inline bool is_valid(IterationResult result) noexcept
	{
		return result.node != nullptr;
	}



	struct DirectChildIterator
	{
		AstNode* curr;
	};

	static inline DirectChildIterator direct_children_of(AstNode* node) noexcept
	{
		return { has_children(node) ? first_child_of(node) : nullptr };
	}

	static inline OptPtr<AstNode> next(DirectChildIterator* iterator) noexcept
	{
		if (iterator->curr == nullptr)
			return none<AstNode>();

		AstNode* const curr = iterator->curr;

		iterator->curr = has_next_sibling(curr) ? next_sibling_of(curr) : nullptr;

		return some(curr);
	}

	static inline OptPtr<AstNode> peek(const DirectChildIterator* iterator) noexcept
	{
		return maybe(iterator->curr);
	}



	struct PreorderIterator
	{
		AstNode* curr;

		u8 depth;

		s32 top;

		u8 prev_depths[MAX_TREE_DEPTH];

		static_assert(MAX_TREE_DEPTH <= UINT8_MAX);
	};

	static inline PreorderIterator preorder_ancestors_of(AstNode* node) noexcept
	{
		PreorderIterator iterator;

		if (has_children(node))
		{
			iterator.curr = first_child_of(node);
			iterator.depth = 0;
			iterator.top = -1;
		}
		else
		{
			iterator.curr = nullptr;
			iterator.depth = 0;
			iterator.top = -1;
		}

		return iterator;
	}

	static inline IterationResult next(PreorderIterator* iterator) noexcept
	{
		if (iterator->curr == nullptr)
			return { nullptr, 0 };

		IterationResult result = { iterator->curr, iterator->depth };

		AstNode* const curr = iterator->curr;

		iterator->curr = apply_offset_(curr, curr->data_dwords);

		if ((curr->internal_flags & AstNode::FLAG_NO_CHILDREN) == 0)
		{
			if ((curr->internal_flags & AstNode::FLAG_LAST_SIBLING) == 0)
			{
				ASSERT_OR_IGNORE(iterator->top + 1 < MAX_TREE_DEPTH);

				iterator->top += 1;

				iterator->prev_depths[iterator->top] = iterator->depth;
			}

			ASSERT_OR_IGNORE(iterator->depth + 1 < MAX_TREE_DEPTH);

			iterator->depth += 1;
		}
		else if ((curr->internal_flags & AstNode::FLAG_LAST_SIBLING) == AstNode::FLAG_LAST_SIBLING)
		{
			if (iterator->top == -1)
			{
				iterator->curr = nullptr;
			}
			else
			{
				iterator->depth = iterator->prev_depths[iterator->top];

				iterator->top -= 1;
			}
		}

		return result;
	}

	static inline IterationResult peek(const PreorderIterator* iterator) noexcept
	{
		return { iterator->curr, iterator->depth };
	}



	struct PostorderIterator
	{
		AstNode* base;

		s32 depth;

		u32 offsets[MAX_TREE_DEPTH];
	};

	static inline PostorderIterator postorder_ancestors_of(AstNode* node) noexcept
	{
		PostorderIterator iterator;

		iterator.base = node;
		iterator.depth = -1;

		while (has_children(node))
		{
			ASSERT_OR_IGNORE(iterator.depth < MAX_TREE_DEPTH);

			node = first_child_of(node);

			iterator.depth += 1;

			iterator.offsets[iterator.depth] = static_cast<u32>(reinterpret_cast<u32*>(node) - reinterpret_cast<u32*>(iterator.base));
		}

		return iterator;
	}

	static inline IterationResult next(PostorderIterator* iterator) noexcept
	{
		if (iterator->depth < 0)
			return { nullptr, 0 };

		AstNode* const ret_node = reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(iterator->base) + iterator->offsets[iterator->depth]);

		const u32 ret_depth = static_cast<u32>(iterator->depth);

		AstNode* curr = ret_node;

		if (has_next_sibling(curr))
		{
			curr = next_sibling_of(curr);

			iterator->offsets[iterator->depth] = static_cast<u32>(reinterpret_cast<u32*>(curr) - reinterpret_cast<u32*>(iterator->base));

			while (has_children(curr))
			{
				curr = first_child_of(curr);

				iterator->depth += 1;

				ASSERT_OR_IGNORE(iterator->depth < MAX_TREE_DEPTH);

				iterator->offsets[iterator->depth] = static_cast<u32>(reinterpret_cast<u32*>(curr) - reinterpret_cast<u32*>(iterator->base));
			}
		}
		else
		{
			iterator->depth -= 1;

			if (iterator->depth >= 0)
				curr = reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(iterator->base) + iterator->offsets[iterator->depth]);
		}

		return { ret_node, ret_depth };
	}

	static inline IterationResult peek(const PostorderIterator* iterator) noexcept
	{
		if (iterator->depth == -1)
			return { nullptr, 0 };

		return { apply_offset_(iterator->base, iterator->offsets[iterator->depth]), static_cast<u32>(iterator->depth) };
	}



	struct AstBuilderToken
	{
		u32 rep;
	};

	struct AstBuilder
	{
		static constexpr AstBuilderToken NO_CHILDREN = { ~0u };

		ReservedVec<u32> scratch;
	};

	static inline bool operator==(AstBuilderToken lhs, AstBuilderToken rhs) noexcept
	{
		return lhs.rep == rhs.rep;
	}
	
	static inline bool operator!=(AstBuilderToken lhs, AstBuilderToken rhs) noexcept
	{
		return lhs.rep != rhs.rep;
	}
	
	static inline AstBuilder create_ast_builder() noexcept
	{
		AstBuilder builder;

		builder.scratch.init(1u << 31, 1u << 18);

		return builder;
	}

	static inline AstBuilderToken push_node(AstBuilder* builder, AstBuilderToken first_child, AstTag tag, AstFlag flags) noexcept
	{
		static_assert(sizeof(AstNode) % sizeof(u32) == 0);

		AstNode* const node = reinterpret_cast<AstNode*>(builder->scratch.reserve_exact(sizeof(AstNode)));

		node->next_sibling_offset = first_child.rep;
		node->tag = tag;
		node->flags = flags;
		node->data_dwords = sizeof(AstNode) / sizeof(u32);
		node->internal_flags = first_child == AstBuilder::NO_CHILDREN ? AstNode::FLAG_NO_CHILDREN : 0;

		return { static_cast<u32>(reinterpret_cast<u32*>(node) - builder->scratch.begin()) };
	}

	template<typename T>
	static inline AstBuilderToken push_node(AstBuilder* builder, AstBuilderToken first_child, AstFlag flags, T attachment) noexcept
	{
		static_assert(sizeof(AstNode) % sizeof(u32) == 0);
		
		static_assert(sizeof(T) % sizeof(u32) == 0);

		const u32 required_dwords = (sizeof(AstNode) + sizeof(T)) / sizeof(u32);

		AstNode* const node = reinterpret_cast<AstNode*>(builder->scratch.reserve_exact(required_dwords * sizeof(u32)));

		node->next_sibling_offset = first_child.rep;
		node->tag = T::TAG;
		node->flags = flags;
		node->data_dwords = required_dwords;
		node->internal_flags = first_child == AstBuilder::NO_CHILDREN ? AstNode::FLAG_NO_CHILDREN : 0;

		memcpy(node + 1, &attachment, sizeof(T));

		return { static_cast<u32>(reinterpret_cast<u32*>(node) - builder->scratch.begin()) };
	}

	AstNode* complete_ast(AstBuilder* builder, AstPool* dst) noexcept;



	const char8* ast_tag_name(AstTag tag) noexcept;
}

#endif // AST2_INCLUDE_GUARD

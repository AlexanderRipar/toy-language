#ifndef AST_RAW_INCLUDE_GUARD
#define AST_RAW_INCLUDE_GUARD

#include "../common.hpp"
#include "ast_common.hpp"

namespace ast::raw
{
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

		Func_IsProc          = 0x01,
		Func_HasReturnType   = 0x02,
		Func_HasWhere        = 0x04,
		Func_HasBody         = 0x08,

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
		NodeType type;

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

	struct TreeBuilder
	{
		ReservedVec<u32>* m_buffer;

		ReservedVec<u32>* m_stack;

		NodeHeader* append(NodeType type, u16 child_count, Flag flags, u8 data_dwords) noexcept
		{
			ASSERT_OR_IGNORE(static_cast<u8>(flags) < 64);

			ASSERT_OR_IGNORE(data_dwords < 3 || (child_count == 0 && data_dwords < 4));

			NodeHeader* const node = static_cast<NodeHeader*>(m_buffer->reserve_exact(sizeof(NodeHeader) + (data_dwords + static_cast<u8>(child_count != 0)) * sizeof(u32)));

			node->type = type;

			node->data_dwords = data_dwords;

			node->flags = static_cast<u8>(flags);

			node->child_count = child_count;

			if (child_count != 0)
			{
				u32 child_index = m_stack->begin()[m_stack->used() - child_count];

				reinterpret_cast<u32*>(node)[2 + data_dwords] = static_cast<u32>(reinterpret_cast<u32*>(node) - (m_buffer->begin() + child_index));

				for (u16 i = 1; i != child_count; ++i)
				{
					NodeHeader* const child = reinterpret_cast<NodeHeader*>(m_buffer->begin() + child_index);

					const u32 next_child_index = m_stack->begin()[m_stack->used() - child_count + i];

					child->next_sibling_offset = next_child_index - child_index;

					child_index = next_child_index;
				}

				m_stack->pop(child_count);
			}

			m_stack->append(static_cast<u32>(reinterpret_cast<u32*>(node) - m_buffer->begin()));

			return node;
		}

		static void reverse_node(ReservedVec<u32>* target, const NodeHeader* src) noexcept
		{
			NodeHeader* const dst = reinterpret_cast<NodeHeader*>(target->reserve_exact(sizeof(NodeHeader) + src->data_dwords * sizeof(u32)));

			memcpy(dst, src, sizeof(NodeHeader) + src->data_dwords * sizeof(u32));

			if (src->child_count != 0)
			{
				const u32 offset = reinterpret_cast<const u32*>(src)[2 + src->data_dwords];

				reverse_node(target, reinterpret_cast<const NodeHeader*>(reinterpret_cast<const u32*>(src) - offset));
			}

			if (src->next_sibling_offset != 0)
				reverse_node(target, reinterpret_cast<const NodeHeader*>(reinterpret_cast<const u32*>(src) + src->next_sibling_offset));
		}

	public:

		TreeBuilder() noexcept = default;

		TreeBuilder(ReservedVec<u32>* buffer, ReservedVec<u32>* stack) noexcept :
			m_buffer{ buffer },
			m_stack{ stack }
		{}

		NodeHeader* append(NodeType type, u16 child_count, Flag flags = Flag::EMPTY) noexcept
		{
			return append(type, child_count, flags, 0);
		}

		template<typename T>
		NodeHeader* append(T** out_data, u16 child_count, Flag flags = Flag::EMPTY) noexcept
		{
			static_assert(alignof(T) <= alignof(NodeHeader));

			NodeHeader* const node = append(T::TYPE, child_count, flags, (sizeof(T) + sizeof(u32) - 1) / sizeof(u32));

			*out_data = reinterpret_cast<T*>(node + 1);

			return node;
		}

		Tree build(ReservedVec<u32>* target) noexcept
		{
			ASSERT_OR_IGNORE(m_stack->used() == 1);

			const u32 begin = target->used();

			const NodeHeader* const header = reinterpret_cast<NodeHeader*>(m_buffer->begin() + *m_stack->begin());

			reverse_node(target, header);

			return Tree{ reinterpret_cast<NodeHeader*>(target->begin() + begin), target->used() - begin };

		}
	};
}

#endif // AST_RAW_INCLUDE_GUARD

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
		Definition_HasType   = 0x08,
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

	struct alignas(4) NodeHeader
	{
		NodeType type;

		u8 child_count;

		u8 data_dwords;

		Flag flags;
	};

	static_assert(sizeof(NodeHeader) == 4 && alignof(NodeHeader) == 4);

	namespace attach
	{
		struct DefinitionData
		{
			static constexpr NodeType TYPE = NodeType::Definition;

			u32 identifier_id;
		};

		struct ValIdentifierData
		{
			static constexpr NodeType TYPE = NodeType::ValIdentifer;

			u32 identifier_id;
		};

		struct ValIntegerData
		{
			static constexpr NodeType TYPE = NodeType::ValInteger;

			u32 value_halves[2];

			u64 get() const noexcept
			{
				u64 value;
	
				static_assert(sizeof(value_halves) == sizeof(value));

				memcpy(&value, value_halves, sizeof(value));

				return value;
			}

			void set(u64 value) noexcept
			{
				static_assert(sizeof(value_halves) == sizeof(value));

				memcpy(value_halves, &value, sizeof(value));
			}
		};

		struct ValFloatData
		{
			static constexpr NodeType TYPE = NodeType::ValFloat;

			u32 value_halves[2];

			f64 get() const noexcept
			{
				f64 value;
	
				static_assert(sizeof(value_halves) == sizeof(value));

				memcpy(&value, value_halves, sizeof(value));

				return value;
			}

			void set(f64 value) noexcept
			{
				static_assert(sizeof(value_halves) == sizeof(value));

				memcpy(value_halves, &value, sizeof(value));
			}
		};

		struct ValCharData
		{
			static constexpr NodeType TYPE = NodeType::ValChar;

			u32 codepoint;
		};

		struct ValStringData
		{
			static constexpr NodeType TYPE = NodeType::ValString;

			u32 string_id;
		};
	}

	struct Tree
	{
	private:

		NodeHeader* const m_begin;

		const u32 m_length;

		const u32 m_commit;

	public:

		Tree(NodeHeader* begin, u32 length, u32 commit) noexcept :
			m_begin{ begin },
			m_length{ length },
			m_commit{ commit } {}

		void release() noexcept
		{
			minos::decommit(m_begin, m_commit * 8);
		}
	};

	struct TreeBuilder : public TreeBuilderBase<NodeHeader>
	{
		TreeBuilder() noexcept = default;

		TreeBuilder(NodeHeader* memory, u32 reserve, u32 commit_increment) noexcept :
			TreeBuilderBase{ memory, reserve, commit_increment }
		{}

		NodeHeader* append(NodeType type, u8 child_count, Flag flags = Flag::EMPTY, u8 data_dwords = 0) noexcept
		{
			ensure_capacity(1 + data_dwords);

			NodeHeader* const node = m_memory + m_used;

			node->type = type;

			node->child_count = child_count;

			node->data_dwords = data_dwords;

			node->flags = flags;

			m_used += 1 + data_dwords;

			return node;
		}

		template<typename T>
		NodeHeader* append(T** out_data, u8 child_count, Flag flags = Flag::EMPTY) noexcept
		{
			static_assert(alignof(T) <= alignof(NodeHeader));

			NodeHeader* const node = append(T::TYPE, child_count, flags, (sizeof(T) + sizeof(NodeHeader) - 1) / sizeof(NodeHeader));

			*out_data = reinterpret_cast<T*>(node + 1);

			return node;
		}

		Tree build() noexcept
		{
			return Tree{ m_memory, m_used, m_commit };
		}
	};
}

#endif // AST_RAW_INCLUDE_GUARD

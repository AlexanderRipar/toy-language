#ifndef AST_COMMON_INCLUDE_GUARD
#define AST_COMMON_INCLUDE_GUARD

#include "../common.hpp"

namespace ast
{
	enum class NodeType : u8
	{
		INVALID = 0,
		Call,
		OpTypeArray,
		OpArrayIndex,
		CompositeInitializer,
		ArrayInitializer,

		Definition,

		ValIdentifer,
		ValInteger,
		ValFloat,
		ValChar,
		ValString,

		UOpTypTailArray,
		UOpTypeSlice,
		UOpTypeMultiPtr,
		UOpTry,
		UOpDefer,
		UOpAddr,
		UOpDeref,
		UOpBitNot,
		UOpLogNot,
		UOpTypOptPtr,
		UOpTypVar,
		OpImpliedMember,
		UOpTypPtr,
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
		OpCatch,
	};

	/*
	struct alignas(8) NodeHeader
	{
		static constexpr u32 EMPTY_NEXT_OFFSET = 0;

		static constexpr u16 EMPTY_CHILD_OFFSET = 0;

		u32 next_offset;

		u16 child_offset;

		NodeType type;

		u8 flags : 7;

		u8 is_last: 1;
	};

	static_assert(sizeof(NodeHeader) == 8 && alignof(NodeHeader) == 8);
	*/

	/*
	template<typename Ref>
	struct NodeIterator
	{
	private:

		Ref m_curr;

	public:

		NodeIterator(Ref ref) noexcept : m_curr{ ref } {}

		bool operator!=([[maybe_unused]] NodeIterator<Ref> rhs) const noexcept
		{
			return m_curr.has_next();
		}

		bool operator++() noexcept
		{
			m_curr = m_curr.next();
		}

		Ref operator*() const noexcept
		{
			return m_curr;
		}
	};

	struct NodeRefBase
	{
	protected:

		const NodeHeader* const m_ref;

	public:

		NodeRefBase(const NodeHeader* header) noexcept : m_ref{ header } {}

		bool has_next() const noexcept
		{
			return m_ref->next_offset != NodeHeader::EMPTY_NEXT_OFFSET;
		}

		bool has_children() const noexcept
		{
			return m_ref->child_offset != NodeHeader::EMPTY_CHILD_OFFSET;
		}

		NodeType type() const noexcept
		{
			return m_ref->type;
		}

		const void* data() const noexcept
		{
			return m_ref + 1;
		}

		template<typename T>
		const T* data() const noexcept
		{
			return reinterpret_cast<T*>(m_ref + 1);
		}

		template<typename T>
		T flags() noexcept
		{
			return static_cast<T>(m_ref->flags);
		}

		const NodeHeader* raw_header() const noexcept
		{
			return m_ref;
		}
	};

	struct MutNodeRefBase
	{
	protected:

		NodeHeader* const m_ref;

	public:

		MutNodeRefBase(NodeHeader* ref) noexcept : m_ref{ ref } {}

		bool has_next() const noexcept
		{
			return m_ref->next_offset != NodeHeader::EMPTY_NEXT_OFFSET;
		}

		bool has_children() const noexcept
		{
			return m_ref->child_offset != NodeHeader::EMPTY_CHILD_OFFSET;
		}

		NodeType type() const noexcept
		{
			return m_ref->type;
		}

		const void* data() const noexcept
		{
			return m_ref + 1;
		}

		void* data() noexcept
		{
			return m_ref + 1;
		}

		template<typename T>
		const T* data() const noexcept
		{
			return static_cast<const T*>(data());
		}

		template<typename T>
		T* data() noexcept
		{
			return static_cast<T*>(data());
		}

		template<typename T>
		T flags() const noexcept
		{
			return static_cast<T>(m_ref->flags);
		}

		template<typename T>
		bool set_flag(T flag) noexcept
		{
			const bool already_set = has_flag(flag);

			const u8 bit = static_cast<u8>(flag);

			ASSERT_OR_IGNORE(is_pow2(bit) && bit <= 0x7F);

			m_ref->flags |= bit;

			return already_set;
		}

		template<typename T>
		bool unset_flag(T flag) noexcept
		{
			const bool already_set = has_flag(flag);

			const u8 bit = static_cast<u8>(flag);

			ASSERT_OR_IGNORE(is_pow2(bit) && bit <= 0x7F);

			m_ref->flags &= ~bit;

			return already_set;
		}

		template<typename T>
		void set_flags(T flags) noexcept
		{
			ASSERT_OR_IGNORE(static_cast<u8>(flags) <= 0x7F);

			m_ref->flags = static_cast<u8>(flags);
		}

		const u32 data_qwords() const noexcept
		{
			return m_ref->child_offset;
		}

		const NodeHeader* raw_header() const noexcept
		{
			return m_ref;
		}

		NodeHeader* raw_header() noexcept
		{
			return m_ref;
		}
	};
	*/

	template<typename Header>
	struct TreeBuilderBase
	{
	protected:

		Header* m_memory;

		u32 m_used;

		u32 m_commit;

		u32 m_commit_increment;

		u32 m_reserve;

		u32 m_last;

		void ensure_capacity(u32 extra_headers) noexcept
		{
			const u32 required_commit = m_used + extra_headers;

			if (required_commit <= m_commit)
				return;

			if (required_commit > m_reserve)
				panic("Could not allocate additional memory for AstBuilder, as the required memory %u exceeds the reserve of %u header sizes\n", required_commit, m_reserve);

			const u32 new_commit = next_multiple(required_commit, m_commit_increment);

			if (!minos::commit(m_memory + m_commit, static_cast<uint>(new_commit - m_commit) * sizeof(Header)))
				panic("Could not allocate additional memory for AstBuilder (0x%X)\n", minos::last_error());

			m_commit = new_commit;
		}

	public:

		TreeBuilderBase() noexcept = default;

		TreeBuilderBase(Header* memory, u32 reserve, u32 commit_increment) noexcept :
			m_memory{ memory },
			m_used{ 0 },
			m_commit{ commit_increment },
			m_commit_increment{ commit_increment },
			m_reserve{ reserve }
		{
			if (!minos::commit(memory, static_cast<uint>(commit_increment) * sizeof(Header)))
				panic("Could not commit initial memory for AstBuilder (0x%X)\n", minos::last_error());
		}

		Header* begin() noexcept
		{
			return m_memory;
		}

		u32 length() const noexcept
		{
			return m_used;
		}

		u32 commit() const noexcept
		{
			return m_commit;
		}
	};
}

#endif // AST_COMMON_INCLUDE_GUARD

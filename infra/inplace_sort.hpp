#ifndef INPLACE_SORT_INCLUDE_GUARD
#define INPLACE_SORT_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"

namespace inplace_sort_impl
{
	static inline u64 heap_parent(u64 index) noexcept
	{
		return (index - 1) >> 1;
	}

	static inline u64 heap_child(u64 index) noexcept
	{
		return (index << 1) + 1;
	}

	template<typename T, typename Comparator>
	static inline void heap_sift(u64 count, T* begin, u64 root) noexcept
	{
		u64 child = heap_child(root);

		while (child < count)
		{
			// Select the largest child. If we are looking at the very last
			// element, skip this step and use it (as the only child).
			if (child + 1 < count && Comparator::compare(begin[child], begin[child + 1]) < 0)
				child += 1;

			// If the larger child is already smaller than the root, we are
			// done.
			if (Comparator::compare(begin[root], begin[child]) >= 0)
				return;

			// Swap the larger child with the root and recurse with the child
			// as the new root.
			const T tmp = begin[root];

			begin[root] = begin[child];

			begin[child] = tmp;

			root = child;

			child = heap_child(root);
		}
	}

	template<typename T, typename Comparator>
	static inline void make_heap(u64 count, T* begin) noexcept
	{
		for (u64 curr = heap_parent(count - 1); curr != 0; curr -= 1)
			heap_sift<T, Comparator>(count, begin, curr);
	}

	template<typename T, typename Comparator>
	static inline void insertion_sort(u64 count, T* begin) noexcept
	{
		for (u64 i = 1; i < count; i += 1)
		{
			u64 j = i;

			while (j > 0 && Comparator::compare(begin[j], begin[j - 1]) < 0)
			{
				const T tmp = begin[j];

				begin[j] = begin[j - 1];

				begin[j - 1] = tmp;

				j -= 1;
			}
		}
	}
}

template<typename T, typename Comparator, u64 short_cutoff = 8>
static inline void inplace_sort(MutRange<T> elems) noexcept
{
	T* const begin = elems.begin();

	const u64 count = elems.count();

	if (count <= short_cutoff)
	{
		inplace_sort_impl::insertion_sort<T, Comparator>(count, begin);
	}
	else
	{
		inplace_sort_impl::make_heap<T, Comparator>(count, begin);
	
		for (u64 i = count - 1; i > 1; i -= 1)
		{
			const T tmp = begin[i];

			begin[i] = begin[0];

			begin[0] = tmp;

			inplace_sort_impl::heap_sift<T, Comparator>(count, begin, i);
		}
	}
}

#endif // INPLACE_SORT_INCLUDE_GUARD

#ifndef UTIL_VEC_INCLUDE_GUARD
#define UTIL_VEC_INCLUDE_GUARD

#include <utility>
#include <new>
#include "util/types.hpp"

template<typename T, u16 Local_Capacity = sizeof(usz) * 2 / sizeof(T)>
struct vec
{
private:

	// Use the fact that windows does not use addresses below 0x10000 to encode the local count in the data pointer
	union
	{
		T* m_data = nullptr;
		
		usz m_local_used;
	};
	
	union
	{
		struct
		{
			usz m_capacity;

			usz m_used;
		};

		T m_local[Local_Capacity]{};
	};

public:

	~vec() noexcept
	{
		for (T* t = begin(); t != end(); ++t)
			t->~T();

		if (!is_local())
			free(m_data);
	}

	T& operator[](usz i) noexcept
	{
		return data()[i];
	}

	const T& operator[](usz i) const noexcept
	{
		return data()[i];
	}

	const T* begin() const noexcept
	{
		if (is_local())
			return m_local;

		return m_data;
	}

	const T* end() const noexcept
	{
		if (is_local())
			return m_local + m_local_used;
		
		return m_data + m_used;
	}

	T* begin() noexcept
	{
		if (is_local())
			return m_local;

		return m_data;
	}

	T* end() noexcept
	{
		if (is_local())
			return m_local + m_local_used;
		
		return m_data + m_used;
	}

	const T* data() const noexcept
	{
		if (is_local())
			return m_local;

		return m_data;
	}

	T* data() noexcept
	{
		if (is_local())
			return m_local;

		return m_data;
	}

	bool push_back(const T& t) noexcept
	{
		if (!ensure_capacity(1))
			return false;

		if (is_local())
			m_local[m_local_used++] = t;
		else
			m_data[m_used++] = t;

		return true;
	}

	bool push_back(T&& t) noexcept
	{
		if (!ensure_capacity(1))
			return false;

		if (is_local())
			m_local[m_local_used++] = std::move(t);
		else
			m_data[m_used++] = std::move(t);

		return true;
	}

	void pop() noexcept
	{
		if (is_local())
			m_local[--m_local_used].~T();
		else
			m_data[--m_used].~T();
	}

	void remove(usz index) noexcept
	{
		if (is_local())
		{
			m_local[index].~T();

			for (usz i = index + 1; i != m_local_used; ++i)
				m_local[i - 1] = std::move(m_local[i]);

			--m_local_used;
		}
		else
		{
			m_data[index].~T();

			for (usz i = index + 1; i != m_used; ++i)
				m_data[i - 1] = std::move(m_data[i]);

			--m_used;
		}
	}

	bool insert(usz index, const T& t) noexcept
	{
		if (!ensure_capacity(1))
			return false;

		if (is_local())
		{
			for (usz i = index; i != m_local_used; ++i)
				m_local[i + 1] = std::move(m_local[i]);

			m_local[index] = t;

			++m_local_used;
		}
		else
		{
			for (usz i = index; i != m_used; ++i)
				m_data[i + 1] = std::move(m_data[i]);

			m_data[index] = t;

			++m_used;
		}

		return true;
	}

	bool insert(usz index, T&& t) noexcept
	{
		if (!ensure_capacity(1))
			return false;

		if (is_local())
		{
			for (usz i = index; i != m_local_used; ++i)
				m_local[i + 1] = std::move(m_local[i]);

			m_local[index] = std::move(t);

			++m_local_used;
		}
		else
		{
			for (usz i = index; i != m_used; ++i)
				m_data[i + 1] = std::move(m_data[i]);

			m_data[index] = std::move(t);

			++m_used;
		}

		return true;
	}

	bool insert(usz index, usz count, const T* ts) noexcept
	{
		if (!ensure_capacity(count))
			return false;

		if (is_local())
		{
			for (usz i = index; i != m_local_used; ++i)
				m_local[i + count] = std::move(m_local[i]);

			for (usz i = 0; i != count; ++i)
				m_local[index + i] = ts[i];

			m_local_used += count;
		}
		else
		{
			for (usz i = index; i != m_used; ++i)
				m_data[i + count] = std::move(m_data[i]);

			for (usz i = 0; i != count; ++i)
				m_data[index + i] = ts[i];

			m_used += count;
		}

		return true;
	}

	T& first() noexcept
	{
		if (is_local())
			return *m_local;
		
		return *m_data;
	}

	T& last() noexcept
	{
		if (is_local())
			return m_local[m_local_used - 1];

		return m_data[m_used - 1];
	}

	const T& first() const noexcept
	{
		if (is_local())
			return *m_local;
		
		return *m_data;
	}

	const T& last() const noexcept
	{
		if (is_local())
			return m_local[m_local_used - 1];

		return m_data[m_used - 1];
	}

	usz size() const noexcept
	{
		if (is_local())
			return m_local_used;
		
		return m_used;
	}

	usz capacity() const noexcept
	{
		if (is_local())
			return Local_Capacity;
		
		return m_capacity;
	}

	void clear()
	{
		const usz s = size();

		T* d = data();

		for (usz i = 0; i != s; ++i)
			d[i].~T();

		if (!is_local())
			free(m_data);

		m_data = nullptr;
	}

	bool reserve(usz count) noexcept
	{
		return ensure_capacity(count);
	}

	void set_size(usz size) noexcept
	{
		if (is_local())
			m_local_used = size & 0xFFFF;
		else
			m_used = size;
	}

	vec() noexcept = default;

	vec(const vec<T, Local_Capacity>&) = delete;

	vec(vec<T, Local_Capacity>&& v) noexcept
	{
		memcpy(this, &v, sizeof(*this));

		memset(&v, 0, sizeof(*this))
	}

	vec<T, Local_Capacity>& operator=(vec<T, Local_Capacity>&& v) noexcept
	{
		clear();

		memcpy(this, &v, sizeof(*this));

		memset(&v, 0, sizeof(*this));

		return *this;
	}

private:

	bool ensure_capacity(usz extra) noexcept
	{
		if (is_local())
		{
			const usz curr_used = m_local_used;

			if (curr_used + extra > Local_Capacity)
			{
				const usz new_capacity = next_heap_capacity(curr_used + extra);

				T* heap_ptr = static_cast<T*>(malloc(new_capacity * sizeof(T)));

				if (heap_ptr == nullptr)
					return false;

				memset(heap_ptr, 0, new_capacity * sizeof(T));

				for (usz i = 0; i != curr_used; ++i)
					heap_ptr[i] = std::move(m_local[i]);

				m_data = heap_ptr;

				m_used = curr_used;

				m_capacity = new_capacity;

				for (usz i = m_used; i != m_capacity; ++i)
					new(heap_ptr + i) T;
			}
		}
		else if (m_used + extra > m_capacity)
		{
			const usz new_capacity = next_heap_capacity(m_used + extra);

			if (T* tmp = static_cast<T*>(realloc(m_data, new_capacity * sizeof(T))); tmp == nullptr)
				return false;
			else
				m_data = tmp;

			m_capacity = new_capacity;

			memset(m_data + m_used, 0, (new_capacity - m_used) * sizeof(T));

			for (usz i = m_used; i != m_capacity; ++i)
				new(m_data + i) T;
		}

		return true;
	}

	static usz next_heap_capacity(usz required) noexcept
	{
		required -= 1;

		required |= required >> 1;

		required |= required >> 2;

		required |= required >> 4;

		required |= required >> 8;

		required |= required >> 16;

		required |= required >> 32;

		return required + 1;
	}

	bool is_local() const noexcept
	{
		return m_data < reinterpret_cast<T*>(0x10000);
	}
};

template<typename T>
struct vec<T, 0>
{
private:

	T* m_data = nullptr;
	
		usz m_capacity = 0;

		usz m_used = 0;

public:

	~vec() noexcept
	{
		for (T* t = begin(); t != end(); ++t)
			t->~T();
	}

	T& operator[](usz i) noexcept
	{
		return m_data[i];
	}

	const T& operator[](usz i) const noexcept
	{
		return m_data[i];
	}

	const T* begin() const noexcept
	{
		return m_data;
	}

	const T* end() const noexcept
	{
		return m_data + m_used;
	}

	T* begin() noexcept
	{
		return m_data;
	}

	T* end() noexcept
	{
		return m_data + m_used;
	}

	const T* data() const noexcept
	{
		return m_data;
	}

	T* data() noexcept
	{
		return m_data;
	}

	bool push_back(const T& t) noexcept
	{
		if (!ensure_capacity(1))
			return false;

		m_data[m_used++] = t;

		return true;
	}

	bool push_back(T&& t) noexcept
	{
		if (!ensure_capacity(1))
			return false;

		m_data[m_used++] = std::move(t);

		return true;
	}

	void pop() noexcept
	{
		m_data[--m_used].~T();
	}

	void remove(usz index) noexcept
	{
		m_data[i].~T();

		for (usz i = index + 1; i != m_used; ++i)
			m_data[i - 1] = std::move(m_data[i]);

		--m_used;
	}

	bool insert(usz index, const T& t) noexcept
	{
		if (!ensure_capacity(1))
			return false;

		for (usz i = index; i != m_used; ++i)
			m_data[i + 1] = std::move(m_data[i]);

		m_data[index] = t;

		++m_used;

		return true;
	}

	bool insert(usz index, T&& t) noexcept
	{
		if (!ensure_capacity(1))
			return false;

		for (usz i = index; i != m_used; ++i)
			m_data[i + 1] = std::move(m_data[i]);

		m_data[index] = std::move(t);

		++m_used;

		return true;
	}

	bool insert(usz index, usz count, const T* ts)
	{
		if (!ensure_capacity(count))
			return false;

		for (usz i = index; i != m_used; ++i)
			m_data[i + count] = std::move(m_data[i]);

		for (usz i = 0; i != count; ++i)
			m_data[index + i] = ts[i];

		m_used += count;

		return true;
	}

	T& first() noexcept
	{
		return *m_data;
	}

	T& last() noexcept
	{
		return m_data[m_used - 1];
	}

	const T& first() const noexcept
	{
		return *m_data;
	}

	const T& last() const noexcept
	{
		return m_data[m_used - 1];
	}

	usz size() const noexcept
	{
		return m_used;
	}

	usz capacity() const noexcept
	{
		return m_capacity;
	}

	void clear()
	{
		for (usz i = 0; i != m_used; ++i)
			m_data[i].~T();

		free(m_data);

		m_used = 0;

		m_capacity = 0;

		m_data = nullptr;
	}

	bool reserve(usz count) noexcept
	{
		return ensure_capacity(count);
	}

	void set_size(usz size) noexcept
	{
		m_used = size;
	}

	vec() noexcept = default;

	vec(const vec<T, 0>&) = delete;

	vec(vec<T, 0>&& v) noexcept
	{
		memcpy(this, &v, sizeof(*this));

		memset(&v, 0, sizeof(*this));
	}

	vec<T, 0>& operator=(vec<T, 0>&& v) noexcept
	{
		clear();

		memcpy(this, &v, sizeof(*this));

		memset(&v, 0, sizeof(*this));

		return *this;
	}

private:

	bool ensure_capacity(usz extra) noexcept
	{
		if (m_data == nullptr)
		{
			const usz new_capacity = next_heap_capacity(extra);

			if ((m_data = static_cast<T*>(malloc(new_capacity * sizeof(T)))) == nullptr)
				return false;

			memset(m_data, 0, new_capacity * sizeof(T));

			m_capacity = new_capacity;

			for (usz i = 0; i != m_capacity; ++i)
				new(m_data + i) T;
		}
		else if (m_used + extra > m_capacity)
		{
			const usz new_capacity = next_heap_capacity(m_used + extra);

			T* tmp = static_cast<T*>(realloc(m_data, new_capacity * sizeof(T)));

			if (tmp == nullptr)
				return tmp;

			memset(tmp + m_used, 0, (new_capacity - m_used) * sizeof(T));

			m_data = tmp;

			m_capacity = new_capacity;

			for (usz i = m_used; i != m_capacity; ++i)
				new(m_data + i) T;
		}

		return true;
	}

	static usz next_heap_capacity(usz required) noexcept
	{
		required -= 1;

		required |= required >> 1;

		required |= required >> 2;

		required |= required >> 4;

		required |= required >> 8;

		required |= required >> 16;

		return required + 1;
	}
};

#endif // UTIL_VEC_INCLUDE_GUARD

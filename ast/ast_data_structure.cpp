#include "ast_data_structure.hpp"

#include <cassert>

void ast::cleanup(FileModule& file_module) noexcept
{
	file_module;

	// TODO
}




bool ast::DefinitionMap::ensure_capacity() noexcept
{
	if (m_capacity * 3 > m_used * 2 + 1)
		return true;

	u32 new_capacity = m_capacity * 2;

	if (new_capacity == 0)
		new_capacity = INITIAL_CAPACITY;

	const size_t new_bytes = new_capacity * (sizeof(ValueEntry) + sizeof(KeyEntry));

	void* new_data = malloc(new_bytes);

	if (new_data == nullptr)
		return false;

	memset(new_data, 0, new_bytes);

	KeyEntry* new_keys = static_cast<KeyEntry*>(new_data);

	ValueEntry* new_values = reinterpret_cast<ValueEntry*>(new_keys + new_capacity);

	const KeyEntry* old_keys = m_keys();

	const ValueEntry* old_values = m_values();

	for (u32 i = 0; i != m_capacity; ++i)
	{
		const KeyEntry ke = old_keys[i];

		if (ke.value_count == 0)
			continue;

		const ValueEntry ve = old_values[i];

		const u32 hash = ke.value_count == 1 ? ve.definition->ident.hash() : ve.list[0]->ident.hash();

		u32 index = hash & (new_capacity - 1);

		while (new_keys[index].value_count != 0)
			index = (index + 1) & (new_capacity - 1);

		new_keys[index] = ke;

		new_values[index] = ve;
	}

	free(m_data);

	m_data = new_data;

	m_capacity = new_capacity;

	return true;
}

bool ast::DefinitionMap::add(const ast::Definition& definition) noexcept
{
	if (!ensure_capacity())
		return false;

	const u32 hash = definition.ident.hash();

	KeyEntry* keys = m_keys();

	ValueEntry* values = m_values();

	u32 index = hash & (m_capacity - 1);

	while(keys[index].value_count != 0)
	{
		if (keys[index].hash == static_cast<u16>(hash))
		{
			const hashed_strview& prev_ident = keys[index].value_count == 1 ? values[index].definition->ident : values[index].list[0]->ident;

			if (streqc(definition.ident, prev_ident))
				break;
		}

		index = (index + 1) & (m_capacity - 1);
	}

	const u16 prev_value_count = keys[index].value_count;

	if (prev_value_count == 0)
	{
		values[index].definition = &definition;

		keys[index] = KeyEntry{ static_cast<u16>(hash), 1 };

		m_used += 1;

		return true;
	}
	else if (prev_value_count == 1)
	{
		const usz list_bytes = INITIAL_LIST_CAPACITY * sizeof(const ast::Definition*);

		const ast::Definition** list = static_cast<const ast::Definition**>(malloc(list_bytes));

		if (list == nullptr)
			return false;

		list[0] = values[index].definition;

		list[1] = &definition;

		values[index].list = list;
	}
	else if ((prev_value_count & (prev_value_count - 1)) == 0)
	{
		const usz list_bytes = prev_value_count * 2 * sizeof(const ast::Definition*);

		const ast::Definition** list = static_cast<const ast::Definition**>(malloc(list_bytes));

		if (list == nullptr)
			return false;

		memcpy(list, values[index].list, prev_value_count * sizeof(const ast::Definition*));

		list[prev_value_count] = &definition;

		free(values[index].list);

		values[index].list = list;
	}
	else
	{
		values[index].list[prev_value_count] = &definition;
	}

	keys[index].value_count += 1;

	return true;
}

u32 ast::DefinitionMap::get(const hashed_strview ident, const ast::Definition* const ** out_matches) const noexcept
{
	if (m_used == 0)
		return 0;

	const u32 hash = ident.hash();

	u32 index = ident.hash() & (m_capacity - 1);

	const KeyEntry* keys = m_keys();

	const ValueEntry* values = m_values();

	while (keys[index].value_count != 0)
	{
		if (keys[index].hash == static_cast<u16>(hash))
		{
			const hashed_strview& key_v = keys[index].value_count == 1 ? values[index].definition->ident : values[index].list[0]->ident;

			if (streqc(ident, key_v))
			{
				*out_matches = keys[index].value_count == 1 ? &values[index].definition : values[index].list;

				break;
			}
		}

		index = (index + 1) & (m_capacity - 1);
	}

	return keys[index].value_count;
}
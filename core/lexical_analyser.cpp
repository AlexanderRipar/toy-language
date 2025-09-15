#include "core.hpp"

#include <cstring>

#include "../infra/container/reserved_heap.hpp"
#include "../infra/container/reserved_vec.hpp"
#include "../infra/hash.hpp"

static constexpr u32 MIN_SCOPE_MAP_SIZE_LOG2 = 6;

static constexpr u32 MAX_SCOPE_MAP_SIZE_LOG2 = 16;

static constexpr u16 MAX_SCOPE_ENTRY_COUNT = static_cast<u16>(1 << 15);

struct ScopeEntry
{
	u16 rank : 15;
	
	u16 is_global : 1;
};

struct ScopeMap
{
	u32 capacity;

	u16 used;

	bool is_global;

	bool next_outer_closes;

	#if COMPILER_MSVC
	#pragma warning(push)
	#pragma warning(disable : 4200) // C4200: nonstandard extension used: zero-sized array in struct/union
	#elif COMPILER_CLANG
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wc99-extensions" // flexible array members are a C99 feature
	#elif COMPILER_GCC
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids flexible array member
	#endif
	u64 occupied_bits[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif

	// IdentifierId names[];

	// ScopeEntry entries[];
};

struct ScopeMapInfo
{
	IdentifierId* names;

	ScopeEntry* entries;
};

struct LexicalAnalyser
{
	ReservedHeap<MIN_SCOPE_MAP_SIZE_LOG2, MAX_SCOPE_MAP_SIZE_LOG2> scope_pool;
	
	s32 scopes_top = 0;

	ScopeMap* scopes[MAX_AST_DEPTH];

	IdentifierPool* identifiers;

	AstPool* asts;

	ErrorSink* errors;

	bool has_error;

	MutRange<byte> memory;
};

static_assert(sizeof(ScopeEntry) == 2);



ScopeMapInfo scope_map_info(ScopeMap* scope) noexcept
{
	IdentifierId* const names = reinterpret_cast<IdentifierId*>(scope->occupied_bits + (scope->capacity + 63) / 64);

	ScopeEntry* const entries = reinterpret_cast<ScopeEntry*>(names + scope->capacity);

	return { names, entries };
}

static u32 scope_map_size(u32 capacity) noexcept
{
	const u32 occupied_bits_bytes = sizeof(u64) * ((capacity + 63) / 64);

	return sizeof(ScopeMap) + occupied_bits_bytes + capacity * (sizeof(u32) + sizeof(ScopeEntry));
}

static ScopeMap* scope_map_alloc_sized(LexicalAnalyser* lex, bool is_global, u32 capacity) noexcept
{
	ASSERT_OR_IGNORE(is_pow2(capacity));

	MutRange<byte> memory = lex->scope_pool.alloc(scope_map_size(capacity));

	ScopeMap* const scope = reinterpret_cast<ScopeMap*>(memory.begin());
	scope->capacity = capacity;
	scope->used = 0;
	scope->is_global = is_global;
	scope->next_outer_closes = false;
	memset(scope->occupied_bits, 0, sizeof(u64) * ((capacity + 63) / 64));

	return scope;
}

static ScopeMap* scope_map_alloc(LexicalAnalyser* lex, bool is_global) noexcept
{
	return scope_map_alloc_sized(lex, is_global, 8);
}

static void scope_map_dealloc(LexicalAnalyser* lex, ScopeMap* scope) noexcept
{
	const u32 size = scope_map_size(scope->capacity);

	lex->scope_pool.dealloc({ reinterpret_cast<byte*>(scope), size });
}

template<bool check_duplicates>
static void scope_map_add_nogrow(LexicalAnalyser* lex, ScopeMap* scope, IdentifierId name, ScopeEntry entry, const AstNode* error_source) noexcept
{
	const u32 hash = fnv1a(range::from_object_bytes(&name));

	const ScopeMapInfo info = scope_map_info(scope);

	u32 index = hash & (scope->capacity - 1);

	u64 occupied_bits = scope->occupied_bits[index / 64];

	u64 occupied_mask = static_cast<u64>(1) << (index % 64);

	while ((occupied_bits & occupied_mask) != 0)
	{
		if constexpr (check_duplicates)
		{
			if (info.names[index] == name)
			{
				const Range<char8> name_str = identifier_name_from_id(lex->identifiers, name);

				source_error_nonfatal(lex->errors, source_id_of(lex->asts, error_source), "Name `%.*s` defined more than once.\n", static_cast<s32>(name_str.count()), name_str.begin());

				return;
			}
		}
		else
		{
			ASSERT_OR_IGNORE(info.names[index] != name);
		}

		index += 1;

		if (index == scope->capacity)
		{
			index = 0;

			occupied_bits = scope->occupied_bits[0];

			occupied_mask = 1;
		}
		else if (static_cast<s64>(occupied_mask) < 0)
		{
			occupied_bits = scope->occupied_bits[index / 64];

			occupied_mask = 1;
		}
		else
		{
			occupied_mask <<= 1;
		}
	}

	scope->used += 1;
	scope->occupied_bits[index / 64] |= static_cast<u64>(1) << (index % 64);

	info.names[index] = name;
	info.entries[index] = entry;
}

static ScopeMap* scope_map_grow(LexicalAnalyser* lex, ScopeMap* old_scope) noexcept
{
	const u32 new_capacity = old_scope->capacity * 2;

	ScopeMap* const new_scope = scope_map_alloc_sized(lex, old_scope->is_global, new_capacity);
	new_scope->capacity = new_capacity;
	new_scope->used = 0;

	const ScopeMapInfo old_info = scope_map_info(old_scope);

	const u64* bits_curr = old_scope->occupied_bits;

	const u64* const bits_end = bits_curr + (old_scope->capacity + 63) / 64;

	u32 index_base = 0;

	while (bits_curr != bits_end)
	{
		u64 bitmask = *bits_curr;

		while (bitmask != 0)
		{
			const u8 least_index = count_trailing_zeros_assume_one(bitmask);

			const u32 index = index_base + least_index;

			scope_map_add_nogrow<false>(lex, new_scope, old_info.names[index], old_info.entries[index], nullptr);

			bitmask ^= static_cast<u64>(1) << least_index;
		}

		bits_curr += 1;

		index_base += 64;
	}

	scope_map_dealloc(lex, old_scope);

	return new_scope;
}

static bool scope_map_get(ScopeMap* scope, IdentifierId name, ScopeEntry* out) noexcept
{
	const ScopeMapInfo info = scope_map_info(scope);

	const u32 hash = fnv1a(range::from_object_bytes(&name));

	const u32 initial_index = hash & (scope->capacity - 1);

	u32 index = initial_index;

	u64 occupied_mask = static_cast<u64>(1) << (index % 64);

	u64 occupied_bits = scope->occupied_bits[index / 64];

	while (true)
	{
		const bool has_value = (occupied_bits & occupied_mask) != 0;

		if (!has_value)
			return false;

		if (info.names[index] == name)
		{
			*out = info.entries[index];

			return true;
		}

		index += 1;

		if (index == scope->capacity)
		{
			// If we have reached the end of the scope, wrap around.

			index = 0;

			occupied_bits = scope->occupied_bits[0];

			occupied_mask = 1;
		}
		else if (static_cast<s64>(occupied_mask) < 0)
		{
			// if `occupied_mask` is going to overflow, move to the next u64 in `scope->occupied_bits`.

			occupied_bits = scope->occupied_bits[index / 64];

			occupied_mask = 1;
		}
		else
		{
			occupied_mask <<= 1;
		}

		if (initial_index == index)
			return false;
	}
}

static ScopeMap* scope_map_add(LexicalAnalyser* lex, ScopeMap* scope, IdentifierId name, ScopeEntry entry, const AstNode* error_source) noexcept
{
	if (scope->used == MAX_SCOPE_ENTRY_COUNT)
		source_error(lex->errors, source_id_of(lex->asts, error_source), "Exceeded maximum of %u definitions in a single scope.\n", MAX_SCOPE_ENTRY_COUNT);

	if (static_cast<u32>(scope->used) * 3 > scope->capacity * 2)
		scope = scope_map_grow(lex, scope);

	scope_map_add_nogrow<true>(lex, scope, name, entry, error_source);

	return scope;
}



static void push_scope(LexicalAnalyser* lex, ScopeMap* scope) noexcept
{
	ASSERT_OR_IGNORE(static_cast<u64>(lex->scopes_top) + 1 < array_count(lex->scopes));

	lex->scopes_top += 1;

	lex->scopes[lex->scopes_top] = scope;

}

static void pop_scope(LexicalAnalyser* lex) noexcept
{
	ASSERT_OR_IGNORE(lex->scopes_top >= 0);

	scope_map_dealloc(lex, lex->scopes[lex->scopes_top]);

	lex->scopes_top -= 1;
}



static void resolve_names_rec(LexicalAnalyser* lex, AstNode* node, bool do_pop) noexcept
{
	ASSERT_OR_IGNORE(lex->scopes_top >= 0 && static_cast<u64>(lex->scopes_top) < array_count(lex->scopes));

	const AstTag tag = node->tag;

	if (tag == AstTag::Identifier)
	{
		ASSERT_OR_IGNORE(!has_children(node));

		AstIdentifierData* const attach = attachment_of<AstIdentifierData>(node);

		s32 offset_start = lex->scopes_top;

		bool is_global = false;

		bool is_in_closure = false;

		bool is_in_nested_closure = false;

		for (s32 i = lex->scopes_top; i >= 0; --i)
		{
			ScopeMap* const scope = lex->scopes[i];

			if (!is_global && scope->is_global)
			{
				offset_start = i;

				is_global = true;
			}

			ASSERT_OR_IGNORE(is_global || !scope->is_global);

			ScopeEntry entry;

			if (scope_map_get(scope, attach->identifier_id, &entry))
			{
				attach->binding.out = static_cast<u16>(offset_start - i);
				attach->binding.is_global = is_global || entry.is_global;
				attach->binding.is_closed_over = is_in_closure && !is_global;
				attach->binding.is_closed_over_closure = is_in_nested_closure && !is_global;
				attach->binding.rank = entry.rank;

				return;
			}

			if (scope->next_outer_closes)
			{
				is_in_nested_closure = is_in_closure;

				is_in_closure = true;
			}
		}

		const Range<char> name = identifier_name_from_id(lex->identifiers, attach->identifier_id);

		source_error_nonfatal(lex->errors, source_id_of(lex->asts, node), "Identifier `%.*s` is not defined.\n", static_cast<s32>(name.count()), name.begin());

		lex->has_error = true;
	}
	else if (tag == AstTag::Func)
	{
		// Special cased since the signature's parameters must be kept alive
		// for its sibling - the function body - and only popped afterwards.

		AstNode* const signature = first_child_of(node);

		ASSERT_OR_IGNORE(signature->tag == AstTag::Signature);

		resolve_names_rec(lex, signature, false);

		lex->scopes[lex->scopes_top]->next_outer_closes = true;

		AstNode* const body = next_sibling_of(signature);

		ASSERT_OR_IGNORE(!has_next_sibling(body));

		resolve_names_rec(lex, body, true);

		pop_scope(lex);
	}
	else
	{
		bool needs_pop = false;

		if (tag == AstTag::Definition || tag == AstTag::Parameter)
		{
			const AstDefinitionData* const attach = tag == AstTag::Definition
				? attachment_of<AstDefinitionData>(node)
				: reinterpret_cast<AstDefinitionData*>(attachment_of<AstParameterData>(node));

			ScopeMap* scope = lex->scopes[lex->scopes_top];

			ScopeEntry entry;
			entry.rank = scope->used;
			entry.is_global = scope->is_global || has_flag(node, AstFlag::Definition_IsGlobal);

			scope = scope_map_add(lex, scope, attach->identifier_id, entry, node);

			lex->scopes[lex->scopes_top] = scope;
		}
		else if (tag == AstTag::Block || tag == AstTag::Signature)
		{
			push_scope(lex, scope_map_alloc(lex, false));

			needs_pop = true;
		}

		AstDirectChildIterator it = direct_children_of(node);

		while (has_next(&it))
		{
			AstNode* const child = next(&it);

			resolve_names_rec(lex, child, true);
		}

		if (needs_pop && do_pop)
			pop_scope(lex);
	}
}

static void resolve_names_root(LexicalAnalyser* lex, AstNode* root) noexcept
{
	ScopeMap* scope = scope_map_alloc(lex, true);

	u16 rank = 0;

	AstDirectChildIterator it = direct_children_of(root);

	while (has_next(&it))
	{
		AstNode* node = next(&it);

		ASSERT_OR_IGNORE(node->tag != AstTag::Identifier);

		if (node->tag != AstTag::Definition)
			continue;

		const AstDefinitionData* const attach = attachment_of<AstDefinitionData>(node);

		ScopeEntry entry;
		entry.rank = rank;
		entry.is_global = true;

		scope = scope_map_add(lex, scope, attach->identifier_id, entry, node);

		rank += 1;
	}

	push_scope(lex, scope);

	it = direct_children_of(root);

	while (has_next(&it))
	{
		AstNode* const node = next(&it);

		if (node->tag == AstTag::Definition)
		{
			AstNode* child = first_child_of(node);

			resolve_names_rec(lex, child, true);

			if (has_next_sibling(child))
			{
				child = next_sibling_of(child);

				resolve_names_rec(lex, child, true);

				ASSERT_OR_IGNORE(!has_next_sibling(child));
			}
		}
		else
		{
			resolve_names_rec(lex, node, true);
		}
	}

	if (lex->has_error)
		error_exit();
}



LexicalAnalyser* create_lexical_analyser(AllocPool* alloc, IdentifierPool* identifiers, AstPool* asts, ErrorSink* errors) noexcept
{
	static constexpr u32 SCOPE_POOL_CAPACITIES[MAX_SCOPE_MAP_SIZE_LOG2 - MIN_SCOPE_MAP_SIZE_LOG2 + 1] = {
		MAX_AST_DEPTH     , MAX_AST_DEPTH    , MAX_AST_DEPTH    , MAX_AST_DEPTH / 2, MAX_AST_DEPTH / 2 ,
		MAX_AST_DEPTH / 2 , MAX_AST_DEPTH / 4, MAX_AST_DEPTH / 4, MAX_AST_DEPTH / 8, MAX_AST_DEPTH / 16,
		MAX_AST_DEPTH / 32,
	};

	static constexpr u32 SCOPE_POOL_COMMITS[MAX_SCOPE_MAP_SIZE_LOG2 - MIN_SCOPE_MAP_SIZE_LOG2 + 1] = {
		64, 32, 16, 8, 4,
		 2,  1,  1, 1, 1,
		 1,
	};

	u64 scope_pool_size = 0;

	for (u32 i = 0; i != array_count(SCOPE_POOL_CAPACITIES); ++i)
		scope_pool_size += static_cast<u64>(SCOPE_POOL_CAPACITIES[i]) << (i + MIN_SCOPE_MAP_SIZE_LOG2);

	byte* const memory = static_cast<byte*>(minos::mem_reserve(scope_pool_size));

	if (memory == nullptr)
		panic("Could not reserve memory for LexicalAnalyser (0x%X).\n", minos::last_error());

	LexicalAnalyser* const lex = static_cast<LexicalAnalyser*>(alloc_from_pool(alloc, sizeof(LexicalAnalyser), alignof(LexicalAnalyser)));
	lex->scope_pool.init({ memory, scope_pool_size }, Range{ SCOPE_POOL_CAPACITIES }, Range{ SCOPE_POOL_COMMITS });
	lex->scopes_top = -1;
	lex->identifiers = identifiers;
	lex->asts = asts;
	lex->errors = errors;
	lex->has_error = false;
	lex->memory = { memory, scope_pool_size };

	return lex;
}

void release_lexical_analyser(LexicalAnalyser* lex) noexcept
{
	minos::mem_unreserve(lex->memory.begin(), lex->memory.count());
}

void set_prelude_scope(LexicalAnalyser* lex, AstNode* prelude) noexcept
{
	ASSERT_OR_IGNORE(prelude->tag == AstTag::File && lex->scopes_top == -1);

	resolve_names_root(lex, prelude);

	ASSERT_OR_IGNORE(lex->scopes_top == 0);	
}

void resolve_names(LexicalAnalyser* lex, AstNode* root) noexcept
{
	ASSERT_OR_IGNORE(root->tag == AstTag::File && lex->scopes_top == 0);

	resolve_names_root(lex, root);

	pop_scope(lex);

	ASSERT_OR_IGNORE(lex->scopes_top == 0);
}

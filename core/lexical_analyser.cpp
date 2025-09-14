#include "core.hpp"

#include <cstring>

#include "../infra/container/reserved_heap.hpp"
#include "../infra/container/reserved_vec.hpp"
#include "../infra/hash.hpp"

static constexpr u32 MIN_SCOPE_MAP_SIZE_LOG2 = 6;

static constexpr u32 MAX_SCOPE_MAP_SIZE_LOG2 = 16;

struct ScopeMap
{
	u32 capacity;

	u32 used;

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

	// u16 ranks[];
};

struct ScopeMapInfo
{
	IdentifierId* names;

	u16* ranks;
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



ScopeMapInfo scope_map_info(ScopeMap* scope) noexcept
{
	IdentifierId* const names = reinterpret_cast<IdentifierId*>(scope->occupied_bits + (scope->capacity + 63) / 64);

	u16* const ranks = reinterpret_cast<u16*>(names + scope->capacity);

	return { names, ranks };
}

static u32 scope_map_size(u32 capacity) noexcept
{
	const u32 occupied_bits_bytes = sizeof(u64) * ((capacity + 63) / 64);

	return sizeof(ScopeMap) + occupied_bits_bytes + capacity * (sizeof(u32) + sizeof(u16));
}

static ScopeMap* scope_map_alloc_sized(LexicalAnalyser* lex, u32 capacity) noexcept
{
	ASSERT_OR_IGNORE(is_pow2(capacity));

	MutRange<byte> memory = lex->scope_pool.alloc(scope_map_size(capacity));

	ScopeMap* const scope = reinterpret_cast<ScopeMap*>(memory.begin());
	scope->capacity = capacity;
	scope->used = 0;
	memset(scope->occupied_bits, 0, sizeof(u64) * ((capacity + 63) / 64));

	return scope;
}

static ScopeMap* scope_map_alloc(LexicalAnalyser* lex) noexcept
{
	return scope_map_alloc_sized(lex, 8);
}

static void scope_map_dealloc(LexicalAnalyser* lex, ScopeMap* scope) noexcept
{
	const u32 size = scope_map_size(scope->capacity);

	lex->scope_pool.dealloc({ reinterpret_cast<byte*>(scope), size });
}

template<bool check_duplicates>
static void scope_map_add_nogrow(LexicalAnalyser* lex, ScopeMap* scope, IdentifierId name, u16 rank, const AstNode* error_source) noexcept
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
	info.ranks[index] = rank;
}

static ScopeMap* scope_map_grow(LexicalAnalyser* lex, ScopeMap* old_scope) noexcept
{
	const u32 new_capacity = old_scope->capacity * 2;

	ScopeMap* const new_scope = scope_map_alloc_sized(lex, new_capacity);
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

			scope_map_add_nogrow<false>(lex, new_scope, old_info.names[index], old_info.ranks[index], nullptr);

			bitmask ^= static_cast<u64>(1) << least_index;
		}

		bits_curr += 1;

		index_base += 64;
	}

	scope_map_dealloc(lex, old_scope);

	return new_scope;
}

static bool scope_map_get(ScopeMap* scope, IdentifierId name, u16* out) noexcept
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
			*out = info.ranks[index];

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

static ScopeMap* scope_map_add(LexicalAnalyser* lex, ScopeMap* scope, IdentifierId name, u16 rank, const AstNode* error_source) noexcept
{
	if (scope->used * 3 > scope->capacity * 2)
		scope = scope_map_grow(lex, scope);

	scope_map_add_nogrow<true>(lex, scope, name, rank, error_source);

	return scope;
}



static void push_scope(LexicalAnalyser* lex, ScopeMap* scope) noexcept
{
	ASSERT_OR_IGNORE(lex->scopes_top + 1 < array_count(lex->scopes));

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
	ASSERT_OR_IGNORE(lex->scopes_top >= 0 && lex->scopes_top < array_count(lex->scopes));

	const AstTag tag = node->tag;

	if (tag == AstTag::Identifier)
	{
		ASSERT_OR_IGNORE(!has_children(node));

		AstIdentifierData* const attach = attachment_of<AstIdentifierData>(node);

		for (s32 i = lex->scopes_top; i >= 0; --i)
		{
			u16 rank;

			if (scope_map_get(lex->scopes[i], attach->identifier_id, &rank))
			{
				attach->binding = NameBinding{ static_cast<u16>(lex->scopes_top - i), rank };

				return;
			}
		}

		const Range<char> name = identifier_name_from_id(lex->identifiers, attach->identifier_id);

		source_error_nonfatal(lex->errors, source_id_of(lex->asts, node), "Identifier `%.*s` is not defined.\n", static_cast<s32>(name.count()), name.begin());

		lex->has_error = true;
	}
	else if (tag == AstTag::Func)
	{
		// Special cased as the signature's parameters must be kept alive for
		// its sibling body, and only popped afterwards.
		AstNode* const signature = first_child_of(node);
		
		ASSERT_OR_IGNORE(signature->tag == AstTag::Signature);

		resolve_names_rec(lex, signature, false);

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

			scope = scope_map_add(lex, scope, attach->identifier_id, static_cast<u16>(scope->used), node);

			lex->scopes[lex->scopes_top] = scope;
		}
		else if (tag == AstTag::Block || tag == AstTag::Signature)
		{
			push_scope(lex, scope_map_alloc(lex));

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
	ScopeMap* scope = scope_map_alloc(lex);

	u16 rank = 0;

	AstDirectChildIterator it = direct_children_of(root);

	while (has_next(&it))
	{
		AstNode* node = next(&it);

		ASSERT_OR_IGNORE(node->tag != AstTag::Identifier);

		if (node->tag != AstTag::Definition)
			continue;

		const AstDefinitionData* const attach = attachment_of<AstDefinitionData>(node);

		scope = scope_map_add(lex, scope, attach->identifier_id, rank, node);

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

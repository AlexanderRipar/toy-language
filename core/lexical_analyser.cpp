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
	u16 rank;

	u16 closure_source_rank;

	u8 closure_source_out;

	bool closure_source_is_closure;
};

struct ScopeMap
{
	u32 capacity;

	u16 used;

	bool is_global;

	bool has_closure;

	#if COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids flexible array member
	#endif
	u64 occupied_bits[];
	#if COMPILER_GCC
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
	
	s32 scopes_top;

	ScopeMap* scopes[MAX_AST_DEPTH];

	ScopeMap* closures[MAX_AST_DEPTH];

	IdentifierPool* identifiers;

	AstPool* asts;

	ErrorSink* errors;

	GlobalFileIndex active_file_index;

	bool has_error;

	GlobalFileIndex prelude_file_index;

	MutRange<byte> memory;
};



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
	scope->has_closure = false;
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
				(void) record_error(lex->errors, error_source, CompileError::ScopeDuplicateName);

				lex->has_error = true;

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

static Maybe<ScopeMap*> scope_map_add(LexicalAnalyser* lex, ScopeMap* scope, IdentifierId name, ScopeEntry entry, const AstNode* error_source) noexcept
{
	if (scope->used == MAX_SCOPE_ENTRY_COUNT)
	{
		(void) record_error(lex->errors,error_source, CompileError::ScopeTooManyDefinitions);

		lex->has_error = true;

		return none<ScopeMap*>();
	}

	if (static_cast<u32>(scope->used) * 3 > scope->capacity * 2)
		scope = scope_map_grow(lex, scope);

	scope_map_add_nogrow<true>(lex, scope, name, entry, error_source);

	return some(scope);
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

static void set_closure(LexicalAnalyser* lex, ScopeMap* closure) noexcept
{
	static_assert(sizeof(LexicalAnalyser::scopes) == sizeof(LexicalAnalyser::closures));

	ASSERT_OR_IGNORE(static_cast<u64>(lex->scopes_top) < array_count(lex->scopes));

	ASSERT_OR_IGNORE(!lex->scopes[lex->scopes_top]->has_closure);

	lex->scopes[lex->scopes_top]->has_closure = true;

	lex->closures[lex->scopes_top] = closure;
}



static void set_func_closure_list(LexicalAnalyser* lex, AstNode* node, ScopeMap* closure) noexcept
{
	if (closure->used == 0)
	{
		attachment_of<AstFuncData>(node)->closure_list_id = none<ClosureListId>();

		return;
	}

	ClosureList* const list = alloc_closure_list(lex->asts, closure->used);

	ScopeMapInfo info = scope_map_info(closure);

	const u64* occupied_bits = closure->occupied_bits;

	for (u16 i = 0; i != closure->capacity; ++i)
	{
		const u64 mask = static_cast<u64>(1) << (i & 63);

		if ((occupied_bits[i / 64] & mask) == 0)
			continue;

		const ScopeEntry src = info.entries[i];

		ClosureListEntry* const dst = list->entries + src.rank;
		dst->source_rank = src.closure_source_rank;
		dst->source_out = src.closure_source_out;
		dst->source_is_closure = src.closure_source_is_closure;
	}

	const ClosureListId list_id = id_from_closure_list(lex->asts, list);

	attachment_of<AstFuncData>(node)->closure_list_id = some(list_id);
}

static u16 add_name_to_closures(LexicalAnalyser* lex, IdentifierId name, u16 closed_over_rank, s32 scope_index) noexcept
{
	bool closure_source_is_closure = false;

	s32 source_index = scope_index;

	for (s32 i = scope_index + 1; i <= lex->scopes_top; ++i)
	{
		if (!lex->scopes[i]->has_closure)
			continue;

		ScopeMap* const closure = lex->closures[i];

		ScopeEntry closure_entry;

		if (!scope_map_get(closure, name, &closure_entry))
		{
			closure_entry.rank = closure->used;
			closure_entry.closure_source_rank = closed_over_rank;
			closure_entry.closure_source_is_closure = closure_source_is_closure;
			closure_entry.closure_source_out = static_cast<u8>(i - source_index);

			// This addition can never fail, as we just checked whether the
			// name is already present. As such, we pass a dummy `nullptr` as
			// the `error_source`, and don't check our result.
			const Maybe<ScopeMap*> new_closure = scope_map_add(lex, closure, name, closure_entry, nullptr);

			lex->closures[i] = get(new_closure);
		}

		ASSERT_OR_IGNORE(closure_entry.closure_source_rank == closed_over_rank && closure_entry.closure_source_is_closure == closure_source_is_closure);

		closed_over_rank = closure_entry.rank;

		source_index = i;

		closure_source_is_closure = true;
	}

	// Check that we have entered `name` into at least one closure.
	// Otherwise something has gone quite wrong.
	ASSERT_OR_IGNORE(closure_source_is_closure);

	return closed_over_rank;
}



static void resolve_names_rec(LexicalAnalyser* lex, AstNode* node, bool do_pop) noexcept
{
	ASSERT_OR_IGNORE(lex->scopes_top >= 0 && static_cast<u64>(lex->scopes_top) < array_count(lex->scopes));

	const AstTag tag = node->tag;

	ASSERT_OR_IGNORE(do_pop || tag == AstTag::Signature);

	if (tag == AstTag::Identifier)
	{
		// This is the meat of the algorithm.
		// We traverse the active scopes from innermost to outermost, looking
		// for a definition matching `node`'s `IdentifierId`.
		// Scoped and global variables are pretty easily handled here, but
		// closed-over ones are more problematic, since we need to record not
		// only in which closure `node` needs to look up its value, but also
		// where the closure itself can find the closed-over value when it is
		// constructed. Note that this second component must be relative to
		// the closure's construction point, and not relative to `node`, and
		// gets recorded into the closure list of the relevant signature
		// attachment instead of the `NameBinding` of `node`.

		AstIdentifierData* const attachment = attachment_of<AstIdentifierData>(node);

		const IdentifierId name = attachment->identifier_id;

		NameBinding* const binding = &attachment->binding;

		bool is_closed_over = false;

		for (s32 i = lex->scopes_top; i >= 0; --i)
		{
			ScopeMap* const scope = lex->scopes[i];

			ScopeEntry scope_entry;

			if (scope_map_get(scope, name, &scope_entry))
			{
				if (scope->is_global)
				{
					// Global takes precedence over closed-over variables, as
					// globals are never closed over.

					// The global's file index can be either the prelude index
					// (in case `i` is 0) or the file index of the file that is
					// currently being analysed.
					const u16 file_index_bits = i == 0
						? static_cast<u16>(lex->prelude_file_index)
						: static_cast<u16>(lex->active_file_index);

					binding->global.is_global_ = true;
					binding->global.file_index_bits = file_index_bits;
					binding->global.rank = scope_entry.rank;
				}
				else if (is_closed_over)
				{
					// Make sure that `name` is closed-over in all closures
					// between its definition and its use. 
					const u16 rank_in_closure = add_name_to_closures(lex, name, scope_entry.rank, i);

					binding->closed.is_global_ = false;
					binding->closed.is_scoped_ = false;
					binding->closed.unused_ = 0;
					binding->closed.rank_in_closure = rank_in_closure;
				}
				else
				{
					binding->scoped.is_global_ = false;
					binding->scoped.is_scoped_ = true;
					binding->scoped.unused_ = 0;
					binding->scoped.out = static_cast<u16>(lex->scopes_top - i);
					binding->scoped.rank = scope_entry.rank;
				}

				// Return, as we have found a match.
				return;
			}
			else if (scope->has_closure)
			{
				is_closed_over = true;
			}
		}

		// We have traversed up to the outermost scope without finding a match.
		// As such, we need to indicate an error.

		(void) record_error(lex->errors, node, CompileError::ScopeNameNotDefined);

		lex->has_error = true;
	}
	else if (tag == AstTag::Func)
	{
		AstNode* const signature = first_child_of(node);

		// Defer popping of the signature scope, as it remains active for the
		// body.
		resolve_names_rec(lex, signature, false);

		AstNode* const body = next_sibling_of(signature);

		// Since we are traversing a function body, we might encounter
		// closed-over variables from the surrounding scope. To keep track of
		// these, we create and associate a closure `ScopeMap` with the current
		// scope.
		ScopeMap* const new_closure = scope_map_alloc(lex, false);
		set_closure(lex, new_closure);

		resolve_names_rec(lex, body, true);

		ScopeMap* const filled_closure = lex->closures[lex->scopes_top];

		set_func_closure_list(lex, node, filled_closure);

		scope_map_dealloc(lex, filled_closure);

		// Now pop the signature scope that was pushed and not popped by the
		// recursion on `signature`.
		pop_scope(lex);
	}
	else
	{
		bool needs_pop = false;

		if (tag == AstTag::Definition || tag == AstTag::Parameter)
		{
			ASSERT_OR_IGNORE(lex->scopes_top >= 0);

			const IdentifierId name = tag == AstTag::Definition
				? attachment_of<AstDefinitionData>(node)->identifier_id
				: attachment_of<AstParameterData>(node)->identifier_id;

			ScopeMap* const scope = lex->scopes[lex->scopes_top];

			ScopeEntry entry;
			entry.rank = scope->used;

			const Maybe<ScopeMap*> new_scope = scope_map_add(lex, scope, name, entry, node);

			if (is_none(new_scope))
				return;

			lex->scopes[lex->scopes_top] = get(new_scope);
		}
		else if (tag == AstTag::Block || tag == AstTag::Signature)
		{
			// Push a new scope, later popping it if `do_pop` is `true` and
			// leaving it on the stack to be popped externally otherwise.

			ASSERT_OR_IGNORE(static_cast<u32>(lex->scopes_top + 1) < array_count(lex->scopes));

			ScopeMap* const scope = scope_map_alloc(lex, false);

			push_scope(lex, scope);

			needs_pop = do_pop;
		}

		// Traverse node's children recursively.

		AstDirectChildIterator it = direct_children_of(node);

		while (has_next(&it))
		{
			AstNode* const child = next(&it);

			resolve_names_rec(lex, child, true);
		}

		if (needs_pop)
			pop_scope(lex);
	}
}

static void resolve_names_root(LexicalAnalyser* lex, AstNode* root, GlobalFileIndex file_index) noexcept
{
	lex->active_file_index = file_index;

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

		const Maybe<ScopeMap*> new_scope = scope_map_add(lex, scope, attach->identifier_id, entry, node);

		if (is_none(new_scope))
			return;

		scope = get(new_scope);

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
}



LexicalAnalyser* create_lexical_analyser(HandlePool* alloc, IdentifierPool* identifiers, AstPool* asts, ErrorSink* errors) noexcept
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

	LexicalAnalyser* const lex = alloc_handle_from_pool<LexicalAnalyser>(alloc);
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

bool set_prelude_scope(LexicalAnalyser* lex, AstNode* prelude, GlobalFileIndex file_index) noexcept
{
	ASSERT_OR_IGNORE(prelude->tag == AstTag::File && lex->scopes_top == -1);

	lex->prelude_file_index = file_index;

	resolve_names_root(lex, prelude, file_index);

	ASSERT_OR_IGNORE(lex->scopes_top == 0);

	return !lex->has_error;
}

bool resolve_names(LexicalAnalyser* lex, AstNode* root, GlobalFileIndex file_index) noexcept
{
	ASSERT_OR_IGNORE(root->tag == AstTag::File && lex->scopes_top == 0);

	resolve_names_root(lex, root, file_index);

	pop_scope(lex);

	ASSERT_OR_IGNORE(lex->scopes_top == 0);

	return !lex->has_error;
}

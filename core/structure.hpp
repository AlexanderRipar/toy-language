#ifndef CORE_STRUCTURE_INCLUDE_GUARD
#define CORE_STRUCTURE_INCLUDE_GUARD

#include "core.hpp"

#include "../infra/types.hpp"
#include "../infra/range.hpp"
#include "../infra/hash.hpp"
#include "../infra/container/id_map.hpp"
#include "../infra/container/reserved_vec.hpp"
#include "../infra/minos/minos.hpp"

#include <cstddef>
#include <csetjmp>

struct AstPool
{
	ReservedVec<AstNode> nodes;

	ReservedVec<SourceId> sources;

	ReservedVec<AstNode> node_builder;

	ReservedVec<SourceId> source_builder;

	ReservedVec<ClosureList> closure_lists;
};



struct ErrorSink
{
	u32 error_count;

	u8 source_tab_size;

	bool enabled;

	PrintSink sink;

	ReservedVec<ErrorRecord> records;
};



struct IdentifierEntry;

struct IdentifierIterator
{
	CoreData* core;

	u32 curr;

	u32 end;

	bool has_next() const noexcept;

	IdentifierEntry* next() noexcept;
};

struct IdentifierAlloc
{
	CoreData* core;

	IdentifierEntry* value_from_id(u32 id) noexcept;

	const IdentifierEntry* value_from_id(u32 id) const noexcept;

	u32 id_from_value(const IdentifierEntry* value) const noexcept;

	IdentifierIterator values() noexcept;

	IdentifierEntry* alloc(Range<char8> key, u32 key_hash) noexcept;

	void dealloc(u32 id) noexcept;
};

struct IdentifierPool
{
	IdMap<Range<char8>, IdentifierEntry, IdentifierAlloc> map;

	ReservedVec<byte> entries;
};



struct Scope;

// "Public" definition required here for ffi argument passing.
struct alignas(16) ScopeMember
{
	u32 offset;

	u32 size;

	u32 align : 31;

	u32 is_mut : 1;

	TypeId type;
};

struct LoopInfo;

struct ArgumentPack;

struct GlobalInitialization;

struct BuiltinInfo
{
	OpcodeId body;

	TypeId signature_type;
};

struct Interpreter
{
	ReservedVec<Scope> scopes;

	ReservedVec<ScopeMember> scope_members;

	ReservedVec<byte> scope_data;

	ReservedVec<CompValue> values;

	ReservedVec<byte> temporary_data;

	ReservedVec<OpcodeId> activations;

	ReservedVec<u32> call_activation_indices;

	ReservedVec<LoopInfo> loop_stack;

	ReservedVec<CompValue> write_ctxs;

	ReservedVec<ClosureId> active_closures;

	ReservedVec<Maybe<OpcodeId>> argument_callbacks;

	ReservedVec<ArgumentPack> argument_packs;

	ReservedVec<GlobalInitialization> global_initializations;

	ReservedVec<TypeId> selfs;

	bool is_ok;

	BuiltinInfo builtin_infos[static_cast<u8>(Builtin::MAX) - 1];

	bool log_imported_asts;

	bool log_imported_opcodes;

	bool log_imported_types;

	PrintSink imported_asts_sink;

	PrintSink imported_opcodes_sink;

	PrintSink imported_types_sink;
};



enum class ScopeMapKind : u8;

struct ScopeMap;

struct LexicalAnalyser
{
	s32 scopes_top;

	SourceFileId active_file_id;

	bool has_error;

	SourceFileId prelude_file_id;

	ScopeMap* scopes[MAX_AST_DEPTH];

	ScopeMap* closures[MAX_AST_DEPTH];
};



enum class FixupKind : u8;

struct Fixup;

struct SourceMapping;

struct OpcodeEffects
{
	s32 values_diff;

	s32 scopes_diff;

	s32 write_ctxs_diff;

	s32 closures_diff;
};

struct OpcodeTranslationFlags
{
	bool allow_return : 1;

	bool allow_self : 1;

	bool allow_void_break : 1;

	bool allow_valued_break : 1;
};

struct OpcodePool
{
	OpcodeEffects state;

	OpcodeEffects return_adjust;

	OpcodeTranslationFlags flags;

	ReservedVec<Opcode> codes;

	ReservedVec<SourceMapping> sources;

	ReservedVec<Fixup> fixups;
};



enum class Token : u8;

struct Lexeme
{
	Token token;

	SourceId source_id;

	union
	{
		CompIntegerValue integer_value;

		CompFloatValue float_value;

		u32 char_value;

		IdentifierId identifier_id;

		Builtin builtin;

		struct
		{
			byte* value_begin;

			u32 value_size;

			TypeId type_id;
		} string;
	};

	Lexeme() noexcept = default;

	Lexeme(Token token) noexcept : token{ token } {}
};

#ifdef COMPILER_MSVC
	#pragma warning(push)
	#pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif
struct Parser
{
	const char8* curr;

	const char8* begin;

	const char8* end;

	Lexeme peek;

	u32 source_id_base;

	bool is_std;

	TypeId u8_type_id;

	bool has_errors;

	bool suppress_errors;

	jmp_buf error_jump_buffer;
};
#ifdef COMPILER_MSVC
	#pragma warning(pop)
#endif



struct SourceFileByPathEntry;

struct SourceFileByPathIterator
{
	CoreData* core;

	u32 curr;

	u32 end;

	bool has_next() const noexcept;

	SourceFileByPathEntry* next() noexcept;
};

struct SourceFileByPathAlloc
{
	CoreData* core;

	SourceFileByPathEntry* value_from_id(u32 id) noexcept;

	const SourceFileByPathEntry* value_from_id(u32 id) const noexcept;

	u32 id_from_value(const SourceFileByPathEntry* value) const noexcept;

	SourceFileByPathIterator values() noexcept;

	SourceFileByPathEntry* alloc(Range<char8> key, u32 key_hash) noexcept;

	void dealloc(u32 id) noexcept;
};

struct SourceFileByIdEntry;

struct SourceFileByIdIterator
{
	CoreData* core;

	u32 curr;

	u32 end;

	bool has_next() const noexcept;

	SourceFileByIdEntry* next() noexcept;
};

struct SourceFileByIdAlloc
{
	CoreData* core;

	SourceFileByIdEntry* value_from_id(u32 id) noexcept;

	const SourceFileByIdEntry* value_from_id(u32 id) const noexcept;

	u32 id_from_value(const SourceFileByIdEntry* value) const noexcept;

	SourceFileByIdIterator values() noexcept;

	SourceFileByIdEntry* alloc(minos::FileIdentity key, u32 key_hash) noexcept;

	void dealloc(u32 id) noexcept;
};

struct SourceReader
{
	IdMap<Range<char8>, SourceFileByPathEntry, SourceFileByPathAlloc> known_files_by_path;

	IdMap<minos::FileIdentity, SourceFileByIdEntry, SourceFileByIdAlloc> known_files_by_identity;

	ReservedVec<byte> path_entries;

	ReservedVec<SourceFileByIdEntry> id_entries;

	u32 curr_source_id_base;

	u32 source_file_count;
};



struct HolotypeInit;

struct Holotype;

struct ImplInit;

struct ImplEntry;

struct HolotypeIterator
{
	CoreData* core;

	u32 curr;

	u32 end;

	bool has_next() const noexcept;

	Holotype* next() noexcept;
};

struct HolotypeAlloc
{
	CoreData* core;

	Holotype* value_from_id(u32 id) noexcept;

	const Holotype* value_from_id(u32 id) const noexcept;

	u32 id_from_value(const Holotype* value) const noexcept;

	HolotypeIterator values() noexcept;

	Holotype* alloc(HolotypeInit key, u32 key_hash) noexcept;

	void dealloc(u32 id) noexcept;
};

struct TypePool
{
	IdMap<HolotypeInit, Holotype, HolotypeAlloc> holotypes;

	ReservedVec<Holotype> holotype_entries;

	CoreId simple_type_base_id;
};



static constexpr u8 COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2 = 4;

static constexpr u8 COMP_HEAP_MAX_FREELIST_SIZE_LOG2 = 18;

static constexpr u64 COMP_HEAP_MIN_ALLOCATION_SIZE = 1 << COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2;

static constexpr u64 COMP_HEAP_MAX_FREELIST_SIZE = 1 << COMP_HEAP_MAX_FREELIST_SIZE_LOG2;

static constexpr u64 COMP_HEAP_ZERO_ADDRESS_MASK = COMP_HEAP_MIN_ALLOCATION_SIZE - 1;

struct CompHeap
{
	byte* memory;

	u64 used;

	u64 commit;

	u64 reserve;

	u64 commit_increment;

	u64* leak_bitmap;

	u64* begin_bitmap;

	u64* header_bitmap;

	u64* gc_bitmap;

	u32 freelists[COMP_HEAP_MAX_FREELIST_SIZE_LOG2 - COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2];
};



struct TempStack
{
	ReservedVec<byte> memory;
};



struct ShadowStoreKey;

struct ShadowStoreEntry;

struct ShadowStoreIterator
{
	CoreData* core;

	u32 curr;

	u32 end;

	bool has_next() const noexcept;

	ShadowStoreEntry* next() noexcept;
};

struct ShadowStoreAlloc
{
	CoreData* core;

	ShadowStoreEntry* value_from_id(u32 id) noexcept;

	const ShadowStoreEntry* value_from_id(u32 id) const noexcept;

	u32 id_from_value(const ShadowStoreEntry* value) const noexcept;

	ShadowStoreIterator values() noexcept;

	ShadowStoreEntry* alloc(ShadowStoreKey key, u32 key_hash) noexcept;

	void dealloc(u32 id) noexcept;
};

struct ShadowLayoutKey;

struct ShadowLayoutEntry;

struct ShadowLayoutIterator
{
	CoreData* core;

	u32 curr;

	u32 end;

	bool has_next() const noexcept;

	ShadowLayoutEntry* next() noexcept;
};

struct ShadowLayoutAlloc
{
	CoreData* core;

	ShadowLayoutEntry* value_from_id(u32 id) noexcept;

	const ShadowLayoutEntry* value_from_id(u32 id) const noexcept;

	u32 id_from_value(const ShadowLayoutEntry* value) const noexcept;

	ShadowLayoutIterator values() noexcept;

	ShadowLayoutEntry* alloc(ShadowLayoutKey key, u32 key_hash) noexcept;

	void dealloc(u32 id) noexcept;
};

struct ShadowStore
{
	IdMap<ShadowStoreKey, ShadowStoreEntry, ShadowStoreAlloc> address_map;

	IdMap<ShadowLayoutKey, ShadowLayoutEntry, ShadowLayoutAlloc> layout_map;

	Maybe<ShadowStoreEntry*> address_entries_freelist_head;

	ReservedVec<ShadowStoreEntry> address_entries;

	ReservedVec<CoreId> layout_ids;
};



struct CoreData
{
	u64 allocation_size;

	const Config* config;

	CompHeap heap;

	TempStack temp;

	AstPool asts;

	ErrorSink errors;

	IdentifierPool identifiers;

	Interpreter interp;

	OpcodePool opcodes;

	Parser parser;

	SourceReader reader;

	TypePool types;

	ShadowStore shadow;

	LexicalAnalyser lex;
};



static constexpr u64 MAX_MEMORY_RANGE_REQUIREMENTS_COUNT = 3;

struct MemoryRangeRequirement
{
	u64 size;

	u64 max_offset;
};

struct MemoryRequirements
{
	u32 count;

	MemoryRangeRequirement ranges[MAX_MEMORY_RANGE_REQUIREMENTS_COUNT];
};

struct MemoryAllocation
{
	MutRange<byte> ranges[MAX_MEMORY_RANGE_REQUIREMENTS_COUNT];
};

#endif // CORE_STRUCTURE_INCLUDE_GUARD

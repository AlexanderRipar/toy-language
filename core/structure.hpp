#ifndef CORE_STRUCTURE_INCLUDE_GUARD
#define CORE_STRUCTURE_INCLUDE_GUARD

#include "core.hpp"

#include "../infra/types.hpp"
#include "../infra/range.hpp"
#include "../infra/hash.hpp"
#include "../infra/container/index_map.hpp"
#include "../infra/container/reserved_heap.hpp"
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

	ReservedVec<ErrorRecord> records;

	Maybe<minos::FileHandle> log_file;
};



struct IdentifierEntry;

struct IdentifierPool
{
	IndexMap<Range<char8>, IdentifierEntry> map;
};



struct Scope;

struct ScopeMember;

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

	ReservedVec<CTValue> values;

	ReservedVec<byte> temporary_data;

	ReservedVec<OpcodeId> activations;

	ReservedVec<u32> call_activation_indices;

	ReservedVec<LoopInfo> loop_stack;

	ReservedVec<CTValue> write_ctxs;

	ReservedVec<ClosureId> active_closures;

	ReservedVec<Maybe<OpcodeId>> argument_callbacks;

	ReservedVec<ArgumentPack> argument_packs;

	ReservedVec<GlobalInitialization> global_initializations;

	ReservedVec<TypeId> selfs;

	bool is_ok;

	BuiltinInfo builtin_infos[static_cast<u8>(Builtin::MAX) - 1];

	Maybe<minos::FileHandle> imported_asts_log_file;

	Maybe<minos::FileHandle> imported_opcodes_log_file;

	Maybe<minos::FileHandle> imported_types_log_file;
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

	// TODO: This might be replaceable with `comp_heap_arena_*`
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

struct SourceFileByIdEntry;

struct SourceReader
{
	IndexMap<Range<char8>, SourceFileByPathEntry> known_files_by_path;

	IndexMap<minos::FileIdentity, SourceFileByIdEntry> known_files_by_identity;

	u32 curr_source_id_base;

	u32 source_file_count;
};



struct HolotypeInit;

struct Holotype;

struct ImplInit;

struct ImplEntry;

struct TypePool
{
	IndexMap<HolotypeInit, Holotype> holotypes;

	CoreId simple_type_base_id;
};



static constexpr u8 COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2 = 4;

static constexpr u8 COMP_HEAP_MAX_ALLOCATION_SIZE_LOG2 = 18;

static constexpr u64 COMP_HEAP_MIN_ALLOCATION_SIZE = 1 << COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2;

static constexpr u64 COMP_HEAP_MAX_ALLOCATION_SIZE = 1 << COMP_HEAP_MAX_ALLOCATION_SIZE_LOG2;

static constexpr u64 COMP_HEAP_ZERO_ADDRESS_MASK = COMP_HEAP_MIN_ALLOCATION_SIZE - 1;

struct CompHeap
{
	byte* memory;

	u64 used;

	u64 commit;

	u64 reserve;

	u64 commit_increment;

	u64 arena_count;

	u64 arena_begin;

	byte* gc_bitmap;

	byte* freelists[COMP_HEAP_MAX_ALLOCATION_SIZE_LOG2 - COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2];
};



struct CoreData
{
	u64 allocation_size;

	const Config* config;

	CompHeap heap;

	AstPool asts;

	ErrorSink errors;

	IdentifierPool identifiers;

	Interpreter interp;

	OpcodePool opcodes;

	Parser parser;

	SourceReader reader;

	TypePool types;

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

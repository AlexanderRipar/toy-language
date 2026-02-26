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

	minos::FileHandle log_file;
};



struct alignas(8) GlobalFile;

struct alignas(8) ForeverValue;

struct GlobalValuePool
{
	ReservedVec<GlobalFile> files;

	ReservedVec<ForeverValue> forever_values;

	ReservedVec<byte> data;
};



struct alignas(8) IdentifierEntry;

struct IdentifierPool
{
	IndexMap2<Range<char8>, IdentifierEntry> map;
};



struct Scope;

struct alignas(8) ScopeMember;

struct LoopInfo;

struct alignas(8) ArgumentPack;

struct alignas(4) GlobalInitialization;

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

	ReservedVec<ScopeMember> closure_members;

	ReservedVec<OpcodeId> argument_callbacks;

	ReservedVec<ArgumentPack> argument_packs;

	ReservedVec<GlobalInitialization> global_initializations;

	bool is_ok;

	BuiltinInfo builtin_infos[static_cast<u8>(Builtin::MAX) - 1];

	minos::FileHandle imported_asts_log_file;

	minos::FileHandle imported_opcodes_log_file;

	minos::FileHandle imported_types_log_file;
};



static constexpr u32 MIN_SCOPE_MAP_SIZE_LOG2 = 6;

static constexpr u32 MAX_SCOPE_MAP_SIZE_LOG2 = 16;

enum class ScopeMapKind : u8;

struct alignas(8) ScopeMap;

struct LexicalAnalyser
{
	ReservedHeap<MIN_SCOPE_MAP_SIZE_LOG2, MAX_SCOPE_MAP_SIZE_LOG2> scope_pool;

	s32 scopes_top;

	GlobalFileIndex active_file_index;

	bool has_error;

	GlobalFileIndex prelude_file_index;

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

struct OpcodePool
{
	OpcodeEffects state;

	OpcodeEffects return_adjust;

	bool allow_return;

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
			ForeverValueId value_id;

			TypeId type_id;
		} string;
	};
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
	IndexMap2<Range<char8>, SourceFileByPathEntry> known_files_by_path;

	IndexMap2<minos::FileIdentity, SourceFileByIdEntry> known_files_by_identity;

	u32 curr_source_id_base;

	u32 source_file_count;
};



static constexpr u32 MIN_STRUCTURE_SIZE_LOG2 = 4;

static constexpr u32 MAX_STRUCTURE_SIZE_LOG2 = 12;

struct DeduplicatedTypeInit;

struct alignas(8) DeduplicatedTypeInfo;

struct TypePool
{
	IndexMap2<DeduplicatedTypeInit, DeduplicatedTypeInfo> dedup;

	ReservedHeap<MIN_STRUCTURE_SIZE_LOG2, MAX_STRUCTURE_SIZE_LOG2> structures;

	ReservedVec<u64> scratch;
};



struct CoreData
{
	u64 allocation_size;

	const Config* config;

	AstPool asts;

	ErrorSink errors;

	GlobalValuePool globals;

	IdentifierPool identifiers;

	Interpreter interp;

	OpcodePool opcodes;

	Parser parser;

	SourceReader reader;

	TypePool types;

	LexicalAnalyser lex;
};



static constexpr u64 MAX_MEMORY_ID_REQUIREMENTS_COUNT = 3;

struct MemoryIdRequirements
{
	u32 reserve;

	u32 alignment;
};

struct MemoryRequirements
{
	u32 private_reserve;

	u32 id_requirements_count;

	MemoryIdRequirements id_requirements[MAX_MEMORY_ID_REQUIREMENTS_COUNT];
};

struct MemoryAllocation
{
	byte* private_data;

	byte* ids[MAX_MEMORY_ID_REQUIREMENTS_COUNT];
};

#endif // CORE_STRUCTURE_INCLUDE_GUARD

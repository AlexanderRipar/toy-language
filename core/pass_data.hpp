#ifndef PASS_DATA_INCLUDE_GUARD
#define PASS_DATA_INCLUDE_GUARD

#include "../infra/common.hpp"
#include "../infra/alloc_pool.hpp"
#include "../infra/optptr.hpp"
#include "../infra/minos.hpp"



// Forward declarations.
// These are necessary because the modules defining them would otherwise appear
// after those using them.

// Id used to refer to a type in the `TypePool`. See `TypePool` for further
// information.
struct TypeId
{
	u32 rep;
};

// A `TypeId` with one bit used for indicating mutability.
struct TypeIdWithAssignability
{
	u32 type_id : 31;

	u32 is_mut : 1;
};

// Id used to identify a particular source code location.
// This encodes the location's file, line and column. See `SourceReader` for
// further information.
struct SourceId
{
	u32 m_rep;
};

// Id used to reference values with global lifetime.
// This includes the values of global variables, as well as default values. See
// `GlobalValuePool` for further information
struct GlobalValueId
{
	u32 rep;
};





// Structure holding config parameters used to parameterize the further
// compilation process.
// This is filled in by `create_config` and must only be read afterwards.
// To free it, use `release_config`.
struct Config
{
	struct
	{
		Range<char8> filepath = range::from_literal_string("main.evl");

		Range<char8> symbol = range::from_literal_string("main");
	} entrypoint;

	struct
	{
		Range<char8> filepath = range::from_literal_string("std.evl");
	} std;

	struct
	{
		struct
		{
			bool enable = false;

			Range<char8> log_filepath = {};
		} asts;

		struct
		{
			bool enable = false;

			Range<char8> log_filepath = {};
		} config;
	} logging;


	void* m_heap_ptr;

	Range<char8> m_config_filepath;
};

// Parses the file at `filepath` into a `Config` struct that is allocated into
// `alloc`. The file is expected to be in [TOML](https://toml.io) format.
Config* create_config(AllocPool* alloc, Range<char8> filepath) noexcept;

// Releases resources associated with a `Config` that was previously returned
// from `create_config`.
void release_config(Config* config) noexcept;

// Prints the values in the given `Config` to `out` in a readable, JSON-like
// format.
void print_config(minos::FileHandle out, const Config* config) noexcept;

// Prints help and explanations for the supported config parameters to the
// standard error stream. Only parameters that are nested less than `depth` are
// included in the output. If `depth` is set to `0`, all parameters are
// printed, regardless of depth.
void print_config_help(u32 depth = 0) noexcept;





// Identifier Pool.
// This deduplicates identifiers, making it possible to refer to them with
// fixed-size `IdentifierId`s.
// Additionally supports storing an 8-bit attachment for each identifier, which
// is currently used to distinguish keywords and builtins from other
// identifiers.
// This is created by `create_identifier_pool` and freed by
// `release_identifier_pool`.
struct IdentifierPool;

// Id used to refer to an identifier. Obtained from `id_from_identifier` or
// `id_and_attachment_from_identifier` and usable with
// `identifier_name_from_id` to retrieve associated name.
struct IdentifierId
{
	u32 rep;
};

// Used to indicate that there is no identifier associated with a construct. No
// valid identifier will ever map to this `IdentifierId`.
static constexpr IdentifierId INVALID_IDENTIFIER_ID = { 0 };

static inline bool operator==(IdentifierId lhs, IdentifierId rhs) noexcept
{
	return lhs.rep == rhs.rep;
}

static inline bool operator!=(IdentifierId lhs, IdentifierId rhs) noexcept
{
	return lhs.rep != rhs.rep;
}

// Creates an `IdentifierPool`, allocating the necessary storage from `alloc`.
// Resources associated with the created `IdentifierPool` can be freed using
// `release_identifier_pool`.
IdentifierPool* create_identifier_pool(AllocPool* alloc) noexcept;

// Releases the resources associated with the given `IdentifierPool`.
void release_identifier_pool(IdentifierPool* identifiers) noexcept;

// Returns the unique `IdentifierId` that corresponds to the given `identifier`
// in `identifiers`. All calls with the same `IdentifierPool` and same
// byte-for-byte `identifier` are guaranteed to return the same `IdentifierId`.
// All calls to the same `IdentifierPool` with distinct `identifier`s are
// guaranteed to return distinct `IdentifierId`s.
IdentifierId id_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept;

// Same as `id_from_identifier`, but additionally sets `*out_attachment` to the
// value previously set for the given `identifier` by
// `identifier_set_attachment`, or `0` if no attachment has been set.
// Note that this call will never return `INVALID_IDENTIFIER_ID`.
IdentifierId id_and_attachment_from_identifier(IdentifierPool* identifiers, Range<char8> identifier, u8* out_attachment) noexcept;

// Sets the attachment associated with `identifier` in the given
// `IdentifierPool` to the given `attachment`. The given `identifier` must not
// have previously had an attachment set.
// Additionally, `attachment` must not be `0`.
void identifier_set_attachment(IdentifierPool* identifiers, Range<char8> identifier, u8 attachment) noexcept;

// Returns the byte-sequence corresponding to the given `IdentifierId` in the
// given `IdentifierPool`. `id` must not be `INVALID_IDENTIFIER_ID` and must
// have been returned from a previous call to `id_from_identifier` or
// `id_and_attachment_from_identifier`.
Range<char8> identifier_name_from_id(const IdentifierPool* identifiers, IdentifierId id) noexcept;




// Representation of a compile-time known, arbitrary-width signed integer.
// This is used to represent integer literals, and arithmetic involving them.
// Currently, it really only supports values in the range [-2^62, 2^62-1].
struct CompIntegerValue
{
	u64 rep;
};

// Representation of a compile-time known floating-point value.
struct CompFloatValue
{
	f64 rep;
};

// Creates a `CompIntegerValue` representing the given unsigned 64-bit value.
CompIntegerValue comp_integer_from_u64(u64 value) noexcept;

// Creates a `CompIntegerValue` representing the given signed 64-bit value.
CompIntegerValue comp_integer_from_s64(s64 value) noexcept;

// Creates a `CompIntegerValue` representing the given floating point value.
// If the given `value` is `+/-inf` or `nan`, the function returns `false` and
// leaves `*out` uninitialized. The same occurs if `round` is false and `value`
// does not represent a whole number.
// Otherwise, the function returns `true` and initializes `*out` with the
// integer value corresponding to the given floating point `value`.
bool comp_integer_from_comp_float(CompFloatValue value, bool round, CompIntegerValue* out) noexcept;

// Attempts to extract the value of the given `CompIntegerValue` into a `u64`.
// If the value is outside the range of a 64-bit unsigned integer, `false` is
// returned and `*out` is left uninitialized. Otherwise `true` is returned and
// `*out` contains the value of the given `CompIntegerValue`.
bool u64_from_comp_integer(CompIntegerValue value, u64* out) noexcept;

// Attempts to extract the value of the given `CompIntegerValue` into a `s64`.
// If the value is outside the range of a 64-bit signed integer, `false` is
// returned and `*out` is left uninitialized. Otherwise `true` is returned and
// `*out` contains the value of the given `CompIntegerValue`.
bool s64_from_comp_integer(CompIntegerValue value, s64* out) noexcept;

// Adds `lhs` and `rhs` together, returning a new `CompIntegerValue`
// representing the result.
CompIntegerValue comp_integer_add(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;

// Subtracts `rhs` from 'lhs', returning a new `CompIntegerValue` representing
// the result.
CompIntegerValue comp_integer_sub(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;

// Multiplies `lhs` with `rhs`, returning a new `CompIntegerValue` representing
// the result.
CompIntegerValue comp_integer_mul(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;

// Performs truncating division of `lhs` by 'rhs'.
// If `rhs` is `0`, `false` is returned and `*out` is left uninitialized.
// Otherwise, `true` is returned and `*out` receives the resulting value.
bool comp_integer_div(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Takes the module of `lhs` by 'rhs'.
// If `rhs` is `0`, `false` is returned and `*out` is left uninitialized.
// Otherwise, `true` is returned and `*out` receives the resulting value.
bool comp_integer_mod(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Flips the sign of the given `CompIntegerValue`, returning a new
// `CompIntegerValue` holding the resulting value.
CompIntegerValue comp_integer_neg(CompIntegerValue value) noexcept;

// Shift `lhs` left by `rhs` bits, effectively calculating `lhs * 2^rhs`.
// If `rhs` is negative, returns `false` and leaves `*out` uninitialized.
// Otherwise returns `true` and sets `*out` to the resulting value.
bool comp_integer_shift_left(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Shift `lhs` right by `rhs` bits, effectively calculating `lhs / 2^rhs` where
// `/` is truncating division.
// If `rhs` is negative, returns `false` and leaves `*out` uninitialized.
// Otherwise returns `true` and sets `*out` to the resulting value.
bool comp_integer_shift_right(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Takes the bitwise and of `lhs` and `rhs`.
// If either `lhs` or `rhs` are negative, returns `false` and leaves `*out`
// uninitialized.
// Otherwise returns `true` and sets `*out` to the resulting value.
bool comp_integer_bit_and(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Takes the bitwise or of `lhs` and `rhs`.
// If either `lhs` or `rhs` are negative, returns `false` and leaves `*out`
// uninitialized.
// Otherwise returns `true` and sets `*out` to the resulting value.
bool comp_integer_bit_or(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Takes the bitwise exclusive or of `lhs` and `rhs`.
// If either `lhs` or `rhs` are negative, returns `false` and leaves `*out`
// uninitialized.
// Otherwise returns `true` and sets `*out` to the resulting value.
bool comp_integer_bit_xor(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Creates a `CompFloatValue` representing the given double-precision float
// value.
CompFloatValue comp_float_from_f64(f64 value) noexcept;

// Creates a `CompFloatValue` representing the given single-precision float
// value.
CompFloatValue comp_float_from_f32(f32 value) noexcept;

// Attempts to crete a `CompFloatValue` representing the given 64-bit unsigned
// integer. If the integer is not exactly representable, `false` is returned
// and `*out` is left uninitialized.
// Otherwise `true` is returned and `*out` receives the resulting value.
bool comp_float_from_u64(u64 value, CompFloatValue* out) noexcept;

// Attempts to crete a `CompFloatValue` representing the given 64-bit signed
// integer. If the integer is not exactly representable, `false` is returned
// and `*out` is left uninitialized.
// Otherwise `true` is returned and `*out` receives the resulting value.
bool comp_float_from_s64(s64 value, CompFloatValue* out) noexcept;

// Attempts to crete a `CompFloatValue` representing the given
// `CompIntegerValue`. If the integer is not exactly representable, `false`
// is returned and `*out` is left uninitialized.
// Otherwise `true` is returned and `*out` receives the resulting value.
bool comp_float_from_comp_integer(CompIntegerValue value, CompFloatValue* out) noexcept;

// Returns the value represented by the given `CompFloatValue` as a
// double-precision float.
f64 f64_from_comp_float(CompFloatValue value) noexcept;

// Returns the value represented by the given `CompFloatValue` as a
// single-precision float.
f32 f32_from_comp_float(CompFloatValue value) noexcept;

// Adds `lhs` and `rhs` together, returning a new `CompFloatValue` representing
// the result.
CompFloatValue comp_float_add(CompFloatValue lhs, CompFloatValue rhs) noexcept;

// Subtracts `rhs` from `lhs`, returning a new `CompFloatValue` representing
// the result.
CompFloatValue comp_float_sub(CompFloatValue lhs, CompFloatValue rhs) noexcept;

// Multiplies `lhs` with `rhs`, returning a new `CompFloatValue` representing
// the result.
CompFloatValue comp_float_mul(CompFloatValue lhs, CompFloatValue rhs) noexcept;

// Divides `lhs` by `rhs`, returning a new `CompFloatValue` representing the
// result.
CompFloatValue comp_float_div(CompFloatValue lhs, CompFloatValue rhs) noexcept;

// Takes the modulo of `lhs` by `rhs`, returning a new `CompFloatValue`
// representing the result.
CompFloatValue comp_float_neg(CompFloatValue value) noexcept;





struct AstPool;

static constexpr s32 MAX_AST_DEPTH = 128;

enum class AstTag : u8
{
	INVALID = 0,
	Builtin,
	File,
	CompositeInitializer,
	ArrayInitializer,
	Wildcard,
	Where,
	Expects,
	Ensures,
	Definition,
	Block,
	If,
	For,
	ForEach,
	Switch,
	Case,
	Func,
	Trait,
	Impl,
	Catch,
	Identifer,
	LitInteger,
	LitFloat,
	LitChar,
	LitString,
	Return,
	Leave,
	Yield,
	ParameterList,
	Call,
	UOpTypeTailArray,
	UOpTypeSlice,
	UOpTypeMultiPtr,
	UOpTypeOptMultiPtr,
	UOpEval,
	UOpTry,
	UOpDefer,
	UOpDistinct,
	UOpAddr,
	UOpDeref,
	UOpBitNot,
	UOpLogNot,
	UOpTypeOptPtr,
	UOpTypeVar,
	UOpImpliedMember,
	UOpTypePtr,
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
	OpTypeArray,
	OpArrayIndex,
	MAX,
};

enum class AstFlag : u8
{
	EMPTY                = 0,

	Definition_IsPub     = 0x01,
	Definition_IsMut     = 0x02,
	Definition_IsGlobal  = 0x04,
	Definition_IsAuto    = 0x08,
	Definition_IsUse     = 0x10,
	Definition_HasType   = 0x20,

	If_HasWhere          = 0x20,
	If_HasElse           = 0x01,

	For_HasCondition     = 0x01,
	For_HasWhere         = 0x20,
	For_HasStep          = 0x02,
	For_HasFinally       = 0x04,

	ForEach_HasWhere     = 0x20,
	ForEach_HasIndex     = 0x01,
	ForEach_HasFinally   = 0x02,

	Switch_HasWhere      = 0x20,

	Func_HasExpects      = 0x01,
	Func_HasEnsures      = 0x02,
	Func_IsProc          = 0x04,
	Func_HasReturnType   = 0x08,
	Func_HasBody         = 0x10,

	Trait_HasExpects     = 0x01,

	Impl_HasExpects      = 0x01,

	Catch_HasDefinition  = 0x01,

	Type_IsMut           = 0x02,
};

struct AstNodeId
{
	u32 rep;
};

struct AstNode
{
	static constexpr u8 FLAG_LAST_SIBLING  = 0x01;
	static constexpr u8 FLAG_FIRST_SIBLING = 0x02;
	static constexpr u8 FLAG_NO_CHILDREN   = 0x04;

	AstTag tag;

	AstFlag flags;

	u8 data_dwords;

	u8 internal_flags;

	u32 next_sibling_offset;

	TypeIdWithAssignability type_id;

	SourceId source_id;
};

struct AstBuilderToken
{
	u32 rep;
};

struct AstIterationResult
{
	AstNode* node;

	u32 depth;
};

struct AstDirectChildIterator
{
	AstNode* curr;
};

struct AstPreorderIterator
{
	AstNode* curr;

	u8 depth;

	s32 top;

	u8 prev_depths[MAX_AST_DEPTH];

	static_assert(MAX_AST_DEPTH <= UINT8_MAX);
};

struct AstPostorderIterator
{
	AstNode* base;

	s32 depth;

	u32 offsets[MAX_AST_DEPTH];
};

struct AstLitIntegerData
{
	static constexpr AstTag TAG = AstTag::LitInteger;

	#pragma pack(push)
	#pragma pack(4)
	CompIntegerValue value;
	#pragma pack(pop)
};

struct AstLitFloatData
{
	static constexpr AstTag TAG = AstTag::LitFloat;

	#pragma pack(push)
	#pragma pack(4)
	CompFloatValue value;
	#pragma pack(pop)
};

struct AstLitCharData
{
	static constexpr AstTag TAG = AstTag::LitChar;

	u32 codepoint;
};

struct AstIdentifierData
{
	static constexpr AstTag TAG = AstTag::Identifer;

	IdentifierId identifier_id;
};

struct AstLitStringData
{
	static constexpr AstTag TAG = AstTag::LitString;

	GlobalValueId string_value_id;
};

struct AstDefinitionData
{
	static constexpr AstTag TAG = AstTag::Definition;

	IdentifierId identifier_id;
};

struct AstBlockData
{
	static constexpr AstTag TAG = AstTag::Block;

	TypeId scope_type_id;
};

struct FuncInfo
{
	AstNode* parameters;

	OptPtr<AstNode> return_type;

	OptPtr<AstNode> expects;

	OptPtr<AstNode> ensures;

	OptPtr<AstNode> body;
};

struct DefinitionInfo
{
	OptPtr<AstNode> type;

	OptPtr<AstNode> value;
};

struct IfInfo
{
	AstNode* condition;

	AstNode* consequent;

	OptPtr<AstNode> alternative;

	OptPtr<AstNode> where;
};

struct ForInfo
{
	AstNode* condition;

	OptPtr<AstNode> step;

	OptPtr<AstNode> where;

	AstNode* body;

	OptPtr<AstNode> finally;
};

struct ForEachInfo
{
	AstNode* element;

	OptPtr<AstNode> index;

	AstNode* iterated;

	OptPtr<AstNode> where;

	AstNode* body;

	OptPtr<AstNode> finally;
};

static constexpr AstBuilderToken AST_BUILDER_NO_CHILDREN = { ~0u };

inline AstFlag operator|(AstFlag lhs, AstFlag rhs) noexcept
{
	return static_cast<AstFlag>(static_cast<u8>(lhs) | static_cast<u8>(rhs));
}

inline AstFlag operator&(AstFlag lhs, AstFlag rhs) noexcept
{
	return static_cast<AstFlag>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
}

inline AstFlag& operator|=(AstFlag& lhs, AstFlag rhs) noexcept
{
	lhs = lhs | rhs;

	return lhs;
}

inline AstFlag& operator&=(AstFlag& lhs, AstFlag rhs) noexcept
{
	lhs = lhs & rhs;

	return lhs;
}

inline bool operator==(AstNodeId lhs, AstNodeId rhs) noexcept
{
	return lhs.rep == rhs.rep;
}

inline bool operator!=(AstNodeId lhs, AstNodeId rhs) noexcept
{
	return lhs.rep != rhs.rep;
}

static inline bool operator==(AstBuilderToken lhs, AstBuilderToken rhs) noexcept
{
	return lhs.rep == rhs.rep;
}

static inline bool operator!=(AstBuilderToken lhs, AstBuilderToken rhs) noexcept
{
	return lhs.rep != rhs.rep;
}

static constexpr AstNodeId INVALID_AST_NODE_ID = { 0 };

AstPool* create_ast_pool(AllocPool* pool) noexcept;

void release_ast_pool(AstPool* asts) noexcept;

AstNode* alloc_ast(AstPool* asts, u32 dwords) noexcept;

AstNodeId id_from_ast_node(AstPool* asts, AstNode* node) noexcept;

AstNode* ast_node_from_id(AstPool* asts, AstNodeId id) noexcept;

static inline AstNode* apply_offset_(AstNode* node, ureg offset) noexcept
{
	static_assert(sizeof(AstNode) % sizeof(u32) == 0 && alignof(AstNode) % sizeof(u32) == 0);

	return reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(node) + offset);
}

static inline bool has_children(const AstNode* node) noexcept
{
	return (node->internal_flags & AstNode::FLAG_NO_CHILDREN) == 0;
}

static inline bool has_next_sibling(const AstNode* node) noexcept
{
	return (node->internal_flags & AstNode::FLAG_LAST_SIBLING) == 0;
}

static inline bool has_flag(AstNode* node, AstFlag flag) noexcept
{
	return (static_cast<u8>(node->flags) & static_cast<u8>(flag)) != 0;
}

static inline AstNode* next_sibling_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(has_next_sibling(node));

	return apply_offset_(node, node->next_sibling_offset);
}

static inline AstNode* first_child_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(has_children(node));

	return apply_offset_(node, node->data_dwords);
}

template<typename T>
static inline T* attachment_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(T::TAG == node->tag);

	ASSERT_OR_IGNORE(sizeof(T) + sizeof(AstNode) == node->data_dwords * sizeof(u32));

	return reinterpret_cast<T*>(node + 1);
}

template<typename T>
static inline const T* attachment_of(const AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(T::TAG == node->tag);

	ASSERT_OR_IGNORE(sizeof(T) + sizeof(AstNode) == node->data_dwords * sizeof(u32));

	return reinterpret_cast<const T*>(node + 1);
}

static inline bool is_valid(AstIterationResult result) noexcept
{
	return result.node != nullptr;
}

AstDirectChildIterator direct_children_of(AstNode* node) noexcept;

OptPtr<AstNode> next(AstDirectChildIterator* iterator) noexcept;

AstPreorderIterator preorder_ancestors_of(AstNode* node) noexcept;

AstIterationResult next(AstPreorderIterator* iterator) noexcept;

AstPostorderIterator postorder_ancestors_of(AstNode* node) noexcept;

AstIterationResult next(AstPostorderIterator* iterator) noexcept;

AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag) noexcept;

AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag, u8 attachment_dwords, const void* attachment) noexcept;

template<typename T>
static inline AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, T attachment) noexcept
{
	static_assert(sizeof(T) % sizeof(u32) == 0);

	return push_node(asts, first_child, source_id, flags, T::TAG, sizeof(attachment) / sizeof(u32), &attachment);
}

AstNode* complete_ast(AstPool* asts) noexcept;

const char8* tag_name(AstTag tag) noexcept;

FuncInfo get_func_info(AstNode* func) noexcept;

DefinitionInfo get_definition_info(AstNode* definition) noexcept;

IfInfo get_if_info(AstNode* node) noexcept;

ForInfo get_for_info(AstNode* node) noexcept;

ForEachInfo get_foreach_info(AstNode* node) noexcept;





struct SourceReader;

struct SourceFile
{
	minos::FileHandle file;

	AstNodeId ast_root;

	SourceId source_id_base;
};

struct SourceFileRead
{
	SourceFile* source_file;

	Range<char8> content;
};

struct SourceLocation
{
	Range<char8> filepath;

	u32 line_number;

	u32 column_number;

	u32 context_offset;

	u32 context_chars;

	char8 context[512];
};

static inline bool operator==(SourceId lhs, SourceId rhs) noexcept
{
	return lhs.m_rep == rhs.m_rep;
}

static inline bool operator!=(SourceId lhs, SourceId rhs) noexcept
{
	return lhs.m_rep != rhs.m_rep;
}

static constexpr SourceId INVALID_SOURCE_ID = { 0 };

SourceReader* create_source_reader(AllocPool* pool) noexcept;

void release_source_reader(SourceReader* reader) noexcept;

SourceFileRead read_source_file(SourceReader* reader, Range<char8> filepath) noexcept;

void release_read(SourceReader* reader, SourceFileRead read) noexcept;

SourceLocation source_location_from_ast_node(SourceReader* reader, AstNode* node) noexcept;

SourceLocation source_location_from_source_id(SourceReader* reader, SourceId source_id) noexcept;

SourceFile* source_file_from_source_id(SourceReader* reader, SourceId source_id) noexcept;

Range<char8> source_file_path(SourceReader* reader, SourceFile* source_file) noexcept;





struct ErrorSink;

ErrorSink* create_error_sink(AllocPool* pool, SourceReader* reader, IdentifierPool* identifiers) noexcept;

void release_error_sink(ErrorSink* errors) noexcept;

NORETURN void source_error(ErrorSink* errors, SourceId source_id, const char8* format, ...) noexcept;

NORETURN void vsource_error(ErrorSink* errors, SourceId source_id, const char8* format, va_list args) noexcept;

void source_warning(ErrorSink* errors, SourceId source_id, const char8* format, ...) noexcept;

void vsource_warning(ErrorSink* errors, SourceId source_id, const char8* format, va_list args) noexcept;

void print_error(const SourceLocation* location, const char8* format, va_list args) noexcept;





struct TypePool;

enum class TypeTag : u8
{
	INVALID = 0,
	Void,
	Type,
	Definition,
	CompInteger,
	CompFloat,
	Integer,
	Float,
	Boolean,
	Slice,
	Ptr,
	Array,
	Func,
	Builtin,
	Composite,
	CompositeLiteral,
	ArrayLiteral,
	TypeBuilder,
	Variadic,
	Divergent,
	Trait,
	TypeInfo,
};

struct TypeMetrics
{
	u64 size;

	u64 stride;

	u32 align;
};

struct IncompleteMemberIterator
{
	void* structure;

	void* name;

	u16 rank;

	TypeId type_id;
};

struct MemberIterator
{
	void* structure;

	void* name;

	u16 rank;

	TypeId type_id;
};

union DelayableTypeId
{
	TypeId complete;

	AstNodeId pending;
};

union DelayableValueId
{
	GlobalValueId complete;

	AstNodeId pending;
};

struct MemberInfo
{
	IdentifierId name;

	SourceId source;

	DelayableTypeId type;

	DelayableValueId value;

	bool is_global : 1;

	bool is_pub : 1;

	bool is_use : 1;

	bool is_mut : 1;

	bool has_pending_type : 1;

	bool has_pending_value : 1;

	u16 rank;

	TypeId completion_context_type_id;

	TypeId surrounding_type_id;

	s64 offset;
};

struct MemberInit
{
	IdentifierId name;

	SourceId source;

	DelayableTypeId type;

	DelayableValueId value;

	TypeId lexical_parent_type_id;

	bool is_global : 1;

	bool is_pub : 1;

	bool is_use : 1;

	bool is_mut : 1;

	bool has_pending_type : 1;

	bool has_pending_value : 1;

	s64 offset;
};

struct ReferenceType
{
	TypeId referenced_type_id;

	bool is_opt;

	bool is_multi;

	bool is_mut;

	u8 unused_ = 0;
};

struct NumericType
{
	u16 bits;

	bool is_signed;

	u8 unused_ = 0;
};

struct ArrayType
{
	u64 element_count;

	TypeId element_type;

	u32 unused_ = 0;
};

struct FuncType
{
	TypeId return_type_id;

	u16 param_count;

	bool is_proc;

	TypeId signature_type_id;
};

static constexpr TypeId INVALID_TYPE_ID = { 0 };

static constexpr TypeId CHECKING_TYPE_ID = { 1 };

static constexpr TypeId NO_TYPE_TYPE_ID = { 2 };

TypePool* create_type_pool(AllocPool* alloc, ErrorSink* errors) noexcept;

void release_type_pool(TypePool* types) noexcept;

const char8* tag_name(TypeTag tag) noexcept;

TypeId primitive_type(TypePool* types, TypeTag tag, Range<byte> data) noexcept;

TypeId alias_type(TypePool* types, TypeId aliased_type_id, bool is_distinct, SourceId source_id, IdentifierId name_id) noexcept;

IdentifierId type_name_from_id(const TypePool* types, TypeId type_id) noexcept;

SourceId type_source_from_id(const TypePool* types, TypeId type_id) noexcept;

TypeId type_lexical_parent_from_id(const TypePool* types, TypeId type_id) noexcept;

TypeId create_open_type(TypePool* types, TypeId lexical_parent_type_id, SourceId source_id) noexcept;

void add_open_type_member(TypePool* types, TypeId open_type_id, MemberInit member) noexcept;

void close_open_type(TypePool* types, TypeId open_type_id, u64 size, u32 align, u64 stride) noexcept;

void set_incomplete_type_member_type_by_rank(TypePool* types, TypeId open_type_id, u16 rank, TypeId member_type_id) noexcept;

void set_incomplete_type_member_value_by_rank(TypePool* types, TypeId open_type_id, u16 rank, GlobalValueId member_value_id) noexcept;

TypeMetrics type_metrics_from_id(TypePool* types, TypeId type_id) noexcept;

TypeTag type_tag_from_id(TypePool* types, TypeId type_id) noexcept;

bool type_can_implicitly_convert_from_to(TypePool* types, TypeId from_type_id, TypeId to_type_id) noexcept;

TypeId common_type(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept;

bool type_member_info_by_name(TypePool* types, TypeId type_id, IdentifierId member_name_id, MemberInfo* out) noexcept;

bool type_member_info_by_rank(TypePool* types, TypeId type_id, u16 rank, MemberInfo* out) noexcept;

const void* primitive_type_structure(TypePool* types, TypeId type_id) noexcept;



IncompleteMemberIterator incomplete_members_of(TypePool* types, TypeId type_id) noexcept;

MemberInfo next(IncompleteMemberIterator* it) noexcept;

bool has_next(IncompleteMemberIterator* it) noexcept;

MemberIterator members_of(TypePool* types, TypeId type_id) noexcept;

MemberInfo next(MemberIterator* it) noexcept;

bool has_next(MemberIterator* it) noexcept;





struct GlobalValuePool;

static constexpr GlobalValueId INVALID_GLOBAL_VALUE_ID = { 0 };

GlobalValuePool* create_global_value_pool(AllocPool* alloc, TypePool* types) noexcept;

void release_global_value_pool(GlobalValuePool* globals) noexcept;

GlobalValueId make_global_value(GlobalValuePool* globals, u64 size, u32 align, const void* opt_initial_value) noexcept;

void* global_value_from_id(GlobalValuePool* globals, GlobalValueId value_id) noexcept;





struct Parser;

Parser* create_parser(AllocPool* pool, IdentifierPool* identifiers, GlobalValuePool* globals, ErrorSink* errors, minos::FileHandle log_file) noexcept;

AstNode* parse(Parser* parser, Range<char8> content, SourceId base_source_id, bool is_std, Range<char8> filepath) noexcept;





struct Interpreter;

enum class Builtin : u8
{
	INVALID = 0,
	Integer,
	Type,
	Typeof,
	Returntypeof,
	Sizeof,
	Alignof,
	Strideof,
	Offsetof,
	Nameof,
	Import,
	CreateTypeBuilder,
	AddTypeMember,
	CompleteType,
	MAX,
};

static inline TypeIdWithAssignability with_assignability(TypeId type_id, bool is_assignable) noexcept
{
	return TypeIdWithAssignability{ type_id.rep, is_assignable };
}

static inline bool is_assignable(TypeIdWithAssignability id) noexcept
{
	return id.is_mut;
}

static inline TypeId type_id(TypeIdWithAssignability id) noexcept
{
	return TypeId{ id.type_id };
}

Interpreter* create_interpreter(AllocPool* alloc, Config* config, SourceReader* reader, Parser* parser, TypePool* types, AstPool* asts, IdentifierPool* identifiers, GlobalValuePool* globals, ErrorSink* errors) noexcept;

void release_interpreter(Interpreter* interp) noexcept;

TypeId import_file(Interpreter* interp, Range<char8> filepath, bool is_std) noexcept;

const char8* tag_name(Builtin builtin) noexcept;

#endif // PASS_DATA_INCLUDE_GUARD

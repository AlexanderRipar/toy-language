#include "core.hpp"

#include <cstdarg>
#include <cstdlib>
#include <csetjmp>
#include <errno.h>

#include "../diag/diag.hpp"
#include "../infra/common.hpp"
#include "../infra/container/reserved_vec.hpp"
#include "../infra/hash.hpp"

static constexpr u32 MAX_STRING_LITERAL_BYTES = 4096;

enum class Token : u8
{
		EMPTY = 0,
		KwdIf,                // if
		KwdThen,              // then
		KwdElse,              // else
		KwdFor,               // for
		KwdDo,                // do
		KwdFinally,           // finally
		KwdSwitch,            // switch
		KwdCase,              // case
		KwdFunc,              // func
		KwdProc,              // proc
		KwdTrait,             // trait
		KwdImpl,              // impl
		KwdWhere,             // where
		KwdExpects,           // expects
		KwdEnsures,           // ensures
		KwdCatch,             // catch
		KwdLet,               // let
		KwdPub,               // pub
		KwdMut,               // mut
		KwdUnreachable,       // unreachable
		KwdUndefined,         // undefined
		KwdReturn,            // return
		KwdLeave,             // leave
		KwdYield,             // yield
		OpMemberOrRef,        // .
		DoubleDot,            // ..
		ArrayInitializer,     // .[
		CompositeInitializer, // .{
		BracketR,             // ]
		BracketL,             // [
		CurlyR,               // }
		CurlyL,               // {
		ParenR,               // )
		ParenL,               // (
		KwdEval,              // eval
		KwdTry,               // try
		KwdDefer,             // defer
		KwdDistinct,          // distinct
		UOpNot,               // ~
		UOpLogNot,            // !
		TypOptPtr,            // ?
		TypVar,               // ...
		TypTailArray,         // [...]
		TypMultiPtr,          // [*]
		TypOptMultiPtr,       // [?]
		TypSlice,             // []
		OpMulOrTypPtr,        // *
		OpSub,                // -
		OpAdd,                // +
		OpDiv,                // /
		OpAddTC,              // +:
		OpSubTC,              // -:
		OpMulTC,              // *:
		OpMod,                // %
		UOpAddr,              // .&
		UOpDeref,             // .*
		OpAnd,                // &
		OpOr,                 // |
		OpXor,                // ^
		OpShl,                // <<
		OpShr,                // >>
		OpLogAnd,             // &&
		OpLogOr,              // ||
		OpLt,                 // <
		OpGt,                 // >
		OpLe,                 // <=
		OpGe,                 // >=
		OpNe,                 // !=
		OpEq,                 // ==
		OpSet,                // =
		OpSetAdd,             // +=
		OpSetSub,             // -=
		OpSetMul,             // *=
		OpSetDiv,             // /=
		OpSetAddTC,           // +:=
		OpSetSubTC,           // -:=
		OpSetMulTC,           // *:=
		OpSetMod,             // %=
		OpSetAnd,             // &=
		OpSetOr,              // |=
		OpSetXor,             // ^=
		OpSetShl,             // <<=
		OpSetShr,             // >>=
		Colon,                // :
		Comma,                // ,
		ThinArrowL,           // <-
		ThinArrowR,           // ->
		WideArrowR,           // =>
		Pragma,               // #
		LitInteger,           // ( '0' - '9' )+
		LitFloat,             // ( '0' - '9' )+ '.' ( '0' - '9' )+
		LitChar,              // '\'' .* '\''
		LitString,            // '"' .* '"'
		Ident,                // ( 'a' - 'z' | 'A' - 'Z' ) ( 'a' - 'z' | 'A' - 'Z' | '0' - '9' | '_' )*
		Builtin,              // '_' ( 'a' - 'z' | 'A' - 'Z' | '0' - '9' | '_' )+    --- Only if is_std == true
		Wildcard,             // _
		END_OF_SOURCE,
		MAX,
};

const char8* token_name(Token token) noexcept
{
	static constexpr const char8* const TOKEN_NAMES[] = {
		"[Unknown]",
		"if",
		"then",
		"else",
		"for",
		"do",
		"finally",
		"switch",
		"case",
		"func",
		"proc",
		"trait",
		"impl",
		"where",
		"expects",
		"ensures",
		"catch",
		"let",
		"pub",
		"mut",
		"unreachable",
		"undefined",
		"return",
		"leave",
		"yield",
		".",
		"..",
		".[",
		".{",
		"]",
		"[",
		"}",
		"{",
		")",
		"(",
		"eval",
		"try",
		"defer",
		"distinct",
		"~",
		"!",
		"?",
		"...",
		"[...]",
		"[*]",
		"[?]",
		"[]",
		"*",
		"-",
		"+",
		"/",
		"+:",
		"-:",
		"*:",
		"%",
		".&",
		".*",
		"&",
		"|",
		"^",
		"<<",
		">>",
		"&&",
		"||",
		"<",
		">",
		"<=",
		">=",
		"!=",
		"==",
		"=",
		"+=",
		"-=",
		"*=",
		"/=",
		"+:=",
		"-:=",
		"*:=",
		"%=",
		"&=",
		"|=",
		"^=",
		"<<=",
		">>=",
		":",
		",",
		"<-",
		"->",
		"=>",
		"#",
		"LiteralInteger",
		"LiteralFloat",
		"LiteralChar",
		"LiteralString",
		"Identifier",
		"Builtin",
		"_",
		"[END-OF-SOURCE]",
	};

	const u8 ordinal = static_cast<u8>(token);

	if (ordinal < array_count(TOKEN_NAMES))
		return TOKEN_NAMES[ordinal];

	return TOKEN_NAMES[0];
}

static constexpr AttachmentRange<char8, u8> KEYWORDS[] = {
	range::from_literal_string("if",          static_cast<u8>(Token::KwdIf)),
	range::from_literal_string("then",        static_cast<u8>(Token::KwdThen)),
	range::from_literal_string("else",        static_cast<u8>(Token::KwdElse)),
	range::from_literal_string("for",         static_cast<u8>(Token::KwdFor)),
	range::from_literal_string("do",          static_cast<u8>(Token::KwdDo)),
	range::from_literal_string("finally",     static_cast<u8>(Token::KwdFinally)),
	range::from_literal_string("switch",      static_cast<u8>(Token::KwdSwitch)),
	range::from_literal_string("case",        static_cast<u8>(Token::KwdCase)),
	range::from_literal_string("eval",        static_cast<u8>(Token::KwdEval)),
	range::from_literal_string("try",         static_cast<u8>(Token::KwdTry)),
	range::from_literal_string("catch",       static_cast<u8>(Token::KwdCatch)),
	range::from_literal_string("defer",       static_cast<u8>(Token::KwdDefer)),
	range::from_literal_string("func",        static_cast<u8>(Token::KwdFunc)),
	range::from_literal_string("proc",        static_cast<u8>(Token::KwdProc)),
	range::from_literal_string("trait",       static_cast<u8>(Token::KwdTrait)),
	range::from_literal_string("impl",        static_cast<u8>(Token::KwdImpl)),
	range::from_literal_string("where",       static_cast<u8>(Token::KwdWhere)),
	range::from_literal_string("expects",     static_cast<u8>(Token::KwdExpects)),
	range::from_literal_string("ensures",     static_cast<u8>(Token::KwdEnsures)),
	range::from_literal_string("pub",         static_cast<u8>(Token::KwdPub)),
	range::from_literal_string("mut",         static_cast<u8>(Token::KwdMut)),
	range::from_literal_string("let",         static_cast<u8>(Token::KwdLet)),
	range::from_literal_string("unreachable", static_cast<u8>(Token::KwdUnreachable)),
	range::from_literal_string("undefined",   static_cast<u8>(Token::KwdUndefined)),
	range::from_literal_string("return",      static_cast<u8>(Token::KwdReturn)),
	range::from_literal_string("leave",       static_cast<u8>(Token::KwdLeave)),
	range::from_literal_string("yield",       static_cast<u8>(Token::KwdYield)),
	range::from_literal_string("distinct",    static_cast<u8>(Token::KwdDistinct)),
	range::from_literal_string("_integer",             static_cast<u8>(Builtin::Integer)),
	range::from_literal_string("_float",               static_cast<u8>(Builtin::Float)),
	range::from_literal_string("_type",                static_cast<u8>(Builtin::Type)),
	range::from_literal_string("_definition",          static_cast<u8>(Builtin::Definition)),
	range::from_literal_string("_type_info",           static_cast<u8>(Builtin::TypeInfo)),
	range::from_literal_string("_typeof",              static_cast<u8>(Builtin::Typeof)),
	range::from_literal_string("_returntypeof",        static_cast<u8>(Builtin::Returntypeof)),
	range::from_literal_string("_sizeof",              static_cast<u8>(Builtin::Sizeof)),
	range::from_literal_string("_alignof",             static_cast<u8>(Builtin::Alignof)),
	range::from_literal_string("_strideof",            static_cast<u8>(Builtin::Strideof)),
	range::from_literal_string("_offsetof",            static_cast<u8>(Builtin::Offsetof)),
	range::from_literal_string("_nameof",              static_cast<u8>(Builtin::Nameof)),
	range::from_literal_string("_import",              static_cast<u8>(Builtin::Import)),
	range::from_literal_string("_create_type_builder", static_cast<u8>(Builtin::CreateTypeBuilder)),
	range::from_literal_string("_add_type_member",     static_cast<u8>(Builtin::AddTypeMember)),
	range::from_literal_string("_complete_type",       static_cast<u8>(Builtin::CompleteType)),
	range::from_literal_string("_source_id",           static_cast<u8>(Builtin::SourceId)),
	range::from_literal_string("_caller_source_id",    static_cast<u8>(Builtin::CallerSourceId)),
	range::from_literal_string("_definition_typeof",   static_cast<u8>(Builtin::DefinitionTypeof)),
};

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

struct RawLexeme
{
	Token token;

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

	RawLexeme() noexcept = default;

	RawLexeme(Token token) noexcept : token{ token } {}

	RawLexeme(Token token, CompIntegerValue integer_value) noexcept : token{ token }, integer_value{ integer_value } {}

	RawLexeme(Token token, CompFloatValue float_value) noexcept : token{ token }, float_value{ float_value } {}

	RawLexeme(Token token, u32 char_value) noexcept : token{ token }, char_value{ char_value } {}

	RawLexeme(Token token, IdentifierId identifier_id) noexcept : token{ token }, identifier_id{ identifier_id } {}

	RawLexeme(Token token, Builtin builtin) noexcept : token{ token }, builtin{ builtin } {}

	RawLexeme(Token token, ForeverValueId string_value_id, TypeId string_type_id) noexcept : token{ token }, string{ string_value_id, string_type_id } {}
};

// Operator Description Tuple. Consists of:
//  - `AstTag` with the node type
//  - `AstFlag` with the node flags
//  - `u8` precedence (low to high)
//  - `u8` whether it's right-associative
//  - `u8` is_binary whether it's a binary operator
//         (or unary, no ternary operators anywhere)
struct OperatorDesc
{
	AstTag node_type;

	AstFlag node_flags;

	u8 precedence;

	u8 is_right_to_left: 1;

	u8 is_binary : 1;
};

struct OperatorDescWithSource
{
	OperatorDesc operator_desc;

	SourceId source_id;
};

#ifdef COMPILER_MSVC
	#pragma warning(push)
	#pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif
struct Lexer
{
	const char8* curr;

	const char8* begin;

	const char8* end;

	Lexeme peek;

	u32 source_id_base;

	bool is_std;

	TypeId u8_type_id;

	IdentifierPool* identifiers;

	GlobalValuePool* globals;

	TypePool* types;

	ErrorSink* errors;

	bool has_errors;

	bool suppress_errors;

	jmp_buf error_jump_buffer;
};
#ifdef COMPILER_MSVC
	#pragma warning(pop)
#endif

struct OperatorStack
{
	u32 operand_count;

	u32 operator_top;

	SourceId expression_source_id;

	OperatorDescWithSource operators[64];

	AstBuilderToken operand_tokens[128];
};

struct Parser
{
	Lexer lexer;

	AstPool* builder;
};

static constexpr OperatorDesc UNARY_OPERATOR_DESCS[] = {
	{ AstTag::INVALID,            AstFlag::EMPTY,      10, false, true  }, // ( - Opening Parenthesis
	{ AstTag::UOpEval,            AstFlag::EMPTY,       8, false, false }, // eval
	{ AstTag::UOpTry,             AstFlag::EMPTY,       8, false, false }, // try
	{ AstTag::UOpDefer,           AstFlag::EMPTY,       8, false, false }, // defer
	{ AstTag::UOpDistinct,        AstFlag::EMPTY,       2, false, false }, // distinct
	{ AstTag::UOpBitNot,          AstFlag::EMPTY,       2, false, false }, // ~
	{ AstTag::UOpLogNot,          AstFlag::EMPTY,       2, false, false }, // !
	{ AstTag::UOpTypeOptPtr,      AstFlag::Type_IsMut,  2, false, false }, // ?
	{ AstTag::UOpTypeVarArgs,         AstFlag::EMPTY,       2, false, false }, // ...
	{ AstTag::UOpTypeTailArray,   AstFlag::EMPTY,       2, false, false }, // [...]
	{ AstTag::UOpTypeMultiPtr,    AstFlag::Type_IsMut,  2, false, false }, // [*]
	{ AstTag::UOpTypeOptMultiPtr, AstFlag::Type_IsMut,  2, false, false }, // [?]
	{ AstTag::UOpTypeSlice,       AstFlag::Type_IsMut,  2, false, false }, // []
	{ AstTag::UOpTypePtr,         AstFlag::Type_IsMut,  2, false, false }, // *
	{ AstTag::UOpNegate,          AstFlag::EMPTY,       2, false, false }, // -
	{ AstTag::UOpPos,             AstFlag::EMPTY,       2, false, false }, // +
};

static constexpr OperatorDesc BINARY_OPERATOR_DESCS[] = {
	{ AstTag::OpMul,       AstFlag::EMPTY, 2, true,  true  }, // *
	{ AstTag::OpSub,       AstFlag::EMPTY, 3, true,  true  }, // -
	{ AstTag::OpAdd,       AstFlag::EMPTY, 3, true,  true  }, // +
	{ AstTag::OpDiv,       AstFlag::EMPTY, 2, true,  true  }, // /
	{ AstTag::OpAddTC,     AstFlag::EMPTY, 3, true,  true  }, // +:
	{ AstTag::OpSubTC,     AstFlag::EMPTY, 3, true,  true  }, // -:
	{ AstTag::OpMulTC,     AstFlag::EMPTY, 2, true,  true  }, // *:
	{ AstTag::OpMod,       AstFlag::EMPTY, 2, true,  true  }, // %
	{ AstTag::UOpAddr,     AstFlag::EMPTY, 1, false, false }, // .&
	{ AstTag::UOpDeref,    AstFlag::EMPTY, 1, false, false }, // .*
	{ AstTag::OpBitAnd,    AstFlag::EMPTY, 6, true,  true  }, // &
	{ AstTag::OpBitOr,     AstFlag::EMPTY, 6, true,  true  }, // |
	{ AstTag::OpBitXor,    AstFlag::EMPTY, 6, true,  true  }, // ^
	{ AstTag::OpShiftL,    AstFlag::EMPTY, 4, true,  true  }, // <<
	{ AstTag::OpShiftR,    AstFlag::EMPTY, 4, true,  true  }, // >>
	{ AstTag::OpLogAnd,    AstFlag::EMPTY, 7, true,  true  }, // &&
	{ AstTag::OpLogOr,     AstFlag::EMPTY, 7, true,  true  }, // ||
	{ AstTag::OpCmpLT,     AstFlag::EMPTY, 5, true,  true  }, // <
	{ AstTag::OpCmpGT,     AstFlag::EMPTY, 5, true,  true  }, // >
	{ AstTag::OpCmpLE,     AstFlag::EMPTY, 5, true,  true  }, // <=
	{ AstTag::OpCmpGE,     AstFlag::EMPTY, 5, true,  true  }, // >=
	{ AstTag::OpCmpNE,     AstFlag::EMPTY, 5, true,  true  }, // !=
	{ AstTag::OpCmpEQ,     AstFlag::EMPTY, 5, true,  true  }, // ==
	{ AstTag::OpSet,       AstFlag::EMPTY, 9, false, true  }, // =
	{ AstTag::OpSetAdd,    AstFlag::EMPTY, 9, false, true  }, // +=
	{ AstTag::OpSetSub,    AstFlag::EMPTY, 9, false, true  }, // -=
	{ AstTag::OpSetMul,    AstFlag::EMPTY, 9, false, true  }, // *=
	{ AstTag::OpSetDiv,    AstFlag::EMPTY, 9, false, true  }, // /=
	{ AstTag::OpSetAddTC,  AstFlag::EMPTY, 9, false, true  }, // +:=
	{ AstTag::OpSetSubTC,  AstFlag::EMPTY, 9, false, true  }, // -:=
	{ AstTag::OpSetMulTC,  AstFlag::EMPTY, 9, false, true  }, // *:=
	{ AstTag::OpSetMod,    AstFlag::EMPTY, 9, false, true  }, // %=
	{ AstTag::OpSetBitAnd, AstFlag::EMPTY, 9, false, true  }, // &=
	{ AstTag::OpSetBitOr,  AstFlag::EMPTY, 9, false, true  }, // |=
	{ AstTag::OpSetBitXor, AstFlag::EMPTY, 9, false, true  }, // ^=
	{ AstTag::OpSetShiftL, AstFlag::EMPTY, 9, false, true  }, // <<=
	{ AstTag::OpSetShiftR, AstFlag::EMPTY, 9, false, true  }, // >>=
};





static constexpr const u8 INVALID_HEX_CHAR_VALUE = 255;

static bool is_whitespace(char8 c) noexcept
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool is_alphabetic_char(char8 c) noexcept
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_numeric_char(char8 c) noexcept
{
	return c >= '0' && c <= '9';
}

static bool is_identifier_continuation_char(char8 c) noexcept
{
	return is_alphabetic_char(c) || is_numeric_char(c) || c == '_';
}

static u8 hex_char_value(char8 c) noexcept
{
	if (c >= 'a' && c <= 'f')
		return 10 + c - 'a';
	else if (c >= 'A' && c <= 'F')
		return 10 + c - 'A';
	else if (c >= '0' && c <= '9')
		return c - '0';
	else
		return INVALID_HEX_CHAR_VALUE;
}



NORETURN static void parse_error_fatal(Lexer* lexer, SourceId source_id, CompileError error) noexcept
{
	if (!lexer->suppress_errors)
		(void) record_error(lexer->errors, source_id, error);

	longjmp(lexer->error_jump_buffer, 1);
}

static void parse_error_continuable(Lexer* lexer, SourceId source_id, CompileError error) noexcept
{
	if (!lexer->suppress_errors)
		(void) record_error(lexer->errors, source_id, error);

	lexer->has_errors = true;
}

static void skip_block_comment(Lexer* lexer) noexcept
{
	const char8* curr = lexer->curr;

	curr += 2;

	u32 comment_nesting = 1;

	while (comment_nesting != 0)
	{
		const char8 c = *curr;

		if (c == '/')
		{
			if (curr[1] == '*')
			{
				curr += 2;

				comment_nesting += 1;
			}
			else
			{
				curr += 1;
			}
		}
		else if (c == '*')
		{
			if (curr[1] == '/')
			{
				curr += 2;

				comment_nesting -= 1;
			}
			else
			{
				curr += 1;
			}
		}
		else if (c == '\0')
		{
			const CompileError error = lexer->curr == lexer->end ? CompileError::LexCommentMismatchedBegin : CompileError::LexNullCharacter;

			parse_error_fatal(lexer, static_cast<SourceId>(lexer->source_id_base + static_cast<u32>(curr - lexer->curr)), error);
		}
		else
		{
			curr += 1;
		}
	}

	lexer->curr = curr;
}

static void skip_whitespace(Lexer* lexer) noexcept
{
	const char8* curr = lexer->curr;

	while (true)
	{
		while (is_whitespace(*curr))
			curr += 1;

		if (*curr == '/')
		{
			if (curr[1] == '/')
			{
				curr += 2;

				while (*curr != '\n' && *curr != '\0')
					curr += 1;
			}
			else if (curr[1] == '*')
			{
				lexer->curr = curr;

				skip_block_comment(lexer);

				curr = lexer->curr;
			}
			else
			{
				break;
			}
		}
		else
		{
			break;
		}
	}

	lexer->curr = curr;
}

static RawLexeme scan_identifier_token(Lexer* lexer, bool is_builtin) noexcept
{
	const char8* curr = lexer->curr;

	const char8* const token_begin = curr - 1;

	while (is_identifier_continuation_char(*curr))
		curr += 1;

	lexer->curr = curr;

	const Range<char8> identifier_bytes{ token_begin, curr };

	u8 identifier_attachment;

	const IdentifierId identifier_id = id_and_attachment_from_identifier(lexer->identifiers, identifier_bytes, &identifier_attachment);

	if (is_builtin)
	{
		const Builtin builtin = static_cast<Builtin>(identifier_attachment);

		if (builtin == Builtin::INVALID)
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexBuiltinUnknown);

		return { Token::Builtin, builtin };
	}
	else
	{
		const Token token = identifier_attachment == 0 ? Token::Ident : static_cast<Token>(identifier_attachment);

		return { token, token == Token::Ident ? identifier_id : IdentifierId::INVALID };
	}
}

static RawLexeme scan_number_token_with_base(Lexer* lexer, char8 base) noexcept
{
	const char8* curr = lexer->curr;

	const char8* const token_begin = curr;

	curr += 1;

	CompIntegerValue integer_value = comp_integer_from_u64(0);

	if (base == 'b')
	{
		while (*curr == '0' || *curr == '1')
		{
			integer_value = comp_integer_add(comp_integer_mul(integer_value, comp_integer_from_u64(10)), comp_integer_from_u64(*curr - '0'));

			curr += 1;
		}
	}
	else if (base == 'o')
	{
		while (*curr >= '0' && *curr <= '7')
		{
			integer_value = comp_integer_add(comp_integer_mul(integer_value, comp_integer_from_u64(8)), comp_integer_from_u64(*curr - '0'));

			curr += 1;
		}
	}
	else
	{
		ASSERT_OR_IGNORE(base == 'x');

		while (true)
		{
			const u8 hex = hex_char_value(*curr);

			if (hex == INVALID_HEX_CHAR_VALUE)
				break;

			integer_value = comp_integer_add(comp_integer_mul(integer_value, comp_integer_from_u64(16)), comp_integer_from_u64(hex));

			curr += 1;
		}
	}

	if (curr == token_begin + 1)
		parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexNumberWithBaseMissingDigits);

	if (is_identifier_continuation_char(*curr))
		parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexNumberUnexpectedCharacterAfterInteger);

	lexer->curr = curr;

	return { Token::LitInteger, integer_value };
}

static u32 scan_utf8_char_surrogates(Lexer* lexer, u32 leader_value, u32 surrogate_count) noexcept
{
	const char8* curr = lexer->curr;

	u32 codepoint = leader_value;

	for (u32 i = 0; i != surrogate_count; ++i)
	{
		const char8 surrogate = curr[i + 1];

		if ((surrogate & 0xC0) != 0x80)
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexCharacterBadSurrogateCodeUnit);

		codepoint |= (surrogate & 0x3F) << (6 * (surrogate_count - i - 1));
	}

	lexer->curr += surrogate_count + 1;

	return codepoint;
}

static u32 scan_utf8_char(Lexer* lexer) noexcept
{
	const char8 first = *lexer->curr;

	if ((first & 0x80) == 0)
	{
		lexer->curr += 1;

		return first;
	}
	else if ((first & 0xE0) == 0xC0)
	{
		return scan_utf8_char_surrogates(lexer, (first & 0x1F) << 6, 1);
	}
	else if ((first & 0xF0) == 0xE0)
	{
		return scan_utf8_char_surrogates(lexer, (first & 0x0F) << 12, 2);

	}
	else if ((first & 0xF8) == 0xF0)
	{
		return scan_utf8_char_surrogates(lexer, (first & 0x07) << 18, 3);
	}
	else
	{
		parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexCharacterBadLeadCodeUnit);
	}
}

static u32 scan_escape_char(Lexer* lexer) noexcept
{
	const char8* curr = lexer->curr;

	u32 codepoint = 0;

	const char8 escapee = curr[1];

	switch (escapee)
	{
	case 'x':
	{
		const u8 hi = hex_char_value(curr[2]);

		if (hi == INVALID_HEX_CHAR_VALUE)
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexCharacterEscapeSequenceLowerXBadChar);

		const u8 lo = hex_char_value(curr[3]);

		if (lo == INVALID_HEX_CHAR_VALUE)
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexCharacterEscapeSequenceLowerXBadChar);

		curr += 2;

		codepoint = lo + hi * 16;

		break;
	}

	case 'X':
	{
		codepoint = 0;

		for (u32 i = 0; i != 6; ++i)
		{
			const u8 char_value = hex_char_value(curr[i + 2]);

			if (char_value == INVALID_HEX_CHAR_VALUE)
				parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexCharacterEscapeSequenceUpperXInvalidChar);

			codepoint = codepoint * 16 + char_value;
		}

		if (codepoint > 0x10FFFF)
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexCharacterEscapeSequenceUpperXCodepointTooLarge);

		curr += 6;

		break;
	}

	case 'u':
	{
		for (u32 i = 0; i != 4; ++i)
		{
			const char8 c = curr[i + 2];

			if (c < '0' || c > '9')
				parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexCharacterEscapeSequenceUInvalidChar);

			codepoint = codepoint * 10 + c - '0';
		}

		curr += 4;

		break;
	}

	case '\\':
	case '\'':
	case '"':
		codepoint = escapee;
		break;

	case '0':
		codepoint = '\0';
		break;

	case 'a':
		codepoint = '\a';
		break;

	case 'b':
		codepoint = '\b';
		break;

	case 'f':
		codepoint = '\f';
		break;

	case 'n':
		codepoint = '\n';
		break;

	case 'r':
		codepoint = '\r';
		break;

	case 't':
		codepoint = '\t';
		break;

	case 'v':
		codepoint = '\v';
		break;

	default:
		parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexCharacterEscapeSequenceUnknown);
	}

	lexer->curr = curr + 2;

	return codepoint;
}

static RawLexeme scan_number_token(Lexer* lexer, char8 first) noexcept
{
	const char8* curr = lexer->curr;

	const char8* const token_begin = curr - 1;

	CompIntegerValue integer_value = comp_integer_from_u64(first - '0');

	while (is_numeric_char(*curr))
	{
		integer_value = comp_integer_add(comp_integer_mul(integer_value, comp_integer_from_u64(10)), comp_integer_from_u64(*curr - '0'));

		curr += 1;
	}

	if (*curr == '.' && curr[1] != '.')
	{
		curr += 1;

		if (!is_numeric_char(*curr))
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexNumberUnexpectedCharacterAfterDecimalPoint);

		while (is_numeric_char(*curr))
			curr += 1;

		if (*curr == 'e')
		{
			curr += 1;

			if (*curr == '+' || *curr == '-')
				curr += 1;

			while (is_numeric_char(*curr))
				curr += 1;
		}

		if (is_alphabetic_char(*curr) || *curr == '_')
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexNumberUnexpectedCharacterAfterFloat);

		char8* strtod_end;

		errno = 0;

		const f64 float_value = strtod(token_begin, &strtod_end);

		ASSERT_OR_IGNORE(strtod_end == curr);

		if (errno == ERANGE)
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexNumberFloatTooLarge);

		lexer->curr = curr;

		return { Token::LitFloat, comp_float_from_f64(float_value) };
	}
	else
	{
		if (is_alphabetic_char(*curr) || *curr == '_')
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexNumberUnexpectedCharacterAfterFloat);

		lexer->curr = curr;

		return { Token::LitInteger, integer_value };
	}
}

static RawLexeme scan_char_token(Lexer* lexer) noexcept
{
	u32 codepoint;

	if (*lexer->curr == '\\')
		codepoint = scan_escape_char(lexer);
	else
		codepoint = scan_utf8_char(lexer);

	if (*lexer->curr != '\'')
		parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexCharacterExpectedEnd);

	lexer->curr += 1;

	return { Token::LitChar, codepoint };
}

static RawLexeme scan_string_token(Lexer* lexer) noexcept
{
	char8 buffer[MAX_STRING_LITERAL_BYTES];

	u32 buffer_index = 0;

	const char8* curr = lexer->curr;

	const char8* copy_begin = curr;

	while (*curr != '"')
	{
		if (*curr == '\\')
		{
			const u32 bytes_to_copy = static_cast<u32>(curr - copy_begin);

			if (buffer_index + bytes_to_copy > sizeof(buffer))
				parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexStringTooLong);

			memcpy(buffer + buffer_index, copy_begin, bytes_to_copy);

			buffer_index += bytes_to_copy;

			lexer->curr = curr;

			const u32 codepoint = scan_escape_char(lexer);

			curr = lexer->curr;

			if (codepoint <= 0x7F)
			{
				if (buffer_index + 1 > sizeof(buffer))
					parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexStringTooLong);

				buffer[buffer_index] = static_cast<char8>(codepoint);

				buffer_index += 1;
			}
			else if (codepoint <= 0x7FF)
			{
				if (buffer_index + 2 > sizeof(buffer))
					parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexStringTooLong);

				buffer[buffer_index] = static_cast<char8>((codepoint >> 6) | 0xC0);

				buffer[buffer_index + 1] = static_cast<char8>((codepoint & 0x3F) | 0x80);

				buffer_index += 2;
			}
			else if (codepoint == 0x10000)
			{
				if (buffer_index + 3 > sizeof(buffer))
					parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexStringTooLong);

				buffer[buffer_index] = static_cast<char8>((codepoint >> 12) | 0xE0);

				buffer[buffer_index + 1] = static_cast<char8>(((codepoint >> 6) & 0x3F) | 0x80);

				buffer[buffer_index + 2] = static_cast<char8>((codepoint & 0x3F) | 0x80);

				buffer_index += 3;
			}
			else
			{
				ASSERT_OR_IGNORE(codepoint <= 0x10FFFF);

				if (buffer_index + 4 > sizeof(buffer))
					parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexStringTooLong);

				buffer[buffer_index] = static_cast<char8>((codepoint >> 18) | 0xE0);

				buffer[buffer_index + 1] = static_cast<char8>(((codepoint >> 12) & 0x3F) | 0x80);

				buffer[buffer_index + 2] = static_cast<char8>(((codepoint >> 6) & 0x3F) | 0x80);

				buffer[buffer_index + 3] = static_cast<char8>((codepoint & 0x3F) | 0x80);

				buffer_index += 4;
			}

			copy_begin = buffer + buffer_index;
		}
		else if (*curr == '\n')
		{
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexStringCrossesNewline);
		}
		else if (*curr == '\0')
		{
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexStringMissingEnd);
		}
		else
		{
			curr += 1;
		}
	}

	const u32 bytes_to_copy = static_cast<u32>(curr - copy_begin);

	if (buffer_index + bytes_to_copy > sizeof(buffer))
		parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexStringTooLong);

	memcpy(buffer + buffer_index, copy_begin, bytes_to_copy);

	buffer_index += bytes_to_copy;

	const TypeId string_type_id = type_create_array(lexer->types, TypeTag::Array, ArrayType{ buffer_index, some(lexer->u8_type_id) });

	const MutRange<byte> string_bytes{ reinterpret_cast<byte*>(buffer), buffer_index };

	const CTValue string_value{ string_bytes, alignof(u8), false, string_type_id };

	const ForeverValueId forever_value_id = forever_value_alloc_initialized(lexer->globals, false, string_value);

	lexer->curr = curr + 1;

	return { Token::LitString, forever_value_id, string_type_id };
}

static RawLexeme raw_next(Lexer* lexer) noexcept
{
	const char8 first = *lexer->curr;

	lexer->curr += 1;

	const char8 second = first == '\0' ? '\0' : *lexer->curr;

	switch (first)
	{
	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
	case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p':
	case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x':
	case 'y': case 'z':
	case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H':
	case 'I': case 'J': case 'K': case 'L': case 'M': case 'N': case 'O': case 'P':
	case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
	case 'Y': case 'Z':
		return scan_identifier_token(lexer, false);

	case '0':
		if (second == 'b' || second == 'o' || second == 'x')
			return scan_number_token_with_base(lexer, second);

	// fallthrough

	case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8':
	case '9':
		return scan_number_token(lexer, first);

	case '\'':
		return scan_char_token(lexer);

	case '"':
		return scan_string_token(lexer);

	case '_':
		if (is_identifier_continuation_char(second))
		{
			if (!lexer->is_std)
				parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexIdentifierInitialUnderscore);

			return scan_identifier_token(lexer, true);
		}
		else
		{
			return { Token::Wildcard };
		}


	case '+':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetAdd };
		}
		else if (second == ':')
		{
			if (lexer->curr[1] == '=')
			{
				lexer->curr += 2;

				return { Token::OpSetAddTC };
			}
			else
			{
				lexer->curr += 1;

				return { Token::OpAddTC };
			}
		}
		else
		{
			return { Token::OpAdd };
		}

	case '-':
		if (second == '>')
		{
			lexer->curr += 1;

			return { Token::ThinArrowR };
		}
		else if (second == ':')
		{
			if (lexer->curr[1] == '=')
			{
				lexer->curr += 2;

				return { Token::OpSetSubTC };
			}
			else
			{
				lexer->curr += 1;

				return { Token::OpSubTC };
			}
		}
		else if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetSub };
		}
		else
		{
			return { Token::OpSub };
		}

	case '*':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetMul };
		}
		else if (second == ':')
		{
			if (lexer->curr[1] == '=')
			{
				lexer->curr += 2;

				return { Token::OpSetMulTC };
			}
			else
			{
				lexer->curr += 1;

				return { Token::OpMulTC };
			}
		}
		else if (second == '/')
		{
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexCommentMismatchedEnd);
		}
		else
		{
			return { Token::OpMulOrTypPtr };
		}

	case '/':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetDiv };
		}
		else
		{
			return { Token::OpDiv };
		}

	case '%':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetMod };
		}
		else
		{
			return { Token::OpMod };
		}

	case '&':
		if (second == '&')
		{
			lexer->curr += 1;

			return { Token::OpLogAnd };
		}
		else if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetAnd };
		}
		else
		{
			return { Token::OpAnd };
		}

	case '|':
		if (second == '|')
		{
			lexer->curr += 1;

			return { Token::OpLogAnd };
		}
		else if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetOr };
		}
		else
		{
			return { Token::OpOr };
		}

	case '^':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetXor };
		}
		else
		{
			return { Token::OpXor };
		}

	case '<':
		if (second == '<')
		{
			if (lexer->curr[1] == '=')
			{
				lexer->curr += 2;

				return { Token::OpSetShl };
			}
			else
			{
				lexer->curr += 1;

				return { Token::OpShl };
			}
		}
		else if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpLe };
		}
		else if (second == '-')
		{
			lexer->curr += 1;

			return { Token::ThinArrowL };
		}
		else
		{
			return { Token::OpLt };
		}

	case '>':
		if (second == '>')
		{
			if (lexer->curr[1] == '=')
			{
				lexer->curr += 2;

				return { Token::OpSetShr };
			}
			else
			{
				lexer->curr += 1;

				return { Token::OpShr };
			}
		}
		else if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpGe };
		}
		else
		{
			return { Token::OpGt };
		}

	case '.':
		if (second == '.')
		{
			if (lexer->curr[1] == '.')
			{
				lexer->curr += 2;

				return { Token::TypVar };
			}
			else
			{
				lexer->curr += 1;

				return { Token::DoubleDot };
			}
		}
		else if (second == '*')
		{
			lexer->curr += 1;

			return { Token::UOpDeref };
		}
		else if (second == '[')
		{
			lexer->curr += 1;

			return { Token::ArrayInitializer };
		}
		else if (second == '{')
		{
			lexer->curr += 1;

			return { Token::CompositeInitializer };
		}
		else if (second == '&')
		{
			lexer->curr += 1;

			return { Token::UOpAddr };
		}
		else
		{
			return { Token::OpMemberOrRef };
		}

	case '!':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpNe };
		}
		else
		{
			return { Token::UOpLogNot };
		}

	case '=':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpEq };
		}
		else if (second == '>')
		{
			lexer->curr += 1;

			return { Token::WideArrowR };
		}
		else
		{
			return { Token::OpSet };
		}

	case '~':
		return { Token::UOpNot };

	case '?':
		return { Token::TypOptPtr };

	case ':':
		return { Token::Colon };

	case ',':
		return { Token::Comma };

	case '#':
		return { Token::Pragma };

	case '[':
		if (second == '.' && lexer->curr[1] == '.' && lexer->curr[2] == '.' && lexer->curr[3] == ']')
		{
			lexer->curr += 4;

			return { Token::TypTailArray };
		}
		else if (second == '*' && lexer->curr[1] == ']')
		{
			lexer->curr += 2;

			return { Token::TypMultiPtr };
		}
		else if (second == '?' && lexer->curr[1] == ']')
		{
			lexer->curr += 2;

			return { Token::TypOptMultiPtr };
		}
		else if (second == ']')
		{
			lexer->curr += 1;

			return { Token::TypSlice };
		}
		else
		{
			return { Token::BracketL };
		}

	case ']':
		return { Token::BracketR };

	case '{':
		return { Token::CurlyL };

	case '}':
		return { Token::CurlyR };

	case '(':
		return { Token::ParenL };

	case ')':
		return { Token::ParenR };

	case '\0':
		lexer->curr -= 1;

		if (lexer->curr != lexer->end)
			parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexNullCharacter);

		return { Token::END_OF_SOURCE };

	default:
		parse_error_fatal(lexer, lexer->peek.source_id, CompileError::LexUnexpectedCharacter);
	}
}

static Lexeme next(Lexer* lexer) noexcept
{
	if (lexer->peek.token != Token::EMPTY)
	{
		const Lexeme rst = lexer->peek;

		lexer->peek.token = Token::EMPTY;

		return rst;
	}

	skip_whitespace(lexer);

	lexer->peek.source_id = SourceId{ lexer->source_id_base + static_cast<u32>(lexer->curr - lexer->begin) };

	const RawLexeme raw = raw_next(lexer);

	return { raw.token, lexer->peek.source_id, { raw.integer_value } };
}

static Lexeme peek(Lexer* lexer) noexcept
{
	if (lexer->peek.token == Token::EMPTY)
		lexer->peek = next(lexer);

	return lexer->peek;
}

static Lexeme peek_n(Lexer* lexer, u32 n) noexcept
{
	ASSERT_OR_IGNORE(n != 0);

	const Lexeme remembered_peek = peek(lexer);

	const char8* const remembered_curr = lexer->curr;

	lexer->peek.token = Token::EMPTY;

	Lexeme result = remembered_peek;

	for (u32 i = 0; i != n; ++i)
		result = next(lexer);

	lexer->curr = remembered_curr;

	lexer->peek = remembered_peek;

	return result;
}

static void skip(Lexer* lexer) noexcept
{
	(void) next(lexer);
}



static void pop_operator(Parser* parser, OperatorStack* stack) noexcept
{
	ASSERT_OR_IGNORE(stack->operator_top != 0);

	const OperatorDescWithSource top = stack->operators[stack->operator_top - 1];

	stack->operator_top -= 1;

	if (top.operator_desc.node_type == AstTag::INVALID)
		return;

	if (stack->operand_count <= top.operator_desc.is_binary)
	{
		const CompileError error =  top.operator_desc.is_binary
			? CompileError::ParseBinaryOperatorMissingOperand
			: CompileError::ParseUnaryOperatorMissingOperand;

		parse_error_fatal(&parser->lexer, stack->expression_source_id, error);
	}

	if (top.operator_desc.is_binary)
		stack->operand_count -= 1;

	const AstBuilderToken operator_token = push_node(parser->builder, stack->operand_tokens[stack->operand_count - 1], top.source_id, top.operator_desc.node_flags, top.operator_desc.node_type);

	stack->operand_tokens[stack->operand_count - 1] = operator_token;
}

static bool pop_to_precedence(Parser* parser, OperatorStack* stack, u8 precedence, bool pop_equal) noexcept
{
	while (stack->operator_top != 0)
	{
		const OperatorDescWithSource top = stack->operators[stack->operator_top - 1];

		if (top.operator_desc.precedence > precedence || (top.operator_desc.precedence == precedence && !pop_equal))
			return true;

		pop_operator(parser, stack);
	}

	return false;
}

static void push_operand(Parser* parser, OperatorStack* stack, AstBuilderToken operand_token) noexcept
{
	if (stack->operand_count == array_count(stack->operand_tokens) - 1)
		parse_error_fatal(&parser->lexer, stack->expression_source_id, CompileError::ParseOpenOperandCountTooLarge);

	stack->operand_tokens[stack->operand_count] = operand_token;

	stack->operand_count += 1;
}

static void push_operator(Parser* parser, OperatorStack* stack, OperatorDescWithSource op) noexcept
{
	if (op.operator_desc.node_type != AstTag::INVALID)
		pop_to_precedence(parser, stack, op.operator_desc.precedence, op.operator_desc.is_right_to_left);

	if (stack->operator_top == array_count(stack->operators))
		parse_error_fatal(&parser->lexer, stack->expression_source_id, CompileError::ParseOpenOperatorCountTooLarge);

	stack->operators[stack->operator_top] = op;

	stack->operator_top += 1;
}

static void remove_lparen(OperatorStack* stack) noexcept
{
	ASSERT_OR_IGNORE(stack->operator_top != 0 && stack->operators[stack->operator_top - 1].operator_desc.node_type == AstTag::INVALID);

	stack->operator_top -= 1;
}

static AstBuilderToken pop_remaining(Parser* parser, OperatorStack* stack) noexcept
{
	while (stack->operator_top != 0)
		pop_operator(parser, stack);

	if (stack->operand_count != 1)
		parse_error_fatal(&parser->lexer, stack->expression_source_id, CompileError::ParseOperatorOperandCountMismatch);

	return stack->operand_tokens[0];
}





static bool is_definition_start(Token token) noexcept
{
	return token == Token::KwdLet
		|| token == Token::KwdPub
		|| token == Token::KwdMut;
}

static AstBuilderToken parse_expr(Parser* parser, bool allow_complex) noexcept;

static AstBuilderToken parse_definition(Parser* parser, bool is_optional_value, bool is_param) noexcept
{
	AstFlag flags = AstFlag::EMPTY;

	Lexeme lexeme = next(&parser->lexer);

	const SourceId source_id = lexeme.source_id;

	if (is_param && lexeme.token == Token::KwdEval)
	{
		flags |= AstFlag::Definition_IsEval;

		lexeme = next(&parser->lexer);
	}

	if (lexeme.token == Token::KwdLet)
	{
		lexeme = next(&parser->lexer);
	}
	else
	{
		while (true)
		{
			if (lexeme.token == Token::KwdPub)
			{
				if (is_param)
					parse_error_continuable(&parser->lexer, lexeme.source_id, CompileError::ParseFunctionParameterIsPub);

				if ((flags & AstFlag::Definition_IsPub) != AstFlag::EMPTY)
					parse_error_continuable(&parser->lexer, lexeme.source_id, CompileError::ParseDefinitionMultiplePub);

				flags |= AstFlag::Definition_IsPub;
			}
			else if (lexeme.token == Token::KwdMut)
			{
				if ((flags & AstFlag::Definition_IsMut) != AstFlag::EMPTY)
					parse_error_continuable(&parser->lexer, lexeme.source_id, CompileError::ParseDefinitionMultipleMut);

				flags |= AstFlag::Definition_IsMut;
			}
			else
			{
				break;
			}

			lexeme = next(&parser->lexer);
		}
	}

	if (lexeme.token != Token::Ident)
		parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseDefinitionMissingName);

	const IdentifierId identifier_id = lexeme.identifier_id;

	lexeme = peek(&parser->lexer);

	AstBuilderToken first_child_token = AstBuilderToken::NO_CHILDREN;

	if (lexeme.token == Token::Colon)
	{
		flags |= AstFlag::Definition_HasType;

		skip(&parser->lexer);

		first_child_token = parse_expr(parser, false);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::OpSet)
	{
		skip(&parser->lexer);

		const AstBuilderToken value_token = parse_expr(parser, true);

		if (first_child_token == AstBuilderToken::NO_CHILDREN)
			first_child_token = value_token;
	}
	else if (!is_optional_value)
	{
		parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseDefinitionMissingEquals);
	}

	return is_param
		? push_node(parser->builder, first_child_token, source_id, flags, AstParameterData{ identifier_id })
		: push_node(parser->builder, first_child_token, source_id, flags, AstDefinitionData{ identifier_id });
}

static AstBuilderToken parse_return(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdReturn);

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken value_token = parse_expr(parser, true);

	return push_node(parser->builder, value_token, source_id, AstFlag::EMPTY, AstTag::Return);
}

static AstBuilderToken parse_leave(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdLeave);

	const SourceId source_id = next(&parser->lexer).source_id;

	return push_node(parser->builder, AstBuilderToken::NO_CHILDREN, source_id, AstFlag::EMPTY, AstTag::Leave);
}

static AstBuilderToken parse_yield(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdYield);

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken value_token = parse_expr(parser, true);

	return push_node(parser->builder, value_token, source_id, AstFlag::EMPTY, AstTag::Yield);
}

static AstBuilderToken parse_top_level_expr(Parser* parser, bool is_definition_optional_value, bool* out_is_definition) noexcept
{
	const Lexeme lexeme = peek(&parser->lexer);

	bool is_definition = is_definition_start(lexeme.token);

	*out_is_definition = is_definition;

	if (is_definition)
		return parse_definition(parser, is_definition_optional_value, false);
	else if (lexeme.token == Token::KwdReturn)
		return parse_return(parser);
	else if (lexeme.token == Token::KwdLeave)
		return parse_leave(parser);
	else if (lexeme.token == Token::KwdYield)
		return parse_yield(parser);
	else
		return parse_expr(parser, true);
}

static AstBuilderToken parse_where(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdWhere);

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_definition(parser, false, false);

	while (true)
	{
		if (peek(&parser->lexer).token != Token::Comma)
			break;

		skip(&parser->lexer);

		parse_definition(parser, false, false);
	}

	return push_node(parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::Where);
}

static AstBuilderToken parse_if(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdIf);

	AstFlag flags = AstFlag::EMPTY;

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdWhere)
	{
		flags |= AstFlag::If_HasWhere;

		parse_where(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdThen)
		skip(&parser->lexer);

	parse_expr(parser, true);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdElse)
	{
		flags |= AstFlag::If_HasElse;

		skip(&parser->lexer);

		parse_expr(parser, true);
	}

	return push_node(parser->builder, first_child_token, source_id, flags, AstTag::If);
}

static AstBuilderToken try_parse_foreach(Parser* parser, SourceId source_id) noexcept
{
	bool is_foreach = false;

	if (is_definition_start(peek(&parser->lexer).token))
	{
		is_foreach = true;
	}
	else if (const Lexeme lookahead_1 = peek_n(&parser->lexer, 1); lookahead_1.token == Token::ThinArrowL)
	{
		is_foreach = true;
	}
	else if (lookahead_1.token == Token::Comma)
	{
		if (const Lexeme lookahead_2 = peek_n(&parser->lexer, 2); is_definition_start(lookahead_2.token))
			is_foreach = true;
		if (const Lexeme lookahead_3 = peek_n(&parser->lexer, 3); lookahead_3.token == Token::ThinArrowL)
			is_foreach = true;
	}

	if (!is_foreach)
		return AstBuilderToken::NO_CHILDREN;

	AstFlag flags = AstFlag::EMPTY;

	const AstBuilderToken first_child_token = parse_definition(parser, true, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::Comma)
	{
		flags |= AstFlag::ForEach_HasIndex;

		skip(&parser->lexer);

		parse_definition(parser, true, false);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token != Token::ThinArrowL)
		parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseForeachExpectThinArrowLeft);

	skip(&parser->lexer);

	parse_expr(parser, false);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdWhere)
	{
		flags |= AstFlag::ForEach_HasWhere;

		parse_where(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdDo)
		skip(&parser->lexer);

	parse_expr(parser, true);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdFinally)
	{
		flags |= AstFlag::ForEach_HasFinally;

		parse_expr(parser, true);
	}

	return push_node(parser->builder, first_child_token, source_id, flags, AstTag::ForEach);
}

static AstBuilderToken parse_for(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdFor);

	AstFlag flags = AstFlag::EMPTY;

	const SourceId source_id = next(&parser->lexer).source_id;

	if (const AstBuilderToken foreach_token = try_parse_foreach(parser, source_id); foreach_token != AstBuilderToken::NO_CHILDREN)
		return foreach_token;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::Comma)
	{
		flags |= AstFlag::For_HasStep;

		skip(&parser->lexer);

		parse_expr(parser, true);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdWhere)
	{
		flags |= AstFlag::For_HasWhere;

		parse_where(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdDo)
		skip(&parser->lexer);

	parse_expr(parser, true);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdFinally)
	{
		flags |= AstFlag::For_HasFinally;

		parse_expr(parser, true);
	}

	return push_node(parser->builder, first_child_token, source_id, flags, AstTag::For);
}

static AstBuilderToken parse_case(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdCase);

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = next(&parser->lexer);

	if (lexeme.token != Token::ThinArrowR)
		parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseCaseMissingThinArrowRight);

	return push_node(parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::Case);
}

static AstBuilderToken parse_switch(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdSwitch);

	AstFlag flags = AstFlag::EMPTY;

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdWhere)
	{
		flags |= AstFlag::Switch_HasWhere;

		parse_where(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdCase)
	{
		while (true)
		{
			parse_case(parser);

			lexeme = peek(&parser->lexer);

			if (lexeme.token != Token::KwdCase)
				break;
		}
	}
	else
	{
		parse_error_continuable(&parser->lexer, lexeme.source_id, CompileError::ParseSwitchMissingCase);
	}

	return push_node(parser->builder, first_child_token, source_id, flags, AstTag::Switch);
}

static AstBuilderToken parse_expects(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdExpects);

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	while (true)
	{
		if (peek(&parser->lexer).token != Token::Comma)
			break;

		skip(&parser->lexer);

		parse_expr(parser, false);
	}

	return push_node(parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::Expects);
}

static AstBuilderToken parse_ensures(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdEnsures);

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	while (true)
	{
		if (peek(&parser->lexer).token != Token::Comma)
			break;

		skip(&parser->lexer);

		parse_expr(parser, false);
	}

	return push_node(parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::Ensures);
}

static AstBuilderToken parse_signature(Parser* parser) noexcept
{
	AstFlag flags = AstFlag::EMPTY;

	Lexeme lexeme = next(&parser->lexer);

	const SourceId func_source_id = lexeme.source_id;

	if (lexeme.token == Token::KwdProc)
		flags |= AstFlag::Signature_IsProc;
	else
		ASSERT_OR_IGNORE(lexeme.token == Token::KwdFunc);

	lexeme = next(&parser->lexer);

	const SourceId parameter_list_source_id = lexeme.source_id;

	if (lexeme.token != Token::ParenL)
	{
		const CompileError error = (flags & AstFlag::Signature_IsProc) == AstFlag::EMPTY ? CompileError::ParseSignatureMissingParenthesisAfterProc : CompileError::ParseSignatureMissingParenthesisAfterFunc;

		parse_error_fatal(&parser->lexer, lexeme.source_id, error);
	}

	lexeme = peek(&parser->lexer);

	AstBuilderToken first_parameter_token = AstBuilderToken::NO_CHILDREN;

	u32 param_count = 0;

	while (lexeme.token != Token::ParenR)
	{
		// Only report this for the first parameter after the maximum by
		// performing a strict equality check.
		if (param_count == MAX_FUNC_PARAM_COUNT)
			parse_error_continuable(&parser->lexer, lexeme.source_id, CompileError::ParseSignatureTooManyParameters);

		param_count += 1;

		const AstBuilderToken parameter_token = parse_definition(parser, true, true);

		if (first_parameter_token == AstBuilderToken::NO_CHILDREN)
			first_parameter_token = parameter_token;

		lexeme = peek(&parser->lexer);

		if (lexeme.token == Token::Comma)
			skip(&parser->lexer);
		else if (lexeme.token != Token::ParenR)
			parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseSignatureUnexpectedParameterListEnd);
	}

	const AstBuilderToken parameter_list_token = push_node(parser->builder, first_parameter_token, parameter_list_source_id, AstFlag::EMPTY, AstTag::ParameterList);

	skip(&parser->lexer);

	lexeme = peek(&parser->lexer);

	if (lexeme.token != Token::ThinArrowR)
		parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseSignatureMissingReturnType);

	skip(&parser->lexer);

	// Return type
	parse_expr(parser, false);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdExpects)
	{
		flags |= AstFlag::Signature_HasExpects;

		// Expects
		parse_expects(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdEnsures)
	{
		flags |= AstFlag::Signature_HasEnsures;

		// Ensures
		parse_ensures(parser);
	}

	return push_node(parser->builder, parameter_list_token, func_source_id, flags, AstTag::Signature);
}

static AstBuilderToken parse_func(Parser* parser) noexcept
{
	AstFlag flags = AstFlag::EMPTY;

	Lexeme lexeme = peek(&parser->lexer);

	const SourceId func_source_id = lexeme.source_id;

	const AstBuilderToken signature_token = parse_signature(parser);

	lexeme = peek(&parser->lexer);

	if (lexeme.token != Token::WideArrowR)
		return signature_token;

	skip(&parser->lexer);

	parse_expr(parser, true);

	return push_node(parser->builder, signature_token, func_source_id, flags, AstFuncData{ none<ClosureListId>() });
}

static AstBuilderToken parse_trait(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdTrait);

	AstFlag flags = AstFlag::EMPTY;

	const SourceId source_id = next(&parser->lexer).source_id;

	Lexeme lexeme = next(&parser->lexer);

	if (lexeme.token != Token::ParenL)
		parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseSignatureMissingParenthesisAfterTrait);

	lexeme = peek(&parser->lexer);

	AstBuilderToken first_child_token = AstBuilderToken::NO_CHILDREN;

	while (lexeme.token != Token::ParenR)
	{
		const AstBuilderToken parameter_token = parse_definition(parser, true, false);

		if (first_child_token == AstBuilderToken::NO_CHILDREN)
			first_child_token = parameter_token;

		lexeme = next(&parser->lexer);

		if (lexeme.token == Token::Comma)
			lexeme = peek(&parser->lexer);
		else if (lexeme.token != Token::ParenR)
			parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseSignatureUnexpectedParameterListEnd);
	}

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdExpects)
	{
		flags |= AstFlag::Trait_HasExpects;

		const AstBuilderToken expects_token = parse_expects(parser);

		if (first_child_token == AstBuilderToken::NO_CHILDREN)
			first_child_token = expects_token;

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token != Token::OpSet)
	{
		const CompileError error = (flags & AstFlag::Trait_HasExpects) == AstFlag::EMPTY
			? CompileError::ParseTraitMissingSetOrExpects
			: CompileError::ParseTraitMissingSet;

		parse_error_fatal(&parser->lexer, lexeme.source_id, error);
	}

	skip(&parser->lexer);

	const AstBuilderToken body_token = parse_expr(parser, true);

	if (first_child_token == AstBuilderToken::NO_CHILDREN)
		first_child_token = body_token;

	return push_node(parser->builder, first_child_token, source_id, flags, AstTag::Trait);
}

static AstBuilderToken parse_impl(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdImpl);

	AstFlag flags = AstFlag::EMPTY;

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdExpects)
	{
		flags |= AstFlag::Impl_HasExpects;

		parse_expects(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token != Token::OpSet)
	{
		const CompileError error = (flags & AstFlag::Trait_HasExpects) == AstFlag::EMPTY
			? CompileError::ParseTraitMissingSetOrExpects
			: CompileError::ParseTraitMissingSet;

		parse_error_fatal(&parser->lexer, lexeme.source_id, error);
	}

	skip(&parser->lexer);

	parse_expr(parser, true);

	return push_node(parser->builder, first_child_token, source_id, flags, AstTag::Impl);
}

static AstBuilderToken parse_definition_or_impl(Parser* parser, bool* out_is_definition) noexcept
{
	const Lexeme lexeme = peek(&parser->lexer);

	bool is_definition = is_definition_start(lexeme.token);

	*out_is_definition = is_definition;

	if (is_definition)
		return parse_definition(parser, false, false);
	else if (lexeme.token == Token::KwdImpl)
		return parse_impl(parser);
	else
	{
		parse_error_continuable(&parser->lexer, lexeme.source_id, CompileError::ParseUnexpectedTopLevelExpr);

		parser->lexer.suppress_errors = true;

		const AstBuilderToken result = parse_expr(parser, true);

		parser->lexer.suppress_errors = false;

		return result;
	}
}

static AstBuilderToken parse_expr(Parser* parser, bool allow_complex) noexcept
{
	Lexeme lexeme = peek(&parser->lexer);

	OperatorStack stack;
	stack.operator_top = 0;
	stack.operand_count = 0;
	stack.expression_source_id = lexeme.source_id;

	bool expecting_operand = true;

	while (true)
	{
		if (expecting_operand)
		{
			if (lexeme.token == Token::Ident)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(parser->builder, AstBuilderToken::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, AstIdentifierData{ lexeme.identifier_id, NameBinding{} });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::LitString)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(parser->builder, AstBuilderToken::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, AstLitStringData{ lexeme.string.value_id, lexeme.string.type_id });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::LitFloat)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(parser->builder, AstBuilderToken::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, AstLitFloatData{ lexeme.float_value });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::LitInteger)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(parser->builder, AstBuilderToken::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, AstLitIntegerData{ lexeme.integer_value });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::LitChar)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(parser->builder, AstBuilderToken::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, AstLitCharData{ lexeme.char_value });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::Wildcard)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(parser->builder, AstBuilderToken::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, AstTag::Wildcard);

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::CompositeInitializer)
			{
				expecting_operand = false;

				const SourceId source_id = lexeme.source_id;

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				AstBuilderToken first_child_token = AstBuilderToken::NO_CHILDREN;

				while (lexeme.token != Token::CurlyR)
				{
					const AstBuilderToken curr_token = parse_expr(parser, true);

					if (first_child_token == AstBuilderToken::NO_CHILDREN)
						first_child_token = curr_token;

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::Comma)
					{
						skip(&parser->lexer);

						lexeme = peek(&parser->lexer);
					}
					else if (lexeme.token != Token::CurlyR)
					{
						parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseCompositeLiteralUnexpectedToken);
					}
				}

				const AstBuilderToken composite_token = push_node(parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::CompositeInitializer);

				push_operand(parser, &stack, composite_token);
			}
			else if (lexeme.token == Token::ArrayInitializer)
			{
				expecting_operand = false;

				const SourceId source_id = lexeme.source_id;

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				AstBuilderToken first_child_token = AstBuilderToken::NO_CHILDREN;

				while (lexeme.token != Token::BracketR)
				{
					const AstBuilderToken curr_token = parse_expr(parser, true);

					if (first_child_token == AstBuilderToken::NO_CHILDREN)
						first_child_token = curr_token;

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::Comma)
					{
						skip(&parser->lexer);

						lexeme = peek(&parser->lexer);
					}
					else if (lexeme.token != Token::BracketR)
					{
						parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseArrayLiteralUnexpectedToken);
					}
				}

				const AstBuilderToken array_token = push_node(parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::ArrayInitializer);

				push_operand(parser, &stack, array_token);
			}
			else if (lexeme.token == Token::BracketL) // Array Type
			{
				const SourceId source_id = lexeme.source_id;

				skip(&parser->lexer);

				const AstBuilderToken count_token = parse_expr(parser, false);

				lexeme = peek(&parser->lexer);

				if (lexeme.token != Token::BracketR)
					parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseArrayTypeUnexpectedToken);

				push_operand(parser, &stack, count_token);

				push_operator(parser, &stack, OperatorDescWithSource{ { AstTag::OpTypeArray, AstFlag::EMPTY, 2, false, true }, source_id });
			}
			else if (lexeme.token == Token::CurlyL) // Block
			{
				expecting_operand = false;

				const SourceId source_id = lexeme.source_id;

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				AstBuilderToken first_child_token = AstBuilderToken::NO_CHILDREN;

				while (lexeme.token != Token::CurlyR)
				{
					bool is_definition;

					const AstBuilderToken curr_token = parse_top_level_expr(parser, false, &is_definition);

					if (first_child_token == AstBuilderToken::NO_CHILDREN)
						first_child_token = curr_token;

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::CurlyR)
						break;
				}

				const AstBuilderToken block_token = push_node(parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::Block);

				push_operand(parser, &stack, block_token);
			}
			else if (lexeme.token == Token::KwdIf)
			{
				expecting_operand = false;

				const AstBuilderToken if_token = parse_if(parser);

				push_operand(parser, &stack, if_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdFor)
			{
				expecting_operand = false;

				const AstBuilderToken for_token = parse_for(parser);

				push_operand(parser, &stack, for_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdSwitch)
			{
				expecting_operand = false;

				const AstBuilderToken switch_token = parse_switch(parser);

				push_operand(parser, &stack, switch_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdFunc || lexeme.token == Token::KwdProc)
			{
				expecting_operand = false;

				const AstBuilderToken func_token = parse_func(parser);

				push_operand(parser, &stack, func_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdTrait)
			{
				expecting_operand = false;

				const AstBuilderToken trait_token = parse_trait(parser);

				push_operand(parser, &stack, trait_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdImpl)
			{
				expecting_operand = false;

				const AstBuilderToken impl_token = parse_impl(parser);

				push_operand(parser, &stack, impl_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdUnreachable)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(parser->builder, AstBuilderToken::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, AstTag::Unreachable);

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::KwdUndefined)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(parser->builder, AstBuilderToken::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, AstTag::Undefined);

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::Builtin)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(parser->builder, AstBuilderToken::NO_CHILDREN, lexeme.source_id, static_cast<AstFlag>(lexeme.builtin), AstTag::Builtin);

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::OpMemberOrRef)
			{
				expecting_operand = false;

				const SourceId source_id = lexeme.source_id;

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				if (lexeme.token != Token::Ident)
					parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseImpliedMemberUnexpectedToken);

				const AstBuilderToken implied_member_token = push_node(parser->builder, AstBuilderToken::NO_CHILDREN, source_id, AstFlag::EMPTY, AstImpliedMemberData{ lexeme.identifier_id });

				push_operand(parser, &stack, implied_member_token);
			}
			else // Unary operator
			{
				const SourceId source_id = lexeme.source_id;

				const u8 token_ordinal = static_cast<u8>(lexeme.token);

				const u8 lo_ordinal = static_cast<u8>(Token::ParenL);

				const u8 hi_ordinal = static_cast<u8>(Token::OpAdd);

				if (token_ordinal < lo_ordinal || token_ordinal > hi_ordinal)
					parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseExprExpectOperand);

				OperatorDesc op = UNARY_OPERATOR_DESCS[token_ordinal - lo_ordinal];

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				if (op.node_flags == AstFlag::Type_IsMut)
				{
					if (lexeme.token == Token::KwdMut)
					{
						skip(&parser->lexer);

						lexeme = peek(&parser->lexer);
					}
					else
					{
						op.node_flags = AstFlag::EMPTY;
					}
				}

				push_operator(parser, &stack, { op, source_id });

				continue;
			}
		}
		else
		{
			if (lexeme.token == Token::ParenL) // Function call
			{
				ASSERT_OR_IGNORE(stack.operand_count != 0);

				const SourceId source_id = lexeme.source_id;

				pop_to_precedence(parser, &stack, 1, true);

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				u32 arg_count = 0;

				while (lexeme.token != Token::ParenR)
				{
					// Only report this for the first parameter after the
					// maximum by performing a strict equality check.
					if (arg_count == MAX_FUNC_PARAM_COUNT)
						parse_error_continuable(&parser->lexer, lexeme.source_id, CompileError::ParseCallTooManyArguments);

					arg_count += 1;

					bool unused;

					parse_top_level_expr(parser, true, &unused);

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::Comma)
					{
						skip(&parser->lexer);

						lexeme = peek(&parser->lexer);
					}
					else if (lexeme.token != Token::ParenR)
					{
						parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseCallUnexpectedToken);
					}
				}

				const AstBuilderToken call_token = push_node(parser->builder, stack.operand_tokens[stack.operand_count - 1], source_id, AstFlag::EMPTY, AstTag::Call);

				stack.operand_tokens[stack.operand_count - 1] = call_token;
			}
			else if (lexeme.token == Token::ParenR) // Closing parenthesis
			{
				if (!pop_to_precedence(parser, &stack, 10, false))
				{
					ASSERT_OR_IGNORE(stack.operand_count == 1);

					// No need for stack.pop_remaining; pop_to_precedence already
					// popped everything.
					return stack.operand_tokens[stack.operand_count - 1];
				}

				remove_lparen(&stack);
			}
			else if (lexeme.token == Token::BracketL) // Array Index or Slice
			{
				ASSERT_OR_IGNORE(stack.operand_count != 0);

				const SourceId source_id = lexeme.source_id;

				pop_to_precedence(parser, &stack, 1, true);

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				if (lexeme.token == Token::DoubleDot)
				{
					AstFlag flags = AstFlag::EMPTY;

					skip(&parser->lexer);

					lexeme = peek(&parser->lexer);

					if (lexeme.token != Token::BracketR)
					{
						flags |= AstFlag::OpSliceOf_HasEnd;

						parse_expr(parser, false);

						lexeme = peek(&parser->lexer);

						if (lexeme.token != Token::BracketR)
							parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseSliceUnexpectedToken);
					}

					const AstBuilderToken slice_token = push_node(parser->builder, stack.operand_tokens[stack.operand_count - 1], source_id, flags, AstTag::OpSliceOf);

					stack.operand_tokens[stack.operand_count - 1] = slice_token;
				}
				else
				{
					parse_expr(parser, false);

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::BracketR)
					{
						const AstBuilderToken index_token = push_node(parser->builder, stack.operand_tokens[stack.operand_count - 1], source_id, AstFlag::EMPTY, AstTag::OpArrayIndex);

						stack.operand_tokens[stack.operand_count - 1] = index_token;
					}
					else if (lexeme.token == Token::DoubleDot)
					{
						AstFlag flags = AstFlag::OpSliceOf_HasBegin;

						skip(&parser->lexer);

						lexeme = peek(&parser->lexer);

						if (lexeme.token != Token::BracketR)
						{
							flags |= AstFlag::OpSliceOf_HasEnd;

							parse_expr(parser, false);

							lexeme = peek(&parser->lexer);

							if (lexeme.token != Token::BracketR)
								parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseSliceUnexpectedToken);
						}

						const AstBuilderToken slice_token = push_node(parser->builder, stack.operand_tokens[stack.operand_count - 1], source_id, flags, AstTag::OpSliceOf);

						stack.operand_tokens[stack.operand_count - 1] = slice_token;
					}
					else
					{
						parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseArrayIndexUnexpectedToken);
					}
				}
			}
			else if (lexeme.token == Token::KwdCatch)
			{
				const SourceId source_id = lexeme.source_id;

				AstFlag flags = AstFlag::EMPTY;

				pop_to_precedence(parser, &stack, 1, true);

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				if (is_definition_start(lexeme.token) || peek_n(&parser->lexer, 1).token == Token::ThinArrowR)
				{
					flags |= AstFlag::Catch_HasDefinition;

					parse_definition(parser, true, false);

					lexeme = next(&parser->lexer);

					if (lexeme.token != Token::ThinArrowR)
						parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseCatchMissingThinArrowRightAfterDefinition);
				}

				parse_expr(parser, false);

				const AstBuilderToken catch_token = push_node(parser->builder, stack.operand_tokens[stack.operand_count - 1], source_id, flags, AstTag::Catch);

				stack.operand_tokens[stack.operand_count - 1] = catch_token;

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::OpMemberOrRef)
			{
				const SourceId source_id = lexeme.source_id;

				pop_to_precedence(parser, &stack, 1, true);

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				if (lexeme.token != Token::Ident)
					parse_error_fatal(&parser->lexer, lexeme.source_id, CompileError::ParseMemberUnexpectedToken);

				const AstBuilderToken member_token = push_node(parser->builder, stack.operand_tokens[stack.operand_count - 1], source_id, AstFlag::EMPTY, AstMemberData{ lexeme.identifier_id });

				stack.operand_tokens[stack.operand_count - 1] = member_token;
			}
			else // Binary operator
			{
				const u8 token_ordinal = static_cast<u8>(lexeme.token);

				const u8 lo_ordinal = static_cast<u8>(Token::OpMulOrTypPtr);

				const u8 hi_ordinal = static_cast<u8>(Token::OpSetShr);

				if (token_ordinal < lo_ordinal || token_ordinal > hi_ordinal || (!allow_complex && lexeme.token == Token::OpSet))
					break;

				const OperatorDesc op = BINARY_OPERATOR_DESCS[token_ordinal - lo_ordinal];

				push_operator(parser, &stack, { op, lexeme.source_id });

				expecting_operand = op.is_binary;
			}
		}

		skip(&parser->lexer);

		lexeme = peek(&parser->lexer);
	}

	return pop_remaining(parser, &stack);
}

static bool parse_file(Parser* parser) noexcept
{
	if (setjmp(parser->lexer.error_jump_buffer) != 0)
		return false;

	AstBuilderToken first_child_token = AstBuilderToken::NO_CHILDREN;

	u32 member_count = 0;

	while (true)
	{
		const Lexeme lexeme = peek(&parser->lexer);

		if (lexeme.token == Token::END_OF_SOURCE)
			break;

		bool is_definition;

		const AstBuilderToken curr_token = parse_definition_or_impl(parser, &is_definition);

		if (is_definition)
			member_count += 1;

		if (first_child_token == AstBuilderToken::NO_CHILDREN)
			first_child_token = curr_token;
	};

	push_node(parser->builder, first_child_token, SourceId{ parser->lexer.source_id_base }, AstFlag::EMPTY, AstFileData{ member_count });
	
	return !parser->lexer.has_errors;
}



Parser* create_parser(HandlePool* pool, IdentifierPool* identifiers, GlobalValuePool* globals, TypePool* types, AstPool* asts, ErrorSink* errors) noexcept
{
	Parser* const parser = alloc_handle_from_pool<Parser>(pool);

	parser->builder = asts;
	parser->lexer.u8_type_id = type_create_numeric(types, TypeTag::Integer, NumericType{ 8, false });
	parser->lexer.identifiers = identifiers;
	parser->lexer.globals = globals;
	parser->lexer.types = types;
	parser->lexer.errors = errors;
	parser->lexer.suppress_errors = false;

	for (const AttachmentRange keyword : KEYWORDS)
		identifier_set_attachment(identifiers, keyword.range(), keyword.attachment());

	return parser;
}

void release_parser([[maybe_unused]] Parser* parser) noexcept
{
	// No-op
}

Maybe<AstNode*> parse(Parser* parser, Range<char8> content, SourceId source_id_base, bool is_std) noexcept
{
	ASSERT_OR_IGNORE(content.count() != 0 && content.end()[-1] == '\0');

	parser->lexer.begin = content.begin();
	parser->lexer.end = content.end() - 1;
	parser->lexer.curr = content.begin();
	parser->lexer.source_id_base = static_cast<u32>(source_id_base);
	parser->lexer.peek.token = Token::EMPTY;
	parser->lexer.is_std = is_std;
	parser->lexer.has_errors = false;

	if (!parse_file(parser))
		return none<AstNode*>();

	AstNode* const root = complete_ast(parser->builder);

	return some(root);
}

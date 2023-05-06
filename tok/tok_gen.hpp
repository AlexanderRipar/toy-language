#ifndef TOK_GEN_INCLUDE_GUARD
#define TOK_GEN_INCLUDE_GUARD

#include <vector>
#include "../util/vec.hpp"
#include "../util/strview.hpp"
#include "../util/types.hpp"

struct Token
{
	enum class Tag
	{
		INVALID = 0,
		Ident,
		Set,
		SetAdd,
		SetSub,
		SetMul,
		SetDiv,
		SetMod,
		SetBitAnd,
		SetBitOr,
		SetBitXor,
		SetBitShl,
		SetBitShr,
		UOpLogNot,
		UOpBitNot,
		UOpDeref,
		OpMul_Ptr,
		OpDiv,
		OpMod,
		OpAdd,
		OpSub,
		OpBitShl,
		OpBitShr,
		OpLt,
		OpLe,
		OpGt,
		OpGe,
		OpEq,
		OpNe,
		OpBitAnd_Ref,
		OpBitXor,
		OpBitOr,
		OpLogAnd,
		OpLogOr,
		Dot,
		Colon,
		TripleDot,
		Semicolon,
		Comma,
		ArrowLeft,
		ArrowRight,
		SquiggleBeg,
		SquiggleEnd,
		BracketBeg,
		BracketEnd,
		ParenBeg,
		ParenEnd,
		LitString,
		LitChar,
		LitInt,
		LitFloat,
		LitBadNumber,
		Hashtag,
		Comment,
		IncompleteComment,
		If,
		Else,
		For,
		Do,
		Break,
		Until,
		Catch,
		Try,
		Switch,
		Case,
		Yield,
		Return,
		Defer,
		DoubleColon,
		Proc,
		Struct,
		Union,
		Enum,
		Trait,
		Impl,
		Module,
		Mut,
		Pub,
		Undefined,
	} tag;

	u32 line_number;

	strview data;

	strview type_strview() const noexcept;

	strview data_strview() const noexcept;
};

vec<Token> tokenize(strview data, bool include_comments) noexcept;

#endif // TOK_GEN_INCLUDE_GUARD

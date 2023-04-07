#ifndef TOKEN_GEN_INCLUDE_GUARD
#define TOKEN_GEN_INCLUDE_GUARD

#include <vector>
#include "../util/vec.hpp"
#include "../util/strview.hpp"
#include "../util/types.hpp"

struct Token
{
	enum class Tag
	{
		INVALID = 0,
		EndOfStream,
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
		OpMul,
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
		LitString,
		LitChar,
		LitInt,
		LitFloat,
		LitBadNumber,
		Colon,
		Dot,
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
		Hashtag,
		Comment,
		IncompleteComment,
		If,
		Else,
		For,
		Do,
		Until,
		Catch,
		Switch,
		Case,
		Go,
		To,
		Yield,
		Return,
		DoubleColon,
		Proc,
		Struct,
		Union,
		Enum,
		Bitset,
		Alias,
		Trait,
		Impl,
		Annotation,
		Module,
		Mut,
		Const,
		Pub,
	} tag;

	u32 line_number;

	strview data;

	strview type_strview() const noexcept;

	strview data_strview() const noexcept;
};

vec<Token> tokenize(strview data, bool include_comments) noexcept;

#endif // TOKEN_GEN_INCLUDE_GUARD
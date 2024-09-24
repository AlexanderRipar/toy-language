#ifndef TOKEN_INCLUDE_GUARD
#define TOKEN_INCLUDE_GUARD

#include "common.hpp"
#include <cassert>

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
		KwdCatch,             // catch
		KwdLet,               // let
		KwdPub,               // pub
		KwdMut,               // mut
		KwdGlobal,            // global
		KwdAuto,              // auto
		KwdUse,               // use
		ArrayInitializer,     // .[
		CompositeInitializer, // .{
		BracketR,             // ]
		BracketL,             // [
		CurlyR,               // }
		CurlyL,               // {
		ParenR,               // )
		ParenL,               // (
		KwdTry,               // try
		KwdDefer,             // defer
		UOpAddr,              // $
		UOpDeref,             // .*
		UOpNot,               // ~
		UOpLogNot,            // !
		TypOptPtr,            // ?
		TypVar,               // ...
		TypTailArray,         // [...]
		TypMultiPtr,          // [*]
		TypSlice,             // []
		OpMemberOrRef,        // .
		OpMulOrTypPtr,        // *
		OpSub,                // -
		OpAdd,                // +
		OpDiv,                // /
		OpAddTC,              // +:
		OpSubTC,              // -:
		OpMulTC,              // *:
		OpMod,                // %
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
		END_OF_SOURCE,
		MAX,
};

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
	"catch",
	"let",
	"pub",
	"mut",
	"global",
	"auto",
	"use",
	".[",
	".{",
	"]",
	"[",
	"}",
	"{",
	")",
	"(",
	"try",
	"defer",
	"$",
	".*",
	"~",
	"!",
	"?",
	"...",
	"[...]",
	"[*]",
	"[]",
	".",
	"*",
	"-",
	"+",
	"/",
	"+:",
	"-:",
	"*:",
	"%",
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
	"[END-OF-SOURCE]",
};

constexpr inline const char8* token_name(Token token) noexcept
{
	const u8 ordinal = static_cast<u8>(token);

	if (ordinal < array_count(TOKEN_NAMES))
		return TOKEN_NAMES[ordinal];

	return TOKEN_NAMES[0];
}

#endif // TOKEN_INCLUDE_GUARD

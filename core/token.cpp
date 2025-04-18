#include "pass_data.hpp"

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
	"global",
	"auto",
	"use",
	"return",
	"leave",
	"yield",
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
	"$",
	"~",
	"!",
	"?",
	"...",
	"[...]",
	"[*]",
	"[?]",
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

const char8* token_name(Token token) noexcept
{
	const u8 ordinal = static_cast<u8>(token);

	if (ordinal < array_count(TOKEN_NAMES))
		return TOKEN_NAMES[ordinal];

	return TOKEN_NAMES[0];
}

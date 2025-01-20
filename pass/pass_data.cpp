#include "pass_data.hpp"

#include "../infra/hash.hpp"

static constexpr AttachmentRange<char8, Token> KEYWORDS[] = {
	range::from_literal_string("if",      Token::KwdIf),
	range::from_literal_string("then",    Token::KwdThen),
	range::from_literal_string("else",    Token::KwdElse),
	range::from_literal_string("for",     Token::KwdFor),
	range::from_literal_string("do",      Token::KwdDo),
	range::from_literal_string("finally", Token::KwdFinally),
	range::from_literal_string("switch",  Token::KwdSwitch),
	range::from_literal_string("case",    Token::KwdCase),
	range::from_literal_string("eval",    Token::KwdEval),
	range::from_literal_string("try",     Token::KwdTry),
	range::from_literal_string("catch",   Token::KwdCatch),
	range::from_literal_string("defer",   Token::KwdDefer),
	range::from_literal_string("func",    Token::KwdFunc),
	range::from_literal_string("proc",    Token::KwdProc),
	range::from_literal_string("trait",   Token::KwdTrait),
	range::from_literal_string("impl",    Token::KwdImpl),
	range::from_literal_string("where",   Token::KwdWhere),
	range::from_literal_string("expects", Token::KwdExpects),
	range::from_literal_string("ensures", Token::KwdEnsures),
	range::from_literal_string("pub",     Token::KwdPub),
	range::from_literal_string("mut",     Token::KwdMut),
	range::from_literal_string("let",     Token::KwdLet),
	range::from_literal_string("auto",    Token::KwdAuto),
	range::from_literal_string("use",     Token::KwdUse),
	range::from_literal_string("global",  Token::KwdGlobal),
};

static u32 read_completion_thread_proc(void* param) noexcept
{
	Globals* const glob = static_cast<Globals*>(param);

	while (true)
	{
		minos::CompletionResult result;

		if (!minos::completion_wait(glob->read.completion_handle, &result))
			panic("Could not wait for read completion (0x%X)\n", minos::last_error());

		Read* const read = reinterpret_cast<Read*>(result.overlapped);

		glob->read.completed_reads.push(glob->read.reads, static_cast<u32>(read - glob->read.reads));

		glob->read.available_read_count.post();
	}
}

Globals::Globals() noexcept :
	identifiers{ 1 << 24, 1 << 14, 1 << 28, 1 << 16, 1 << 16 },
	read{ {}, { read.reads, static_cast<u32>(array_count(read.reads)) }, { 0 }, { 0 } }
{
	asts.init(1ui64 << 31, 1ui64 << 17);

	values.init(1ui64 << 31, 1ui64 << 17);

	ast_scratch.init(1ui64 << 31, 1ui64 << 17);

	stack_scratch.init(1ui64 << 31, 1ui64 << 17);

	static constexpr u64 SHM_BYTES = 1ui64 << 31;

	for (u32 i = 0; i != array_count(KEYWORDS); ++i)
		identifiers.value_from(KEYWORDS[i].range(), fnv1a(KEYWORDS[i].as_byte_range()))->set_token(KEYWORDS[i].attachment());

	if (!minos::completion_create(&read.completion_handle))
		panic("Could not create read completion handle (0x%X)\n", minos::last_error());

	if (!minos::thread_create(read_completion_thread_proc, this, range::from_literal_string("Read Completions"), &read.completion_thread))
		panic("Could not create read completion thread (0x%X)\n", minos::last_error());
}

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

#include "tok_gen.hpp"

#include "../util/vec.hpp"
#include "../util/strview.hpp"
#include <cassert>

strview Token::type_strview() const noexcept
{
	static constexpr strview types[]
	{
		strview::from_literal("INVALID"),
		strview::from_literal("Ident"),
		strview::from_literal("OpAdd"),
		strview::from_literal("OpSub"),
		strview::from_literal("OpMul_Ptr"),
		strview::from_literal("OpDiv"),
		strview::from_literal("OpMod"),
		strview::from_literal("OpBitAnd_Ref"),
		strview::from_literal("OpBitOr"),
		strview::from_literal("OpBitXor"),
		strview::from_literal("OpShiftL"),
		strview::from_literal("OpShiftR"),
		strview::from_literal("OpLogAnd"),
		strview::from_literal("OpLogOr"),
		strview::from_literal("OpCmpLt"),
		strview::from_literal("OpCmpLe"),
		strview::from_literal("OpCmpGt"),
		strview::from_literal("OpCmpGe"),
		strview::from_literal("OpCmpNe"),
		strview::from_literal("OpCmpEq"),
		strview::from_literal("Dot"),
		strview::from_literal("Catch"),
		strview::from_literal("Index"),
		strview::from_literal("Set"),
		strview::from_literal("SetAdd"),
		strview::from_literal("SetSub"),
		strview::from_literal("SetMul"),
		strview::from_literal("SetDiv"),
		strview::from_literal("SetMod"),
		strview::from_literal("SetBitAnd"),
		strview::from_literal("SetBitOr"),
		strview::from_literal("SetBitXor"),
		strview::from_literal("SetShiftL"),
		strview::from_literal("SetShiftR"),
		strview::from_literal("UOpLogNot"),
		strview::from_literal("UOpBitNot"),
		strview::from_literal("UOpDeref"),
		strview::from_literal("Colon"),
		strview::from_literal("TripleDot"),
		strview::from_literal("Semicolon"),
		strview::from_literal("Comma"),
		strview::from_literal("ArrowLeft"),
		strview::from_literal("ArrowRight"),
		strview::from_literal("FatArrowRight"),
		strview::from_literal("SquiggleBeg"),
		strview::from_literal("SquiggleEnd"),
		strview::from_literal("BracketBeg"),
		strview::from_literal("BracketEnd"),
		strview::from_literal("ParenBeg"),
		strview::from_literal("ParenEnd"),
		strview::from_literal("LitString"),
		strview::from_literal("LitChar"),
		strview::from_literal("LitInt"),
		strview::from_literal("LitFloat"),
		strview::from_literal("LitBadNumber"),
		strview::from_literal("Hashtag"),
		strview::from_literal("Comment"),
		strview::from_literal("IncompleteComment"),
		strview::from_literal("If"),
		strview::from_literal("Then"),
		strview::from_literal("Else"),
		strview::from_literal("For"),
		strview::from_literal("Do"),
		strview::from_literal("Break"),
		strview::from_literal("Finally"),
		strview::from_literal("Try"),
		strview::from_literal("Switch"),
		strview::from_literal("Case"),
		strview::from_literal("Return"),
		strview::from_literal("Defer"),
		strview::from_literal("DoubleColon"),
		strview::from_literal("Proc"),
		strview::from_literal("Func"),
		strview::from_literal("Trait"),
		strview::from_literal("Module"),
		strview::from_literal("Impl"),
		strview::from_literal("Mut"),
		strview::from_literal("Pub"),
		strview::from_literal("Undefined"),
	};

	if (static_cast<u32>(tag) >= _countof(types))
		return types[0];

	return types[static_cast<u32>(tag)];
}

strview Token::data_strview() const noexcept
{
	return data;
}

static bool is_name_token_char(char c) noexcept
{
	return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static Token get_token(const char* beg, const char* const end, const char** out_token_end, uint32_t& inout_curr_line_number) noexcept
{
	const char fst = *beg;

	const char nxt = beg + 1 < end ? beg[1] : '\0';

	const char* c = beg;

	Token t{};

	t.line_number = inout_curr_line_number;

	if (fst == '_' || (fst >= 'a' && fst <= 'z') || (fst >= 'A' && fst <= 'Z'))
	{
		++c;

		while (c != end && is_name_token_char(*c))
			++c;

		if (streqc({ beg, c }, strview::from_literal("for")))
			t.tag = Token::Tag::For;
		else if (streqc({ beg, c }, strview::from_literal("do")))
			t.tag = Token::Tag::Do;
		else if (streqc({ beg, c }, strview::from_literal("break")))
			t.tag = Token::Tag::Break;
		else if (streqc({ beg, c }, strview::from_literal("finally")))
			t.tag = Token::Tag::Finally;
		else if (streqc({ beg, c }, strview::from_literal("if")))
			t.tag = Token::Tag::If;
		else if (streqc({ beg, c }, strview::from_literal("then")))
			t.tag = Token::Tag::Then;
		else if (streqc({ beg, c }, strview::from_literal("else")))
			t.tag = Token::Tag::Else;
		else if (streqc({ beg, c }, strview::from_literal("switch")))
			t.tag = Token::Tag::Switch;
		else if (streqc({ beg, c }, strview::from_literal("return")))
			t.tag = Token::Tag::Return;
		else if (streqc({ beg, c }, strview::from_literal("defer")))
			t.tag = Token::Tag::Defer;
		else if (streqc({ beg, c }, strview::from_literal("case")))
			t.tag = Token::Tag::Case;
		else if (streqc({ beg, c }, strview::from_literal("func")))
			t.tag = Token::Tag::Func;
		else if (streqc({ beg, c }, strview::from_literal("proc")))
			t.tag = Token::Tag::Proc;
		else if (streqc({ beg, c }, strview::from_literal("trait")))
			t.tag = Token::Tag::Trait;
		else if (streqc({ beg, c }, strview::from_literal("module")))
			t.tag = Token::Tag::Module;
		else if (streqc({ beg, c }, strview::from_literal("impl")))
			t.tag = Token::Tag::Impl;
		else if (streqc({ beg, c }, strview::from_literal("mut")))
			t.tag = Token::Tag::Mut;
		else if (streqc({ beg, c }, strview::from_literal("pub")))
			t.tag = Token::Tag::Pub;
		else if (streqc({ beg, c }, strview::from_literal("catch")))
			t.tag = Token::Tag::Catch;
		else if (streqc({ beg, c }, strview::from_literal("try")))
			t.tag = Token::Tag::Try;
		else if (streqc({ beg, c }, strview::from_literal("undefined")))
			t.tag = Token::Tag::Undefined;
		else
			t.tag = Token::Tag::Ident;
	}
	else if (fst >= '0' && fst <= '9')
	{
		bool is_float = false;

		if (fst == '0' && (nxt == 'x' || nxt == 'X'))
		{
			c += 2;

			while (c != end && ((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f') || (*c >= 'A' && *c <= 'F')))
				++c;
		}
		else if (fst == '0' && (nxt == 'o' || nxt == 'O'))
		{
			c += 2;

			while (c != end && *c >= '0' && *c <= '7')
				++c;
		}
		else if (fst == '0' && (nxt == 'b' || nxt == 'B'))
		{
			c += 2;

			while (c != end && *c >= '0' && *c <= '1')
				++c;
		}
		else
		{
			while (c != end && *c >= '0' && *c <= '9')
				++c;

			if (c != end && *c == '.')
			{
				++c;

				is_float = true;

				while (c != end && *c >= '0' && *c <= '9')
					++c;
			}
			
			if (c != end && (*c == 'e' || *c == 'E'))
			{
				++c;

				is_float = true;

				if (c != end && (*c == '+' || *c == '-'))
					++c;

				while (c != end && *c >= '0' && *c <= '9')
					++c;
			}
		}

		if (c != end && is_name_token_char(*c))
			t.tag = Token::Tag::LitBadNumber;
		else if (is_float)
			t.tag = Token::Tag::LitFloat;
		else
			t.tag = Token::Tag::LitInt;
	}
	else if (fst == '\'')
	{
		++c;

		t.tag = Token::Tag::LitChar;

		bool escaped = false;

		while (c != end)
		{
			if (!escaped && *c == '\'')
				break;
			
			escaped = *c == '\\';

			++c;
		}

		++c;
	}
	else if (fst == '"')
	{
		++c;

		t.tag = Token::Tag::LitString;

		bool escaped = false;

		while (c != end)
		{
			if (!escaped && *c == '\"')
				break;
			
			escaped = *c == '\\';

			++c;
		}

		++c;
	}
	else
	{
		switch(fst)
		{
		case '[': {
			t.tag = Token::Tag::BracketBeg;
			
			break;
		}

		case ']': {
			t.tag = Token::Tag::BracketEnd;
			
			break;
		}
		
		case '{': {
			t.tag = Token::Tag::SquiggleBeg;
			
			break;
		}
		
		case '}': {
			t.tag = Token::Tag::SquiggleEnd;
			
			break;
		}

		case '(': {
			t.tag = Token::Tag::ParenBeg;
			
			break;
		}

		case ')': {
			t.tag = Token::Tag::ParenEnd;
			
			break;
		}

		case ':': {
			if (nxt == ':')
			{
				++c;

				t.tag = Token::Tag::DoubleColon;
			}
			else
			{
				t.tag = Token::Tag::Colon;
			}
			
			break;
		}

		case ';': {
			t.tag = Token::Tag::Semicolon;
			
			break;
		}

		case ',': {
			t.tag = Token::Tag::Comma;
			
			break;
		}

		case '#': {
			t.tag = Token::Tag::Hashtag;
			
			break;
		}
		
		case '.': {
			if (c + 2 < end && c[1] == '.' && c[2] == '.')
			{
				t.tag = Token::Tag::TripleDot;

				c += 2;
			}
			else
			{
				t.tag = Token::Tag::Dot;
			}

			break;
		}

		case '<': {
			if (nxt == '<')
			{
				if (beg + 2 < end && beg[2] == '=')
				{
					c += 2;

					t.tag = Token::Tag::SetShiftL;
				}
				else
				{
					++c;

					t.tag = Token::Tag::OpShiftL;
				}
			}
			else if (nxt == '=')
			{
				++c;

				t.tag = Token::Tag::OpCmpLe;
			}
			else if (nxt == '-')
			{
				++c;

				t.tag = Token::Tag::ArrowLeft;
			}
			else
			{
				t.tag = Token::Tag::OpCmpLt;
			}

			break;
		}

		case '>': {
			if (nxt == '>')
			{
				if (beg + 2 < end && beg[2] == '=')
				{
					c += 2;

					t.tag = Token::Tag::SetShiftR;
				}
				else
				{
					++c;

					t.tag = Token::Tag::OpShiftR;
				}
			}
			else if (nxt == '=')
			{
				++c;

				t.tag = Token::Tag::OpCmpGe;
			}
			else
			{
				t.tag = Token::Tag::OpCmpGt;
			}

			break;
		}

		case '=': {
			if (nxt == '=')
			{
				++c;

				t.tag = Token::Tag::OpCmpEq;
			}
			else if (nxt == '>')
			{
				++c;

				t.tag = Token::Tag::FatArrowRight;
			}
			else
			{
				t.tag = Token::Tag::Set;
			}

			break;
		}

		case '+': {
			if (nxt == '=')
			{
				++c;

				t.tag = Token::Tag::SetAdd;
			}
			else
			{
				t.tag = Token::Tag::OpAdd;
			}

			break;
		}
		
		case '-': {
			if (nxt == '>')
			{
				++c;

				t.tag = Token::Tag::ArrowRight;
			}
			else if (nxt == '=')
			{
				++c;

				t.tag = Token::Tag::SetSub;
			}
			else
			{
				t.tag = Token::Tag::OpSub;
			}

			break;
		}

		case '*': {
			if (nxt == '=')
			{
				++c;

				t.tag = Token::Tag::SetMul;
			}
			else
			{
				t.tag = Token::Tag::OpMul_Ptr;
			}

			break;
		}

		case '/': {
			if (nxt == '=')
			{
				++c;

				t.tag = Token::Tag::SetDiv;
			}
			else if (nxt == '/')
			{
				t.tag = Token::Tag::Comment;

				c += 2;

				while (c != end && *c != '\r' && *c != '\n')
					++c;

				t.data = { beg, c };
			}
			else if (nxt == '*')
			{
				uint32_t nesting_count = 1;

				c += 2;

				while (c != end)
				{
					if (c + 1 < end && c[0] == '*' && c[1] == '/')
					{
						++c;

						--nesting_count;
						
						if (nesting_count == 0)
							break;
					}
					else if (c + 1 < end && c[0] == '/' && c[1] == '*')
					{
						++c;

						++nesting_count;
					}
					else if (*c == '\n')
					{
						++inout_curr_line_number;
					}
					
					++c;
				}
				
				if (nesting_count == 0)
					t.tag = Token::Tag::Comment;
				else
					t.tag = Token::Tag::IncompleteComment;
			}
			else
			{
				t.tag = Token::Tag::OpDiv;
			}

			break;
		}

		case '%': {
			if (nxt == '=')
			{
				++c;

				t.tag = Token::Tag::SetMod;
			}
			else
			{
				t.tag = Token::Tag::OpMod;
			}

			break;
		}

		case '&': {
			if (nxt == '&')
			{
				++c;

				t.tag = Token::Tag::OpLogAnd;
			}
			else if (nxt == '=')
			{
				++c;

				t.tag = Token::Tag::SetBitAnd;
			}
			else
			{
				t.tag = Token::Tag::OpBitAnd_Ref;
			}

			break;
		}

		case '|': {
			if (nxt == '|')
			{
				++c;

				t.tag = Token::Tag::OpLogOr;
			}
			else if (nxt == '=')
			{
				++c;

				t.tag = Token::Tag::SetBitOr;
			}
			else
			{
				t.tag = Token::Tag::OpBitOr;
			}

			break;
		}

		case '^': {
			if (nxt == '=')
			{
				++c;

				t.tag = Token::Tag::SetBitXor;
			}
			else
			{
				t.tag = Token::Tag::OpBitXor;
			}

			break;
		}

		case '~': {
			t.tag = Token::Tag::UOpBitNot;

			break;
		}

		case '!': {
			if (nxt == '=')
			{
				++c;

				t.tag = Token::Tag::OpCmpNe;
			}
			else
			{
				t.tag = Token::Tag::UOpLogNot;
			}

			break;
		}

		case '$': {
			t.tag = Token::Tag::UOpDeref;

			break;
		}
		
		default: {
			t.tag = Token::Tag::INVALID;

			break;
		}
		}

		if (c != end)
			++c;
	}

	if (fst == '\'' || fst == '"')
		t.data = { beg + 1, c - 1 };
	else
		t.data = { beg, c };

	*out_token_end = c;

	return t;
}

vec<Token> tokenize(strview data, bool include_comments) noexcept
{
	u32 curr_line_number = 1;

	vec<Token> tokens;

	const char* const end = data.end();

	const char* c = data.begin();

	while (c != end)
	{
		while(c != end && is_whitespace(*c))
		{
			if (*c == '\n')
				++curr_line_number;

			++c;
		}

		if (c == end)
			break;

		tokens.push_back(get_token(c, end, &c, curr_line_number));

		if (!include_comments && tokens.last().tag == Token::Tag::Comment)
			tokens.pop();
	}

	return std::move(tokens);
}

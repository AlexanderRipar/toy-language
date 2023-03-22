#include "token_gen.hpp"

#include "util/vec.hpp"
#include "util/strview.hpp"
#include <cassert>

strview Token::type_strview() const noexcept
{
	static constexpr strview types[]
	{
		strview::from_literal("INVALID"),
		strview::from_literal("EndOfStream"),
		strview::from_literal("Ident"),
		strview::from_literal("Set"),
		strview::from_literal("SetAdd"),
		strview::from_literal("SetSub"),
		strview::from_literal("SetMul"),
		strview::from_literal("SetDiv"),
		strview::from_literal("SetMod"),
		strview::from_literal("SetBitAnd"),
		strview::from_literal("SetBitOr"),
		strview::from_literal("SetBitXor"),
		strview::from_literal("SetBitShl"),
		strview::from_literal("SetBitShr"),
		strview::from_literal("UOpLogNot"),
		strview::from_literal("UOpBitNot"),
		strview::from_literal("OpMul"),
		strview::from_literal("OpDiv"),
		strview::from_literal("OpMod"),
		strview::from_literal("OpAdd"),
		strview::from_literal("OpSub"),
		strview::from_literal("OpBitShl"),
		strview::from_literal("OpBitShr"),
		strview::from_literal("OpLt"),
		strview::from_literal("OpLe"),
		strview::from_literal("OpGt"),
		strview::from_literal("OpGe"),
		strview::from_literal("OpEq"),
		strview::from_literal("OpNe"),
		strview::from_literal("OpBitAnd_Ref"),
		strview::from_literal("OpBitXor"),
		strview::from_literal("OpBitOr"),
		strview::from_literal("OpLogAnd"),
		strview::from_literal("OpLogOr"),
		strview::from_literal("LitString"),
		strview::from_literal("LitChar"),
		strview::from_literal("LitInt"),
		strview::from_literal("LitFloat"),
		strview::from_literal("LitBadNumber"),
		strview::from_literal("Colon"),
		strview::from_literal("Dot"),
		strview::from_literal("TripleDot"),
		strview::from_literal("Semicolon"),
		strview::from_literal("Comma"),
		strview::from_literal("Arrow"),
		strview::from_literal("SquiggleBeg"),
		strview::from_literal("SquiggleEnd"),
		strview::from_literal("BracketBeg"),
		strview::from_literal("BracketEnd"),
		strview::from_literal("ParenBeg"),
		strview::from_literal("ParenEnd"),
		strview::from_literal("Hashtag"),
		strview::from_literal("Comment"),
		strview::from_literal("IncompleteComment"),
		strview::from_literal("If"),
		strview::from_literal("Else"),
		strview::from_literal("For"),
		strview::from_literal("Do"),
		strview::from_literal("Until"),
		strview::from_literal("When"),
		strview::from_literal("Switch"),
		strview::from_literal("Case"),
		strview::from_literal("Go"),
		strview::from_literal("To"),
		strview::from_literal("Yield"),
		strview::from_literal("Return"),
		strview::from_literal("DoubleColon"),
		strview::from_literal("Proc"),
		strview::from_literal("Struct"),
		strview::from_literal("Union"),
		strview::from_literal("Enum"),
		strview::from_literal("Bitset"),
		strview::from_literal("Alias"),
		strview::from_literal("Trait"),
		strview::from_literal("Impl"),
		strview::from_literal("Annotation"),
		strview::from_literal("Module"),
		strview::from_literal("Mut"),
		strview::from_literal("Const"),
		strview::from_literal("Pub"),
	};

	if (static_cast<u32>(type) >= _countof(types))
		return types[0];

	return types[static_cast<u32>(type)];
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

	if (fst == '?' || fst == '_' || (fst >= 'a' && fst <= 'z') || (fst >= 'A' && fst <= 'Z'))
	{
		++c;

		while (c != end && is_name_token_char(*c))
			++c;

		if (streqc({ beg, c }, strview::from_literal("for")))
			t.type = Token::Type::For;
		else if (streqc({ beg, c }, strview::from_literal("do")))
			t.type = Token::Type::Do;
		else if (streqc({ beg, c }, strview::from_literal("until")))
			t.type = Token::Type::Until;
		else if (streqc({ beg, c }, strview::from_literal("if")))
			t.type = Token::Type::If;
		else if (streqc({ beg, c }, strview::from_literal("else")))
			t.type = Token::Type::Else;
		else if (streqc({ beg, c }, strview::from_literal("switch")))
			t.type = Token::Type::Switch;
		else if (streqc({ beg, c }, strview::from_literal("go")))
			t.type = Token::Type::Go;
		else if (streqc({ beg, c }, strview::from_literal("to")))
			t.type = Token::Type::To;
		else if (streqc({ beg, c }, strview::from_literal("yield")))
			t.type = Token::Type::Yield;
		else if (streqc({ beg, c }, strview::from_literal("return")))
			t.type = Token::Type::Return;
		else if (streqc({ beg, c }, strview::from_literal("case")))
			t.type = Token::Type::Case;
		else if (streqc({ beg, c }, strview::from_literal("proc")))
			t.type = Token::Type::Proc;
		else if (streqc({ beg, c }, strview::from_literal("struct")))
			t.type = Token::Type::Struct;
		else if (streqc({ beg, c }, strview::from_literal("union")))
			t.type = Token::Type::Union;
		else if (streqc({ beg, c }, strview::from_literal("enum")))
			t.type = Token::Type::Enum;
		else if (streqc({ beg, c }, strview::from_literal("bitset")))
			t.type = Token::Type::Bitset;
		else if (streqc({ beg, c }, strview::from_literal("alias")))
			t.type = Token::Type::Alias;
		else if (streqc({ beg, c }, strview::from_literal("trait")))
			t.type = Token::Type::Trait;
		else if (streqc({ beg, c }, strview::from_literal("impl")))
			t.type = Token::Type::Impl;
		else if (streqc({ beg, c }, strview::from_literal("annotation")))
			t.type = Token::Type::Annotation;
		else if (streqc({ beg, c }, strview::from_literal("module")))
			t.type = Token::Type::Module;
		else if (streqc({ beg, c }, strview::from_literal("mut")))
			t.type = Token::Type::Mut;
		else if (streqc({ beg, c }, strview::from_literal("const")))
			t.type = Token::Type::Const;
		else if (streqc({ beg, c }, strview::from_literal("pub")))
			t.type = Token::Type::Pub;
		else if (streqc({ beg, c }, strview::from_literal("when")))
			t.type = Token::Type::When;
		else
			t.type = Token::Type::Ident;
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
			t.type = Token::Type::LitBadNumber;
		else if (is_float)
			t.type = Token::Type::LitFloat;
		else
			t.type = Token::Type::LitInt;
	}
	else if (fst == '\'')
	{
		++c;

		t.type = Token::Type::LitChar;

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

		t.type = Token::Type::LitString;

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
			t.type = Token::Type::BracketBeg;
			
			break;
		}

		case ']': {
			t.type = Token::Type::BracketEnd;
			
			break;
		}
		
		case '{': {
			t.type = Token::Type::SquiggleBeg;
			
			break;
		}
		
		case '}': {
			t.type = Token::Type::SquiggleEnd;
			
			break;
		}

		case '(': {
			t.type = Token::Type::ParenBeg;
			
			break;
		}

		case ')': {
			t.type = Token::Type::ParenEnd;
			
			break;
		}

		case ':': {
			if (nxt == ':')
			{
				++c;

				t.type = Token::Type::DoubleColon;
			}
			else
			{
				t.type = Token::Type::Colon;
			}
			
			break;
		}

		case ';': {
			t.type = Token::Type::Semicolon;
			
			break;
		}

		case ',': {
			t.type = Token::Type::Comma;
			
			break;
		}

		case '#': {
			t.type = Token::Type::Hashtag;
			
			break;
		}
		
		case '.': {
			if (c + 2 < end && c[1] == '.' && c[2] == '.')
			{
				t.type = Token::Type::TripleDot;

				c += 2;
			}
			else
			{
				t.type = Token::Type::Dot;
			}

			break;
		}

		case '<': {
			if (nxt == '<')
			{
				if (beg + 2 < end && beg[2] == '=')
				{
					c += 2;

					t.type = Token::Type::SetBitShl;
				}
				else
				{
					++c;

					t.type = Token::Type::OpBitShl;
				}
			}
			else if (nxt == '=')
			{
				++c;

				t.type = Token::Type::OpLe;
			}
			else
			{
				t.type = Token::Type::OpLt;
			}

			break;
		}

		case '>': {
			if (nxt == '>')
			{
				if (beg + 2 < end && beg[2] == '=')
				{
					c += 2;

					t.type = Token::Type::SetBitShr;
				}
				else
				{
					++c;

					t.type = Token::Type::OpBitShr;
				}
			}
			else if (nxt == '=')
			{
				++c;

				t.type = Token::Type::OpGe;
			}
			else
			{
				t.type = Token::Type::OpGt;
			}

			break;
		}

		case '=': {
			if (nxt == '=')
			{
				++c;

				t.type = Token::Type::OpEq;
			}
			else
			{
				t.type = Token::Type::Set;
			}

			break;
		}

		case '+': {
			if (nxt == '=')
			{
				++c;

				t.type = Token::Type::SetAdd;
			}
			else
			{
				t.type = Token::Type::OpAdd;
			}

			break;
		}
		
		case '-': {
			if (nxt == '>')
			{
				++c;

				t.type = Token::Type::Arrow;
			}
			else if (nxt == '=')
			{
				++c;

				t.type = Token::Type::SetSub;
			}
			else
			{
				t.type = Token::Type::OpSub;
			}

			break;
		}

		case '*': {
			if (nxt == '=')
			{
				++c;

				t.type = Token::Type::SetMul;
			}
			else
			{
				t.type = Token::Type::OpMul;
			}

			break;
		}

		case '/': {
			if (nxt == '=')
			{
				++c;

				t.type = Token::Type::SetDiv;
			}
			else if (nxt == '/')
			{
				t.type = Token::Type::Comment;

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
					t.type = Token::Type::Comment;
				else
					t.type = Token::Type::IncompleteComment;
			}
			else
			{
				t.type = Token::Type::OpDiv;
			}

			break;
		}

		case '%': {
			if (nxt == '=')
			{
				++c;

				t.type = Token::Type::SetMod;
			}
			else
			{
				t.type = Token::Type::OpMod;
			}

			break;
		}

		case '&': {
			if (nxt == '&')
			{
				++c;

				t.type = Token::Type::OpLogAnd;
			}
			else if (nxt == '=')
			{
				++c;

				t.type = Token::Type::SetBitAnd;
			}
			else
			{
				t.type = Token::Type::OpBitAnd_Ref;
			}

			break;
		}

		case '|': {
			if (nxt == '|')
			{
				++c;

				t.type = Token::Type::OpLogOr;
			}
			else if (nxt == '=')
			{
				++c;

				t.type = Token::Type::SetBitOr;
			}
			else
			{
				t.type = Token::Type::OpBitOr;
			}

			break;
		}

		case '^': {
			if (nxt == '=')
			{
				++c;

				t.type = Token::Type::SetBitXor;
			}
			else
			{
				t.type = Token::Type::OpBitXor;
			}

			break;
		}

		case '~': {
			t.type = Token::Type::UOpBitNot;

			break;
		}

		case '!': {
			if (nxt == '=')
			{
				++c;

				t.type = Token::Type::OpNe;
			}
			else
			{
				t.type = Token::Type::UOpLogNot;
			}

			break;
		}
		
		default: {
			t.type = Token::Type::INVALID;

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

		if (!include_comments && tokens.last().type == Token::Type::Comment)
			tokens.pop();
	}

	return std::move(tokens);
}

#include "ast_gen.hpp"

#include <cassert>
#include <cstdlib>

struct pstate
{
	const Token* beg;

	const Token* end;

	const Token* curr;

	Result rst;
};



static bool error_out_of_memory(pstate& s, const char* ctx) noexcept
{
	s.rst.type = Result::Type::OutOfMemory;

	s.rst.error_ctx = ctx;

	s.rst.message = "Out of memory.";

	return false;
}

static bool error_invalid_syntax(pstate& s, const char* ctx, const Token* curr, const char* msg) noexcept
{
	s.rst.type = Result::Type::InvalidSyntax;

	s.rst.error_ctx = ctx;

	s.rst.message = msg;

	s.rst.problematic_token = curr;

	return false;
}

static bool error_unexpected_token(pstate& s, const char* ctx, const Token* curr, Token::Type expected) noexcept
{
	s.rst.type = Result::Type::UnexpectedToken;

	s.rst.error_ctx = ctx;

	s.rst.expected_token = expected;

	s.rst.problematic_token = curr;

	return false;
}

static bool error_not_implemented(pstate& s, const char* ctx) noexcept
{
	s.rst.type = Result::Type::NotImplemented;

	s.rst.error_ctx = ctx;

	s.rst.message = "Not yet implemented.";

	return false;
}

static bool error_unexpected_end(pstate& s, const char* ctx) noexcept
{
	s.rst.type = Result::Type::UnexpectedEndOfStream;

	s.rst.error_ctx = ctx;

	s.rst.message = "Unexpectedly ran out of input Tokens.";

	return false;
}



static const Token* peek(const pstate& s, usz offset = 0) noexcept
{
	if (s.curr + offset >= s.end)
		return nullptr;

	return s.curr + offset;
}

static const Token* next(pstate& s, const char* ctx) noexcept
{
	if (s.curr >= s.end)
	{
		error_unexpected_end(s, ctx);

		return nullptr;
	}

	return s.curr++;
}

static const Token* expect(pstate& s, Token::Type expected, const char* ctx) noexcept
{
	const Token* t = next(s, ctx);

	if (t->type != expected)
	{
		error_unexpected_token(s, ctx, t, expected);

		return nullptr;
	}

	return t;
}



template<typename T>
static bool alloc(T** out) noexcept
{
	if ((*out = static_cast<T*>(malloc(sizeof(T)))) != nullptr)
	{
		memset(*out, 0, sizeof(T));

		return true;
	}

	return false;
}



struct SYOp
{
	u8 precedence;

	u8 opcnt;

	enum class Assoc : bool
	{
		Left = true,
		Right = false,
	} assoc;
	
	union
	{
		UnaryOp::Op uop;

		BinaryOp::Op bop;
	};

	constexpr SYOp() noexcept : precedence{ 0ui8 }, opcnt{ 0ui8 }, assoc{ Assoc::Right }, uop{ UnaryOp::Op::EMPTY } {}

	constexpr SYOp(u8 precedence, Assoc assoc, UnaryOp::Op op) noexcept : precedence{ precedence }, opcnt{ 1ui8 }, assoc{ assoc }, uop{ op } {}

	constexpr SYOp(u8 precedence, Assoc assoc, BinaryOp::Op op) noexcept : precedence{ precedence }, opcnt{ 2ui8 }, assoc{ assoc }, bop{ op } {}
};

static const SYOp* from_token(const Token* t, bool is_unary) noexcept
{
	static constexpr const SYOp bin_ops[]
	{
		{  3, SYOp::Assoc::Right, BinaryOp::Op::Mul    },
		{  3, SYOp::Assoc::Right, BinaryOp::Op::Div    },
		{  3, SYOp::Assoc::Right, BinaryOp::Op::Mod    },
		{  4, SYOp::Assoc::Left,  BinaryOp::Op::Add    },
		{  4, SYOp::Assoc::Left,  BinaryOp::Op::Sub    },
		{  5, SYOp::Assoc::Left,  BinaryOp::Op::ShiftL },
		{  5, SYOp::Assoc::Left,  BinaryOp::Op::ShiftR },
		{  6, SYOp::Assoc::Left,  BinaryOp::Op::CmpLt  },
		{  6, SYOp::Assoc::Left,  BinaryOp::Op::CmpLe  },
		{  6, SYOp::Assoc::Left,  BinaryOp::Op::CmpGt  },
		{  6, SYOp::Assoc::Left,  BinaryOp::Op::CmpGe  },
		{  7, SYOp::Assoc::Left,  BinaryOp::Op::CmpEq  },
		{  7, SYOp::Assoc::Left,  BinaryOp::Op::CmpNe  },
		{  8, SYOp::Assoc::Left,  BinaryOp::Op::BitAnd },
		{  9, SYOp::Assoc::Left,  BinaryOp::Op::BitXor },
		{ 10, SYOp::Assoc::Left,  BinaryOp::Op::BitOr  },
		{ 11, SYOp::Assoc::Left,  BinaryOp::Op::LogAnd },
		{ 11, SYOp::Assoc::Left,  BinaryOp::Op::LogOr  },
	};

	static constexpr const SYOp un_ops[]
	{
		{  2, SYOp::Assoc::Left,  UnaryOp::Op::LogNot  },
		{  2, SYOp::Assoc::Left,  UnaryOp::Op::BitNot  },
	};

	static constexpr const SYOp un_minus{  2, SYOp::Assoc::Left,  UnaryOp::Op::Neg };

	if (!is_unary)
	{
		if (const isz idx = static_cast<isz>(t->type) - static_cast<isz>(Token::Type::OpMul); idx < _countof(bin_ops) && idx >= 0)
			return &bin_ops[idx];
		else
			return nullptr;
	}
	else if (t->type == Token::Type::OpSub)
	{
		return &un_minus;
	}
	else
	{
		if (const isz idx = static_cast<isz>(t->type) - static_cast<isz>(Token::Type::UOpLogNot); idx < _countof(un_ops) && idx >= 0)
			return &un_ops[static_cast<i32>(t->type) - static_cast<i32>(Token::Type::UOpLogNot)];
		else
			return nullptr;
	}
}

static bool expr_pop_syop(pstate& s, const Token* t, vec<SYOp, 32>& op_stack, vec<Expr, 32>& subexprs) noexcept
{
	static constexpr const char ctx[] = "Expr@pop_syop";

	SYOp popped = op_stack.last();

	op_stack.pop();

	Expr expr{};

	if (popped.opcnt > subexprs.size())
		return error_invalid_syntax(s, ctx, t, "Not enough subexpressions for binary operator");

	if (popped.opcnt == 1)
	{
		expr.type = Expr::Type::UnaryOp;

		if (!alloc(&expr.unary_op))
			return error_out_of_memory(s, ctx);

		expr.unary_op->op = popped.uop;

		expr.unary_op->operand = std::move(subexprs.last());

		subexprs.last() = std::move(expr);
	}
	else if (popped.opcnt == 2)
	{
		expr.type = Expr::Type::BinaryOp;

		if (!alloc(&expr.binary_op))
			return error_out_of_memory(s, ctx);

		expr.binary_op->op = popped.bop;

		expr.binary_op->rhs = std::move(subexprs.last());

		subexprs.pop();

		expr.binary_op->lhs = std::move(subexprs.last());

		subexprs.last() = std::move(expr);
	}
	else
		assert(false);

	return true;
}

static bool token_type_to_assign_oper(const Token::Type t, Assignment::Op& out_assign_op) noexcept
{
	const i32 idx = static_cast<i32>(t) - static_cast<i32>(Token::Type::Set);

	if (idx < 0 || idx >= static_cast<i32>(Token::Type::SetBitShr) - static_cast<i32>(Token::Type::Set))
		return false;

	out_assign_op = static_cast<Assignment::Op>(idx + 1);

	return true;
}

static bool hex_val(char c, usz& inout_v) noexcept
{
	inout_v <<= 4;

	if (c >= '0' && c <= '9')
		inout_v += c - '0';
	else if (c >= 'A' && c <= 'F')
		inout_v += 10 + c - 'A';
	else if (c >= 'a' && c <= 'f')
		inout_v += 10 + c -  'a';
	else
		return false;

	return true;
}

static bool dec_val(char c, usz& inout_v) noexcept
{
	inout_v *= 10;

	if (c >= '0' && c <= '9')
		inout_v += c - '0';
	else
		return false;

	return true;
}

static bool oct_val(char c, usz& inout_v) noexcept
{
	inout_v <<= 3;

	if (c >= '0' && c <= '7')
		inout_v += c - '0';
	else
		return false;

	return true;
}

static bool bin_val(char c, usz& inout_v) noexcept
{
	inout_v <<= 1;

	inout_v |= c == '1';
	
	return c == '0' || c == '1';
}


/*
enum class PeekedType
{
	Other,
	VariableDef,
	Assignment,
	ForEach,
};

static PeekedType peek_type(const pstate& s) noexcept
{
	usz idx = 0;

	while (true)
	{
		Assignment::Op unused_assign_op;

		if (const Token* t = peek(s, idx); t == nullptr || t->type != Token::Type::Ident)
			return PeekedType::Other;

		if (const Token* t = peek(s, idx + 1); t == nullptr)
			return PeekedType::Other;
		else if (t->type == Token::Type::Colon)
			return PeekedType::VariableDef;
		else if (token_type_to_assign_oper(t->type, unused_assign_op))
			return PeekedType::Assignment;
		else if (t->type == Token::Type::ArrowLeft)
			return PeekedType::ForEach;
		else if (t->type != Token::Type::Comma)
			return PeekedType::Other;

		idx += 2;
	}
}
*/



static bool parse_type_ref(pstate& s, TypeRef& out) noexcept;

static bool parse_definition(pstate& s, Definition& out, bool is_anonymous) noexcept;

static bool parse_name_ref(pstate& s, NameRef& out) noexcept;

static bool parse_block(pstate& s, Block& out) noexcept;

static bool parse_variable_def(pstate& s, VariableDef& out) noexcept;

static bool parse_expr(pstate& s, Expr& out) noexcept;

static bool parse_top_level_expr(pstate& s, TopLevelExpr& out) noexcept;

static bool parse_statement(pstate& s, Statement& out) noexcept;

static bool parse_call(pstate& s, Call& out, NameRef* proc_name = nullptr) noexcept;



static bool parse_float_literal(pstate& s, FloatLiteral& out) noexcept
{
	static constexpr const char ctx[] = "FloatLiteral";

	const Token* t = expect(s, Token::Type::LitFloat, ctx);

	if (t == nullptr)
		return false;

	out.value = atof(t->data_strview().begin());

	return true;
}

static bool parse_integer_literal(pstate& s, IntegerLiteral& out) noexcept
{
	static constexpr const char ctx[] = "IntegerLiteral";

	const Token* t = expect(s, Token::Type::LitInt, ctx);

	if (t == nullptr)
		return false;

	const strview str = t->data_strview();

	usz value = 0;

	if (str.len() >= 3 && str[0] == '0')
	{
		const char base = str[1];
		
		if (base == 'x' || base == 'X')
		{
			for (const char* c = str.begin() + 2; c != str.end(); ++c)
				if (!hex_val(*c, value))
					return error_invalid_syntax(s, ctx, t, "Not a valid hexadecimal string");

			out.value = value;

			return true;
		}
		else if (base == 'o' || base == 'O')
		{
			for (const char* c = str.begin() + 2; c != str.end(); ++c)
				if (!oct_val(*c, value))
					return error_invalid_syntax(s, ctx, t, "Not a valid octal string");

			out.value = value;

			return true;
		}
		else if (base == 'b' || base == 'B')
		{
			for (const char* c = str.begin() + 2; c != str.end(); ++c)
				if (!bin_val(*c, value))
					return error_invalid_syntax(s, ctx, t, "Not a valid octal string");

			out.value = value;

			return true;
		}
	}

	if (str.len() >= 2)
	{
		const char base = str[1];

		if (base == 'x' || base == 'X' || base == 'o' || base == 'O' || base == 'b' || base == 'B')
			return error_invalid_syntax(s, ctx, t, "Cannot have empty integer literal");
	}

	for (const char* c = str.begin(); c != str.end(); ++c)
		if (!dec_val(*c, value))
			return error_invalid_syntax(s, ctx, t, "Not a valid decimal string");

	out.value = value;

	return true;
}

static bool parse_char_literal(pstate& s, CharLiteral& out) noexcept
{
	static constexpr const char ctx[] = "CharLiteral";

	const Token* t = expect(s, Token::Type::LitChar, ctx);

	if (t == nullptr)
		return false;

	const strview str = t->data_strview();

	if (str.len() == 0)
		return error_invalid_syntax(s, ctx, t, "Empty character literal");

	const unsigned char cp0 = static_cast<unsigned char>(str[0]);

	if (cp0 <= 0x7F)
	{
		usz codepoint = 0;

		if (str[0] == '\\')
		{
			if (str.len() == 1)
				return error_invalid_syntax(s, ctx, t, "Empty escape sequence");

			const char escapee = str[1];

			if (escapee == 'x')
			{
				if (str.len() <= 2)
					return error_invalid_syntax(s, ctx, t, "Empty hexadecimal character escape sequence");

				for (const char* c = str.begin() + 2; c != str.end(); ++c)
					if (!hex_val(*c, codepoint))
						return error_invalid_syntax(s, ctx, t, "Non-hexadecimal character in hexadecimal character escape sequence");
			}
			else if (escapee >= '0' && escapee <= '9')
			{
				for (const char* c = str.begin() + 2; c != str.end(); ++c)
					if (!dec_val(*c, codepoint))
						return error_invalid_syntax(s, ctx, t, "Non-decimal character in decimal character escape sequence");
			}
			else
			{
				switch(escapee)
				{
				case  'a': out.value[0] = '\a'; break;
				case  'b': out.value[0] = '\b'; break;
				case  'f': out.value[0] = '\f'; break;
				case  'n': out.value[0] = '\n'; break;
				case  'r': out.value[0] = '\r'; break;
				case  't': out.value[0] = '\t'; break;
				case  'v': out.value[0] = '\v'; break;
				case '\\': out.value[0] = '\\'; break;
				case '\'': out.value[0] = '\''; break;
				default: return error_invalid_syntax(s, ctx, t, "Unknown character escape sequence");
				}

				return true;
			}

			if (codepoint <= 0x7F)
			{
				out.value[0] = static_cast<char>(codepoint);
			}
			else if (codepoint <= 0x7FF)
			{
				out.value[0] = static_cast<char>(0xC0 | (codepoint >> 6));

				out.value[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
			}
			else if (codepoint <= 0xFFFF)
			{
				out.value[0] = static_cast<char>(0xE0 | (codepoint >> 12));

				out.value[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));

				out.value[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
			}
			else if (codepoint <= 0x10FFFF)
			{
				out.value[0] = static_cast<char>(0xF0 | (codepoint >> 18));

				out.value[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));

				out.value[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));

				out.value[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
			}
			else
			{
				return error_invalid_syntax(s, ctx, t, "Value of decimal character escape sequence exceeds maximal unicode code point (0x10FFFF)");
			}

			return true;
		}
		else if (str.len() != 1)
		{
			return error_invalid_syntax(s, ctx, t, "Invalid length for single-byte code point");
		}
	}
	else if (cp0 <= 0xBF)
	{
		if (str.len() != 2)
			return error_invalid_syntax(s, ctx, t, "Invalid length for two-byte code point");
	}
	else if (cp0 <= 0xDF)
	{
		if (str.len() != 3)
			return error_invalid_syntax(s, ctx, t, "Invalid length for three-byte code point");
	}
	else if (cp0 <= 0xEF)
	{
		if (str.len() != 4)
			return error_invalid_syntax(s, ctx, t, "Invalid length for four-byte code point");
	}
	else
	{
		return error_invalid_syntax(s, ctx, t, "Unexpected code unit");
	}

	out.value[0] = static_cast<char>(cp0);

	for (usz i = 1; i != str.len(); ++i)
		if ((str[i] & 0xC0) != 0x80)
			return error_invalid_syntax(s, ctx, t, "Invalid surrogate codeunit");
		else
			out.value[i] = str[i];

	return true;
}

static bool parse_string_literal(pstate& s, StringLiteral& out) noexcept
{
	static constexpr const char ctx[] = "StringLiteral";

	const Token* t = expect(s, Token::Type::LitString, ctx);

	if (t == nullptr)
		return false;

	const strview str = t->data_strview();

	for (const char* c = str.begin(); c != str.end(); ++c)
	{
		if (*c == '\\')
		{
			char escaped;

			++c;

			if (c == str.end())
				return error_invalid_syntax(s, ctx, t, "Empty character escape sequence");

			switch (*c)
			{
			case  'a': escaped = '\a'; break;
			case  'b': escaped = '\b'; break;
			case  'f': escaped = '\f'; break;
			case  'n': escaped = '\n'; break;
			case  'r': escaped = '\r'; break;
			case  't': escaped = '\t'; break;
			case  'v': escaped = '\v'; break;
			case '\\': escaped = '\\'; break;
			case '\"': escaped = '"'; break;
			default: return error_invalid_syntax(s, ctx, t, "Invalid character escape sequence");
			}

			if (!out.value.push_back(escaped))
				return error_out_of_memory(s, ctx);
		}
		else if (!out.value.push_back(*c))
		{
			return error_out_of_memory(s, ctx);
		}
	}

	return true;
}

static bool parse_assignable_expr(pstate& s, AssignableExpr& out) noexcept
{
	static constexpr const char ctx[] = "AssignableExpr";

	NameRef name_ref{};

	if (!parse_name_ref(s, name_ref))
		return false;

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::ParenBeg)
	{
		out.type = AssignableExpr::Type::Call;

		return parse_call(s, out.call, &name_ref);
	}
	else
	{
		out.type = AssignableExpr::Type::NameRef;
		
		out.name_ref = std::move(name_ref);

		return true;
	}
}

static bool parse_call(pstate& s, Call& out, NameRef* proc_name) noexcept
{
	static constexpr const char ctx[] = "Call";

	if (proc_name != nullptr)
		out.proc_name_ref = std::move(*proc_name);
	else if (!parse_name_ref(s, out.proc_name_ref))
			return false;

	if (expect(s, Token::Type::ParenBeg, ctx) == nullptr)
		return false;

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::ParenEnd)
	{
		next(s, ctx);

		return true;
	}

	while (true)
	{
		if (!out.args.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse_expr(s, out.args.last()))
			return false;

		if (const Token* t = next(s, ctx); t == nullptr)
			return false;
		else if (t->type == Token::Type::ParenEnd)
			return true;
		else if (t->type != Token::Type::Comma)
			return error_invalid_syntax(s, ctx, t, "Expected ParenEnd or Comma");

	}
}

static bool parse_literal(pstate& s, Literal& out) noexcept
{
	static constexpr const char ctx[] = "Literal";

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->type == Token::Type::LitString)
	{
		out.type = Literal::Type::String;

		return parse_string_literal(s, out.string_literal);
	}
	else if (t->type == Token::Type::LitChar)
	{
		out.type = Literal::Type::Char;

		return parse_char_literal(s, out.char_literal);
	}
	else if (t->type == Token::Type::LitInt)
	{
		out.type = Literal::Type::Integer;

		return parse_integer_literal(s, out.integer_literal);
	}
	else if (t->type == Token::Type::LitFloat)
	{
		out.type = Literal::Type::Float;

		return parse_float_literal(s, out.float_literal);
	}
	else
	{
		return error_invalid_syntax(s, ctx, t, "Expected LitString, LitChar, LitInt or LitFloat");
	}
}

static bool parse_assignment(pstate& s, Assignment& out, AssignableExpr* assignee = nullptr) noexcept
{
	static constexpr const char ctx[] = "Assignment";

	if (assignee != nullptr)
	{
		out.assignee = std::move(*assignee);
	}
	else
	{
		if (!parse_assignable_expr(s, out.assignee))
			return false;
	}

	const Token* t = next(s, ctx);

	if (t == nullptr)
		return false;
	else if (!token_type_to_assign_oper(t->type, out.op))
		return error_invalid_syntax(s, ctx, t, "Expected assignment operator");
	
	return parse_top_level_expr(s, out.assigned_value);
}

static bool parse_case(pstate& s, Case& out) noexcept
{
	static constexpr const char ctx[] = "Case";

	if (expect(s, Token::Type::Case, ctx) == nullptr)
		return false;

	if (!parse_expr(s, out.label))
		return false;

	if (!parse_statement(s, out.body))
		return false;

	return true;
}

static bool parse_type_binding_constraint(pstate& s, TypeBindingConstraint& out) noexcept
{
	static constexpr const char ctx[] = "TypeBindingConstraint";

	return parse_name_ref(s, out.bound_trait);
}

static bool parse_type_binding(pstate& s, TypeBinding& out) noexcept
{
	static constexpr const char ctx[] = "TypeBinding";

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->type != Token::Type::ParenBeg)
	{
		if (!out.constraints.push_back({}))
			return error_out_of_memory(s, ctx);

		return parse_type_binding_constraint(s, out.constraints.last());
	}

	if (const  Token* t = peek(s); t != nullptr && t->type == Token::Type::ParenEnd)
	{
		next(s, ctx);

		return true;
	}

	while (true)
	{
		if (!out.constraints.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse_type_binding_constraint(s, out.constraints.last()))
			return false;

		if (const Token* t = next(s, ctx); t == nullptr)
			return false;
		else if (t->type == Token::Type::ParenEnd)
			return true;
		else if (t->type != Token::Type::Comma)
			return error_invalid_syntax(s, ctx, t, "Expected ParenEnd or Comma");
	}
}

static bool parse_value_binding(pstate& s, ValueBinding& out) noexcept
{
	static constexpr const char ctx[] = "ValueBinding";

	return parse_type_ref(s, out.type_ref);
}

static bool parse_yield(pstate& s, Yield& out) noexcept
{
	static constexpr const char ctx[] = "Yield";

	if (expect(s, Token::Type::Yield, ctx) == nullptr)
		return false;

	if (!parse_top_level_expr(s, out.yield_value))
		return false;

	return true;
}

static bool parse_return(pstate& s, Return& out) noexcept
{
	static constexpr const char ctx[] = "Return";

	if (expect(s, Token::Type::Return, ctx) == nullptr)
		return false;

	if (!parse_top_level_expr(s, out.return_value))
		return false;

	return true;
}

static bool parse_go(pstate& s, Go& out) noexcept
{
	static constexpr const char ctx[] = "Go";

	if (expect(s, Token::Type::Go, ctx) == nullptr)
		return false;

	return parse_expr(s, out.label);
}

static bool parse_switch(pstate& s, Switch& out) noexcept
{
	static constexpr const char ctx[] = "Switch";

	if (expect(s, Token::Type::Switch, ctx) == nullptr)
		return false;

	if (!parse_expr(s, out.switched))
		return false;

	while (true)
	{
		if (!out.cases.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse_case(s, out.cases.last()))
			return false;

		if (const Token* t = peek(s); t == nullptr || t->type != Token::Type::Case)
			return true;
	}
}

static bool parse_when(pstate& s, When& out) noexcept
{
	static constexpr const char ctx[] = "When";

	if (expect(s, Token::Type::When, ctx) == nullptr)
		return false;

	if (!parse_expr(s, out.condition))
		return false;

	if (!parse_statement(s, out.body))
		return false;

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::Else)
	{
		next(s, ctx);

		if (!parse_statement(s, out.opt_else_body))
			return false;
	}

	return true;
}

static bool parse_for_each(pstate& s, ForEach& out) noexcept
{
	static constexpr const char ctx[] = "ForEach";

	while (true)
	{
		if (const Token* t = expect(s, Token::Type::Ident, ctx); t == nullptr)
		{
			return false;
		}
		else
		{
			if (!out.idents.push_back({}))
				return error_out_of_memory(s, ctx);

			out.idents.last() = t->data_strview();
		}

		if (const Token* t = next(s, ctx); t == nullptr)
			return false;
		else if (t->type == Token::Type::ArrowLeft)
			break;
		else if (t->type != Token::Type::Comma)
			return error_invalid_syntax(s, ctx, t, "Expected ArrowLeft or Comma");
	}

	return parse_expr(s, out.iterated);
}

static bool parse_for_signature(pstate& s, ForSignature& out) noexcept
{
	static constexpr const char ctx[] = "ForSignature";

	if (expect(s, Token::Type::For, ctx) == nullptr)
		return false;

	if (const Token* t = peek(s, 1); t != nullptr && t->type == Token::Type::Comma)
	{
		if (const Token* t1 = peek(s, 3); t1 != nullptr && t1->type == Token::Type::ArrowRight)
		{
			out.type = ForSignature::Type::ForEach;

			if (!parse_for_each(s, out.for_each))
				return false;
				
			if (const Token* t2 = peek(s); t2 != nullptr && t2->type == Token::Type::Do)
				next(s, ctx);

			return true;
		}
	}
	else if (t->type == Token::Type::Colon)
	{
		if (!parse_variable_def(s, out.normal.opt_init))
			return false;
			
		if (expect(s, Token::Type::Semicolon, ctx) == nullptr)
			return false;
	}

	if (const Token* t = peek(s); t != nullptr && t->type != Token::Type::Do && t->type != Token::Type::SquiggleBeg)
	{
		if (!parse_expr(s, out.normal.opt_condition))
			return false;
	}

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::Semicolon)
	{
		next(s, ctx);

		if (!parse_assignment(s, out.normal.opt_step))
			return false;
	}

	if (const Token* t = peek(s); t == nullptr)
		return false;
	else if (t->type == Token::Type::Do)
		next(s, ctx);
	else if (t->type != Token::Type::SquiggleBeg)
		return error_invalid_syntax(s, ctx, t, "Expected Do or SquiggleBeg");

	return true;
}

static bool parse_for(pstate& s, For& out) noexcept
{
	static constexpr const char ctx[] = "For";

	if (!parse_for_signature(s, out.signature))
		return false;

	if (!parse_statement(s, out.body))
		return false;

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::Until)
	{
		next(s, ctx);

		if (!parse_statement(s, out.opt_until_body))
			return false;
	}

	return true;
}

static bool parse_if(pstate& s, If& out) noexcept
{
	static constexpr const char ctx[] = "If";

	if (expect(s, Token::Type::If, ctx) == nullptr)
		return false;

	if (const Token* t = peek(s, 1); t != nullptr && t->type == Token::Type::Colon)
	{
		if (!parse_variable_def(s, out.opt_init))
			return false;
	}

	if (!parse_expr(s, out.condition))
		return false;

	if (!parse_statement(s, out.body))
		return false;

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::Else)
	{
		next(s, ctx);

		if (!parse_statement(s, out.opt_else_body))
			return false;
	}

	return true;
}

static bool parse_top_level_expr(pstate& s, TopLevelExpr& out) noexcept
{
	static constexpr const char ctx[] = "TopLevelExpr";

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->type == Token::Type::If)
	{
		out.type = TopLevelExpr::Type::If;

		if (!alloc(&out.if_block))
			return error_out_of_memory(s, ctx);

		return parse_if(s, *out.if_block);
	}
	else if (t->type == Token::Type::For)
	{
		out.type = TopLevelExpr::Type::For;

		if (!alloc(&out.for_block))
			return error_out_of_memory(s, ctx);

		return parse_for(s, *out.for_block);
	}
	else if (t->type == Token::Type::SquiggleBeg)
	{
		out.type = TopLevelExpr::Type::Block;

		if (!alloc(&out.block))
			return error_out_of_memory(s, ctx);

		return parse_block(s, *out.block);
	}
	else if (t->type == Token::Type::Switch)
	{
		out.type = TopLevelExpr::Type::Switch;

		if (!alloc(&out.switch_block))
			return error_out_of_memory(s, ctx);

		return parse_switch(s, *out.switch_block);
	}
	else if (t->type == Token::Type::When)
	{
		out.type = TopLevelExpr::Type::When;

		if (!alloc(&out.when_block))
			return error_out_of_memory(s, ctx);

		return parse_when(s, *out.when_block);
	}
	else
	{
		out.type = TopLevelExpr::Type::Expr;

		if (!alloc(&out.expr))
			return error_out_of_memory(s, ctx);

		return parse_expr(s, *out.expr);
	}
}

static bool parse_type_name(pstate& s, TypeName& out) noexcept
{
	static constexpr const char ctx[] = "TypeName";

	const Token* t = expect(s, Token::Type::Ident, ctx); 
	
	if (t == nullptr)
		return false;

	out.name = t->data_strview();
	
	if (const Token* t1 = peek(s); t != nullptr && t1->type == Token::Type::BracketBeg)
	{
		next(s, ctx);

		if (const Token* t2 = peek(s); t2 != nullptr && t2->type == Token::Type::BracketEnd)
			return true;

		while (true)
		{
			if (!out.bounds.push_back({}))
				return error_out_of_memory(s, ctx);

			if (!parse_expr(s, out.bounds.last()))
				return false;

			if (const Token* t2 = next(s, ctx); t2 == nullptr)
				return false;
			else if (t2->type == Token::Type::BracketEnd)
				break;
			else if (t2->type != Token::Type::Comma)
				return error_invalid_syntax(s, ctx, t, "Expected BracketEnd or Dot");
		}
	}
	
	return true;
}

static bool parse_name_ref(pstate& s, NameRef& out) noexcept
{
	static constexpr const char ctx[] = "Ident";

	while (true)
	{
		if (!out.parts.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse_type_name(s, out.parts.last()))
			return false;

		if (const Token* t = peek(s); t == nullptr || t->type != Token::Type::Dot)
			return true;

		next(s, ctx);
	}
}

static bool parse_binding(pstate& s, Binding& out) noexcept
{
	static constexpr const char ctx[] = "Binding";

	const Token* t = expect(s, Token::Type::Ident, ctx);

	if (t == nullptr)
		return false;

		assert(t->data_strview().len() != 0);

	if (t->data_strview()[0] == '?')
	{
		out.type = Binding::Type::TypeBinding;

		out.ident = t->data_strview();

		if (const Token* t1 = peek(s); t1 == nullptr || t1->type != Token::Type::Colon)
			return true;

		next(s, ctx);

		return parse_type_binding(s, out.type_binding);
	}
	else
	{
		out.type = Binding::Type::ValueBinding;

		out.ident = t->data_strview();

		if (expect(s, Token::Type::Colon, ctx) == nullptr)
			return false;

		return parse_value_binding(s, out.value_binding);
	}
}

static bool parse_expr(pstate& s, Expr& out) noexcept
{
	static constexpr const char ctx[] = "Expr";

	vec<SYOp, 32> op_stack;

	vec<Expr, 32> subexprs;

	bool expecting_operator = false;

	u32 paren_nesting = 0;

	while (true)
	{
		if (const Token* t = peek(s); t == nullptr)
		{
			if (!expecting_operator)
				return error_unexpected_end(s, ctx);

			// Currently this should also be error_unexpected_end,
			// but if there can ever be top-level VariableDefs,
			// this would be ok, so just break. Any error will be
			// handled by the caller anyways if further Tokens are
			// expected.
			break;
		}
		else if (t->type == Token::Type::LitString || t->type == Token::Type::LitChar || t->type == Token::Type::LitInt || t->type == Token::Type::LitFloat)
		{
			if (expecting_operator)
				break;

			if (!subexprs.push_back({}))
				return error_out_of_memory(s, ctx);

			Expr& expr = subexprs.last();
			
			expr.type = Expr::Type::Literal;

			if (!alloc(&expr.literal))
				return error_out_of_memory(s, ctx);

			if (!parse_literal(s, *expr.literal))
				return false;

			expecting_operator = true;
		}
		else if (t->type == Token::Type::Ident)
		{
			if (expecting_operator)
				break;

			if (!subexprs.push_back({}))
				return error_out_of_memory(s, ctx);

			Expr& expr = subexprs.last();
			
			NameRef name_ref{};

			if (!parse_name_ref(s, name_ref))
				return false;

			if (const Token* t1 = peek(s); t1 != nullptr && t1->type == Token::Type::ParenBeg)
			{
				expr.type = Expr::Type::Call;

				if (!alloc(&expr.call))
					return error_out_of_memory(s, ctx);

				if (!parse_call(s, *expr.call, &name_ref))
					return false;
			}
			else
			{
				expr.type = Expr::Type::NameRef;

				if (!alloc(&expr.name_ref))
					return error_out_of_memory(s, ctx);

				*expr.name_ref = std::move(name_ref);
			}
			
			expecting_operator = true;
		}
		else if (t->type == Token::Type::ParenBeg)
		{
			if (expecting_operator)
				break;

			next(s, ctx);

			++paren_nesting;

			if (!op_stack.push_back({ 255, SYOp::Assoc::Left, UnaryOp::Op::EMPTY }))
				return error_out_of_memory(s, ctx);
		}
		else if (t->type == Token::Type::ParenEnd)
		{
			if (!expecting_operator || paren_nesting == 0)
				break;

			next(s, ctx);

			--paren_nesting;

			assert(op_stack.size() != 0);

			while (op_stack.last().precedence != 255)
			{
				if (!expr_pop_syop(s, t, op_stack, subexprs))
					return false;

				assert(op_stack.size() != 0);
			}

			op_stack.pop();
		}
		else
		{
			const SYOp* oper = from_token(t, !expecting_operator);

			if (oper == nullptr)
			{
				if (expecting_operator)
					break;
				else
					return error_invalid_syntax(s, ctx, t, "Expected a unary operator");
			}

			next(s, ctx);

			while (op_stack.size() != 0)
			{
				const SYOp& last = op_stack.last();

				if (last.precedence >= oper->precedence && !(last.precedence == oper->precedence && oper->assoc == SYOp::Assoc::Left))
					break;

				if (!expr_pop_syop(s, t, op_stack, subexprs))
					return false;
			}

			if (!op_stack.push_back(*oper))
				return error_out_of_memory(s, ctx);
			
			expecting_operator = false;
		}
	}

	if (paren_nesting != 0)
		return error_invalid_syntax(s, ctx, nullptr, "Unmatched ParenBeg");

	while (op_stack.size() != 0)
		if (!expr_pop_syop(s, nullptr, op_stack, subexprs))
			return false;

	if (subexprs.size() != 1)
		return error_invalid_syntax(s, ctx, nullptr, "Too many subexpressions");

	out = std::move(subexprs.first());

	return true;
} 

static bool parse_to(pstate& s, To& out) noexcept
{
	static constexpr const char ctx[] = "To";

	if (expect(s, Token::Type::To, ctx) == nullptr)
		return false;

	while (true)
	{
		if (!out.cases.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse_case(s, out.cases.last()))
			return false;

		if (const Token* t = peek(s); t == nullptr || t->type != Token::Type::Case)
			return true;
	}
}

static bool parse_statement(pstate& s, Statement& out) noexcept
{
	static constexpr const char ctx[] = "Statement";

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->type == Token::Type::SquiggleBeg)
	{
		out.type = Statement::Type::Block;

		if (!alloc(&out.block))
			return error_out_of_memory(s, ctx);

		return parse_block(s, *out.block);
	}
	else if (t->type == Token::Type::If)
	{
		out.type = Statement::Type::If;

		if (!alloc(&out.if_block))
			return error_out_of_memory(s, ctx);

		return parse_if(s, *out.if_block);
	}
	else if (t->type == Token::Type::For)
	{
		out.type = Statement::Type::For;

		if (!alloc(&out.for_block))
			return error_out_of_memory(s, ctx);

		return parse_for(s, *out.for_block);
	}
	else if (t->type == Token::Type::When)
	{
		out.type = Statement::Type::When;

		if (!alloc(&out.when_block))
			return error_out_of_memory(s, ctx);

		return parse_when(s, *out.when_block);
	}
	else if (t->type == Token::Type::Switch)
	{
		out.type = Statement::Type::Switch;

		if (!alloc(&out.switch_block))
			return error_out_of_memory(s, ctx);

		return parse_switch(s, *out.switch_block);
	}
	else if (t->type == Token::Type::Go)
	{
		out.type = Statement::Type::Go;

		if (!alloc(&out.go_stmt))
			return error_out_of_memory(s, ctx);

		return parse_go(s, *out.go_stmt);
	}
	else if (t->type == Token::Type::Return)
	{
		out.type = Statement::Type::Return;

		if (!alloc(&out.return_stmt))
			return error_out_of_memory(s, ctx);

		return parse_return(s, *out.return_stmt);
	}
	else if (t->type == Token::Type::Yield)
	{
		out.type = Statement::Type::Yield;

		if (!alloc(&out.yield_stmt))
			return error_out_of_memory(s, ctx);

		return parse_yield(s, *out.yield_stmt);
	}
	else
	{
		bool is_variable_def = false;

		usz variable_def_idx = 1;

		while (true)
		{
			if (const Token* t1 = peek(s, variable_def_idx); t1 == nullptr || t1->type != Token::Type::Comma)
			{
				if (t1->type == Token::Type::Colon)
					is_variable_def = true;

				break;
			}

			if (const Token* t1 = peek(s, variable_def_idx + 1); t1 == nullptr || t1->type != Token::Type::Ident)
				break;

			variable_def_idx += 2;
		}

		if (is_variable_def)
		{
			out.type = Statement::Type::VariableDef;

			if (!alloc(&out.variable_def))
				return error_out_of_memory(s, ctx);

			return parse_variable_def(s, *out.variable_def);
		}

		AssignableExpr assignable_expr;

		if (!parse_assignable_expr(s, assignable_expr))
			return false;

		Assignment::Op assign_op;
		
		if (const Token* t2 = peek(s); t2 != nullptr && (t2->type == Token::Type::Comma || t2->type == Token::Type::Colon || token_type_to_assign_oper(t2->type, assign_op)))
		{
			out.type = Statement::Type::Assignment;

			if (!alloc(&out.assignment))
				return false;

			if (!parse_assignment(s, *out.assignment, &assignable_expr))
				return false;
		}
		else if (assignable_expr.type == AssignableExpr::Type::Call)
		{
			out.type = Statement::Type::Call;

			if (!alloc(&out.call))
				return error_out_of_memory(s, ctx);

			*out.call = std::move(assignable_expr.call);
		}
		else
		{
			return error_invalid_syntax(s, ctx, t2, "Expected Statement of Type Call or Assignment");
		}
	}

	return true;
}

static bool parse_variable_def(pstate& s, VariableDef& out) noexcept
{
	static constexpr const char ctx[] = "VariableDef";

	if (const Token* t = expect(s, Token::Type::Ident, ctx); t == nullptr)
	{
		return false;
	}
	else
	{
		out.ident = t->data_strview();
	}

	if (expect(s, Token::Type::Colon, ctx) == nullptr)
		return false;

	if (const Token* t = peek(s); t != nullptr && t->type != Token::Type::Set)
	{
		if (!parse_type_ref(s, out.opt_type_ref))
			return false;
	}

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::Set)
	{
		next(s, ctx);

		return parse_top_level_expr(s, out.opt_initializer);
	}
	
	return true;
}

static bool parse_type_ref(pstate& s, TypeRef& out) noexcept
{
	static constexpr const char ctx[] = "TypeRef";

	const Token* t = peek(s);

	if (t == nullptr)
		return false;

	while (true)
	{
		if (t->type == Token::Type::Const)
		{
			if (out.mutability != TypeRef::Mutability::Immutable)
				return error_invalid_syntax(s, ctx, t, "More than one mutability specifier used");

			out.mutability = TypeRef::Mutability::Const;
		}
		else if (t->type == Token::Type::Mut)
		{
			if (out.mutability != TypeRef::Mutability::Immutable)
				return error_invalid_syntax(s, ctx, t, "More than one mutability specifier used");

			out.mutability = TypeRef::Mutability::Mutable;
		}
		else
		{
			break;
		}

		next(s, ctx);
		
		t = peek(s);

		if (t == nullptr)
			return error_unexpected_end(s, ctx);
	}

	if (t->type == Token::Type::OpBitAnd_Ref)
	{
		next(s, ctx);

		out.type = TypeRef::Type::Ref;

		if (!alloc(&out.ref))
			return error_out_of_memory(s, ctx);

		return parse_type_ref(s, *out.ref);
	}
	else if (t->type == Token::Type::Struct || t->type == Token::Type::Union || t->type == Token::Type::Enum || t->type == Token::Type::Bitset || t->type == Token::Type::Proc)
	{
		out.type = TypeRef::Type::Inline;

		if (!alloc(&out.inline_def))
			return error_out_of_memory(s, ctx);

		return parse_definition(s, *out.inline_def, true);
	}
	else if (t->type == Token::Type::Ident)
	{
		if (const Token* t1 = peek(s, 1); t1 != nullptr && t1->type == Token::Type::DoubleColon)
		{
			out.type = TypeRef::Type::Inline;

			if (!alloc(&out.inline_def))
				return error_out_of_memory(s, ctx);

			return parse_definition(s, *out.inline_def, true);
		}
		else
		{
			out.type = TypeRef::Type::NameRef;

			if (!alloc(&out.name_ref))
				return error_out_of_memory(s, ctx);

			return parse_name_ref(s, *out.name_ref);
		}
	}
	else
	{
		out.type = TypeRef::Type::TypeExpr;

		if (!alloc(&out.type_expr))
			return error_out_of_memory(s, ctx);

		return parse_expr(s, *out.type_expr);
	}
}

static bool parse_type_value(pstate& s, TypeValue& out) noexcept
{
	static constexpr const char ctx[] = "TypeValue";

	if (const Token* t = expect(s, Token::Type::Ident, ctx); t == nullptr)
		return false;
	else
		out.ident = t->data_strview();

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::Set)
	{
		next(s, ctx);

		return parse_expr(s, out.value);
	}

	return true;
}

static bool parse_type_member(pstate& s, TypeMember& out) noexcept
{
	static constexpr const char ctx[] = "TypeMember";

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::Ident)
	{
		next(s, ctx);

		out.opt_ident = t->data_strview();

		if (expect(s, Token::Type::Colon, ctx) == nullptr)
			return false;
	}

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::Pub)
	{
		next(s, ctx);

		out.is_pub = true;
	}

	return parse_type_ref(s, out.type_ref);
}

static bool parse_block(pstate& s, Block& out) noexcept
{
	static constexpr const char ctx[] = "Block";

	if (expect(s, Token::Type::SquiggleBeg, ctx) == nullptr)
		return false;

	while (true)
	{
		if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::SquiggleEnd)
		{
			next(s, ctx);

			break;
		}
		else if (t->type == Token::Type::Ident)
		{
			if (const Token* t1 = peek(s, 1); t1 != nullptr && t1->type == Token::Type::DoubleColon)
			{
				if (!out.definitions.push_back({}))
					return error_out_of_memory(s, ctx);

				if (!parse_definition(s, out.definitions.last(), false))
					return false;

				continue;
			}
		}

		if (!out.statements.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse_statement(s, out.statements.last()))
			return false;
	}

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::To)
		return !parse_to(s, out.to_block);

	return true;
}

static bool parse_proc_param(pstate& s, ProcParam& out) noexcept
{
	static constexpr const char ctx[] = "ProcParam";

	const Token* t = peek(s);

	if (t == nullptr)
		return error_unexpected_end(s, ctx);
	else if (t->type != Token::Type::Ident)
		return error_unexpected_token(s, ctx, t, Token::Type::Ident);

	if (t->data_strview()[0] == '?')
	{
		next(s, ctx);

		out.type = ProcParam::Type::GenericType;

		out.generic_type = t->data_strview();

		return true;
	}
	else
	{
		out.type = ProcParam::Type::VariableDef;

		return parse_variable_def(s, out.variable_def);
	}
}

static bool parse_proc_signature(pstate& s, ProcSignature& out) noexcept
{
	static constexpr const char ctx[] = "ProcSignature";

	if (expect(s, Token::Type::ParenBeg, ctx) == nullptr)
		return false;

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::ParenEnd)
	{
		next(s, ctx);

		goto AFTER_PARAMS;
	}

	while (true)
	{
		if (!out.params.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse_proc_param(s, out.params.last()))
			return false;

		if (const Token* t = next(s, ctx); t == nullptr)
			return false;
		else if (t->type == Token::Type::ParenEnd)
			break;
		else if (t->type != Token::Type::Comma)
			return error_invalid_syntax(s, ctx, t, "Expected ParenEnd or Comma");
	}

AFTER_PARAMS:

	if (const Token* t = peek(s); t == nullptr || t->type != Token::Type::ArrowRight)
		return true;

	next(s, ctx);

	if (!parse_type_ref(s, out.return_type))
		return false;

	return true;
}

static bool parse_module_def(pstate& s, ModuleDef& out) noexcept
{
	static constexpr const char ctx[] = "ModuleDef";

	if (expect(s, Token::Type::SquiggleBeg, ctx) == nullptr)
		return false;

	for (const Token* t = peek(s); t != nullptr; t = peek(s))
	{
		if (t->type == Token::Type::SquiggleEnd)
		{
			next(s, ctx);

			return true;
		}

		if (!out.definitions.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse_definition(s, out.definitions.last(), false))
			return false;
	}

	return error_unexpected_end(s, ctx);
}

static bool parse_annotation_def(pstate& s, AnnotationDef& out) noexcept
{
	static constexpr const char ctx[] = "AnnotationDef";

	out;

	return error_not_implemented(s, ctx);
}

static bool parse_impl_def(pstate& s, ProcDef& out) noexcept
{
	static constexpr const char ctx[] = "ImplDef";

	out;

	return error_not_implemented(s, ctx);
}

static bool parse_trait_def(pstate& s, TraitDef& out) noexcept
{
	static constexpr const char ctx[] = "TraitDef";

	out;

	return error_not_implemented(s, ctx);
}

static bool parse_alias_def(pstate& s, AliasDef& out) noexcept
{
	static constexpr const char ctx[] = "AliasDef";

	return parse_type_ref(s, out.type_ref);
}

static bool parse_new_type_def(pstate& s, NewTypeDef& out) noexcept
{
	static constexpr const char ctx[] = "NewTypeDef";

	return parse_type_ref(s, out.type_ref);
}

static bool parse_bitset_def(pstate& s, BitsetDef& out) noexcept
{
	static constexpr const char ctx[] = "BitsetDef";

	if (expect(s, Token::Type::SquiggleBeg, ctx) == nullptr)
		return false;

	for (const Token* t = peek(s); t != nullptr; t = peek(s))
	{
		if (t->type == Token::Type::SquiggleEnd)
		{
			next(s, ctx);

			return true;
		}
		else if (t->type == Token::Type::Ident)
		{
			if (const Token* t1 = peek(s, 1); t1 == nullptr)
			{
				return error_unexpected_end(s, ctx);
			}
			else if (t1->type == Token::Type::Set)
			{
				if (!out.values.push_back({}))
					return error_out_of_memory(s, ctx);

				if (!parse_type_value(s, out.values.last()))
					return false;
			}
			else if (t1->type == Token::Type::DoubleColon)
			{
				if (!out.definitions.push_back({}))
					return error_out_of_memory(s, ctx);

				if (!parse_definition(s, out.definitions.last(), false))
					return false;
			}
			else
			{
				return error_invalid_syntax(s, ctx, t1, "Expected Colon or DoubleColon");
			}
		}
		else
		{
			return error_unexpected_token(s, ctx, t, Token::Type::Ident);
		}
	}

	return error_unexpected_end(s, ctx);
}

static bool parse_enum_def(pstate& s, EnumDef& out) noexcept
{
	static constexpr const char ctx[] = "EnumDef";

	if (expect(s, Token::Type::SquiggleBeg, ctx) == nullptr)
		return false;

	for (const Token* t = peek(s); t != nullptr; t = peek(s))
	{
		if (t->type == Token::Type::SquiggleEnd)
		{
			next(s, ctx);

			return true;
		}
		else if (t->type == Token::Type::Ident)
		{
			if (const Token* t1 = peek(s, 1); t1 == nullptr)
			{
				return error_unexpected_end(s, ctx);
			}
			else if (t1->type == Token::Type::DoubleColon)
			{
				if (!out.definitions.push_back({}))
					return error_out_of_memory(s, ctx);

				if (!parse_definition(s, out.definitions.last(), false))
					return false;
			}
			else
			{
				if (!out.values.push_back({}))
					return error_out_of_memory(s, ctx);

				if (!parse_type_value(s, out.values.last()))
					return false;
			}
		}
		else
		{
			return error_unexpected_token(s, ctx, t, Token::Type::Ident);
		}
	}

	return error_unexpected_end(s, ctx);
}

static bool parse_union_def(pstate& s, UnionDef& out) noexcept
{
	static constexpr const char ctx[] = "UnionDef";

	if (expect(s, Token::Type::SquiggleBeg, ctx) == nullptr)
		return false;

	for (const Token* t = peek(s); t != nullptr; t = peek(s))
	{
		if (t->type == Token::Type::SquiggleEnd)
		{
			next(s, ctx);

			return true;
		}
		else if (t->type == Token::Type::Struct || t->type == Token::Type::Union || t->type == Token::Type::Enum || t->type == Token::Type::Bitset)
		{
			if (!out.members.push_back({}))
				return error_out_of_memory(s, ctx);

			if (!parse_type_member(s, out.members.last()))
				return false;
		}
		else if (t->type == Token::Type::Ident)
		{
			if (const Token* t1 = peek(s, 1); t1 == nullptr)
			{
				return error_unexpected_end(s, ctx);
			}
			else if (t1->type == Token::Type::Colon)
			{
				if (!out.members.push_back({}))
					return error_out_of_memory(s, ctx);

				if (!parse_type_member(s, out.members.last()))
					return false;
			}
			else if (t1->type == Token::Type::DoubleColon)
			{
				if (!out.definitions.push_back({}))
					return error_out_of_memory(s, ctx);

				if (!parse_definition(s, out.definitions.last(), false))
					return false;
			}
			else
			{
				return error_invalid_syntax(s, ctx, t1, "Expected Colon or DoubleColon");
			}
		}
		else
		{
			return error_unexpected_token(s, ctx, t, Token::Type::Ident);
		}
	}

	return error_unexpected_end(s, ctx);
}

static bool parse_struct_def(pstate& s, StructDef& out) noexcept
{
	static constexpr const char ctx[] = "StructDef";
	if (expect(s, Token::Type::SquiggleBeg, ctx) == nullptr)
		return false;

	for (const Token* t = peek(s); t != nullptr; t = peek(s))
	{
		if (t->type == Token::Type::SquiggleEnd)
		{
			next(s, ctx);

			return true;
		}
		else if (t->type == Token::Type::Struct || t->type == Token::Type::Union || t->type == Token::Type::Enum || t->type == Token::Type::Bitset)
		{
			if (!out.members.push_back({}))
				return error_out_of_memory(s, ctx);

			if (!parse_type_member(s, out.members.last()))
				return false;
		}
		else if (t->type == Token::Type::Ident)
		{
			if (const Token* t1 = peek(s, 1); t1 == nullptr)
			{
				return error_unexpected_end(s, ctx);
			}
			else if (t1->type == Token::Type::Colon)
			{
				if (!out.members.push_back({}))
					return error_out_of_memory(s, ctx);

				if (!parse_type_member(s, out.members.last()))
					return false;
			}
			else if (t1->type == Token::Type::DoubleColon)
			{
				if (!out.definitions.push_back({}))
					return error_out_of_memory(s, ctx);

				if (!parse_definition(s, out.definitions.last(), false))
					return false;
			}
			else
			{
				return error_invalid_syntax(s, ctx, t1, "Expected Colon or DoubleColon");
			}
		}
		else
		{
			return error_unexpected_token(s, ctx, t, Token::Type::Ident);
		}
	}

	return error_unexpected_end(s, ctx);
}

static bool parse_proc_def(pstate& s, ProcDef& out, bool has_body) noexcept
{
	static constexpr const char ctx[] = "ProcDef";

	if (!parse_proc_signature(s, out.signature))
		return false;

	if (!has_body)
		return true;

	return parse_block(s, out.body);
}

static bool parse_definition(pstate& s, Definition& out, bool is_anonymous) noexcept
{
	static constexpr const char ctx[] = "Definition";

	if (const Token* t = peek(s); t != nullptr && t->type == Token::Type::Ident)
	{
		next(s, ctx);

		out.ident = t->data_strview();

		out.flags.has_ident = true;

		if (const Token* t1 = peek(s); t1 == nullptr)
		{
			return error_unexpected_end(s, ctx);
		}
		else if (t1->type == Token::Type::BracketBeg)
		{
			next(s, ctx);

			if (const Token* t2 = peek(s); t2 != nullptr && t2->type == Token::Type::BracketEnd)
			{
				next(s, ctx);

				goto AFTER_TYPE_BINDINGS;
			}

			while(true)
			{
				if (!out.bindings.push_back({}))
					return error_out_of_memory(s, ctx);

				if (!parse_binding(s, out.bindings.last()))
					return false;

				if (const Token* t2 = next(s, ctx); t2 == nullptr)
					return false;
				else if (t2->type == Token::Type::BracketEnd)
					break;
				else if (t2->type != Token::Type::Semicolon)
					return error_invalid_syntax(s, ctx, t2, "Expected BracketEnd or Semicolon");
			}

		}

	AFTER_TYPE_BINDINGS:

		if (expect(s, Token::Type::DoubleColon, ctx) == nullptr)
			return false;
	}
	else if (!is_anonymous)
	{
		return error_unexpected_token(s, ctx, t, Token::Type::Ident);
	}

	const Token* t_stereotype = peek(s);

	if (t_stereotype == nullptr)
		return error_unexpected_end(s, ctx);

	if (t_stereotype->type == Token::Type::Pub)
	{
		out.flags.is_pub = true;

		next(s, ctx);

		t_stereotype = peek(s);

		if (t_stereotype == nullptr)
			return false;
	}

	if (t_stereotype->type != Token::Type::Ident)
		next(s, ctx);

	if (t_stereotype->type == Token::Type::Proc)
	{
		out.type = Definition::Type::Proc;

		return parse_proc_def(s, out.proc_def, !is_anonymous);
	}
	else if (t_stereotype->type == Token::Type::Struct)
	{
		out.type = Definition::Type::Struct;

		return parse_struct_def(s, out.struct_def);
	}
	else if (t_stereotype->type == Token::Type::Union)
	{
		out.type = Definition::Type::Union;

		return parse_union_def(s, out.union_def);
	}
	else if (t_stereotype->type == Token::Type::Enum)
	{
		out.type = Definition::Type::Enum;

		return parse_enum_def(s, out.enum_def);
	}
	else if (t_stereotype->type == Token::Type::Bitset)
	{
		out.type = Definition::Type::Bitset;

		return parse_bitset_def(s, out.bitset_def);
	}
	else if (t_stereotype->type == Token::Type::Alias)
	{
		out.type = Definition::Type::Alias;

		return parse_alias_def(s, out.alias_def);
	}
	else if (t_stereotype->type == Token::Type::Trait)
	{
		out.type = Definition::Type::Trait;

		return parse_trait_def(s, out.trait_def);
	}
	else if (t_stereotype->type == Token::Type::Impl)
	{
		out.type = Definition::Type::Impl;

		return parse_impl_def(s, out.impl_def);
	}
	else if (t_stereotype->type == Token::Type::Annotation)
	{
		out.type = Definition::Type::Annotation;

		return parse_annotation_def(s, out.annotation_def);
	}
	else if (t_stereotype->type == Token::Type::Module)
	{
		out.type = Definition::Type::Module;

		return parse_module_def(s, out.module_def);
	}
	else
	{
		out.type = Definition::Type::NewType;

		return parse_new_type_def(s, out.newtype_def);
	}
}

static bool parse_program_unit(pstate& s, ProgramUnit& out) noexcept
{
	static constexpr const char ctx[] = "ProgramUnit";

	while (peek(s) != nullptr)
	{
		if (!out.definitions.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse_definition(s, out.definitions.last(), false))
			return false;
	}

	return true;
}

Result parse_program_unit(const vec<Token>& tokens, ProgramUnit& out_program_unit) noexcept
{
	pstate s{ tokens.begin(), tokens.end(), tokens.begin(), {} };

	parse_program_unit(s, out_program_unit);

	return s.rst;
}

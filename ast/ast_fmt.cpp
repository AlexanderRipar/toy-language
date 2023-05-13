#include "ast_fmt.hpp"

#include <cstdio>
#include <cassert>

static strview get_name(UnaryOp::Op op) noexcept
{
	static constexpr const strview names[] {
		
		strview::from_literal("NONE"),
		strview::from_literal("BitNot"),
		strview::from_literal("LogNot"),
		strview::from_literal("Deref"),
		strview::from_literal("AddrOf"),
		strview::from_literal("Neg"),
	};

	assert(static_cast<u8>(op) < _countof(names));

	return names[static_cast<u8>(op)];
}

static strview get_name(BinaryOp::Op op) noexcept
{
	static constexpr const strview names[] {
		strview::from_literal("NONE"),
		strview::from_literal("Add"),
		strview::from_literal("Sub"),
		strview::from_literal("Mul"),
		strview::from_literal("Div"),
		strview::from_literal("Mod"),
		strview::from_literal("BitAnd"),
		strview::from_literal("BitOr"),
		strview::from_literal("BitXor"),
		strview::from_literal("ShiftL"),
		strview::from_literal("ShiftR"),
		strview::from_literal("LogAnd"),
		strview::from_literal("LogOr"),
		strview::from_literal("CmpLt"),
		strview::from_literal("CmpLe"),
		strview::from_literal("CmpGt"),
		strview::from_literal("CmpGe"),
		strview::from_literal("CmpNe"),
		strview::from_literal("CmpEq"),
		strview::from_literal("Member"),
		strview::from_literal("Index"),
	};

	assert(static_cast<u8>(op) < _countof(names));

	return names[static_cast<u8>(op)];
}

static strview get_name(Assignment::Op op) noexcept
{
	static constexpr const strview names[] {
		strview::from_literal("NONE"),
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
	};

	assert(static_cast<u8>(op) < _countof(names));

	return names[static_cast<u8>(op)];
}

enum class NodeType
{
	Struct = 1,
	Union,
	Member,
	Array,
	Value,
};

struct NodeState
{
	NodeType type;

	bool is_empty;
};

struct FmtState
{
	static constexpr i32 INDENT_STEP = 4;

	bool suppress_indent = false;

	i32 indent = 0;

	vec<NodeState, 32> stk{};
};

#define OUT_FILE stdout

static void start_elem(FmtState& s, NodeType type, strview name) noexcept
{
	if (s.stk.size() != 0)
	{
		const NodeType prev_type = s.stk.last().type;

		const bool prev_is_empty = s.stk.last().is_empty;

		if (((type == NodeType::Struct || type == NodeType::Union) && prev_type == NodeType::Array)
		 || ((type == NodeType::Member || type == NodeType::Array) && prev_type == NodeType::Struct))
		{
			fprintf(OUT_FILE, "\n%*s", s.indent * FmtState::INDENT_STEP, "");
		}
		else if (type == NodeType::Value && (prev_type == NodeType::Struct || prev_type == NodeType::Member || prev_type == NodeType::Array))
		{
			if (prev_type == NodeType::Array && !s.stk.last().is_empty)
				fprintf(OUT_FILE, ", ");
			else if (prev_type == NodeType::Array || prev_type == NodeType::Struct)
				fprintf(OUT_FILE, " ");
		}
		else
		{
			assert((type == NodeType::Union || type == NodeType::Struct) && (prev_type == NodeType::Member || prev_type == NodeType::Union));
		}

		if (prev_type == NodeType::Union || prev_type == NodeType::Member)
		{
			assert(prev_is_empty);
		}
	}
	else
	{
		assert(type == NodeType::Struct);
	}

	const char* name_beg = name.begin();

	const i32 name_len = static_cast<i32>(name.len());

	switch (type)
	{
	case NodeType::Struct:
		fprintf(OUT_FILE, "%.*s {", name_len, name_beg);
		break;

	case NodeType::Union:
		fprintf(OUT_FILE, "%.*s::", name_len, name_beg);
		break;

	case NodeType::Member:
		fprintf(OUT_FILE, "%.*s = ", name_len, name_beg);
		break;

	case NodeType::Array:
		fprintf(OUT_FILE, "%.*s = [", name_len, name_beg);
		break;

	case NodeType::Value:
		fprintf(OUT_FILE, "%.*s", name_len, name_beg);
		break;

	default:
		assert(false);
		break;
	}

	if (type != NodeType::Value || (s.stk.size() != 0 && s.stk.last().type == NodeType::Member))
		s.stk.last().is_empty = false;

	if (type != NodeType::Value)
	{
		if (!s.stk.push_back({ type, true }))
			assert(false);
	}

	if (type != NodeType::Union && type != NodeType::Member && type != NodeType::Value)
		++s.indent;
}

static void close_elem(FmtState& s) noexcept
{
	if (s.stk.size() == 0)
	{
		assert(false);

		return;
	}

	const NodeType type = s.stk.last().type;

	const bool is_empty = s.stk.last().is_empty;

	if (type != NodeType::Member && type != NodeType::Union)
	{
		--s.indent;

		if (!is_empty)
			fprintf(OUT_FILE, "\n%*s", s.indent * 4, "");
	}

	switch (type)
	{
	case NodeType::Struct:
		if (is_empty)
			fprintf(OUT_FILE, " }");
		else
			fprintf(OUT_FILE, "}");
		break;

	case NodeType::Union:
	case NodeType::Member:
		assert(!is_empty);
		break;

	case NodeType::Array:
		fprintf(OUT_FILE, "]");
		break;

	default:
		assert(false);
		break;
	}

	s.stk.pop();
}



static void tree_print(FmtState& s, const TypeRef& node) noexcept;

static void tree_print(FmtState& s, const Expr& node) noexcept;

static void tree_print(FmtState& s, const Definition& node) noexcept;

static void tree_print(FmtState& s, const Call& node) noexcept;

static void tree_print(FmtState& s, const Type& node) noexcept;

static void tree_print(FmtState& s, const Statement& node) noexcept;

static void tree_print(FmtState& s, const TopLevelExpr& node) noexcept;

static void tree_print(FmtState& s, const Assignment& node) noexcept;



static void tree_print(FmtState& s, const ForLoopSignature& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("ForLoopSignature"));

	if (node.opt_init.opt_type.tag != TypeRef::Tag::EMPTY || node.opt_init.opt_value.tag != TopLevelExpr::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, strview::from_literal("init"));
		tree_print(s, node.opt_init);
		close_elem(s);
	}
	
	if (node.opt_cond.tag != Expr::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, strview::from_literal("condition"));
		tree_print(s, node.opt_cond);
		close_elem(s);
	}
	
	if (node.opt_step.op != Assignment::Op::NONE)
	{
		start_elem(s, NodeType::Member, strview::from_literal("step"));
		tree_print(s, node.opt_step);
		close_elem(s);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const ForEachSignature& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("ForEachSignature"));

	start_elem(s, NodeType::Member, strview::from_literal("loop_variable"));
	start_elem(s, NodeType::Value, node.loop_variable);
	close_elem(s);

	if (node.opt_step_variable.begin() != nullptr)
	{
		start_elem(s, NodeType::Member, strview::from_literal("step_variable"));
		start_elem(s, NodeType::Value, node.opt_step_variable);
		close_elem(s);
	}

	start_elem(s, NodeType::Member, strview::from_literal("loopee"));
	tree_print(s, node.loopee);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const Catch& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Catch"));

	start_elem(s, NodeType::Member, strview::from_literal("caught_expr"));
	tree_print(s, node.caught_expr);
	close_elem(s);

	if (node.opt_error_ident.begin() != nullptr)
	{
		start_elem(s, NodeType::Member, strview::from_literal("error_ident"));
		start_elem(s, NodeType::Value, node.opt_error_ident);
		close_elem(s);
	}

	start_elem(s, NodeType::Member, strview::from_literal("stmt"));
	tree_print(s, node.stmt);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const Case& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Case"));

	start_elem(s, NodeType::Member, strview::from_literal("label"));
	tree_print(s, node.label);
	close_elem(s);

	start_elem(s, NodeType::Member, strview::from_literal("body"));
	tree_print(s, node.body);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const ForSignature& node) noexcept
{
	start_elem(s, NodeType::Union, strview::from_literal("ForSignature"));

	switch (node.tag)
	{
	case ForSignature::Tag::ForEachSignature:
		tree_print(s, node.for_each);
		break;

	case ForSignature::Tag::ForLoopSignature:
		tree_print(s, node.for_loop);
		break;

	default:
		assert(false);
		break;
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const Assignment& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Assignment"));

	start_elem(s, NodeType::Member, strview::from_literal("operator"));
	start_elem(s, NodeType::Value, get_name(node.op));
	close_elem(s);

	start_elem(s, NodeType::Member, strview::from_literal("assignee"));
	tree_print(s, node.assignee);
	close_elem(s);

	start_elem(s, NodeType::Member, strview::from_literal("value"));
	tree_print(s, node.value);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const Block& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Block"));

	start_elem(s, NodeType::Array, strview::from_literal("statements"));
	for (const Statement& child : node.statements)
		tree_print(s, child);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const Switch& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Switch"));

	start_elem(s, NodeType::Member, strview::from_literal("switched"));
	tree_print(s, node.switched);
	close_elem(s);

	start_elem(s, NodeType::Array, strview::from_literal("cases"));
	for (const Case& child : node.cases)
		tree_print(s, child);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const For& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("For"));

	start_elem(s, NodeType::Member, strview::from_literal("signature"));
	tree_print(s, node.signature);
	close_elem(s);

	start_elem(s, NodeType::Member, strview::from_literal("body"));
	tree_print(s, node.body);
	close_elem(s);

	if (node.opt_until_body.tag != Statement::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, strview::from_literal("until"));
		tree_print(s, node.opt_until_body);
		close_elem(s);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const If& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("If"));

	if (node.opt_init.opt_type.tag != TypeRef::Tag::EMPTY || node.opt_init.opt_value.tag != TopLevelExpr::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, strview::from_literal("init"));
		tree_print(s, node.opt_init);
		close_elem(s);
	}

	start_elem(s, NodeType::Member, strview::from_literal("condition"));
	tree_print(s, node.condition);
	close_elem(s);

	start_elem(s, NodeType::Member, strview::from_literal("body"));
	tree_print(s, node.body);
	close_elem(s);

	if (node.opt_else_body.tag != Statement::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, strview::from_literal("else"));
		tree_print(s, node.opt_else_body);
		close_elem(s);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const TopLevelExpr& node) noexcept
{
	start_elem(s, NodeType::Union, strview::from_literal("TopLevelExpr"));

	switch (node.tag)
	{
	case TopLevelExpr::Tag::Block:
		tree_print(s, *node.block_expr);
		break;

	case TopLevelExpr::Tag::If:
		tree_print(s, *node.if_expr);
		break;

	case TopLevelExpr::Tag::For:
		tree_print(s, *node.for_expr);
		break;

	case TopLevelExpr::Tag::Switch:
		tree_print(s, *node.switch_expr);
		break;

	case TopLevelExpr::Tag::Catch:
		tree_print(s, *node.catch_expr);
		break;

	case TopLevelExpr::Tag::Try:
		start_elem(s, NodeType::Union, strview::from_literal("Try"));
		tree_print(s, *node.simple_or_try_expr);
		close_elem(s);
		break;

	case TopLevelExpr::Tag::Expr:
		tree_print(s, *node.simple_or_try_expr);
		break;

	case TopLevelExpr::Tag::Type:
		tree_print(s, *node.type_expr);
		break;

	case TopLevelExpr::Tag::Undefined:
		start_elem(s, NodeType::Struct, strview::from_literal("Undefined"));
		close_elem(s);
		break;

	default:
		assert(false);
		break;
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const Argument& node) noexcept
{
	start_elem(s, NodeType::Union, strview::from_literal("Argument"));

	switch (node.tag)
	{
	case Argument::Tag::Type:
		tree_print(s, *node.type);
		break;

	case Argument::Tag::Expr:
		tree_print(s, *node.expr);
		break;

	default:
		assert(false);
		break;
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const CharLiteral& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("CharLiteral"));

	// TODO

	close_elem(s);
}

static void tree_print(FmtState& s, const StringLiteral& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("StringLiteral"));

	start_elem(s, NodeType::Value, { node.value.begin(), node.value.end() });

	close_elem(s);
}

static void tree_print(FmtState& s, const FloatLiteral& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("FloatLiteral"));

	// TODO

	close_elem(s);
}

static void tree_print(FmtState& s, const IntegerLiteral& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("IntegerLiteral"));

	// TODO

	close_elem(s);
}

static void tree_print(FmtState& s, const EnumValue& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("EnumValue"));

	start_elem(s, NodeType::Member, strview::from_literal("ident"));
	start_elem(s, NodeType::Value, node.ident);
	close_elem(s);

	if (node.opt_value.tag != Expr::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, strview::from_literal("value"));
		tree_print(s, node.opt_value);
		close_elem(s);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const Statement& node) noexcept
{
	start_elem(s, NodeType::Union, strview::from_literal("Statement"));

	switch (node.tag)
	{
	case Statement::Tag::If:
		tree_print(s, *node.if_stmt);
		break;

	case Statement::Tag::For:
		tree_print(s, *node.for_stmt);
		break;

	case Statement::Tag::Switch:
		tree_print(s, *node.switch_stmt);
		break;

	case Statement::Tag::Return:
		start_elem(s, NodeType::Union, strview::from_literal("Return"));
		tree_print(s, *node.return_or_yield_or_break_value);
		close_elem(s);
		break;

	case Statement::Tag::Yield:
		start_elem(s, NodeType::Union, strview::from_literal("Yield"));
		tree_print(s, *node.return_or_yield_or_break_value);
		close_elem(s);
		break;

	case Statement::Tag::Break:
		start_elem(s, NodeType::Union, strview::from_literal("Break"));
		tree_print(s, *node.return_or_yield_or_break_value);
		close_elem(s);
		break;

	case Statement::Tag::Block:
		tree_print(s, *node.block);
		break;

	case Statement::Tag::Call:
		tree_print(s, *node.call);
		break;

	case Statement::Tag::Definition:
		tree_print(s, *node.definition);
		break;

	case Statement::Tag::Assignment:
		tree_print(s, *node.assignment);
		break;

	case Statement::Tag::Defer:
		start_elem(s, NodeType::Union, strview::from_literal("Defer"));
		tree_print(s, *node.deferred_stmt);
		close_elem(s);
		break;

	case Statement::Tag::Undefined:
		start_elem(s, NodeType::Struct, strview::from_literal("Undefined"));
		close_elem(s);
		break;

	default:
		assert(false);
		break;
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const ProcSignature& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("ProcSignature"));

	start_elem(s, NodeType::Array, strview::from_literal("parameters"));
	for (const Definition& child : node.parameters)
		tree_print(s, child);
	close_elem(s);

	if (node.opt_return_type.tag != TypeRef::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, strview::from_literal("return_type"));
		tree_print(s, node.opt_return_type);
		close_elem(s);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const Call& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Call"));

	start_elem(s, NodeType::Member, strview::from_literal("callee"));
	tree_print(s, node.callee);
	close_elem(s);

	start_elem(s, NodeType::Array, strview::from_literal("args"));
	for (const Argument& child : node.args)
		tree_print(s, child);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const BinaryOp& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("BinaryOp"));

	start_elem(s, NodeType::Member, strview::from_literal("operator"));
	start_elem(s, NodeType::Value, get_name(node.op));
	close_elem(s);

	start_elem(s, NodeType::Member, strview::from_literal("lhs"));
	tree_print(s, node.lhs);
	close_elem(s);

	start_elem(s, NodeType::Member, strview::from_literal("rhs"));
	tree_print(s, node.rhs);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const UnaryOp& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("UnaryOp"));

	start_elem(s, NodeType::Member, strview::from_literal("operator"));
	start_elem(s, NodeType::Value, get_name(node.op));
	close_elem(s);

	start_elem(s, NodeType::Member, strview::from_literal("operand"));
	tree_print(s, node.operand);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const Literal& node) noexcept
{
	start_elem(s, NodeType::Union, strview::from_literal("Literal"));

	switch (node.tag)
	{
	case Literal::Tag::IntegerLiteral:
		tree_print(s, node.integer_literal);
		break;

	case Literal::Tag::FloatLiteral:
		tree_print(s, node.float_literal);
		break;

	case Literal::Tag::StringLiteral:
		tree_print(s, node.string_literal);
		break;

	case Literal::Tag::CharLiteral:
		tree_print(s, node.char_literal);
		break;
	
	default:
		assert(false);
		break;
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const Impl& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Impl"));

	start_elem(s, NodeType::Member, "trait");
	tree_print(s, node.trait);
	close_elem(s);

	start_elem(s, NodeType::Array, "definitions");
	for (const Definition& child : node.definitions)
		tree_print(s, child);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const Module& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Module"));

	start_elem(s, NodeType::Array, "definitions");
	for (const Definition& child : node.definitions)
		tree_print(s, child);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const Trait& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Trait"));

	start_elem(s, NodeType::Array, "bindings");
	for (const Definition& child : node.bindings)
		tree_print(s, child);
	close_elem(s);

	start_elem(s, NodeType::Array, "definitions");
	for (const Definition& child : node.definitions)
		tree_print(s, child);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const Enum& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Enum"));

	if (node.opt_enum_type.tag != TypeRef::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, strview::from_literal("enum_type"));
		tree_print(s, node.opt_enum_type);
		close_elem(s);
	}

	start_elem(s, NodeType::Array, "values");
	for (const EnumValue& child : node.values)
		tree_print(s, child);
	close_elem(s);

	
	if (node.definitions.size() != 0)
	{
		start_elem(s, NodeType::Array, strview::from_literal("definitions"));
		for (const Definition& child : node.definitions)
			tree_print(s, child);
		close_elem(s);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const StructuredType& node, const strview stereotype) noexcept
{
	start_elem(s, NodeType::Struct, stereotype);

	start_elem(s, NodeType::Array, "members");
	for (const Definition& child : node.members)
		tree_print(s, child);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const Proc& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Proc"));

	start_elem(s, NodeType::Member, strview::from_literal("signature"));
	tree_print(s, node.signature);
	close_elem(s);

	if (node.opt_body.tag != Statement::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, strview::from_literal("body"));
		tree_print(s, node.opt_body);
		close_elem(s);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const Array& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Array"));

	start_elem(s, NodeType::Member, strview::from_literal("elem_cnt"));
	tree_print(s, node.elem_cnt);
	close_elem(s);

	start_elem(s, NodeType::Member, strview::from_literal("elem_type"));
	tree_print(s, node.elem_type);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const Expr& node) noexcept
{
	start_elem(s, NodeType::Union, strview::from_literal("Expr"));

	switch (node.tag)
	{
	case Expr::Tag::Ident:
		start_elem(s, NodeType::Struct, strview::from_literal("Ident"));
		start_elem(s, NodeType::Value, { node.ident_beg, node.ident_len });
		close_elem(s);
		break;

	case Expr::Tag::Literal:
		tree_print(s, *node.literal);
		break;

	case Expr::Tag::UnaryOp:
		tree_print(s, *node.unary_op);
		break;

	case Expr::Tag::BinaryOp:
		tree_print(s, *node.binary_op);
		break;

	case Expr::Tag::Call:
		tree_print(s, *node.call);
		break;

	default:
		assert(false);
		break;
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const Type& node) noexcept
{
	start_elem(s, NodeType::Union, strview::from_literal("Type"));

	switch (node.tag)
	{
	case Type::Tag::Proc:
		tree_print(s, node.proc_type);
		break;
	
	case Type::Tag::Struct:
		tree_print(s, node.struct_or_union_type, strview::from_literal("Struct"));
		break;
	
	case Type::Tag::Union:
		tree_print(s, node.struct_or_union_type, strview::from_literal("Union"));
		break;
	
	case Type::Tag::Enum:
		tree_print(s, node.enum_type);
		break;
	
	case Type::Tag::Trait:
		tree_print(s, node.trait_type);
		break;
	
	case Type::Tag::Module:
		tree_print(s, node.module_type);
		break;
	
	case Type::Tag::Impl:
		tree_print(s, node.impl_type);
		break;
	
	default:
		assert(false);
		break;
	}
	
	close_elem(s);
}

static void tree_print(FmtState& s, const TypeRef& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("TypeRef"));

	start_elem(s, NodeType::Member, strview::from_literal("is_mut"));
	start_elem(s, NodeType::Value, node.is_mut ? strview::from_literal("true") : strview::from_literal("false"));
	close_elem(s);

	start_elem(s, NodeType::Member, strview::from_literal("is_proc_param_ref"));
	start_elem(s, NodeType::Value, node.is_proc_param_ref ? strview::from_literal("true") : strview::from_literal("false"));
	close_elem(s);

	start_elem(s, NodeType::Member, strview::from_literal("typeish"));

	switch (node.tag)
	{
	case TypeRef::Tag::Type:
		tree_print(s, *node.type);
		break;

	case TypeRef::Tag::Expr:
		tree_print(s, *node.expr);
		break;

	case TypeRef::Tag::Ptr:
		start_elem(s, NodeType::Union, strview::from_literal("Ptr"));
		tree_print(s, *node.ptr_or_multiptr_or_slice);
		close_elem(s);
		break;

	case TypeRef::Tag::MultiPtr:
		start_elem(s, NodeType::Union, strview::from_literal("Multiptr"));
		tree_print(s, *node.ptr_or_multiptr_or_slice);
		close_elem(s);
		break;

	case TypeRef::Tag::Slice:
		start_elem(s, NodeType::Union, strview::from_literal("Slice"));
		tree_print(s, *node.ptr_or_multiptr_or_slice);
		close_elem(s);
		break;

	case TypeRef::Tag::Array:
		tree_print(s, *node.array);
		break;

	default:
		assert(false);
		break;
	}

	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const Definition& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("Definition"));

	start_elem(s, NodeType::Member, strview::from_literal("is_comptime"));
	start_elem(s, NodeType::Value, node.is_comptime ? strview::from_literal("true") : strview::from_literal("false"));
	close_elem(s);

	start_elem(s, NodeType::Member, strview::from_literal("is_pub"));
	start_elem(s, NodeType::Value, node.is_pub ? strview::from_literal("true") : strview::from_literal("false"));
	close_elem(s);

	if (node.opt_ident.begin() != nullptr)
	{
		start_elem(s, NodeType::Member, strview::from_literal("ident"));
		start_elem(s, NodeType::Value, node.opt_ident);
		close_elem(s);
	}

	if (node.opt_type.tag != TypeRef::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, strview::from_literal("type"));
		tree_print(s, node.opt_type);
		close_elem(s);
	}

	if (node.opt_value.tag != TopLevelExpr::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, strview::from_literal("value"));
		tree_print(s, node.opt_value);
		close_elem(s);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const ProgramUnit& node) noexcept
{
	start_elem(s, NodeType::Struct, strview::from_literal("ProgramUnit"));

	start_elem(s, NodeType::Array, strview::from_literal("statements"));
	for (const Definition& child : node.definitions)
		tree_print(s, child);
	close_elem(s);

	close_elem(s);
}

void ast_print_tree(const ProgramUnit& node) noexcept
{	
	FmtState s{};

	tree_print(s, node);
}

void ast_print_text(const ProgramUnit& program) noexcept
{
	program;

	fprintf(stderr, "ast_print_text: Not implemented\n");
}

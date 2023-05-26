#include "ast_fmt.hpp"

#include <cstdio>
#include <cassert>

static strview get_name(ast::UnaryOp::Op op) noexcept
{
	static constexpr const strview names[] {
		
		strview::from_literal("NONE"),
		strview::from_literal("BitNot"),
		strview::from_literal("LogNot"),
		strview::from_literal("Deref"),
		strview::from_literal("AddrOf"),
		strview::from_literal("Neg"),
		strview::from_literal("Try"),
	};

	const usz index = static_cast<usz>(op) - 1;

	const bool is_in_range = index < _countof(names);

	if (is_in_range)
		return names[index];
	
	assert(false);

	return strview::from_literal("???");
}

static strview get_name(ast::BinaryOp::Op op) noexcept
{
	static constexpr const strview names[] {
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
	};

	const usz index = static_cast<usz>(op) - 1;

	const bool is_in_range = index < _countof(names);

	if (is_in_range)
		return names[index];
	
	assert(false);

	return strview::from_literal("???");
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
		
		if (prev_type == NodeType::Member)
			s.stk.pop();
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



static void tree_print(FmtState& s, const ast::Expr& node) noexcept;

static void tree_print(FmtState& s, const ast::Definition& node) noexcept;

static void tree_print(FmtState& s, const ast::Type& node, const strview node_name = strview::from_literal("Type")) noexcept;



static void tree_print(FmtState& s, const ast::Case& node) noexcept
{
	start_elem(s, NodeType::Struct, "Case");

	start_elem(s, NodeType::Array, "labels");
	for (const ast::Expr& child : node.labels)
		tree_print(s, child);
	close_elem(s);
	
	start_elem(s, NodeType::Member, "body");
	tree_print(s, node.body);

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::ForLoopSignature& node) noexcept
{
	start_elem(s, NodeType::Struct, "ForLoopSignature");

	if (node.opt_init != nullptr)
	{
		start_elem(s, NodeType::Member, "init");
		tree_print(s, *node.opt_init);
	}

	if (node.opt_condition.tag != ast::Expr::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, "condition");
		tree_print(s, node.opt_condition);
	}

	if (node.opt_step.tag != ast::Expr::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, "step");
		tree_print(s, node.opt_step);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::ForEachSignature& node) noexcept
{
	start_elem(s, NodeType::Struct, "ForEachSignature");

	start_elem(s, NodeType::Member, "loop_var");
	start_elem(s, NodeType::Value, node.loop_var);

	if (node.opt_index_var.begin() != nullptr)
	{
		start_elem(s, NodeType::Member, "index_var");
		start_elem(s, NodeType::Value, node.opt_index_var);
	}

	start_elem(s, NodeType::Member, "looped_over");
	tree_print(s, node.looped_over);

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::CharLiteral& node) noexcept
{
	start_elem(s, NodeType::Struct, "CharLiteral");

	// TODO
	node;

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::StringLiteral& node) noexcept
{
	start_elem(s, NodeType::Struct, "StringLiteral");

	start_elem(s, NodeType::Value, strview{ node.value.begin(), node.value.end() });

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::FloatLiteral& node) noexcept
{
	start_elem(s, NodeType::Struct, "FloatLiteral");

	// TODO
	node;

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::IntegerLiteral& node) noexcept
{
	start_elem(s, NodeType::Struct, "IntegerLiteral");

	// TODO
	node;

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::Block& node) noexcept
{
	start_elem(s, NodeType::Struct, "Block");

	start_elem(s, NodeType::Array, "statements");
	for (const ast::Expr& child : node.statements)
		tree_print(s, child);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::Switch& node) noexcept
{
	start_elem(s, NodeType::Struct, "Switch");

	if (node.opt_init != nullptr)
	{
		start_elem(s, NodeType::Member, "init");
		tree_print(s, *node.opt_init);
	}

	start_elem(s, NodeType::Member, "switched");
	tree_print(s, node.switched_expr);

	start_elem(s, NodeType::Array, "cases");
	for (const ast::Case& child : node.cases)
		tree_print(s, child);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::For& node) noexcept
{
	start_elem(s, NodeType::Struct, "For");
	
	start_elem(s, NodeType::Member, "signature");
	switch (node.tag)
	{
	case ast::For::Tag::ForEachSignature:
		tree_print(s, node.for_each_signature);
		break;

	case ast::For::Tag::ForLoopSignature:
		tree_print(s, node.for_loop_signature);
		break;

	default:
		assert(false);
		break;
	}

	start_elem(s, NodeType::Member, "body");
	tree_print(s, node.body);

	if (node.opt_finally_body.tag != ast::Expr::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, "finally_body");
		tree_print(s, node.opt_finally_body);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::If& node) noexcept
{
	start_elem(s, NodeType::Struct, "If");
	
	if (node.opt_init != nullptr)
	{
		start_elem(s, NodeType::Member, "init");
		tree_print(s, *node.opt_init);
	}

	start_elem(s, NodeType::Member, "condition");
	tree_print(s, node.condition);

	start_elem(s, NodeType::Member, "body");
	tree_print(s, node.body);

	if (node.opt_else_body.tag != ast::Expr::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, "else_body");
		tree_print(s, node.opt_else_body);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::UnaryOp& node) noexcept
{
	start_elem(s, NodeType::Union, "UnaryOp");

	start_elem(s, NodeType::Struct, get_name(node.op));

	start_elem(s, NodeType::Member, "operand");
	tree_print(s, node.operand);

	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::BinaryOp& node) noexcept
{
	start_elem(s, NodeType::Union, "BinaryOp");

	start_elem(s, NodeType::Struct, get_name(node.op));

	start_elem(s, NodeType::Member, "lhs");
	tree_print(s, node.lhs);
	
	start_elem(s, NodeType::Member, "rhs");
	tree_print(s, node.rhs);

	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::Literal& node) noexcept
{
	start_elem(s, NodeType::Union, "Literal");

	switch (node.tag)
	{
	case ast::Literal::Tag::IntegerLiteral:
		tree_print(s, node.integer_literal);
		break;

	case ast::Literal::Tag::FloatLiteral:
		tree_print(s, node.float_literal);
		break;

	case ast::Literal::Tag::StringLiteral:
		tree_print(s, node.string_literal);
		break;
	case ast::Literal::Tag::CharLiteral:
		tree_print(s, node.char_literal);
		break;

	default:
		assert(false);
		break;
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::Call& node) noexcept
{
	start_elem(s, NodeType::Struct, "Call");

	start_elem(s, NodeType::Member, "callee");
	tree_print(s, node.callee);

	start_elem(s, NodeType::Array, "arguments");
	for (const ast::Expr& child : node.arguments)
		tree_print(s, child);
	close_elem(s);

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::Signature& node, const strview node_name = strview::from_literal("Signature")) noexcept
{
	start_elem(s, NodeType::Struct, node_name);

	start_elem(s, NodeType::Array, "parameters");
	for (const ast::Definition& child : node.parameters)
		tree_print(s, child);
	close_elem(s);

	if (node.opt_return_type.tag != ast::Type::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, "return_type");
		tree_print(s, node.opt_return_type);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::Impl& node) noexcept
{
	start_elem(s, NodeType::Struct, "Impl");

	start_elem(s, NodeType::Member, "bound_trait");
	tree_print(s, node.bound_trait);

	start_elem(s, NodeType::Member, "body");
	tree_print(s, node.body);

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::Expr& node) noexcept
{
	start_elem(s, NodeType::Union, "Expr");

	switch (node.tag)
	{
	case ast::Expr::Tag::Ident:
		start_elem(s, NodeType::Struct, "Ident");
		start_elem(s, NodeType::Value, strview{ node.ident_beg, node.ident_len });
		close_elem(s);
		break;

	case ast::Expr::Tag::Call:
		tree_print(s, *node.call);
		break;

	case ast::Expr::Tag::Literal:
		tree_print(s, *node.literal);
		break;

	case ast::Expr::Tag::BinaryOp:
		tree_print(s, *node.binary_op);
		break;

	case ast::Expr::Tag::UnaryOp:
		tree_print(s, *node.unary_op);
		break;

	case ast::Expr::Tag::If:
		tree_print(s, *node.if_expr);
		break;

	case ast::Expr::Tag::For:
		tree_print(s, *node.for_expr);
		break;

	case ast::Expr::Tag::Switch:
		tree_print(s, *node.switch_expr);
		break;

	case ast::Expr::Tag::Block:
		tree_print(s, *node.block);
		break;

	case ast::Expr::Tag::Return:
		start_elem(s, NodeType::Union, "Return");
		tree_print(s, *node.return_or_break_or_defer);
		close_elem(s);
		break;

	case ast::Expr::Tag::Break:
		start_elem(s, NodeType::Union, "Break");
		tree_print(s, *node.return_or_break_or_defer);
		close_elem(s);
		break;

	case ast::Expr::Tag::Defer:
		start_elem(s, NodeType::Union, "Defer");
		tree_print(s, *node.return_or_break_or_defer);
		close_elem(s);
		break;

	case ast::Expr::Tag::Definition:
		tree_print(s, *node.definition);
		break;

	case ast::Expr::Tag::Impl:
		tree_print(s, *node.impl);
		break;

	default:
		assert(false);
		break;
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::Array& node) noexcept
{
	start_elem(s, NodeType::Struct, "Array");

	start_elem(s, NodeType::Member, "count");
	tree_print(s, node.count);

	start_elem(s, NodeType::Member, "elem_type");
	tree_print(s, node.elem_type);

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::Type& node, const strview node_name) noexcept
{
	start_elem(s, NodeType::Struct, node_name);

	start_elem(s, NodeType::Member, "is_mut");
	start_elem(s, NodeType::Value, node.is_mut ? "true" : "false");

	start_elem(s, NodeType::Member, "data");

	switch (node.tag)
	{
	case ast::Type::Tag::Expr:
		tree_print(s, *node.expr);
		break;

	case ast::Type::Tag::Array:
		tree_print(s, *node.array);
		break;

	case ast::Type::Tag::Slice:
		tree_print(s, *node.nested_type, "Slice");
		break;

	case ast::Type::Tag::Ptr:
		tree_print(s, *node.nested_type, "Ptr");
		break;

	case ast::Type::Tag::MultiPtr:
		tree_print(s, *node.nested_type, "MultiPtr");
		break;

	case ast::Type::Tag::Variadic:
		tree_print(s, *node.nested_type, "Variadic");
		break;

	case ast::Type::Tag::Reference:
		tree_print(s, *node.nested_type, "Reference");
		break;

	case ast::Type::Tag::ProcSignature:
		tree_print(s, *node.signature, "ProcSignature");
		break;

	case ast::Type::Tag::FuncSignature:
		tree_print(s, *node.signature, "FuncSignature");
		break;

	case ast::Type::Tag::TraitSignature:
		tree_print(s, *node.signature, "TraitSignature");
		break;

	default:
		assert(false);
		break;
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::Definition& node) noexcept
{
	start_elem(s, NodeType::Struct, "Definition");

	start_elem(s, NodeType::Member, "ident");
	start_elem(s, NodeType::Value, node.ident);

	start_elem(s, NodeType::Member, "is_pub");
	start_elem(s, NodeType::Value, node.is_pub ? "true" : "false");

	start_elem(s, NodeType::Member, "is_comptime");
	start_elem(s, NodeType::Value, node.is_comptime ? "true" : "false");

	if (node.opt_type.tag != ast::Type::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, "type");
		tree_print(s, node.opt_type);
	}

	if (node.opt_value.tag != ast::Expr::Tag::EMPTY)
	{
		start_elem(s, NodeType::Member, "value");
		tree_print(s, node.opt_value);
	}

	close_elem(s);
}

static void tree_print(FmtState& s, const ast::FileModule& node) noexcept
{
	start_elem(s, NodeType::Struct, "FileModule");

	start_elem(s, NodeType::Member, "filename");
	start_elem(s, NodeType::Value, node.filename);

	start_elem(s, NodeType::Array, "exprs");
	for (const ast::Expr& child : node.exprs)
		tree_print(s, child);
	close_elem(s);

	close_elem(s);
}

void ast_print_tree(const ast::FileModule& program) noexcept
{	
	FmtState s{};

	tree_print(s, program);
}

void ast_print_text(const ast::FileModule& program) noexcept
{
	program;

	fprintf(stderr, "ast_print_text: Not implemented\n");
}

#include "ast_fmt.hpp"

#include <cstdio>
#include <cassert>


static bool prev_node_was_inline = false;

static void print_beg_node(const char* node_name, i32 indent, const char* name, bool is_inline = false, bool no_newline = false) noexcept
{
	const char* const newline = is_inline ? "" : no_newline ? " { " : " {\n";

	const char* indent_str = "";

	if (prev_node_was_inline)
	{
		indent = 0;

		indent_str = "::";
	}

	prev_node_was_inline = is_inline;

	if (name != nullptr)
		fprintf(stderr, "%*s%s = %s%s", indent * 4, indent_str, name, node_name, newline);
	else
		fprintf(stderr, "%*s%s%s", indent * 4, indent_str, node_name, newline);
}

static void print_end_node(i32 indent, bool no_newline = false) noexcept
{
	const char* indent_str = "";

	if (no_newline)
	{
		indent = 0;

		indent_str = " ";
	}

	fprintf(stderr, "%*s}\n", indent * 4, indent_str);
}

static void print_scalar(const char* name, strview value, i32 indent, bool no_quotes = false) noexcept
{
	if (no_quotes)
		fprintf(stderr, "%*s%s = %.*s\n", indent * 4, "", name, static_cast<i32>(value.len()), value.begin());
	else
		fprintf(stderr, "%*s%s = \"%.*s\"\n", indent * 4, "", name, static_cast<i32>(value.len()), value.begin());
}

static void print_inline_value(strview value) noexcept
{
	fprintf(stderr, "\"%.*s\"", static_cast<i32>(value.len()), value.begin());
}

static void print_inline_value(f64 value) noexcept
{
	fprintf(stderr, "%f", value);
}

static void print_inline_value(u64 value) noexcept
{
	fprintf(stderr, "%llu", value);
}

static void print_inline_value(char value) noexcept
{
	fprintf(stderr, "'%c'", value);
}

static void print_beg_array(const char* name, i32 indent) noexcept
{
	fprintf(stderr, "%*s%s = [\n", indent * 4, "", name);
}

static void print_end_array(i32 indent) noexcept
{
	fprintf(stderr, "%*s]\n", indent * 4, "");
}

static void print_text(const char* text) noexcept
{
	fprintf(stderr, "%s", text);
}



static strview get_name(UnaryOp::Op op) noexcept
{
	static constexpr const strview names[] {
		
		strview::from_literal("NONE"),
		strview::from_literal("BitNot"),
		strview::from_literal("LogNot"),
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



static void tree_print(const If& node, u32 indent, const char* name = nullptr) noexcept;

static void tree_print(const For& node, u32 indent, const char* name = nullptr) noexcept;

static void tree_print(const Switch& node, u32 indent, const char* name = nullptr) noexcept;

static void tree_print(const TypeRef& node, u32 indent, const char* name = nullptr) noexcept;

static void tree_print(const Expr& node, u32 indent, const char* name = nullptr) noexcept;

static void tree_print(const Definition& node, u32 indent, const char* name = nullptr) noexcept;

static void tree_print(const Trait& node, u32 indent, const char* name = nullptr) noexcept;

static void tree_print(const Call& node, u32 indent, const char* name = nullptr) noexcept;

static void tree_print(const TopLevelExpr& node, u32 indent, const char* name = nullptr) noexcept;

static void tree_print(const Type& node, u32 indent, const char* name = nullptr) noexcept;

static void tree_print(const Statement& node, u32 indent, const char* name = nullptr) noexcept;



static void tree_print(const Assignment& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Assignment", indent, name);

	print_scalar("op", get_name(node.op), indent + 1, true);

	tree_print(node.assignee, indent + 1, "assignee");

	tree_print(node.value, indent + 1, "value");

	print_end_node(indent);
}

static void tree_print(const Go& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Go", indent, name);

	tree_print(node.label, indent + 1, "label");

	print_end_node(indent);
}

static void tree_print(const Block& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Block", indent, name);

	if (node.statements.size() != 0)
	{
		print_beg_array("statements", indent + 1);

		for (const Statement& child : node.statements)
			tree_print(child, indent + 2);

		print_end_array(indent + 1);
	}
	else
	{
		print_scalar("statements", strview::from_literal("[]"), indent + 1, true);
	}

	print_end_node(indent);
}

static void tree_print(const ForLoopSignature& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("ForLoopSignature", indent, name);

	if (node.opt_init.opt_type.tag != TypeRef::Tag::EMPTY || node.opt_init.opt_value.tag != TopLevelExpr::Tag::EMPTY)
		tree_print(node.opt_init, indent + 1, "opt_init");

	if (node.opt_cond.tag != Expr::Tag::EMPTY)
		tree_print(node.opt_cond, indent + 1, "opt_cond");

	if (node.opt_step.op != Assignment::Op::NONE)
		tree_print(node.opt_step, indent + 1, "opt_step");

	print_end_node(indent);
}

static void tree_print(const ForEachSignature& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("ForEachSignature", indent, name);

	print_scalar("loop_variable", node.loop_variable, indent + 1);

	if (node.opt_step_variable.begin() != nullptr)
		print_scalar("opt_step_variable", node.opt_step_variable, indent + 1);

	tree_print(node.loopee, indent + 1, "loopee");

	print_end_node(indent);
}

static void tree_print(const EnumValue& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("EnumValue", indent, name);

	print_scalar("ident", node.ident, indent + 1);

	if (node.opt_value.tag != Expr::Tag::EMPTY)
		tree_print(node.opt_value, indent + 1, "opt_value");

	print_end_node(indent);
}

static void tree_print(const ProcSignature& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("ProcSignature", indent, name);

	if (node.parameters.size() != 0)
	{
		print_beg_array("parameters", indent + 1);

		for (const Definition& child : node.parameters)
			tree_print(child, indent + 2);

		print_end_array(indent + 1);
	}
	else
	{
		print_scalar("parameters", strview::from_literal("[]"), indent + 1, true);
	}

	if (node.opt_return_type.tag != TypeRef::Tag::EMPTY)
	{
		tree_print(node.opt_return_type, indent + 1, "opt_return_type");
	}

	print_end_node(indent);
}

static void tree_print(const Argument& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Argument", indent, name, true);

	switch (node.tag)
	{
	case Argument::Tag::Type:
		tree_print(*node.type, indent);
		break;

	case Argument::Tag::Expr:
		tree_print(*node.expr, indent);
		break;

	default:
		assert(false);
		break;
	}
}

static void tree_print(const CharLiteral& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("CharLiteral", indent, name, false, true);

	for (usz i = 0; i != 4; ++i)
	{
		if (i != 0)
			print_text(", ");
	
		print_inline_value(node.value[i]);

		if (node.value[i] == '\0')
			break;
	}

	print_end_node(indent, true);
}

static void tree_print(const StringLiteral& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("StringLiteral", indent, name, false, true);

	print_inline_value(strview{ node.value.begin(), node.value.end() });

	print_end_node(indent, true);
}

static void tree_print(const FloatLiteral& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("FloatLiteral", indent, name, false, true);

	print_inline_value(node.value);

	print_end_node(indent, true);
}

static void tree_print(const IntegerLiteral& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("IntegerLiteral", indent, name, false, true);

	print_inline_value(node.value);

	print_end_node(indent, true);
}

static void tree_print(const Statement& node, u32 indent, const char* name) noexcept
{
	print_beg_node("Statement", indent, name, false);

	switch (node.tag)
	{
	case Statement::Tag::If:
		tree_print(*node.if_stmt, indent);
		break;

	case Statement::Tag::For:
		tree_print(*node.for_stmt, indent);
		break;

	case Statement::Tag::Switch:
		tree_print(*node.switch_stmt, indent);
		break;

	case Statement::Tag::Return:
		tree_print(*node.return_or_yield_value, indent, "Return");
		break;

	case Statement::Tag::Yield:
		tree_print(*node.return_or_yield_value, indent, "Yield");
		break;

	case Statement::Tag::Go:
		tree_print(*node.go_stmt, indent);
		break;

	case Statement::Tag::Block:
		tree_print(*node.block, indent);
		break;

	case Statement::Tag::Call:
		tree_print(*node.call, indent);
		break;

	case Statement::Tag::Definition:
		tree_print(*node.definition, indent);
		break;

	case Statement::Tag::Assignment:
		tree_print(*node.assignment, indent);
		break;

	default:
		assert(false);
		break;
	}
}

static void tree_print(const ForSignature& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("ForSignature", indent, name, true);

	switch (node.tag)
	{
	case ForSignature::Tag::ForEachSignature:
		tree_print(node.for_each, indent);
		break;
	
	case ForSignature::Tag::ForLoopSignature:
		tree_print(node.for_loop, indent);
		break;
	
	default:
		assert(false);
		break;
	}
}

static void tree_print(const Case& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Case", indent, name);

	tree_print(node.label, indent + 1, "label");
	
	tree_print(node.body, indent + 1, "body");

	print_end_node(indent);
}

static void tree_print(const Impl& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Impl", indent, name);

	tree_print(node.trait, indent + 1, "trait");

	if (node.definitions.size() != 0)
	{
		print_beg_array("definitions", indent + 1);

		for (const Definition& child : node.definitions)
			tree_print(child, indent + 2);

		print_end_array(indent + 1);
	}
	else
	{
		print_scalar("definitions", strview::from_literal("[]"), indent + 1, true);
	}

	print_end_node(indent);
}

static void tree_print(const Module& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Module", indent, name);

	if (node.definitions.size() != 0)
	{
		print_beg_array("definitions", indent + 1);

		for (const Definition& child : node.definitions)
			tree_print(child, indent + 2);

		print_end_array(indent + 1);
	}
	else
	{
		print_scalar("definitions", strview::from_literal("[]"), indent + 1, true);
	}

	print_end_node(indent);
}

static void tree_print(const Trait& node, u32 indent, const char* name) noexcept
{
	print_beg_node("Trait", indent, name);

	if (node.bindings.size() != 0)
	{
		print_beg_array("bindings", indent + 1);

		for (const Definition& child : node.bindings)
			tree_print(child, indent + 2);

		print_end_array(indent + 1);
	}
	else
	{
		print_scalar("bindings", strview::from_literal("[]"), indent + 1, true);
	}

	if (node.definitions.size() != 0)
	{
		print_beg_array("definitions", indent + 1);

		for (const Definition& child : node.definitions)
			tree_print(child, indent + 2);

		print_end_array(indent + 1);
	}
	else
	{
		print_scalar("definitions", strview::from_literal("[]"), indent + 1, true);
	}

	print_end_node(indent);
}

static void tree_print(const Enum& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Enum", indent, name);

	if (node.opt_enum_type.tag != TypeRef::Tag::EMPTY)
		tree_print(node.opt_enum_type, indent + 1, "opt_enum_type");

	if (node.values.size() != 0)
	{
		print_beg_array("values", indent + 1);

		for (const EnumValue& child : node.values)
			tree_print(child, indent + 2);

		print_end_array(indent + 1);
	}
	else
	{
		print_scalar("values", strview::from_literal("[]"), indent + 1, true);
	}

	if (node.definitions.size() != 0)
	{
		print_beg_array("definitions", indent + 1);

		for (const Definition& child : node.definitions)
			tree_print(child, indent + 2);

		print_end_array(indent + 1);
	}

	print_end_node(indent);
}

static void tree_print(const StructuredType& node, u32 indent, const char* name = nullptr, const char* stereotype = nullptr) noexcept
{
	if (stereotype == nullptr)
		stereotype = "StructuredType";

	print_beg_node(stereotype, indent, name);

	if (node.members.size() != 0)
	{
		print_beg_array("members", indent + 1);

		for (const Definition& child : node.members)
			tree_print(child, indent + 2);

		print_end_array(indent + 1);
	}
	else
	{
		print_scalar("members", strview::from_literal("[]"), indent + 1, true);
	}

	print_end_node(indent);
}

static void tree_print(const Proc& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Proc", indent, name);

	tree_print(node.signature, indent + 1, "signature");

	if (node.opt_body.tag != Statement::Tag::EMPTY)
		tree_print(node.opt_body, indent + 1, "body");

	print_end_node(indent);
}

static void tree_print(const Call& node, u32 indent, const char* name) noexcept
{
	print_beg_node("Call", indent, name);

	tree_print(node.callee, indent + 1, "callee");

	if (node.args.size() != 0)
	{
		print_beg_array("args", indent + 1);

		for (const Argument& child : node.args)
			tree_print(child, indent + 2);

		print_end_array(indent + 1);
	}
	else
	{
		print_scalar("args", strview::from_literal("[]"), indent + 1, true);
	}

	print_end_node(indent);
}

static void tree_print(const BinaryOp& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("BinaryOp", indent, name);

	print_scalar("op", get_name(node.op), indent + 1, true);

	tree_print(node.lhs, indent + 1, "lhs");

	tree_print(node.rhs, indent + 1, "rhs");

	print_end_node(indent);
}

static void tree_print(const UnaryOp& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("UnaryOp", indent, name);

	print_scalar("op", get_name(node.op), indent + 1, true);

	tree_print(node.operand, indent + 1, "operand");

	print_end_node(indent);
}

static void tree_print(const Literal& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Literal", indent, name, true);

	switch (node.tag)
	{
	case Literal::Tag::IntegerLiteral:
		tree_print(node.integer_literal, indent);
		break;

	case Literal::Tag::FloatLiteral:
		tree_print(node.float_literal, indent);
		break;

	case Literal::Tag::StringLiteral:
		tree_print(node.string_literal, indent);
		break;

	case Literal::Tag::CharLiteral:
		tree_print(node.char_literal, indent);
		break;

	default:
		assert(false);
		break;
	}
}

static void tree_print(const strview& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Ident", indent, name, false, true);

	print_inline_value(node);

	print_end_node(indent, true);
}

static void tree_print(const If& node, u32 indent, const char* name) noexcept
{
	print_beg_node("If", indent, name);

	if (node.opt_init.opt_type.tag != TypeRef::Tag::EMPTY || node.opt_init.opt_value.tag != TopLevelExpr::Tag::EMPTY)
		tree_print(node.opt_init, indent + 1, "opt_init");

	tree_print(node.condition, indent + 1, "condition");

	tree_print(node.body, indent + 1, "body");

	if (node.opt_else_body.tag != Statement::Tag::EMPTY)
		tree_print(node.opt_else_body, indent + 1, "opt_else_body");

	print_end_node(indent);
}

static void tree_print(const For& node, u32 indent, const char* name) noexcept
{
	print_beg_node("For", indent, name);

	tree_print(node.signature, indent + 1, "signature");

	tree_print(node.body, indent + 1, "body");

	if (node.opt_until_body.tag != Statement::Tag::EMPTY)
		tree_print(node.opt_until_body, indent + 1, "opt_until_body");

	print_end_node(indent);
}

static void tree_print(const Switch& node, u32 indent, const char* name) noexcept
{
	print_beg_node("Switch", indent, name);

	tree_print(node.switched, indent + 1, "switched");

	print_beg_array("cases", indent + 1);

	for (const Case& child : node.cases)
		tree_print(child, indent + 2);

	print_end_array(indent + 1);

	print_end_node(indent);
}

static void tree_print(const Type& node, u32 indent, const char* name) noexcept
{
	print_beg_node("Type", indent, name, true);

	switch (node.tag)
	{
	case Type::Tag::Proc:
		tree_print(node.proc_type, indent);
		break;

	case Type::Tag::Struct:
		tree_print(node.struct_or_union_type, indent, nullptr, "Struct");
		break;

	case Type::Tag::Union:
		tree_print(node.struct_or_union_type, indent, nullptr, "Union");
		break;

	case Type::Tag::Enum:
		tree_print(node.enum_type, indent);
		break;

	case Type::Tag::Trait:
		tree_print(node.trait_type, indent);
		break;

	case Type::Tag::Module:
		tree_print(node.module_type, indent);
		break;

	case Type::Tag::Impl:
		tree_print(node.impl_type, indent);
		break;

	default:
		assert(false);
		break;
	}
}

static void tree_print(const Expr& node, u32 indent, const char* name) noexcept
{
	print_beg_node("Expr", indent, name, true);

	switch (node.tag)
	{
	case Expr::Tag::Ident:
		
		tree_print(strview{ node.ident_beg, node.ident_len }, indent);
		break;
		
	case Expr::Tag::Literal:
		tree_print(*node.literal, indent);
		break;
		
	case Expr::Tag::UnaryOp:
		tree_print(*node.unary_op, indent);
		break;
		
	case Expr::Tag::BinaryOp:
		tree_print(*node.binary_op, indent);
		break;
		
	case Expr::Tag::Call:
		tree_print(*node.call, indent);
		break;
		
	default:
		assert(false);
		break;
	}
}

static void tree_print(const Array& node, u32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Array", indent, name);

	tree_print(node.elem_cnt, indent + 1, "elem_cnt");

	tree_print(node.elem_type, indent + 1, "elem_type");

	print_end_node(indent);
}

static void tree_print(const TopLevelExpr& node, u32 indent, const char* name) noexcept
{
	print_beg_node("TopLevelExpr", indent, name, true);

	switch (node.tag)
	{
	case TopLevelExpr::Tag::If:
		tree_print(*node.if_stmt, indent);
		break;

	case TopLevelExpr::Tag::For:
		tree_print(*node.for_stmt, indent);
		break;

	case TopLevelExpr::Tag::Switch:
		tree_print(*node.switch_stmt, indent);
		break;

	case TopLevelExpr::Tag::Expr:
		tree_print(*node.expr, indent);
		break;

	case TopLevelExpr::Tag::Type:
		tree_print(*node.type, indent);
		break;
		
	default:
		assert(false);
		break;
	}
}

static void tree_print(const TypeRef& node, u32 indent, const char* name) noexcept
{
	print_beg_node("TypeRef", indent, name);

	switch (node.tag)
	{
	case TypeRef::Tag::Type:
		tree_print(*node.type, indent + 1, "type");
		break;

	case TypeRef::Tag::Expr:
		tree_print(*node.expr, indent + 1, "expr");
		break;

	case TypeRef::Tag::Ref:
		tree_print(*node.ref_or_slice, indent + 1, "ref");
		break;

	case TypeRef::Tag::Slice:
		tree_print(*node.ref_or_slice, indent + 1, "slice");
		break;

	case TypeRef::Tag::Array:
		tree_print(*node.array, indent + 1, "array");
		break;
		
	default:
		assert(false);
		break;
	}

	print_end_node(indent);
}

static void tree_print(const Definition& node, u32 indent, const char* name) noexcept
{
	print_beg_node("Definition", indent, name);

	if (node.opt_ident.begin() != nullptr)
		print_scalar("ident", node.opt_ident, indent + 1);

	print_scalar("is_comptime", node.is_comptime ? "true" : "false", indent + 1);

	if (node.opt_type.tag != TypeRef::Tag::EMPTY)
		tree_print(node.opt_type, indent + 1, "opt_type");

	if (node.opt_value.tag != TopLevelExpr::Tag::EMPTY)
		tree_print(node.opt_value, indent + 1, "opt_value");

	print_end_node(indent);
}

void ast_print_tree(const ProgramUnit& node) noexcept
{	
	print_beg_node("ProgramUnit", 0, nullptr);

	print_beg_array("definitions", 1);

	for (const Definition& definition : node.definitions)
		tree_print(definition, 2);
	
	print_end_array(1);

	print_end_node(0);
}

void ast_print_text(const ProgramUnit& program) noexcept
{
	program;

	fprintf(stderr, "ast_print_text: Not implemented\n");
}

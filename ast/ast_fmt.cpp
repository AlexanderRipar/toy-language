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

static void print_scalar(const char* name, strview value, i32 indent) noexcept
{
	fprintf(stderr, "%*s%s = \"%.*s\"\n", indent * 4, "", name, static_cast<i32>(value.len()), value.begin());
}

static void print_value(strview value, i32 indent) noexcept
{
	fprintf(stderr, "%*s\"%.*s\"\n", indent * 4, "", static_cast<i32>(value.len()), value.begin());
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



static void tree_name_ref(const NameRef& node, i32 indent, const char* name = nullptr) noexcept;

static void tree_definition(const Definition& node, i32 indent, const char* name = nullptr) noexcept;

static void tree_type_ref(const TypeRef& node, i32 indent, const char* name = nullptr) noexcept;

static void tree_expr(const Expr& node, i32 indent, const char* name = nullptr) noexcept;

static void tree_block(const Block& node, i32 indent, const char* name = nullptr) noexcept;

static void tree_variable_def(const VariableDef& node, i32 indent, const char* name = nullptr) noexcept;

static void tree_statement(const Statement& node, i32 indent, const char* name = nullptr) noexcept;

static void tree_call(const Call& node, i32 indent, const char* name = nullptr) noexcept;

static void tree_top_level_expr(const TopLevelExpr& node, i32 indent, const char* name = nullptr) noexcept;



static void tree_assignable_expr(const AssignableExpr& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("AssignableExpr", indent, name, true);

	switch (node.type)
	{
	case AssignableExpr::Type::NameRef:
		tree_name_ref(node.name_ref, indent);
		break;

	case AssignableExpr::Type::Call:
		tree_call(node.call, indent);
		break;

	default:
		assert(false);
		break;
	}
}

static void tree_yield(const Yield& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Yield", indent, name);

	print_beg_array("yield_values", indent + 1);

	for (const TopLevelExpr& top_level_expr : node.yield_values)
		tree_top_level_expr(top_level_expr, indent + 2);

	print_end_array(indent + 1);

	print_end_node(indent);
}

static void tree_return(const Return& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Return", indent, name);

	print_beg_array("return_values", indent + 1);

	for (const TopLevelExpr& top_level_expr : node.return_values)
		tree_top_level_expr(top_level_expr, indent + 2);

	print_end_array(indent + 1);

	print_end_node(indent);
}

static void tree_go(const Go& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Go", indent, name);

	tree_expr(node.label, indent + 1, "label");

	print_end_node(indent);
}

static void tree_case(const Case& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Case", indent, name);

	tree_expr(node.label, indent + 1, "label");

	tree_statement(node.body, indent + 1, "body");

	print_end_node(indent);
}

static void tree_assignment(const Assignment& node, i32 indent, const char* name = nullptr) noexcept
{
	static constexpr const strview op_names[]
	{
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

	const usz op_idx = static_cast<usz>(node.op) - 1;

	assert(op_idx < _countof(op_names));

	print_beg_node("Assignment", indent, name);

	print_scalar("op", op_names[op_idx], indent + 1);

	print_beg_array("assignees", indent + 1);

	for (const AssignableExpr& assignable_expr : node.assignees)
		tree_assignable_expr(assignable_expr, indent + 1);

	print_end_array(indent + 1);

	tree_top_level_expr(node.assigned_value, indent + 1, "assigned_value");

	print_end_node(indent);
}

static void tree_when(const When& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("When", indent, name);

	tree_expr(node.condition, indent + 1, "condition");

	tree_statement(node.body, indent + 1, "body");

	if (node.opt_else_body.type != Statement::Type::EMPTY)
		tree_statement(node.opt_else_body, indent + 1, "opt_else_body");

	print_end_node(indent);
}

static void tree_switch(const Switch& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Switch", indent, name);

	tree_expr(node.switched, indent + 1, "switched");

	print_beg_array("cases", indent + 1);

	for (const Case& case_stmt : node.cases)
		tree_case(case_stmt, indent + 2);

	print_end_array(indent + 1);

	print_end_node(indent);
}

static void tree_for_each(const ForEach& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("ForEach", indent, name);
	
	print_beg_array("indents", indent + 1);

	for (const strview& v : node.idents)
		print_value(v, indent + 2);

	print_end_array(indent + 1);

	tree_expr(node.iterated, indent + 1, "iterated");

	print_end_node(indent);
}

static void tree_for_signature(const ForSignature& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("ForSignature", indent, name, true);

	if (node.type == ForSignature::Type::ForEach)
	{
		tree_for_each(node.for_each, indent);
	}
	else if (node.type == ForSignature::Type::Normal)
	{
		print_beg_node("Normal", indent + 1, nullptr);

		if (node.normal.opt_init.idents.size() != 0)
			tree_variable_def(node.normal.opt_init, indent + 1, "opt_init");

		if (node.normal.opt_condition.type != Expr::Type::EMPTY)
			tree_expr(node.normal.opt_condition, indent + 1, "opt_condition");

		if (node.normal.opt_step.op != Assignment::Op::EMPTY)
			tree_assignment(node.normal.opt_step, indent + 1, "opt_step");

		print_end_node(indent);
	}
}

static void tree_for(const For& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("For", indent, name);

	tree_for_signature(node.signature, indent + 1, "signature");

	tree_statement(node.body, indent + 1, "body");

	if (node.opt_until_body.type != Statement::Type::EMPTY)
		tree_statement(node.opt_until_body, indent + 1, "opt_until_body");

	print_end_node(indent);
}

static void tree_if(const If& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("If", indent, name);

	if (node.opt_init.idents.size() != 0)
		tree_variable_def(node.opt_init, indent + 1, "opt_init");

	tree_expr(node.condition, indent + 1, "condition");

	tree_statement(node.body, indent + 1, "body");

	if (node.opt_else_body.type != Statement::Type::EMPTY)
		tree_statement(node.opt_else_body, indent + 1, "opt_else_body");

	print_end_node(indent);
}

static void tree_statement(const Statement& node, i32 indent, const char* name) noexcept
{
	print_beg_node("Statement", indent, name, true);

	switch (node.type)
	{
	case Statement::Type::Block:
		tree_block(*node.block, indent);
		break;

	case Statement::Type::If:
		tree_if(*node.if_block, indent);
		break;

	case Statement::Type::For:
		tree_for(*node.for_block, indent);
		break;

	case Statement::Type::When:
		tree_when(*node.when_block, indent);
		break;

	case Statement::Type::Switch:
		tree_switch(*node.switch_block, indent);
		break;

	case Statement::Type::Go:
		tree_go(*node.go_stmt, indent);
		break;

	case Statement::Type::Return:
		tree_return(*node.return_stmt, indent);
		break;

	case Statement::Type::Yield:
		tree_yield(*node.yield_stmt, indent);
		break;

	case Statement::Type::VariableDef:
		tree_variable_def(*node.variable_def, indent);
		break;

	case Statement::Type::Assignment:
		tree_assignment(*node.assignment, indent);
		break;

	case Statement::Type::Call:
		tree_call(*node.call, indent);
		break;

	default:
		assert(false);
		break;
	}
}

static void tree_top_level_expr(const TopLevelExpr& node, i32 indent, const char* name) noexcept
{
	print_beg_node("TopLevelExpr", indent, name, true);

	switch (node.type)
	{
	case TopLevelExpr::Type::Expr:
		tree_expr(*node.expr, indent);
		break;

	case TopLevelExpr::Type::If:
		tree_if(*node.if_block, indent);
		break;

	case TopLevelExpr::Type::For:
		tree_for(*node.for_block, indent);
		break;

	case TopLevelExpr::Type::Block:
		tree_block(*node.block, indent);
		break;

	case TopLevelExpr::Type::Switch:
		tree_switch(*node.switch_block, indent);
		break;

	case TopLevelExpr::Type::When:
		tree_when(*node.when_block, indent);
		break;

	default:
		assert(false);
		break;
	}
}

static void tree_variable_def(const VariableDef& node, i32 indent, const char* name) noexcept
{
	print_beg_node("VariableDef", indent, name);

	print_beg_array("idents", indent + 1);

	for (const strview& ident : node.idents)
		print_value(ident, indent + 2);
	
	print_end_array(indent + 1);

	if (node.opt_type_refs.size() != 0)
	{
		print_beg_array("opt_type_refs", indent + 1);

		for (const TypeRef& type_ref : node.opt_type_refs)
			tree_type_ref(type_ref, indent + 2);

		print_end_array(indent + 1);
	}

	if (node.opt_initializer.type != TopLevelExpr::Type::EMPTY)
		tree_top_level_expr(node.opt_initializer, indent + 1, "opt_initializer");	

	print_end_node(indent);
}

static void tree_proc_param(const ProcParam& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("ProcParam", indent, name, true);

	switch (node.type)
	{
	case ProcParam::Type::VariableDef:
		tree_variable_def(node.variable_def, indent);
		break;

	case ProcParam::Type::GenericType:
		print_beg_node("GenericIdent", indent, nullptr);
		
		print_scalar("value", node.generic_type, indent + 1);
		
		print_end_node(indent);
		
		break;
	
	default:
		assert(false);
		break;
	}
}

static void tree_proc_signature(const ProcSignature& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("ProcSignature", indent, name);

	if (node.params.size() != 0)
	{
		print_beg_array("params", indent + 1);

		for (const ProcParam& proc_param : node.params)
			tree_proc_param(proc_param, indent + 2);

		print_end_array(indent + 1);
	}

	if (node.return_types.size() != 0)
	{
		print_beg_array("return_types", indent + 1);

		for (const TypeRef& return_type : node.return_types)
			tree_type_ref(return_type, indent + 2);

		print_end_array(indent + 1);
	}

	print_end_node(indent);
}

static void tree_block(const Block& node, i32 indent, const char* name) noexcept
{
	print_beg_node("Block", indent, name);

	if (node.statements.size() != 0)
	{
		print_beg_array("statements", indent + 1);

		for (const Statement& statement : node.statements)
			tree_statement(statement, indent + 2);

		print_end_array(indent + 1);
	}

	if (node.definitions.size() != 0)
	{
	print_beg_array("definitions", indent + 1);

	for (const Definition& definition : node.definitions)
		tree_definition(definition, indent + 2);

	print_end_array(indent + 1);
	}

	print_end_node(indent);
}

static void tree_integer_literal(const IntegerLiteral& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("IntegerLiteral", indent, name, false, true);

	print_inline_value(node.value);

	print_end_node(indent, true);
}

static void tree_float_literal(const FloatLiteral& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("FloatLiteral", indent, name, false, true);

	print_inline_value(node.value);

	print_end_node(indent, true);
}

static void tree_string_literal(const StringLiteral& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("StringLiteral", indent, name, false, true);

	print_inline_value(strview{ node.value.begin(), node.value.end() });

	print_end_node(indent, true);
}

static void tree_char_literal(const CharLiteral& node, i32 indent, const char* name = nullptr) noexcept
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

static void tree_binary_op(const BinaryOp& node, i32 indent, const char* name = nullptr) noexcept
{
	static constexpr const strview op_names[]
	{
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
	};

	const usz op_idx = static_cast<usz>(node.op) - 1;

	assert(op_idx < _countof(op_names));

	print_beg_node("BinaryOp", indent, name);

	print_scalar("op", op_names[op_idx], indent + 1);

	tree_expr(node.lhs, indent + 1, "lhs");

	tree_expr(node.rhs, indent + 1, "rhs");

	print_end_node(indent);
}

static void tree_unary_op(const UnaryOp& node, i32 indent, const char* name = nullptr) noexcept
{
	static constexpr const strview op_names[]
	{
		strview::from_literal("BitNot"),
		strview::from_literal("LogNot"),
		strview::from_literal("Neg"),
	};

	const usz op_idx = static_cast<usz>(node.op) - 1;

	assert(op_idx < _countof(op_names));

	print_beg_node("UnaryOp", indent, name);

	print_scalar("op", op_names[op_idx], indent + 1);

	tree_expr(node.operand, indent + 1, "operand");

	print_end_node(indent);
}

static void tree_literal(const Literal& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Literal", indent, name, true);

	switch (node.type)
	{
	case Literal::Type::Integer:
		tree_integer_literal(node.integer_literal, indent);
		break;

	case Literal::Type::Float:
		tree_float_literal(node.float_literal, indent);
		break;

	case Literal::Type::String:
		tree_string_literal(node.string_literal, indent);
		break;

	case Literal::Type::Char:
		tree_char_literal(node.char_literal, indent);
		break;

	default:
		assert(false);
		break;
	}
}

static void tree_call(const Call& node, i32 indent, const char* name) noexcept
{
	print_beg_node("Call", indent, name);

	tree_name_ref(node.proc_name_ref, indent + 1, "proc_name_ref");

	if (node.args.size() != 0)
	{
		print_beg_array("args", indent + 1);

		for (const Expr& expr : node.args)
			tree_expr(expr, indent + 2);

		print_end_array(indent + 1);
	}

	print_end_node(indent);
}

static void tree_type_value(const TypeValue& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("TypeValue", indent, name);

	print_scalar("ident", node.ident, indent + 1);

	if (node.value.type != Expr::Type::EMPTY)
		tree_expr(node.value, indent + 1, "value");

	print_end_node(indent);
}

static void tree_type_member(const TypeMember& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("TypeMember", indent, name);

	if (node.opt_ident.begin() != nullptr)
		print_scalar("opt_ident", node.opt_ident, indent + 1);

	if (node.is_pub)
		print_scalar("visibility", strview::from_literal("Public"), indent + 1);
	else
		print_scalar("visibility", strview::from_literal("Private"), indent + 1);

	tree_type_ref(node.type_ref, indent + 1, "type_ref");

	print_end_node(indent);
}

static void tree_expr(const Expr& node, i32 indent, const char* name) noexcept
{
	print_beg_node("Expr", indent, name, true);

	switch (node.type)
	{
	case Expr::Type::BinaryOp: {
		tree_binary_op(*node.binary_op, indent);
		break;
	}

	case Expr::Type::UnaryOp: {
		tree_unary_op(*node.unary_op, indent);
		break;
	}

	case Expr::Type::Literal: {
		tree_literal(*node.literal, indent);
		break;
	}

	case Expr::Type::NameRef: {
		tree_name_ref(*node.name_ref, indent);
		break;
	}

	case Expr::Type::Call: {
		tree_call(*node.call, indent);
		break;
	}

	default: {
		assert(false);
		break;
	}
	}
}

static void tree_type_ref(const TypeRef& node, i32 indent, const char* name) noexcept
{
	print_beg_node("TypeRef", indent, name);

	if (node.mutability == TypeRef::Mutability::Immutable)
		print_scalar("mutability", strview::from_literal("Immutable"), indent + 1);
	else if (node.mutability == TypeRef::Mutability::Mutable)
		print_scalar("mutability", strview::from_literal("Mutable"), indent + 1);
	else if (node.mutability == TypeRef::Mutability::Const)
		print_scalar("mutability", strview::from_literal("Const"), indent + 1);
	else
		assert(false);

	switch (node.type)
	{
	case TypeRef::Type::Ref: {
		tree_type_ref(*node.ref, indent + 1);
		break;
	}

	case TypeRef::Type::NameRef: {
		tree_name_ref(*node.name_ref, indent + 1);
		break;
	}

	case TypeRef::Type::Inline: {
		tree_definition(*node.inline_def, indent + 1);
		break;
	}

	case TypeRef::Type::TypeExpr: {
		tree_expr(*node.type_expr, indent + 1);
		break;
	}

	default: {
		assert(false);
		break;
	}
	}

	print_end_node(indent);
}

static void tree_type_name(const TypeName& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("TypeName", indent, name);

	print_scalar("name", node.name, indent + 1);

	if (node.bounds.size() != 0)
	{
		print_beg_array("bounds", indent + 1);

		for (const Expr& expr : node.bounds)
			tree_expr(expr, indent + 2);

		print_end_array(indent + 1);
	}

	print_end_node(indent);
}

static void tree_name_ref(const NameRef& node, i32 indent, const char* name) noexcept
{
	print_beg_node("NameRef", indent, name);

	print_beg_array("parts", indent + 1);

	for (const TypeName& type_name : node.parts)
		tree_type_name(type_name, indent + 2);

	print_end_array(indent + 1);

	print_end_node(indent);
}

static void tree_type_binding_constraint(const TypeBindingConstraint& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("TypeBindingConstraint", indent, name);

	tree_name_ref(node.bound_trait, indent + 1, "bound_trait");

	print_end_node(indent);
}

static void tree_value_binding(const ValueBinding& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("ValueBinding", indent, name);

	tree_type_ref(node.type_ref, indent + 1, "type_ref");

	print_end_node(indent);
}

static void tree_type_binding(const TypeBinding& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("TypeBinding", indent, name);

	if (node.constraints.size() != 0)
	{
		print_beg_array("contraints", indent + 1);

		for (const TypeBindingConstraint& type_binding_constraint : node.constraints)
			tree_type_binding_constraint(type_binding_constraint, indent + 2);

		print_end_array(indent + 1);
	}

	print_end_node(indent);
}

static void tree_proc_def(const ProcDef& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("ProcDef", indent, name);

	tree_proc_signature(node.signature, indent + 1, "signature");

	tree_block(node.body, indent + 1, "body");

	print_end_node(indent);
}

static void tree_struct_def(const StructDef& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("StructDef", indent, name);

	if (node.members.size() != 0)
	{
		print_beg_array("members", indent + 1);

		for (const TypeMember& type_member : node.members)
			tree_type_member(type_member, indent + 2);

		print_end_array(indent + 1);
	}

	if (node.definitions.size() != 0)
	{
		print_beg_array("definitions", indent + 1);

		for (const Definition& definition : node.definitions)
			tree_definition(definition, indent + 2);

		print_end_array(indent + 1);
	}

	print_end_node(indent);
}

static void tree_union_def(const UnionDef& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("UnionDef", indent, name);

	if (node.members.size() != 0)
	{
		print_beg_array("members", indent + 1);

		for (const TypeMember& type_member : node.members)
			tree_type_member(type_member, indent + 2);

		print_end_array(indent + 1);
	}

	if (node.definitions.size() != 0)
	{
		print_beg_array("definitions", indent + 1);
		
		for (const Definition& definition : node.definitions)
			tree_definition(definition, indent + 2);

		print_end_array(indent + 1);
	}

	print_end_node(indent);
}

static void tree_enum_def(const EnumDef& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("EnumDef", indent, name);

	if (node.enum_type.type != TypeRef::Type::EMPTY)
		tree_type_ref(node.enum_type, indent + 1, "enum_type");

	if (node.values.size() != 0)
	{
		print_beg_array("values", indent + 1);

		for (const TypeValue& type_member : node.values)
			tree_type_value(type_member, indent + 2);

		print_end_array(indent + 1);
	}

	if (node.definitions.size() != 0)
	{
		print_beg_array("definitions", indent + 1);

		for (const Definition& definition : node.definitions)
			tree_definition(definition, indent + 2);

		print_end_array(indent + 1);
	}

	print_end_node(indent);
}

static void tree_bitset_def(const BitsetDef& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("BitsetDef", indent, name);

	if (node.bitset_type.type != TypeRef::Type::EMPTY)
		tree_type_ref(node.bitset_type, indent + 1, "bitset_type");

	if (node.values.size() != 0)
	{
		print_beg_array("values", indent + 1);

		for (const TypeValue& type_member : node.values)
			tree_type_value(type_member, indent + 2);

		print_end_array(indent + 1);
	}

	if (node.definitions.size() != 0)
	{
		print_beg_array("definitions", indent + 1);

		for (const Definition& definition : node.definitions)
			tree_definition(definition, indent + 2);

		print_end_array(indent + 1);
	}

	print_end_node(indent);
}

static void tree_alias_def(const AliasDef& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("AliasDef", indent, name);

	tree_type_ref(node.type_ref, indent + 1, "type_ref");

	print_end_node(indent);
}

static void tree_newtype_def(const NewTypeDef& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("NewTypeDef", indent, name);

	tree_type_ref(node.type_ref, indent + 1, "type_ref");

	print_end_node(indent);
}

static void tree_trait_def(const TraitDef& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("TraitDef", indent, name);

	// TODO

	node;

	print_end_node(indent);
}

static void tree_impl_def(const ProcDef& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("ImplDef", indent, name);

	// TODO

	node;

	print_end_node(indent);
}

static void tree_annotation_def(const AnnotationDef& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("AnnotationDef", indent, name);

	// TODO

	node;

	print_end_node(indent);
}

static void tree_module_def(const ModuleDef& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("ModuleDef", indent, name);

	if (node.definitions.size() != 0)
	{
		print_beg_array("definitions", indent + 1);

		for (const Definition& definition : node.definitions)
			tree_definition(definition, indent + 2);

		print_end_array(indent + 1);
	}

	print_end_node(indent);
}

static void tree_binding(const Binding& node, i32 indent, const char* name = nullptr) noexcept
{
	print_beg_node("Binding", indent, name);

	print_scalar("ident", node.ident, indent + 1);

	switch (node.type)
	{
	case Binding::Type::TypeBinding: {
		tree_type_binding(node.type_binding, indent + 1);
		break;
	}

	case Binding::Type::ValueBinding: {
		tree_value_binding(node.value_binding, indent + 1);
		break;
	}

	default: {
		assert(false);
		break;
	}
	}

	print_end_node(indent);
}

static void tree_definition(const Definition& node, i32 indent, const char* name) noexcept
{
	print_beg_node("Definition", indent, name);

	if (node.flags.has_ident)
		print_scalar("ident", node.ident, indent + 1);

	if (node.bindings.size() != 0)
	{
		print_beg_array("bindings", indent + 1);

		for (const Binding& binding : node.bindings)
			tree_binding(binding, indent + 2);

		print_end_array(indent + 1);
	}

	if (node.flags.is_pub)
		print_scalar("visibility", strview::from_literal("Public"), indent + 1);
	else
		print_scalar("visibility", strview::from_literal("Private"), indent + 1);
	
	switch (node.type)
	{
	case Definition::Type::Proc: {
		tree_proc_def(node.proc_def, indent + 1);
		break;
		}

	case Definition::Type::Struct: {
		tree_struct_def(node.struct_def, indent + 1);
		break;
		}

	case Definition::Type::Union: {
		tree_union_def(node.union_def, indent + 1);
		break;
		}

	case Definition::Type::Enum: {
		tree_enum_def(node.enum_def, indent + 1);
		break;
		}

	case Definition::Type::Bitset: {
		tree_bitset_def(node.bitset_def, indent + 1);
		break;
		}

	case Definition::Type::Alias: {
		tree_alias_def(node.alias_def, indent + 1);
		break;
		}

	case Definition::Type::NewType: {
		tree_newtype_def(node.newtype_def, indent + 1);
		break;
		}

	case Definition::Type::Trait: {
		tree_trait_def(node.trait_def, indent + 1);
		break;
		}

	case Definition::Type::Impl: {
		tree_impl_def(node.impl_def, indent + 1);
		break;
		}

	case Definition::Type::Annotation: {
		tree_annotation_def(node.annotation_def, indent + 1);
		break;
		}

	case Definition::Type::Module: {
		tree_module_def(node.module_def, indent + 1);
		break;
		}
	
	default: {
		assert(false);
		break;
	}
	}

	print_end_node(indent);
}

void ast_print_tree(const ProgramUnit& program) noexcept
{
	program;

	fprintf(stderr, "<program-tree>\n");

	for (const Definition& definition : program.definitions)
		tree_definition(definition, 1);

	fprintf(stderr, "</program-tree>\n");
}

void ast_print_text(const ProgramUnit& program) noexcept
{
	program;

	fprintf(stderr, "ast_print_text: Not implemented\n");
}

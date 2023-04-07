#include "ast_data_structure.hpp"

#include <cassert>

template<typename T>
static void obj_move(T* dst, T* src) noexcept
{
	memcpy(dst, src, sizeof(T));

	obj_zero(src);
}

template<typename T>
static void obj_zero(T* dst) noexcept
{
	memset(dst, 0, sizeof(T));
}



static void statement_cleanup_helper(Statement& stmt) noexcept
{
	if (stmt.assignment == nullptr)
		return;

	switch (stmt.tag)
	{
	case Statement::Tag::Block:
		stmt.block->~Block();
		break;

	case Statement::Tag::If:
		stmt.if_block->~If();
		break;

	case Statement::Tag::For:
		stmt.for_block->~For();
		break;

	case Statement::Tag::Switch:
		stmt.switch_block->~Switch();
		break;

	case Statement::Tag::VariableDef:
		stmt.variable_def->~VariableDef();
		break;

	case Statement::Tag::Assignment:
		stmt.assignment->~Assignment();
		break;

	case Statement::Tag::Call:
		stmt.call->~Name();
		break;

	case Statement::Tag::Go:
		stmt.go_stmt->~Go();
		break;

	case Statement::Tag::Return:
		stmt.return_stmt->~Return();
		break;

	case Statement::Tag::Yield:
		stmt.yield_stmt->~Yield();
		break;

	default:
		assert(stmt.tag == Statement::Tag::EMPTY);
		break;
	}

	free(stmt.assignment);
}

static void typeref_cleanup_helper(TypeRef& typeref) noexcept
{
	if (typeref.ref == nullptr)
		return;

	switch (typeref.tag)
	{
	case TypeRef::Tag::Ref:
		typeref.ref->~TypeRef();
		break;

	case TypeRef::Tag::Name:
		typeref.name_ref->~Name();
		break;

	case TypeRef::Tag::Inline:
		typeref.inline_def->~Type();
		break;

	case TypeRef::Tag::TypeExpr:
		typeref.type_expr->~Expr();
		break;

	default:
		assert(typeref.tag == TypeRef::Tag::EMPTY);
		break;
	}

	free(typeref.ref);
}

static void traitmember_cleanup_helper(TraitMember& traitmember) noexcept
{
	if (traitmember.signature == nullptr)
		return;

	switch (traitmember.tag)
	{
	case TraitMember::Tag::Type:
		traitmember.definition->~Type();
		break;

	case TraitMember::Tag::ProcSignature:
		traitmember.signature->~ProcSignature();
		break;

	default :
		assert(traitmember.tag == TraitMember::Tag::EMPTY);
		break;
	}

	free(traitmember.signature);
}

static void expr_cleanup_helper(Expr& expr) noexcept
{
	if (expr.binary_op == nullptr)
		return;

	switch (expr.tag)
	{
	case Expr::Tag::BinaryOp:
		expr.binary_op->~BinaryOp();
		break;

	case Expr::Tag::UnaryOp:
		expr.unary_op->~UnaryOp();
		break;

	case Expr::Tag::Literal:
		expr.literal->~Literal();
		break;

	case Expr::Tag::Name:
		expr.name_ref->~Name();
		break;

	default:
		assert(expr.tag == Expr::Tag::EMPTY);
		break;
	}

	free(expr.binary_op);
}

static void toplevelexpr_cleanup_helper(TopLevelExpr& tl_expr) noexcept
{
	if (tl_expr.opt_catch != nullptr)
	{
		tl_expr.opt_catch->~Catch();

		free(tl_expr.opt_catch);
	}

	if (tl_expr.block == nullptr)
		return;

	switch (tl_expr.tag)
	{
	case TopLevelExpr::Tag::Expr: {
		tl_expr.expr->~Expr();
		break;
	}

	case TopLevelExpr::Tag::If: {
		tl_expr.if_block->~If();
		break;
	}

	case TopLevelExpr::Tag::For: {
		tl_expr.for_block->~For();
		break;
	}

	case TopLevelExpr::Tag::Block: {
		tl_expr.block->~Block();
		break;
	}

	case TopLevelExpr::Tag::Switch: {
		tl_expr.switch_block->~Switch();
		break;
	}

	default: {
		assert(tl_expr.tag == TopLevelExpr::Tag::EMPTY);
		break;
	}
	}

	free(tl_expr.block);
}

static void definition_cleanup_helper(Type& definition) noexcept
{
	switch (definition.tag)
	{
	case Type::Tag::Proc:
		definition.proc_def.~ProcDef();
		break;

	case Type::Tag::Struct:
		definition.struct_def.~StructDef();
		break;

	case Type::Tag::Union:
		definition.union_def.~UnionDef();
		break;

	case Type::Tag::Enum:
		definition.enum_def.~EnumDef();
		break;

	case Type::Tag::Bitset:
		definition.bitset_def.~BitsetDef();
		break;

	case Type::Tag::Alias:
		definition.alias_def.~AliasDef();
		break;

	case Type::Tag::NewType:
		definition.newtype_def.~NewTypeDef();

	case Type::Tag::Trait:
		definition.trait_def.~TraitDef();
		break;

	case Type::Tag::Impl:
		definition.impl_def.~ProcDef();
		break;

	case Type::Tag::Annotation:
		definition.annotation_def.~AnnotationDef();
		break;

	case Type::Tag::Module:
		definition.module_def.~ModuleDef();
		break;

	default:
		assert(definition.tag == Type::Tag::EMPTY);
		break;
	}
}

static void proc_param_cleanup_helper(ProcParam& obj) noexcept
{
	switch (obj.tag)
	{
	case ProcParam::Tag::VariableDef:
		obj.variable_def.~VariableDef();
		break;
	
	case ProcParam::Tag::GenericType:
		// noop
		break;

	default:
		assert(obj.tag == ProcParam::Tag::EMPTY);
	}
}

static void for_signature_cleanup_helper(ForSignature& obj) noexcept
{
	switch (obj.tag)
	{
	case ForSignature::Tag::Normal:
		obj.normal.opt_init.~VariableDef();
		obj.normal.opt_condition.~Expr();
		obj.normal.opt_step.~Assignment();
		break;

	case ForSignature::Tag::ForEach:
		obj.for_each.~ForEach();
		break;

	default:
		assert(obj.tag == ForSignature::Tag::EMPTY);
		break;
	}
}


Literal::~Literal() noexcept
{
	switch (tag)
	{
	case Tag::Integer:
		integer_literal.~IntegerLiteral();
		break;

	case Tag::Float:
		float_literal.~FloatLiteral();
		break;

	case Tag::String:
		string_literal.~StringLiteral();
		break;

	case Tag::Char:
		char_literal.~CharLiteral();
		break;

	default:
		assert(false);
		break;
	}
}

Statement::~Statement() noexcept
{
	statement_cleanup_helper(*this);
}

TypeRef::~TypeRef() noexcept
{
	typeref_cleanup_helper(*this);
}

TraitMember::~TraitMember() noexcept
{
	traitmember_cleanup_helper(*this);
}

Expr::~Expr() noexcept
{
	expr_cleanup_helper(*this);
}

TopLevelExpr::~TopLevelExpr() noexcept
{
	toplevelexpr_cleanup_helper(*this);
}

Type::~Type() noexcept
{
	definition_cleanup_helper(*this);
}

ProcParam::~ProcParam() noexcept
{
	proc_param_cleanup_helper(*this);
}

ForSignature::~ForSignature() noexcept
{
	for_signature_cleanup_helper(*this);
}


Type::Type() noexcept
{
	memset(this, 0, sizeof(*this));
}



TypeValue& TypeValue::operator=(TypeValue&& o) noexcept
{
	ident = std::move(o.ident);

	value = std::move(o.value);

	return *this;
}

VariableDef& VariableDef::operator=(VariableDef&& o) noexcept
{
	ident = std::move(o.ident);
	
	opt_type_ref = std::move(o.opt_type_ref);

	opt_initializer = std::move(o.opt_initializer);

	return *this;
}

TypeName& TypeName::operator=(TypeName&& o) noexcept
{
	name = std::move(o.name);

	bounds = std::move(o.bounds);

	return *this;
}

Name& Name::operator=(Name&& o) noexcept
{
	parts = std::move(o.parts);

	return *this;
}

Case& Case::operator=(Case&& o) noexcept
{
	label = std::move(o.label);

	body = std::move(o.body);

	return *this;
}

Type& Type::operator=(Type&& o) noexcept
{
	definition_cleanup_helper(*this);

	obj_move(this, &o);

	return *this;
}

TopLevelExpr& TopLevelExpr::operator=(TopLevelExpr&& o) noexcept
{
	toplevelexpr_cleanup_helper(*this);

	obj_move(this, &o);

	return *this;
}

Expr& Expr::operator=(Expr&& o) noexcept
{
	expr_cleanup_helper(*this);

	obj_move(this, &o);

	return *this;
}

ProcParam& ProcParam::operator=(ProcParam&& o) noexcept
{
	proc_param_cleanup_helper(*this);

	obj_move(this, &o);

	return *this;
}

ForSignature& ForSignature::operator=(ForSignature&& o) noexcept
{
	for_signature_cleanup_helper(*this);

	obj_move(this, &o);

	return *this;
}

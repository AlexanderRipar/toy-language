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

template<typename T>
static T& move_helper(T* dst, T* src) noexcept
{
	cleanup_helper(*dst);

	obj_move(dst, src);

	return *dst;
}

static void cleanup_helper(Literal& obj) noexcept
{
	switch (obj.tag)
	{
	case Literal::Tag::IntegerLiteral:
		obj.integer_literal.~IntegerLiteral();
		break;

	case Literal::Tag::FloatLiteral:
		obj.float_literal.~FloatLiteral();
		break;

	case Literal::Tag::StringLiteral:
		obj.string_literal.~StringLiteral();
		break;

	case Literal::Tag::CharLiteral:
		obj.char_literal.~CharLiteral();
		break;

	default:
		assert(false);
		break;
	}
}

static void cleanup_helper(Expr& obj) noexcept
{
	if (obj.literal == nullptr)
	{
		assert(obj.tag == Expr::Tag::EMPTY);

		return;
	}

	switch (obj.tag)
	{
	case Expr::Tag::Ident:
		return; // Nothing to free :)

	case Expr::Tag::Literal:
		obj.literal->~Literal();
		break;

	case Expr::Tag::UnaryOp:
		obj.unary_op->~UnaryOp();
		break;

	case Expr::Tag::BinaryOp:
		obj.binary_op->~BinaryOp();
		break;

	case Expr::Tag::Call:
		obj.call->~Call();
		break;

	default:
		assert(false);
		break;
	}

	free(obj.literal);
}

static void cleanup_helper(Argument& obj) noexcept
{
	if (obj.type == nullptr)
	{
		assert(obj.tag == Argument::Tag::EMPTY);

		return;
	}

	switch (obj.tag)
	{
	case Argument::Tag::Type:
		obj.type->~Type();
		break;

	case Argument::Tag::Expr:
		obj.expr->~Expr();
		break;

	default:
		assert(false);
		break;
	}

	free(obj.type);
}

static void cleanup_helper(TypeRef& obj) noexcept
{
	if (obj.type == nullptr)
	{
		assert(obj.tag == TypeRef::Tag::EMPTY);

		return;
	}

	switch (obj.tag)
	{
	case TypeRef::Tag::Type:
		obj.type->~Type();
		break;

	case TypeRef::Tag::Expr:
		obj.expr->~Expr();
		break;

	case TypeRef::Tag::Ptr:
	case TypeRef::Tag::MultiPtr:
	case TypeRef::Tag::Slice:
		obj.ptr_or_multiptr_or_slice->~TypeRef();
		break;

	case TypeRef::Tag::Array:
		obj.array->~Array();
		break;

	default:
		assert(false);
		break;
	}

	free(obj.type);
}

static void cleanup_helper(TopLevelExpr& obj) noexcept
{
	if (obj.block_expr == nullptr)
	{
		assert(obj.tag == TopLevelExpr::Tag::EMPTY || obj.tag == TopLevelExpr::Tag::Undefined);

		return;
	}

	switch (obj.tag)
	{
	case TopLevelExpr::Tag::Block:
		obj.block_expr->~Block();
		break;

	case TopLevelExpr::Tag::If:
		obj.if_expr->~If();
		break;

	case TopLevelExpr::Tag::For:
		obj.for_expr->~For();
		break;

	case TopLevelExpr::Tag::Switch:
		obj.switch_expr->~Switch();
		break;

	case TopLevelExpr::Tag::Catch:
		obj.catch_expr->~Catch();
		break;

	case TopLevelExpr::Tag::Try:
	case TopLevelExpr::Tag::Expr:
		obj.simple_or_try_expr->~Expr();
		break;

	case TopLevelExpr::Tag::Type:
		obj.type_expr->~Type();
		break;

	default:
		assert(false);
		break;
	}

	free(obj.if_expr);
}

static void cleanup_helper(Statement& obj) noexcept
{
	if (obj.if_stmt == nullptr)
	{
		assert(obj.tag == Statement::Tag::EMPTY || obj.tag == Statement::Tag::Undefined);

		return;
	}

	switch (obj.tag)
	{
	case Statement::Tag::If:
		obj.if_stmt->~If();
		break;

	case Statement::Tag::For:
		obj.for_stmt->~For();
		break;

	case Statement::Tag::Switch:
		obj.switch_stmt->~Switch();
		break;

	case Statement::Tag::Return:
	case Statement::Tag::Yield:
	case Statement::Tag::Break:
		obj.return_or_yield_or_break_value->~TopLevelExpr();
		break;

	case Statement::Tag::Block:
		obj.block->~Block();
		break;

	case Statement::Tag::Call:
		obj.call->~Call();
		break;

	case Statement::Tag::Definition:
		obj.definition->~Definition();
		break;

	case Statement::Tag::Assignment:
		obj.assignment->~Assignment();
		break;

	case Statement::Tag::Defer:
		obj.deferred_stmt->~Statement();
		break;

	default:
		assert(false);
		break;
	}

	free(obj.if_stmt);
}

static void cleanup_helper(ForSignature& obj) noexcept
{
	switch (obj.tag)
	{
	case ForSignature::Tag::ForEachSignature:
		obj.for_each.~ForEachSignature();
		break;

	case ForSignature::Tag::ForLoopSignature:
		obj.for_loop.~ForLoopSignature();
		break;

	default:
		assert(obj.tag == ForSignature::Tag::EMPTY);
		break;
	}
}

static void cleanup_helper(Type& obj) noexcept
{
	switch (obj.tag)
	{
	case Type::Tag::Proc:
		obj.proc_type.~Proc();
		break;

	case Type::Tag::Union:
	case Type::Tag::Struct:
		obj.struct_or_union_type.~StructuredType();
		break;

	case Type::Tag::Enum:
		obj.enum_type.~Enum();
		break;

	case Type::Tag::Trait:
		obj.trait_type.~Trait();
		break;

	case Type::Tag::Module:
		obj.module_type.~Module();
		break;

	case Type::Tag::Impl:
		obj.impl_type.~Impl();
		break;

	default:
		assert(obj.tag == Type::Tag::EMPTY);
		break;
	}
}



Literal::~Literal() noexcept
{
	cleanup_helper(*this);
}

Expr::~Expr() noexcept
{
	cleanup_helper(*this);
}

Argument::~Argument() noexcept
{
	cleanup_helper(*this);
}

TypeRef::~TypeRef() noexcept
{
	cleanup_helper(*this);
}

TopLevelExpr::~TopLevelExpr() noexcept
{
	cleanup_helper(*this);
}

Statement::~Statement() noexcept
{
	cleanup_helper(*this);
}

ForSignature::~ForSignature() noexcept
{
	cleanup_helper(*this);
}

Type::~Type() noexcept
{
	cleanup_helper(*this);
}



StringLiteral& StringLiteral::operator=(StringLiteral&& o) noexcept
{
	value = std::move(o.value);

	return *this;
}

Literal& Literal::operator=(Literal&& o) noexcept
{
	return move_helper(this, &o);
}

Expr& Expr::operator=(Expr&& o) noexcept
{
	return move_helper(this, &o);
}

Call& Call::operator=(Call&& o) noexcept
{
	callee = std::move(o.callee);

	args = std::move(o.args);

	return *this;
}

Argument& Argument::operator=(Argument&& o) noexcept
{
	return move_helper(this, &o);
}

TypeRef& TypeRef::operator=(TypeRef&& o) noexcept
{
	return move_helper(this, &o);
}

Array& Array::operator=(Array&& o) noexcept
{
	elem_type = std::move(o.elem_type);

	elem_cnt = std::move(o.elem_cnt);

	return *this;
}

BinaryOp& BinaryOp::operator=(BinaryOp&& o) noexcept
{
	op = o.op;

	o.op = Op::NONE;

	lhs = std::move(o.lhs);

	rhs = std::move(o.rhs);

	return *this;
}

UnaryOp& UnaryOp::operator=(UnaryOp&& o) noexcept
{
	op = o.op;

	o.op = Op::NONE;

	operand = std::move(o.operand);

	return *this;
}

Catch& Catch::operator=(Catch&& o) noexcept
{
	caught_expr = std::move(o.caught_expr);

	opt_error_ident = std::move(o.opt_error_ident);

	stmt = std::move(o.stmt);

	return *this;
}

TopLevelExpr& TopLevelExpr::operator=(TopLevelExpr&& o) noexcept
{
	return move_helper(this, &o);
}

Definition& Definition::operator=(Definition&& o) noexcept
{
	is_comptime = std::move(o.is_comptime);

	opt_ident = std::move(o.opt_ident);

	opt_type = std::move(o.opt_type);

	opt_value = std::move(o.opt_value);

	return *this;
}

EnumValue& EnumValue::operator=(EnumValue&& o) noexcept
{
	ident = std::move(o.ident);

	opt_value = std::move(o.opt_value);

	return *this;
}

Statement& Statement::operator=(Statement&& o) noexcept
{
	return move_helper(this, &o);
}

Assignment& Assignment::operator=(Assignment&& o) noexcept
{
	op = o.op;

	o.op = Op::NONE;

	assignee = std::move(o.assignee);

	value = std::move(o.value);

	return *this;
}

If& If::operator=(If&& o) noexcept
{
	opt_init = std::move(o.opt_init);

	condition = std::move(o.condition);

	body = std::move(o.body);

	opt_else_body = std::move(o.opt_else_body);

	return *this;
}

ForEachSignature& ForEachSignature::operator=(ForEachSignature&& o) noexcept
{
	loop_variable = std::move(o.loop_variable);

	opt_step_variable = std::move(o.opt_step_variable);

	loopee = std::move(o.loopee);

	return *this;
}

ForLoopSignature& ForLoopSignature::operator=(ForLoopSignature&& o) noexcept
{
	opt_init = std::move(o.opt_init);

	opt_cond = std::move(o.opt_cond);

	opt_step = std::move(o.opt_step);

	return *this;
}

ForSignature& ForSignature::operator=(ForSignature&& o) noexcept
{
	return move_helper(this, &o);
}

For& For::operator=(For&& o) noexcept
{
	signature = std::move(o.signature);

	body = std::move(o.body);

	opt_until_body = std::move(o.opt_until_body);

	return *this;
}

Case& Case::operator=(Case&& o) noexcept
{
	label = std::move(o.label);

	body = std::move(o.body);

	return *this;
}

Switch& Switch::operator=(Switch&& o) noexcept
{
	switched = std::move(o.switched);

	cases = std::move(o.cases);

	return *this;
}

Impl& Impl::operator=(Impl&& o) noexcept
{
	trait = std::move(o.trait);

	definitions = std::move(o.definitions);

	return *this;
}

Block& Block::operator=(Block&& o) noexcept
{
	statements = std::move(o.statements);

	return *this;
}

ProcSignature& ProcSignature::operator=(ProcSignature&& o) noexcept
{
	parameters = std::move(o.parameters);

	opt_return_type = std::move(o.opt_return_type);

	return *this;
}

Proc& Proc::operator=(Proc&& o) noexcept
{
	signature = std::move(o.signature);

	opt_body = std::move(o.opt_body);

	return *this;
}

StructuredType& StructuredType::operator=(StructuredType&& o) noexcept
{
	members = std::move(o.members);

	return *this;
}

Enum& Enum::operator=(Enum&& o) noexcept
{
	opt_enum_type = std::move(o.opt_enum_type);

	values = std::move(o.values);

	definitions = std::move(o.definitions);

	return *this;
}

Trait& Trait::operator=(Trait&& o) noexcept
{
	bindings = std::move(o.bindings);

	definitions = std::move(o.definitions);

	return *this;
}

Module& Module::operator=(Module&& o) noexcept
{
	definitions = std::move(o.definitions);

	return *this;
}

Type& Type::operator=(Type&& o) noexcept
{
	return move_helper(this, &o);
}

ProgramUnit& ProgramUnit::operator=(ProgramUnit&& o) noexcept
{
	definitions = std::move(o.definitions);

	return *this;
}

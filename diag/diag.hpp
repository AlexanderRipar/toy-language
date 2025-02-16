#ifndef DIAG_INCLUDE_GUARD
#define DIAG_INCLUDE_GUARD

#include <cstdio>

#include "../infra/common.hpp"
#include "../ast2.hpp"
#include "../pass_data.hpp"

namespace diag
{
	void print_ast(FILE* out, IdentifierPool* identifiers, AstNode* root) noexcept;	

	void print_type(FILE* out, IdentifierPool* identifiers, TypePool* types, TypeId type_id) noexcept;
}

#endif // DIAG_INCLUDE_GUARD

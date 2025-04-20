#ifndef DIAG_INCLUDE_GUARD
#define DIAG_INCLUDE_GUARD

#include <cstdio>

#include "../infra/common.hpp"
#include "../core/pass_data.hpp"

namespace diag
{
	void print_ast(FILE* out, IdentifierPool* identifiers, AstNode* root) noexcept;
}

#endif // DIAG_INCLUDE_GUARD

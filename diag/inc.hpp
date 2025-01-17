#ifndef DIAG_INCLUDE_GUARD
#define DIAG_INCLUDE_GUARD

#include <cstdio>

#include "../infra/common.hpp"
#include "../pass/ast.hpp"
#include "../pass/pass_data.hpp"

namespace diag
{
	void print_ast(FILE* out, const ast::Tree* tree, const Globals* data) noexcept;	
}

#endif // DIAG_INCLUDE_GUARD

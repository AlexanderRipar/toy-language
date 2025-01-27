#ifndef DIAG_INCLUDE_GUARD
#define DIAG_INCLUDE_GUARD

#include <cstdio>

#include "../infra/common.hpp"
#include "../ast2.hpp"
#include "../pass_data.hpp"

namespace diag
{
	void print_ast(FILE* out, a2::Node* root) noexcept;	
}

#endif // DIAG_INCLUDE_GUARD

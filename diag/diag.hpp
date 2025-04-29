#ifndef DIAG_INCLUDE_GUARD
#define DIAG_INCLUDE_GUARD

#include <cstdio>
#include <cstdarg>

#include "../infra/common.hpp"
#include "../core/pass_data.hpp"

namespace diag
{
	struct PrintContext
	{
		minos::FileHandle file;

		char8* curr;

		char8 buf[8192];
	};

	void buf_flush(PrintContext* ctx) noexcept;

	void buf_printf(PrintContext* ctx, const char8* format, ...) noexcept;

	void print_ast(minos::FileHandle out, IdentifierPool* identifiers, AstNode* root, Range<char8> source) noexcept;

	void print_type(minos::FileHandle out, IdentifierPool* identifiers, TypePool* types, TypeId type_id, const SourceLocation* source) noexcept;
}

#endif // DIAG_INCLUDE_GUARD

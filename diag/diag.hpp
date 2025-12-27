#ifndef DIAG_INCLUDE_GUARD
#define DIAG_INCLUDE_GUARD

#include "../infra/common.hpp"
#include "../core/core.hpp"

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

	void print_header(minos::FileHandle out, const char8*, ...) noexcept;

	void print_ast(minos::FileHandle out, IdentifierPool* identifiers, AstNode* root) noexcept;

	void print_opcodes(minos::FileHandle out, IdentifierPool* identifiers, OpcodePool* opcodes, const Opcode* code, bool follow_refs) noexcept;

	void print_type(minos::FileHandle out, IdentifierPool* identifiers, TypePool* types, TypeId type_id) noexcept;
}

#endif // DIAG_INCLUDE_GUARD

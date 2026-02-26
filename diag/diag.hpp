#ifndef DIAG_INCLUDE_GUARD
#define DIAG_INCLUDE_GUARD

#include "../core/core.hpp"
#include "../infra/print/print.hpp"

namespace diag
{
	s64 print_ast(PrintSink sink, IdentifierPool* identifiers, AstNode* root) noexcept;

	void print_config(PrintSink sink, const Config* config) noexcept;

	void print_config_help(PrintSink sink, u32 depth = 0) noexcept;

	s64 print_opcodes(PrintSink sink, CoreData* core, const Opcode* code, bool follow_refs) noexcept;

	s64 print_type(PrintSink sink, CoreData* core, TypeId type_id) noexcept;

	template<typename Sink, typename... Inserts>
	s64 print_header(Sink sink, const char8* format, Inserts... inserts) noexcept
	{
		const s64 begin_written = print(sink, "### ");

		if (begin_written < 0)
			return -1;

		const s64 message_written = print(sink, format, inserts...);

		if (message_written < 0)
			return -1;

		const s64 end_written = print(sink, " ###\n");

		if (end_written < 0)
			return -1;

		return begin_written + message_written + end_written;
	}

	template<typename Sink>
	s64 print_ast(Sink sink, IdentifierPool* identifiers, AstNode* root) noexcept
	{
		return print_ast(print_make_sink(sink), identifiers, root);
	}

	template<typename Sink>
	void print_config(Sink sink, const Config* config) noexcept
	{
		print_config(print_make_sink(sink), config);
	}

	template<typename Sink>
	void print_config_help(Sink sink, u32 depth = 0) noexcept
	{
		print_config_help(print_make_sink(sink), depth);
	}

	template<typename Sink>
	s64 print_opcodes(Sink sink, CoreData* core, const Opcode* code, bool follow_refs) noexcept
	{
		return print_opcodes(print_make_sink(sink), core, code, follow_refs);
	}

	template<typename Sink>
	s64 print_type(Sink sink, CoreData* core, TypeId type_id) noexcept
	{
		return print_type(print_make_sink(sink), core, type_id);
	}
}

#endif // DIAG_INCLUDE_GUARD

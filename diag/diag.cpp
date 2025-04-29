#include "diag.hpp"

void diag::buf_flush(PrintContext* ctx) noexcept
{
	if (ctx->curr == ctx->buf)
		return;

	if (!minos::file_write(ctx->file, Range{ ctx->buf, ctx->curr }.as_byte_range(), minos::FILE_WRITE_APPEND))
		panic("diag::print_ast: Failed to write AST log to output file (0x%X)\n", minos::last_error());

	ctx->curr = ctx->buf;
}

void diag::buf_printf(PrintContext* ctx, const char8* format, ...) noexcept
{
	while (true)
	{
		va_list args;

		va_start(args, format);

		const s32 available = static_cast<s32>(ctx->buf + array_count(ctx->buf) - ctx->curr);

		const s32 printed = vsnprintf(ctx->curr, available, format, args);

		va_end(args);

		if (printed < 0)
			panic("diag::print_ast: vsnprintf failed with an encoding error\n");

		// Include equality as an error here, as the terminating '\0' is not
		// included in the returned count.
		if (printed < available)
		{
			ctx->curr += printed;

			return;
		}

		if (printed > static_cast<s32>(array_count(ctx->buf)))
			panic("diag::print_ast: vsnprintf's required character count of %d exceeds the supported maximum of %d\n", printed, static_cast<s32>(array_count(ctx->buf)));

		buf_flush(ctx);
	}
}

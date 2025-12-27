#include "diag.hpp"

#include <cstdio>
#include <cstdarg>

static bool buf_vprintf(diag::PrintContext* ctx, const char* format, va_list args) noexcept
{
	const s32 available = static_cast<s32>(ctx->buf + array_count(ctx->buf) - ctx->curr);

	const s32 printed = vsnprintf(ctx->curr, available, format, args);

	if (printed < 0)
		panic("diag::print_ast: vsnprintf failed with an encoding error\n");

	// Include equality as an error here, as the terminating '\0' is not
	// included in the returned count.
	if (printed < available)
	{
		ctx->curr += printed;

		return true;
	}

	if (printed > static_cast<s32>(array_count(ctx->buf)))
		panic("diag::print_ast: vsnprintf's required character count of %d exceeds the supported maximum of %d\n", printed, static_cast<s32>(array_count(ctx->buf)));

	return false;
}

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
	va_list args;

	va_start(args, format);

	const bool succeeded = buf_vprintf(ctx, format, args);

	va_end(args);

	if (succeeded)
		return;

	// The buffer was too full for everything to be written. Flush it and try
	// again.
	// Note that the second time around this has to work, `buf_vprintf`
	// hard-errors if the printed message would not fit into the buffer after a
	// flush.

	buf_flush(ctx);

	va_start(args, format);

	if (!buf_vprintf(ctx, format, args))
		ASSERT_UNREACHABLE;

	va_end(args);
}

void diag::print_header(minos::FileHandle out, const char8* format, ...) noexcept
{
	PrintContext ctx;
	ctx.curr = ctx.buf;
	ctx.file = out;

	buf_printf(&ctx, "### ");

	va_list args;

	va_start(args, format);

	const bool succeeded = buf_vprintf(&ctx, format, args);

	va_end(args);

	if (!succeeded)
	{
		buf_flush(&ctx);

		va_start(args, format);

		if (!buf_vprintf(&ctx, format, args))
			ASSERT_UNREACHABLE;

		va_end(args);
	}
	
	buf_printf(&ctx, " ###\n");

	buf_flush(&ctx);
}

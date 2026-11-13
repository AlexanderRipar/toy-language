#include "diag_helpers.hpp"

#include "../infra/math.hpp"
#include "../infra/print/insert_helpers.hpp"

struct IdPrintAttach
{
	u32 id;

	u8 prefix_size;

	u8 suffix_size;

	char8 prefix[8];

	char8 suffix[8];
};

PrintInsert create_id_print_insert(u32 id, Range<char8> prefix, Range<char8> suffix) noexcept
{
	ASSERT_OR_IGNORE(prefix.count() <= sizeof(IdPrintAttach::prefix));
	
	ASSERT_OR_IGNORE(suffix.count() <= sizeof(IdPrintAttach::suffix));

	PrintInsert insert;
	insert.format_func = &print_format_id;

	IdPrintAttach* const attach = reinterpret_cast<IdPrintAttach*>(insert.attach);
	attach->id = id;
	attach->prefix_size = static_cast<u8>(prefix.count());
	attach->suffix_size = static_cast<u8>(suffix.count());

	memcpy(attach->prefix, prefix.begin(), prefix.count());
	memcpy(attach->suffix, suffix.begin(), suffix.count());

	return insert;
}

bool print_format_id(PrintState* state, const void* insert_attach, PrintSpec spec) noexcept
{
	ASSERT_OR_IGNORE(is_none(spec.extended_spec));

	const IdPrintAttach* const attach = static_cast<const IdPrintAttach*>(insert_attach);

	u32 id = attach->id;

	if (id == 0)
	{
		const char8 total_chars = attach->prefix_size + attach->suffix_size + 4;

		if (!ps_prepad(state, spec.alignment, total_chars, spec.align_frame_size, spec.align_padding_char))
			return false;

		if (!ps_print_chars(state, Range{ attach->prefix, attach->prefix_size }))
			return false;

		if (!ps_print_chars(state, range::from_literal_string("none")))
			return false;

		if (!ps_print_chars(state, Range{ attach->suffix, attach->suffix_size }))
			return false;

		if (!ps_postpad(state, spec.alignment, total_chars, spec.align_frame_size, spec.align_padding_char))
			return false;
	}
	else
	{
		const u8 id_chars = log10_ceil(static_cast<u32>(id));

		const u32 total_chars = id_chars + attach->prefix_size + attach->suffix_size;

		if (!ps_prepad(state, spec.alignment, total_chars, spec.align_frame_size, spec.align_padding_char))
			return false;

		const Maybe<char8*> opt_buffer = ps_reserve_buffer(state, total_chars);

		if (is_none(opt_buffer))
			return false;

		char8* const buffer = get(opt_buffer);

		for (s32 i = static_cast<s32>(id_chars - 1); i >= 0; --i)
		{
			buffer[i + attach->prefix_size] = '0' + id % 10;

			id /= 10;
		}

		ASSERT_OR_IGNORE(id == 0);

		memcpy(buffer, attach->prefix, attach->prefix_size);

		memcpy(buffer + id_chars + attach->prefix_size, attach->suffix, attach->suffix_size);

		if (!ps_postpad(state, spec.alignment, total_chars, spec.align_frame_size, spec.align_padding_char))
			return false;
	}

	return true;
}

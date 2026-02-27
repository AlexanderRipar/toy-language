#include "test_helpers.hpp"

#include "../infra/types.hpp"
#include "../infra/panic.hpp"
#include "../infra/range.hpp"
#include "../core/core.hpp"

struct IntegrationTestExpectation
{
	Maybe<CompileError> error;

	bool has_line_number;

	bool has_column_number;

	u32 line_number;

	u32 column_number;
};

static IntegrationTestExpectation parse_integration_test_header(Range<char8> filepath) noexcept
{
	minos::FileHandle file;

	if (!minos::file_create(filepath, minos::Access::Read, minos::ExistsMode::Open, minos::NewMode::Fail, minos::AccessPattern::Sequential, nullptr, false, &file))
		panic("Failed to open integration test source file `%` (0x%[|X]).\n", filepath, minos::last_error());

	minos::FileInfo file_info;

	if (!minos::file_get_info(file, &file_info))
		panic("Failed to get size integration test source file `%` (0x%[|X]).\n", filepath, minos::last_error());

	char8 header_buffer[4096];

	u32 bytes_read;

	if (!minos::file_read(file, MutRange{ header_buffer, sizeof(header_buffer) - 1 }.as_mut_byte_range(), 0, &bytes_read))
		panic("Failed to read header of integration test source file `%` (0x%[|X]).\n", filepath, minos::last_error());

	minos::file_close(file);

	header_buffer[bytes_read] = '\0';

	const char8* curr = header_buffer;

	while (*curr == ' ' || *curr == '\t' || *curr == '\n' || *curr == '\r')
		curr += 1;

	if (*curr != '/' || curr[1] != '/')
		panic("Integration test source file `%` appears to be missing a header.\n", filepath);

	curr += 2;

	while (*curr == ' ' || *curr == '\t')
		curr += 1;

	if ((*curr < 'a' || *curr > 'z') && (*curr < 'A' || *curr > 'Z'))
		panic("Integration test source file `%` has a malformed header.\n", filepath);

	const char8* const error_name_begin = curr;

	curr += 1;

	while ((*curr >= 'a' && *curr <= 'z') || (*curr >= 'A' && *curr <= 'Z'))
		curr += 1;

	const char8* const error_name_end = curr;

	while (*curr == ' ' || *curr == '\t')
		curr += 1;

	u32 line_number = 0;

	const bool has_line_number = *curr == ':';

	if (has_line_number)
	{
		curr += 1;

		while (*curr == ' ' || *curr == '\t')
			curr += 1;

		if (*curr < '0' || *curr > '9')
			panic("Integration test source file `%` has a malformed header.\n", filepath);

		while (*curr >= '0' && *curr <= '9')
		{
			line_number = line_number * 10 + *curr - '0';

			curr += 1;
		}

		while (*curr == ' ' || *curr == '\t')
			curr += 1;
	}

	u32 column_number = 0;

	const bool has_column_number = *curr == ':';

	if (has_column_number)
	{
		curr += 1;

		while (*curr == ' ' || *curr == '\t')
			curr += 1;

		if (*curr < '0' || *curr > '9')
			panic("Integration test source file `%` has a malformed header.\n", filepath);

		while (*curr >= '0' && *curr <= '9')
		{
			column_number = column_number * 10 + *curr - '0';

			curr += 1;
		}

		while (*curr == ' ' || *curr == '\t')
			curr += 1;
	}

	if (*curr != '\n' && *curr != '\r')
		panic("Integration test source file `%` has a malformed header.\n", filepath);

	const Range<char8> error_name{ error_name_begin, error_name_end };

	const Range<char8> success_name = range::from_literal_string("success");

	Maybe<CompileError> error;

	if (error_name.count() == success_name.count() && range::mem_equal(error_name, success_name))
	{
		error = none<CompileError>();
	}
	else
	{
		error = compile_error_from_name(Range{ error_name_begin, error_name_end });

		if (is_none(error))
			panic("Integration test source file `%` specifies an unknown `CompileError` `%` in its header.\n", filepath, error_name);
	}

	return IntegrationTestExpectation{
		error,
		has_line_number,
		has_column_number,
		line_number,
		column_number
	};
}

static Config dummy_config(Range<char8> filepath, bool expect_failure) noexcept
{
	Config config{};
	config.compile_all = true;
	config.std.prelude.filepath = range::from_literal_string("../sample/std/prelude.evl");
	config.logging.diagnostics.file.enable = !expect_failure;
	config.entrypoint.filepath = filepath;

	return config;
}

static void run_integration_test(Range<char8> filepath, bool is_std, bool expect_failure) noexcept
{
	const IntegrationTestExpectation expectation = parse_integration_test_header(filepath);

	TEST_BEGIN_NAMED(filepath);

	const Config config = dummy_config(filepath, expect_failure);

	CoreData* const core = create_core_data(&config);

	TEST_EQUAL(run_compilation(core, is_std), is_none(expectation.error));

	if (is_some(expectation.error))
	{
		bool has_expected_error = false;

		const Range<ErrorRecord> errors = get_errors(core);

		for (const ErrorRecord& error : errors)
		{
			if (error.error != get(expectation.error))
				continue;

			if (expectation.has_line_number)
			{
				const SourceLocation location = source_location_from_source_id(core, error.source_id);

				if (expectation.line_number != location.line_number)
					continue;

				if (expectation.has_column_number && expectation.column_number != location.column_number)
					continue;
			}

			has_expected_error = true;

			break;
		}

		TEST_EQUAL(has_expected_error, true);
	}

	release_core_data(core);

	TEST_END;
}

void integration_tests() noexcept
{
	TEST_MODULE_BEGIN;

	const Range<char8> test_directory = range::from_literal_string("integration-test-sources");

	minos::DirectoryEnumerationHandle dir;

	minos::DirectoryEnumerationResult rst;

	minos::DirectoryEnumerationStatus status = minos::directory_enumeration_create(test_directory, &dir, &rst);

	while (status == minos::DirectoryEnumerationStatus::Ok)
	{
		// Skip subdirectories, we don't do those.
		if (rst.is_directory)
		{
			status = minos::directory_enumeration_next(dir, &rst);

			continue;
		}

		const Range<char8> filename = range::from_cstring(rst.filename);

		// Skip files that do not have one of the test prefixes `(u|s)(t|f)_`
		// or the suffix `.evl`.
		if (filename.count() < 7
		|| (filename[0] != 'u' && filename[0] != 's')
		|| (filename[1] != 't' && filename[1] != 'f')
		||  filename[2] != '-'
		|| !range::mem_equal(filename.subrange(filename.count() - 4), range::from_literal_string(".evl")))
		{
			status = minos::directory_enumeration_next(dir, &rst);

			continue;
		}

		char8 path_bytes[4096];

		MutRange<char8> path_buf = MutRange{ path_bytes };

		range::mem_copy(path_buf.mut_subrange(0, test_directory.count()), test_directory);

		path_buf[test_directory.count()] = '/';

		range::mem_copy(path_buf.mut_subrange(test_directory.count() + 1, filename.count()), filename);

		const Range<char8> path = path_buf.subrange(0, test_directory.count() + 1  + filename.count());

		run_integration_test(path, filename[0] == 's', filename[1] == 'f');

		status = minos::directory_enumeration_next(dir, &rst);
	}

	minos::directory_enumeration_close(dir);

	if (status == minos::DirectoryEnumerationStatus::Error)
		panic("Failed to enumerate integration test directory `%` (0x%[|X]).\n", test_directory, minos::last_error());

	TEST_MODULE_END;
}

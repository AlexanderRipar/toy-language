#include "test_helpers.hpp"
#include "../core/core.hpp"
#include "../infra/range.hpp"

static void run_integration_test(Range<char8> filepath, bool is_std, bool expect_failure) noexcept
{
	TEST_BEGIN_NAMED(filepath);

	const Range<char8> config_filepath = expect_failure
		? range::from_literal_string("integration-test-sources/config-failure.toml")
		: range::from_literal_string("integration-test-sources/config-success.toml");

	CoreData core = create_core_data(config_filepath);

	core.config->entrypoint.filepath = filepath;

	TEST_EQUAL(!run_compilation(&core, is_std), expect_failure);

	release_core_data(&core);

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
		panic("Failed to enumerate integration test directory `%.*s` (0x%X).\n", static_cast<s32>(test_directory.count()), test_directory.begin(), minos::last_error());

	TEST_MODULE_END;
}

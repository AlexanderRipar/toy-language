#include "test_helpers.hpp"
#include "../infra/minos/minos.hpp"

#define MINOS_TEST_BEGIN minos::init(); TEST_BEGIN

#define MINOS_TEST_END minos::deinit(); TEST_END

// This is here to shut up IntelliSense. HELPER_PROCESS_PATH is defined in the
// CMakeLists.txt file to refer to the test-process-helper executable.
#ifndef HELPER_PROCESS_PATH
	#define HELPER_PROCESS_PATH "UNREACHABLE"

	#error HELPER_PROCESS_PATH was not defined. Build with cmake to correct this.
#endif

static constexpr u32 TIMEOUT_TEST_MILLIS = 70;

static u32 log10_ceil(u64 n) noexcept
{
	u32 result = 1;

	while (n >= 1000)
	{
		result += 3;

		n /= 1000;
	}

	if (n >= 100)
		return result + 2;
	else if (n >= 10)
		return result + 1;
	else
		return result;
}

static Range<char8> format_u64(u64 n, MutRange<char8> out) noexcept
{
	const u32 chars = log10_ceil(n);

	char8* curr = out.begin() + chars - 1;

	if (curr >= out.end())
		panic("format_u64 got an insufficient buffer\n");

	do
	{
		ASSERT_OR_IGNORE(curr >= out.begin());

		*curr = '0' + (n % 10);

		n /= 10;

		curr -= 1;
	}
	while (n != 0);

	ASSERT_OR_IGNORE(curr + 1 == out.begin());

	return { out.begin(), chars };
}

static Range<char8> format_handle(minos::GenericHandle handle, MutRange<char8> out) noexcept
{
	return format_u64(reinterpret_cast<u64>(handle.m_rep), out);
}


static void mem_reserve_succeeds_on_small_allocation() noexcept
{
	MINOS_TEST_BEGIN;

	static constexpr u64 bytes_4_kb = 4096;

	void* const memory = minos::mem_reserve(bytes_4_kb);

	TEST_UNEQUAL(memory, nullptr);

	minos::mem_unreserve(memory, bytes_4_kb);

	MINOS_TEST_END;
}

static void mem_reserve_succeeds_on_small_odd_sized_allocation() noexcept
{
	MINOS_TEST_BEGIN;

	static constexpr u64 bytes_4_kb_and_a_bit = 5210;

	void* const memory = minos::mem_reserve(bytes_4_kb_and_a_bit);

	TEST_UNEQUAL(memory, nullptr);

	minos::mem_unreserve(memory, bytes_4_kb_and_a_bit);

	MINOS_TEST_END;
}

static void mem_reserve_succeeds_on_huge_allocation() noexcept
{
	MINOS_TEST_BEGIN;

	static constexpr const u64 bytes_256_gb = static_cast<u64>(1024) * static_cast<u64>(1024) * static_cast<u64>(1024) * static_cast<u64>(256);

	void* const memory = minos::mem_reserve(bytes_256_gb);

	TEST_UNEQUAL(memory, nullptr);

	minos::mem_unreserve(memory, bytes_256_gb);

	MINOS_TEST_END;
}

static void mem_commit_with_reserved_pointer_and_exact_size_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	static constexpr u64 bytes = 1024 * 1024 + 123;

	byte* const memory = static_cast<byte*>(minos::mem_reserve(bytes));

	TEST_EQUAL(minos::mem_commit(memory, bytes), true);

	TEST_EQUAL(memory[0], 0);

	memory[0] = 0x5E;

	TEST_EQUAL(memory[0], 0x5E);

	TEST_EQUAL(memory[bytes - 1], 0);

	memory[bytes - 1] = 0xA5;

	TEST_EQUAL(memory[bytes - 1], 0xA5);

	minos::mem_unreserve(memory, bytes);

	MINOS_TEST_END;
}

static void mem_commit_with_reserved_pointer_and_smaller_size_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	static constexpr u64 reserve_bytes = 1024 * 1024 + 123;

	static constexpr u64 commit_bytes = reserve_bytes - 1024 * 400;

	byte* const memory = static_cast<byte*>(minos::mem_reserve(reserve_bytes));

	TEST_EQUAL(minos::mem_commit(memory, commit_bytes), true);

	TEST_EQUAL(memory[0], 0);

	memory[0] = 0x5E;

	TEST_EQUAL(memory[0], 0x5E);

	TEST_EQUAL(memory[commit_bytes - 1], 0);

	memory[commit_bytes - 1] = 0xA5;

	TEST_EQUAL(memory[commit_bytes - 1], 0xA5);

	minos::mem_unreserve(memory, reserve_bytes);

	MINOS_TEST_END;
}

static void mem_commit_with_offset_pointer_and_exact_size_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	static constexpr u64 reserve_bytes = 1024 * 1024 + 123;

	static constexpr u64 offset_bytes = 1024 * 204 + 801;

	byte* const memory = static_cast<byte*>(minos::mem_reserve(reserve_bytes));

	TEST_EQUAL(minos::mem_commit(memory + offset_bytes, reserve_bytes - offset_bytes), true);

	TEST_EQUAL(memory[offset_bytes], 0);

	memory[offset_bytes] = 0x5E;

	TEST_EQUAL(memory[offset_bytes], 0x5E);

	TEST_EQUAL(memory[reserve_bytes - 1], 0);

	memory[reserve_bytes - 1] = 0xA5;

	TEST_EQUAL(memory[reserve_bytes - 1], 0xA5);

	minos::mem_unreserve(memory, reserve_bytes);

	MINOS_TEST_END;
}

static void mem_commit_with_offset_pointer_and_smaller_size_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	static constexpr u64 reserve_bytes = 1024 * 1024 + 123;

	static constexpr u64 offset_bytes = 1024 * 204 + 801;

	static constexpr u64 commit_bytes = reserve_bytes - offset_bytes - 1024 * 43 + 12;

	byte* const memory = static_cast<byte*>(minos::mem_reserve(reserve_bytes));

	TEST_EQUAL(minos::mem_commit(memory + offset_bytes, commit_bytes), true);

	TEST_EQUAL(memory[offset_bytes], 0);

	memory[offset_bytes] = 0x5E;

	TEST_EQUAL(memory[offset_bytes], 0x5E);

	TEST_EQUAL(memory[offset_bytes + commit_bytes - 1], 0);

	memory[offset_bytes + commit_bytes - 1] = 0xA5;

	TEST_EQUAL(memory[offset_bytes + commit_bytes - 1], 0xA5);

	minos::mem_unreserve(memory, reserve_bytes);

	MINOS_TEST_END;
}

static void mem_commit_repeated_on_same_memory_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	static constexpr u64 bytes = 10000;

	void* const memory = minos::mem_reserve(bytes);

	TEST_EQUAL(minos::mem_commit(memory, bytes), true);

	TEST_EQUAL(minos::mem_commit(memory, bytes), true);

	minos::mem_unreserve(memory, bytes);

	MINOS_TEST_END;
}

static void mem_decommit_on_aligned_pointer_and_exact_size_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	static constexpr u64 bytes = 10000;

	void* const memory = minos::mem_reserve(bytes);

	TEST_EQUAL(minos::mem_commit(memory, bytes), true);

	minos::mem_decommit(memory, minos::page_bytes());

	minos::mem_unreserve(memory, bytes);

	MINOS_TEST_END;
}


static void page_bytes_returns_nonzero_power_of_two() noexcept
{
	MINOS_TEST_BEGIN;

	const u32 page_bytes = minos::page_bytes();

	TEST_UNEQUAL(page_bytes, 0);

	TEST_EQUAL(is_pow2(page_bytes), true);

	MINOS_TEST_END;
}


static void logical_processor_count_returns_nonzero() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_UNEQUAL(minos::logical_processor_count(), 0);

	MINOS_TEST_END;
}


static u32 THREAD_PROC thread_test_proc(void* param) noexcept
{
	*static_cast<u64*>(param) += 1;

	return 42;
}

static u32 THREAD_PROC wait_test_proc(void* param) noexcept
{
	minos::sleep(static_cast<u32>(reinterpret_cast<u64>(param)));

	return 42;
}

static void thread_create_and_thread_wait_work() noexcept
{
	MINOS_TEST_BEGIN;

	u64 modified_value = 0;

	minos::ThreadHandle thread;

	const bool thread_ok = minos::thread_create(thread_test_proc, &modified_value, range::from_literal_string("thread test"), &thread);

	TEST_EQUAL(thread_ok, true);

	if (thread_ok)
	{
		u32 thread_result;

		minos::thread_wait(thread, &thread_result);

		TEST_EQUAL(modified_value, 1);

		TEST_EQUAL(thread_result, 42);

		minos::thread_close(thread);
	}

	MINOS_TEST_END;
}

static void thread_wait_timeout_succeeds_on_short_thread() noexcept
{
	MINOS_TEST_BEGIN;

	minos::ThreadHandle thread;

	const bool thread_ok = minos::thread_create(wait_test_proc, reinterpret_cast<void*>(0), range::from_literal_string("empty test"), &thread);

	if (thread_ok)
	{
		u32 thread_result;

		TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, &thread_result), true);

		TEST_EQUAL(thread_result, 42);

		minos::thread_close(thread);
	}

	MINOS_TEST_END;
}

static void thread_wait_timeout_times_out_on_long_thread() noexcept
{
	MINOS_TEST_BEGIN;

	minos::ThreadHandle thread;

	const bool thread_ok = minos::thread_create(wait_test_proc, reinterpret_cast<void*>(1000), range::from_literal_string("empty test"), &thread);

	if (thread_ok)
	{
		u32 thread_result;

		TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, &thread_result), false);

		minos::thread_close(thread);
	}

	MINOS_TEST_END;
}


struct AddressWaitParams
{
	void* address;

	void* undesired;

	u32 bytes;
};

static u32 THREAD_PROC address_wait_test_proc(void* param) noexcept
{
	AddressWaitParams* const params = static_cast<AddressWaitParams*>(param);

	minos::address_wait(params->address, params->undesired, params->bytes);

	return 0;
}

static void address_wait_with_4_bytes_and_wake_single_with_changed_value_wakes() noexcept
{
	MINOS_TEST_BEGIN;

	u32 address = 404;

	u32 undesired = 404;

	AddressWaitParams params;
	params.address = &address;
	params.undesired = &undesired;
	params.bytes = 4;

	minos::ThreadHandle thread;

	const bool thread_ok = minos::thread_create(address_wait_test_proc, &params, range::from_literal_string("addr_wait wake"), &thread);

	TEST_EQUAL(thread_ok, true);

	if (thread_ok)
	{
		address -= 1;

		minos::address_wake_single(params.address);

		TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, nullptr), true);

		minos::thread_close(thread);
	}

	MINOS_TEST_END;
}

static void address_wait_with_4_bytes_and_wake_single_with_unchanged_value_sleeps() noexcept
{
	MINOS_TEST_BEGIN;

	u32 address = 404;

	u32 undesired = 404;

	AddressWaitParams params;
	params.address = &address;
	params.undesired = &undesired;
	params.bytes = 4;

	minos::ThreadHandle thread;

	const bool thread_ok = minos::thread_create(address_wait_test_proc, &params, range::from_literal_string("addr_wait sleep"), &thread);

	TEST_EQUAL(thread_ok, true);

	if (thread_ok)
	{
		minos::address_wake_single(params.address);

		TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, nullptr), false);

		// Just so we don't have a lingering thread
		address -= 1;

		minos::address_wake_single(params.address);

		minos::thread_wait(thread, nullptr);

		minos::thread_close(thread);
	}

	MINOS_TEST_END;
}

static void address_wait_with_2_bytes_and_wake_single_with_changed_value_wakes() noexcept
{
	MINOS_TEST_BEGIN;

	struct alignas(u32)
	{
		u16 padding = 0;

		u16 address = 1;
	} unaligned_2_bytes;

	u16 undesired = 1;

	AddressWaitParams params;
	params.address = &unaligned_2_bytes.address;
	params.undesired = &undesired;
	params.bytes = 2;

	minos::ThreadHandle thread;

	const bool thread_ok = minos::thread_create(address_wait_test_proc, &params, range::from_literal_string("addr_wait wake"), &thread);

	TEST_EQUAL(thread_ok, true);

	if (thread_ok)
	{
		unaligned_2_bytes.address -= 1;

		minos::address_wake_single(params.address);

		TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, nullptr), true);

		minos::thread_close(thread);
	}

	MINOS_TEST_END;
}

static void address_wait_with_2_bytes_and_wake_single_with_unchanged_value_sleeps() noexcept
{
	MINOS_TEST_BEGIN;

	struct alignas(u32)
	{
		u16 padding = 0;

		u16 address = 1;
	} unaligned_2_bytes;

	u16 undesired = 1;

	AddressWaitParams params;
	params.address = &unaligned_2_bytes.address;
	params.undesired = &undesired;
	params.bytes = 2;

	minos::ThreadHandle thread;

	const bool thread_ok = minos::thread_create(address_wait_test_proc, &params, range::from_literal_string("addr_wait sleep"), &thread);

	TEST_EQUAL(thread_ok, true);

	if (thread_ok)
	{
		// minos::address_wake_single(params.address);

		TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, nullptr), false);

		// Just so we don't have a lingering thread
		unaligned_2_bytes.address -= 1;

		minos::address_wake_single(params.address);

		minos::thread_wait(thread, nullptr);

		minos::thread_close(thread);
	}

	MINOS_TEST_END;
}

static void address_wait_with_1_byte_and_wake_single_with_changed_value_wakes() noexcept
{
	MINOS_TEST_BEGIN;

	struct alignas(u32)
	{
		u8 padding[3] = { 0 };

		u8 address = 1;
	} unaligned_1_bytes;

	u8 undesired = 1;

	AddressWaitParams params;
	params.address = &unaligned_1_bytes.address;
	params.undesired = &undesired;
	params.bytes = 1;

	minos::ThreadHandle thread;

	const bool thread_ok = minos::thread_create(address_wait_test_proc, &params, range::from_literal_string("addr_wait wake"), &thread);

	TEST_EQUAL(thread_ok, true);

	if (thread_ok)
	{
		unaligned_1_bytes.address -= 1;

		minos::address_wake_single(params.address);

		TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, nullptr), true);

		minos::thread_close(thread);
	}

	MINOS_TEST_END;
}

static void address_wait_with_1_byte_and_wake_single_with_unchanged_value_sleeps() noexcept
{
	MINOS_TEST_BEGIN;

	struct alignas(u32)
	{
		u8 padding[3] = { 0 };

		u8 address = 1;
	} unaligned_1_bytes;

	u8 undesired = 1;

	AddressWaitParams params;
	params.address = &unaligned_1_bytes.address;
	params.undesired = &undesired;
	params.bytes = 1;

	minos::ThreadHandle thread;

	const bool thread_ok = minos::thread_create(address_wait_test_proc, &params, range::from_literal_string("addr_wait wake"), &thread);

	TEST_EQUAL(thread_ok, true);

	if (thread_ok)
	{
		minos::address_wake_single(params.address);

		TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, nullptr), false);

		// Just so we don't have a lingering thread
		unaligned_1_bytes.address -= 1;

		minos::address_wake_single(params.address);

		minos::thread_wait(thread, nullptr);

		minos::thread_close(thread);
	}

	MINOS_TEST_END;
}

static void multiple_address_wait_and_wake_all_with_changed_value_wakes_all() noexcept
{
	MINOS_TEST_BEGIN;

	u32 address = 404;

	u32 undesired = 404;

	AddressWaitParams params;
	params.address = &address;
	params.undesired = &undesired;
	params.bytes = 4;

	minos::ThreadHandle thread1;

	const bool thread1_ok = minos::thread_create(address_wait_test_proc, &params, range::from_literal_string("addr_wait wake"), &thread1);

	TEST_EQUAL(thread1_ok, true);

	minos::ThreadHandle thread2;

	const bool thread2_ok = minos::thread_create(address_wait_test_proc, &params, range::from_literal_string("addr_wait wake"), &thread2);

	TEST_EQUAL(thread2_ok, true);

	if (thread1_ok && thread2_ok)
	{
		address -= 1;

		minos::address_wake_all(params.address);

		TEST_EQUAL(minos::thread_wait_timeout(thread1, TIMEOUT_TEST_MILLIS, nullptr), true);

		TEST_EQUAL(minos::thread_wait_timeout(thread2, TIMEOUT_TEST_MILLIS, nullptr), true);

		minos::thread_close(thread1);

		minos::thread_close(thread2);
	}

	MINOS_TEST_END;
}


static void file_create_with_existing_file_path_and_read_access_opens_file() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/short_file"),
			minos::Access::Read,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_create_with_existing_file_path_and_write_access_opens_file() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/short_file"),
			minos::Access::Write,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_create_with_existing_file_path_and_readwrite_access_opens_file() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/short_file"),
			minos::Access::Write | minos::Access::Write,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_create_with_existing_file_path_and_none_access_opens_file() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/short_file"),
			minos::Access::None,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_create_with_existing_file_path_and_unbuffered_access_pattern_opens_file() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/short_file"),
			minos::Access::Read | minos::Access::Write,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Unbuffered,
			nullptr,
			false,
			&file
		), true);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_create_with_existing_file_path_and_exists_mode_fail_fails() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/short_file"),
			minos::Access::Read | minos::Access::Write,
			minos::ExistsMode::Fail,
			minos::NewMode::Create, // Create instead of Fail as exists_mode and new_mode cannot both be Fail
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), false);

	MINOS_TEST_END;
}

static void file_create_with_existing_file_path_and_exists_mode_truncate_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/empty_file"), // Test on empty file to leave data untouched
			minos::Access::Read | minos::Access::Write,
			minos::ExistsMode::Truncate,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_create_with_existing_file_path_and_exists_mode_open_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/long_file"),
			minos::Access::Read | minos::Access::Write,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_create_with_existing_directory_path_and_none_access_opens_file() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data"),
			minos::Access::None,
			minos::ExistsMode::OpenDirectory,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_create_with_new_file_path_and_new_mode_fail_fails() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/nonexistent_file"),
			minos::Access::None,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), false);

	MINOS_TEST_END;
}

static void file_create_with_new_file_path_and_new_mode_create_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_A"),
			minos::Access::Read,
			minos::ExistsMode::Fail,
			minos::NewMode::Create,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	minos::file_close(file);

	MINOS_TEST_END;
}


static void file_read_on_empty_file_returns_no_bytes() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/empty_file"),
			minos::Access::Read,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	u32 bytes_read;

	byte buf[1024];

	TEST_EQUAL(minos::file_read(file, MutRange{ buf }, 0, &bytes_read), true);

	TEST_EQUAL(bytes_read, 0);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_read_on_file_shorter_than_buffer_returns_file_size_bytes() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/short_file"),
			minos::Access::Read,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	u32 bytes_read;

	byte buf[1024];

	TEST_EQUAL(minos::file_read(file, MutRange{ buf }, 0, &bytes_read), true);

	TEST_EQUAL(bytes_read, 14);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_read_on_file_longer_than_buffer_returns_buffer_size_bytes() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/long_file"),
			minos::Access::Read,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	u32 bytes_read;

	byte buf[1024];

	TEST_EQUAL(minos::file_read(file, MutRange{ buf }, 0, &bytes_read), true);

	TEST_EQUAL(bytes_read, 1024);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_read_unbuffered_file_with_page_alignment_and_zero_offset_on_short_file_returns_file_size_bytes() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/short_file"),
			minos::Access::Read,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Unbuffered,
			nullptr,
			false,
			&file
		), true);

	u32 bytes_read;

	const u32 buf_bytes = minos::page_bytes();

	byte* const buf = static_cast<byte*>(minos::mem_reserve(buf_bytes));

	TEST_UNEQUAL(buf, nullptr);

	TEST_EQUAL(minos::mem_commit(buf, buf_bytes), true);

	TEST_EQUAL(minos::file_read(file, MutRange{ buf, buf_bytes }, 0, &bytes_read), true);

	TEST_EQUAL(bytes_read, 14);

	minos::file_close(file);

	minos::mem_unreserve(buf, buf_bytes);

	MINOS_TEST_END;
}

static void file_read_unbuffered_file_with_page_alignment_and_zero_offset_on_long_file_returns_buffer_size_bytes() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/long_file"),
			minos::Access::Read,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Unbuffered,
			nullptr,
			false,
			&file
		), true);

	u32 bytes_read;

	const u32 buf_bytes = minos::page_bytes();

	byte* const buf = static_cast<byte*>(minos::mem_reserve(buf_bytes));

	TEST_UNEQUAL(buf, nullptr);

	TEST_EQUAL(minos::mem_commit(buf, buf_bytes), true);

	TEST_EQUAL(minos::file_read(file, MutRange{ buf, buf_bytes }, 0, &bytes_read), true);

	TEST_EQUAL(bytes_read, buf_bytes);

	minos::file_close(file);

	minos::mem_unreserve(buf, buf_bytes);

	MINOS_TEST_END;
}

static void file_read_unbuffered_file_with_page_alignment_and_nonzero_offset_on_medium_file_returns_remaining_file_size_bytes() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/medium_file"),
			minos::Access::Read,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Unbuffered,
			nullptr,
			false,
			&file
		), true);

	u32 bytes_read;

	const u32 buf_bytes = minos::page_bytes();

	TEST_EQUAL(buf_bytes % 4096, 0);

	byte* const buf = static_cast<byte*>(minos::mem_reserve(buf_bytes));

	TEST_UNEQUAL(buf, nullptr);

	TEST_EQUAL(minos::mem_commit(buf, buf_bytes), true);

	TEST_EQUAL(minos::file_read(file, MutRange{ buf, buf_bytes }, 4096, &bytes_read), true);

	TEST_EQUAL(bytes_read, 22);

	minos::file_close(file);

	minos::mem_unreserve(buf, buf_bytes);

	MINOS_TEST_END;
}

static void file_read_unbuffered_file_with_page_alignment_and_nonzero_offset_on_long_file_returns_buffer_size_bytes() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/long_file"),
			minos::Access::Read,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Unbuffered,
			nullptr,
			false,
			&file
		), true);

	u32 bytes_read;

	const u32 buf_bytes = minos::page_bytes();

	TEST_EQUAL(buf_bytes % 4096, 0);

	byte* const buf = static_cast<byte*>(minos::mem_reserve(buf_bytes));

	TEST_UNEQUAL(buf, nullptr);

	TEST_EQUAL(minos::mem_commit(buf, buf_bytes), true);

	TEST_EQUAL(minos::file_read(file, MutRange{ buf, buf_bytes }, buf_bytes * 2, &bytes_read), true);

	TEST_EQUAL(bytes_read, buf_bytes);

	minos::file_close(file);

	minos::mem_unreserve(buf, buf_bytes);

	MINOS_TEST_END;
}


static void file_write_on_empty_file_appends_to_that_file() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_B"),
			minos::Access::Read | minos::Access::Write,
			minos::ExistsMode::Fail,
			minos::NewMode::Create,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	Range<byte> to_append = range::from_literal_string("test data to append").as_byte_range();

	TEST_EQUAL(minos::file_write(file, to_append, 0), true);

	minos::FileInfo fileinfo;

	TEST_EQUAL(minos::file_get_info(file, &fileinfo), true);

	TEST_EQUAL(fileinfo.bytes, to_append.count());

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_write_on_existing_file_part_overwrites_it() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_C"),
			minos::Access::Read | minos::Access::Write,
			minos::ExistsMode::Fail,
			minos::NewMode::Create,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	byte buf[1024];

	memset(buf, 1, sizeof(buf));

	TEST_EQUAL(minos::file_write(file, Range{ buf }, 0), true);

	minos::FileInfo fileinfo;

	TEST_EQUAL(minos::file_get_info(file, &fileinfo), true);

	TEST_EQUAL(fileinfo.bytes, sizeof(buf));

	memset(buf + sizeof(buf) / 4, 2, sizeof(buf) / 2);

	TEST_EQUAL(minos::file_write(file, Range{ buf + sizeof(buf) / 4, sizeof(buf) / 2 }, sizeof(buf) / 4), true);

	byte read_buf[sizeof(buf)];

	u32 bytes_read;

	TEST_EQUAL(minos::file_read(file, MutRange{ read_buf }, 0, &bytes_read), true);

	TEST_EQUAL(bytes_read, sizeof(buf));

	TEST_MEM_EQUAL(buf, read_buf, sizeof(buf));

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_write_unbuffered_file_with_page_alignment_on_existing_file_part_overwrites_it() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_D"),
			minos::Access::Read | minos::Access::Write,
			minos::ExistsMode::Fail,
			minos::NewMode::Create,
			minos::AccessPattern::Unbuffered,
			nullptr,
			false,
			&file
		), true);

	const u32 page_bytes = minos::page_bytes();

	TEST_EQUAL(minos::file_resize(file, page_bytes * 4), true);

	TEST_EQUAL(page_bytes % 4096, 0);

	const u32 buf_bytes = page_bytes * 2;

	byte* const buf = static_cast<byte*>(minos::mem_reserve(buf_bytes));

	TEST_UNEQUAL(buf, nullptr);

	TEST_EQUAL(minos::mem_commit(buf, buf_bytes), true);

	memset(buf, 42, buf_bytes);

	TEST_EQUAL(minos::file_write(file, Range{ buf, buf_bytes }, page_bytes), true);

	memset(buf, 0, buf_bytes);

	u32 bytes_read;

	TEST_EQUAL(minos::file_read(file, MutRange{ buf, buf_bytes }, page_bytes, &bytes_read), true);

	TEST_EQUAL(bytes_read, buf_bytes);

	TEST_EQUAL(buf[0], 42);

	TEST_EQUAL(buf[buf_bytes - 1], 42);

	minos::file_close(file);

	minos::mem_unreserve(buf, buf_bytes);

	MINOS_TEST_END;
}

static void file_write_unbuffered_file_with_page_alignment_on_unaligned_file_end_overwrites_it_and_appends() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_E"),
			minos::Access::Read | minos::Access::Write,
			minos::ExistsMode::Fail,
			minos::NewMode::Create,
			minos::AccessPattern::Unbuffered,
			nullptr,
			false,
			&file
		), true);

	const u32 page_bytes = minos::page_bytes();

	TEST_EQUAL(minos::file_resize(file, page_bytes + page_bytes / 2), true);

	TEST_EQUAL(page_bytes % 4096, 0);

	const u32 buf_bytes = page_bytes;

	byte* const buf = static_cast<byte*>(minos::mem_reserve(buf_bytes));

	TEST_UNEQUAL(buf, nullptr);

	TEST_EQUAL(minos::mem_commit(buf, buf_bytes), true);

	memset(buf, 42, buf_bytes);

	TEST_EQUAL(minos::file_write(file, Range{ buf, buf_bytes }, page_bytes), true);

	memset(buf, 0, buf_bytes);

	u32 bytes_read;

	TEST_EQUAL(minos::file_read(file, MutRange{ buf, buf_bytes }, page_bytes, &bytes_read), true);

	TEST_EQUAL(buf[0], 42);

	TEST_EQUAL(buf[buf_bytes - 1], 42);

	minos::FileInfo fileinfo;

	TEST_EQUAL(minos::file_get_info(file, &fileinfo), true);

	TEST_EQUAL(fileinfo.bytes, page_bytes + buf_bytes);

	minos::file_close(file);

	minos::mem_unreserve(buf, buf_bytes);

	MINOS_TEST_END;
}


static void file_get_info_on_file_handle_returns_not_is_directory_and_file_size() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/short_file"),
			minos::Access::None,
			minos::ExistsMode::Open,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	minos::FileInfo fileinfo;

	TEST_EQUAL(minos::file_get_info(file, &fileinfo), true);

	TEST_EQUAL(fileinfo.is_directory, false);

	TEST_EQUAL(fileinfo.bytes, 14);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_get_info_on_directory_handle_returns_is_directory() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data"),
			minos::Access::None,
			minos::ExistsMode::OpenDirectory,
			minos::NewMode::Fail,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	minos::FileInfo fileinfo;

	TEST_EQUAL(minos::file_get_info(file, &fileinfo), true);

	TEST_EQUAL(fileinfo.is_directory, true);

	minos::file_close(file);

	MINOS_TEST_END;
}


static void file_resize_to_grow_empty_file_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_F"),
			minos::Access::Read | minos::Access::Write,
			minos::ExistsMode::Fail,
			minos::NewMode::Create,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	TEST_EQUAL(minos::file_resize(file, 1024), true);

	minos::FileInfo fileinfo;

	TEST_EQUAL(minos::file_get_info(file, &fileinfo), true);

	TEST_EQUAL(fileinfo.bytes, 1024);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_resize_to_grow_file_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_G"),
			minos::Access::Read | minos::Access::Write,
			minos::ExistsMode::Fail,
			minos::NewMode::Create,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	TEST_EQUAL(minos::file_resize(file, 1024), true);

	TEST_EQUAL(minos::file_resize(file, 1200), true);

	minos::FileInfo fileinfo;

	TEST_EQUAL(minos::file_get_info(file, &fileinfo), true);

	TEST_EQUAL(fileinfo.bytes, 1200);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_resize_to_shrink_file_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_H"),
			minos::Access::Read | minos::Access::Write,
			minos::ExistsMode::Fail,
			minos::NewMode::Create,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	TEST_EQUAL(minos::file_resize(file, 1024), true);

	TEST_EQUAL(minos::file_resize(file, 751), true);

	minos::FileInfo fileinfo;

	TEST_EQUAL(minos::file_get_info(file, &fileinfo), true);

	TEST_EQUAL(fileinfo.bytes, 751);

	minos::file_close(file);

	MINOS_TEST_END;
}

static void file_resize_to_empty_file_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(
			range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_I"),
			minos::Access::Read | minos::Access::Write,
			minos::ExistsMode::Fail,
			minos::NewMode::Create,
			minos::AccessPattern::Sequential,
			nullptr,
			false,
			&file
		), true);

	TEST_EQUAL(minos::file_resize(file, 1024), true);

	TEST_EQUAL(minos::file_resize(file, 0), true);

	minos::FileInfo fileinfo;

	TEST_EQUAL(minos::file_get_info(file, &fileinfo), true);

	TEST_EQUAL(fileinfo.bytes, 0);

	minos::file_close(file);

	MINOS_TEST_END;
}


struct EventThreadParams
{
	minos::EventHandle event;

	bool has_timeout;

	u32 timeout_milliseconds;
};

static u32 THREAD_PROC event_test_proc(void* raw_params) noexcept
{
	EventThreadParams* const params = static_cast<EventThreadParams*>(raw_params);

	if (params->has_timeout)
	{
		if (!minos::event_wait_timeout(params->event, params->timeout_milliseconds))
			return 1;
	}
	else
	{
		minos::event_wait(params->event);
	}

	return 0;
}

static void event_create_creates_an_event() noexcept
{
	MINOS_TEST_BEGIN;

	minos::EventHandle event;

	TEST_EQUAL(minos::event_create(&event), true);

	minos::event_close(event);

	MINOS_TEST_END;
}

static void event_wake_allows_wait() noexcept
{
	MINOS_TEST_BEGIN;

	minos::EventHandle event;

	TEST_EQUAL(minos::event_create(&event), true);

	minos::event_wake(event);

	TEST_EQUAL(minos::event_wait_timeout(event, TIMEOUT_TEST_MILLIS), true);

	minos::event_close(event);

	MINOS_TEST_END;
}

static void event_wait_waits_until_wake() noexcept
{
	MINOS_TEST_BEGIN;

	minos::EventHandle event;

	TEST_EQUAL(minos::event_create(&event), true);

	minos::ThreadHandle thread;

	EventThreadParams params;
	params.event = event;
	params.has_timeout = false;

	TEST_EQUAL(minos::thread_create(event_test_proc, &params, range::from_literal_string("event_wait"), &thread), true);

	TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, nullptr), false);

	minos::event_wake(event);

	u32 thread_result;

	TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, &thread_result), true);

	TEST_EQUAL(thread_result, 0);

	minos::thread_close(thread);

	minos::event_close(event);

	MINOS_TEST_END;
}

static void event_wait_timeout_with_long_timeout_waits_until_wake() noexcept
{
	MINOS_TEST_BEGIN;

	minos::EventHandle event;

	TEST_EQUAL(minos::event_create(&event), true);

	minos::ThreadHandle thread;

	EventThreadParams params;
	params.event = event;
	params.has_timeout = true;
	params.timeout_milliseconds = 1000;

	TEST_EQUAL(minos::thread_create(event_test_proc, &params, range::from_literal_string("event_wait"), &thread), true);

	TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, nullptr), false);

	minos::event_wake(event);

	u32 thread_result;

	TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, &thread_result), true);

	TEST_EQUAL(thread_result, 0);

	minos::thread_close(thread);

	minos::event_close(event);

	MINOS_TEST_END;
}

static void event_wait_timeout_with_no_wakes_times_out() noexcept
{
	MINOS_TEST_BEGIN;

	minos::EventHandle event;

	TEST_EQUAL(minos::event_create(&event), true);

	minos::ThreadHandle thread;

	EventThreadParams params;
	params.event = event;
	params.has_timeout = true;
	params.timeout_milliseconds = 20;

	TEST_EQUAL(minos::thread_create(event_test_proc, &params, range::from_literal_string("event_wait"), &thread), true);

	u32 thread_result;

	TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, &thread_result), true);

	TEST_EQUAL(thread_result, 1);

	minos::thread_close(thread);

	minos::event_close(event);

	MINOS_TEST_END;
}

static void event_wait_and_wake_work_across_processes() noexcept
{
	MINOS_TEST_BEGIN;

	minos::EventHandle event;

	TEST_EQUAL(minos::event_create(&event), true);

	char8 event_buf[64];

	char8 timeout_buf[64];

	const Range<char8> command_line[] = {
		range::from_literal_string("--event-wait"),
		format_handle(event, MutRange{ event_buf }),
		range::from_literal_string("--timeout"),
		format_u64(50, MutRange{ timeout_buf }),
	};

	const minos::GenericHandle generic_event = event;

	minos::ProcessHandle process;

	TEST_EQUAL(minos::process_create({}, Range{ command_line }, {}, Range<minos::GenericHandle>{ &generic_event, 1}, false, &process), true);

	u32 process_result;

	minos::event_wake(event);

	TEST_EQUAL(minos::process_wait_timeout(process, TIMEOUT_TEST_MILLIS, &process_result), true);

	TEST_EQUAL(process_result, 0);

	minos::process_close(process);

	minos::event_close(event);

	MINOS_TEST_END;
}


static void completion_create_and_completion_close_work() noexcept
{
	MINOS_TEST_BEGIN;

	minos::CompletionHandle completion;

	TEST_EQUAL(minos::completion_create(&completion), true);

	minos::completion_close(completion);

	MINOS_TEST_END;
}

static void file_create_with_completion_works() noexcept
{
	MINOS_TEST_BEGIN;

	minos::CompletionHandle completion;

	TEST_EQUAL(minos::completion_create(&completion), true);

	minos::CompletionInitializer completion_init;
	completion_init.completion = completion;
	completion_init.key = 1234;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(range::from_literal_string("minos_fs_data/short_file"), minos::Access::Read, minos::ExistsMode::Open, minos::NewMode::Fail, minos::AccessPattern::Sequential, &completion_init, false, &file), true);

	minos::file_close(file);

	minos::completion_close(completion);

	MINOS_TEST_END;
}

static void file_read_with_completion_works() noexcept
{
	MINOS_TEST_BEGIN;

	minos::CompletionHandle completion;

	TEST_EQUAL(minos::completion_create(&completion), true);

	minos::CompletionInitializer completion_init;
	completion_init.completion = completion;
	completion_init.key = 1234;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(range::from_literal_string("minos_fs_data/short_file"), minos::Access::Read, minos::ExistsMode::Open, minos::NewMode::Fail, minos::AccessPattern::Sequential, &completion_init, false, &file), true);

	byte buf[1024];

	minos::Overlapped overlapped{};
	overlapped.offset = 0;

	TEST_EQUAL(minos::file_read_async(file, MutRange{ buf }, &overlapped), true);

	minos::CompletionResult read_result;

	TEST_EQUAL(minos::completion_wait(completion, &read_result), true);

	TEST_EQUAL(read_result.key, 1234);

	TEST_EQUAL(read_result.bytes, 14);

	TEST_MEM_EQUAL(buf, "abcdefghijklmn", 14);

	minos::file_close(file);

	minos::completion_close(completion);

	MINOS_TEST_END;
}

static void file_read_twice_with_completion_works() noexcept
{
	MINOS_TEST_BEGIN;

	minos::CompletionHandle completion;

	TEST_EQUAL(minos::completion_create(&completion), true);

	minos::CompletionInitializer completion_init;
	completion_init.completion = completion;
	completion_init.key = 1234;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(range::from_literal_string("minos_fs_data/short_file"), minos::Access::Read, minos::ExistsMode::Open, minos::NewMode::Fail, minos::AccessPattern::Sequential, &completion_init, false, &file), true);

	byte buf1[1024];

	minos::Overlapped overlapped1{};
	overlapped1.offset = 0;

	TEST_EQUAL(minos::file_read_async(file, MutRange{ buf1 }, &overlapped1), true);

	byte buf2[1024];

	minos::Overlapped overlapped2{};
	overlapped2.offset = 0;

	TEST_EQUAL(minos::file_read_async(file, MutRange{ buf2 }, &overlapped2), true);

	minos::CompletionResult read_result1;

	TEST_EQUAL(minos::completion_wait(completion, &read_result1), true);

	TEST_EQUAL(read_result1.key, 1234);

	TEST_EQUAL(read_result1.bytes, 14);

	TEST_EQUAL(read_result1.overlapped == &overlapped1 || read_result1.overlapped == &overlapped2, true);

	TEST_MEM_EQUAL(buf1, "abcdefghijklmn", 14);

	minos::CompletionResult read_result2;

	TEST_EQUAL(minos::completion_wait(completion, &read_result2), true);

	TEST_EQUAL(read_result2.key, 1234);

	TEST_EQUAL(read_result2.bytes, 14);

	TEST_EQUAL(read_result2.overlapped == &overlapped1 || read_result2.overlapped == &overlapped2, true);

	TEST_UNEQUAL(read_result1.overlapped, read_result2.overlapped);

	TEST_MEM_EQUAL(buf2, "abcdefghijklmn", 14);

	minos::file_close(file);

	minos::completion_close(completion);

	MINOS_TEST_END;
}


static void process_create_with_empty_exe_path_and_empty_working_directory_spawns_self_in_same_directory() noexcept
{
	MINOS_TEST_BEGIN;

	char8 cwd[8192];

	const u32 cwd_chars = minos::working_directory(MutRange{ cwd });

	TEST_EQUAL(cwd_chars != 0 && cwd_chars <= array_count(cwd), true);

	Range<char8> command_line[] {
		range::from_literal_string("--check-cwd"),
		Range{ cwd, cwd_chars },
	};

	minos::ProcessHandle process;

	TEST_EQUAL(minos::process_create({}, Range{ command_line }, {}, {}, false, &process), true);

	u32 process_result;

	TEST_EQUAL(minos::process_wait_timeout(process, TIMEOUT_TEST_MILLIS, &process_result), true);

	TEST_EQUAL(process_result, 0);

	minos::process_close(process);

	MINOS_TEST_END;
}

static void process_create_with_empty_exe_path_and_given_working_directory_spawns_self_in_given_directory() noexcept
{
	MINOS_TEST_BEGIN;

	Range<char8> command_line[] {
		range::from_literal_string("--check-cwd"),
		range::from_literal_string("minos_fs_data"),
	};

	minos::ProcessHandle process;

	TEST_EQUAL(minos::process_create({}, Range{ command_line }, range::from_literal_string("minos_fs_data"), {}, false, &process), true);

	u32 process_result;

	TEST_EQUAL(minos::process_wait_timeout(process, TIMEOUT_TEST_MILLIS, &process_result), true);

	TEST_EQUAL(process_result, 0);

	minos::process_close(process);

	MINOS_TEST_END;
}

static void process_create_with_given_exe_path_and_empty_working_directory_spawns_given_exe_in_same_directory() noexcept
{
	MINOS_TEST_BEGIN;

	Range<char8> command_line[] {
		range::from_literal_string("test"),
	};

	minos::ProcessHandle process;

	TEST_EQUAL(minos::process_create(range::from_literal_string(HELPER_PROCESS_PATH), Range{ command_line }, {}, {}, false, &process), true);

	u32 process_result;

	TEST_EQUAL(minos::process_wait_timeout(process, TIMEOUT_TEST_MILLIS, &process_result), true);

	TEST_EQUAL(process_result, 0);

	minos::process_close(process);

	MINOS_TEST_END;
}

static void process_create_with_given_exe_path_and_given_working_directory_spawns_given_exe_in_given_directory() noexcept
{
	MINOS_TEST_BEGIN;

	Range<char8> command_line[] {
		range::from_literal_string("minos_fs_data"),
	};

	minos::ProcessHandle process;

	TEST_EQUAL(minos::process_create(range::from_literal_string(HELPER_PROCESS_PATH), Range{ command_line }, range::from_literal_string("./minos_fs_data"), {}, false, &process), true);

	u32 process_result;

	TEST_EQUAL(minos::process_wait_timeout(process, TIMEOUT_TEST_MILLIS, &process_result), true);

	TEST_EQUAL(process_result, 0);

	minos::process_close(process);

	MINOS_TEST_END;
}

static void process_create_makes_inherited_handles_available_to_child() noexcept
{
	MINOS_TEST_BEGIN;

	minos::EventHandle event;

	TEST_EQUAL(minos::event_create(&event), true);

	// Pre-wake the event to minimize waiting
	minos::event_wake(event);

	char8 event_buf[64];

	Range<char8> command_line[] {
		range::from_literal_string("--event-wait"),
		format_u64(reinterpret_cast<u64>(event.m_rep), MutRange{ event_buf }),
	};

	minos::ProcessHandle process;

	minos::GenericHandle inherited_handles[] = {
		event
	};

	TEST_EQUAL(minos::process_create({}, Range{ command_line }, {}, Range{ inherited_handles }, false, &process), true);

	u32 process_result;

	TEST_EQUAL(minos::process_wait_timeout(process, TIMEOUT_TEST_MILLIS, &process_result), true);

	TEST_EQUAL(process_result, 0);

	minos::process_close(process);

	minos::event_close(event);

	MINOS_TEST_END;
}

static void process_wait_timeout_on_sleeping_process_times_out() noexcept
{
	MINOS_TEST_BEGIN;

	minos::EventHandle event;

	TEST_EQUAL(minos::event_create(&event), true);

	char8 event_buf[64];

	Range<char8> command_line[] {
		range::from_literal_string("--event-wait"),
		format_u64(reinterpret_cast<u64>(event.m_rep), MutRange{ event_buf }),
	};

	minos::ProcessHandle process;

	minos::GenericHandle inherited_handles[] = {
		event
	};

	TEST_EQUAL(minos::process_create({}, Range{ command_line }, {}, Range{ inherited_handles }, false, &process), true);

	TEST_EQUAL(minos::process_wait_timeout(process, TIMEOUT_TEST_MILLIS, nullptr), false);

	minos::event_wake(event);

	u32 process_result;

	TEST_EQUAL(minos::process_wait_timeout(process, TIMEOUT_TEST_MILLIS, &process_result), true);

	TEST_EQUAL(process_result, 0);

	minos::process_close(process);

	minos::event_close(event);

	MINOS_TEST_END;
}

static void process_wait_waits_for_process_to_exit() noexcept
{
	MINOS_TEST_BEGIN;

	minos::EventHandle event;

	TEST_EQUAL(minos::event_create(&event), true);

	char8 event_buf[64];

	Range<char8> command_line[] {
		range::from_literal_string("--event-wait"),
		format_u64(reinterpret_cast<u64>(event.m_rep), MutRange{ event_buf }),
		range::from_literal_string("--timeout"),
		range::from_literal_string("1"),
	};

	minos::ProcessHandle process;

	minos::GenericHandle inherited_handles[] = {
		event
	};

	TEST_EQUAL(minos::process_create({}, Range{ command_line }, {}, Range{ inherited_handles }, false, &process), true);

	u32 process_result;

	minos::process_wait(process, &process_result);

	TEST_EQUAL(process_result, 2);

	minos::process_close(process);

	minos::event_close(event);

	MINOS_TEST_END;
}

static void process_wait_on_exited_process_still_works() noexcept
{
	MINOS_TEST_BEGIN;

	Range<char8> command_line[] {
		range::from_literal_string("--exit-with"),
		range::from_literal_string("17"),
	};

	minos::ProcessHandle process;

	TEST_EQUAL(minos::process_create({}, Range{ command_line }, {}, {}, false, &process), true);

	u32 process_result;

	minos::process_wait(process, nullptr);

	minos::process_wait(process, &process_result);

	TEST_EQUAL(process_result, 17);

	minos::process_close(process);

	MINOS_TEST_END;
}


static void shm_create_with_small_requst_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::ShmHandle shm;

	TEST_EQUAL(minos::shm_create(minos::Access::Read | minos::Access::Write, 1024, &shm), true);

	minos::shm_close(shm);

	MINOS_TEST_END;
}

static void shm_create_with_large_requst_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::ShmHandle shm;

	TEST_EQUAL(minos::shm_create(minos::Access::Read | minos::Access::Write, 1024 * 1024 * 1024, &shm), true);

	minos::shm_close(shm);

	MINOS_TEST_END;
}

static void shm_create_with_execute_access_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::ShmHandle shm;

	TEST_EQUAL(minos::shm_create(minos::Access::Read | minos::Access::Execute, 1024, &shm), true);

	minos::shm_close(shm);

	MINOS_TEST_END;
}

static void shm_reserve_of_entire_shm_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::ShmHandle shm;

	TEST_EQUAL(minos::shm_create(minos::Access::Read | minos::Access::Write, 1024 * 1024, &shm), true);

	byte* const mem = static_cast<byte*>(minos::shm_reserve(shm, 0, 1024 * 1024));

	TEST_UNEQUAL(mem, nullptr);

	minos::shm_unreserve(mem, 1024 * 1024);

	minos::shm_close(shm);

	MINOS_TEST_END;
}

static void shm_reserve_of_shm_subrange_at_zero_offset_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::ShmHandle shm;

	TEST_EQUAL(minos::shm_create(minos::Access::Read | minos::Access::Write, 1024 * 1024, &shm), true);

	byte* const mem = static_cast<byte*>(minos::shm_reserve(shm, 0, 1024));

	TEST_UNEQUAL(mem, nullptr);

	minos::shm_unreserve(mem, 1024);

	minos::shm_close(shm);

	MINOS_TEST_END;
}

static void shm_reserve_of_shm_subrange_at_nonzero_aligned_offset_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::ShmHandle shm;

	TEST_EQUAL(minos::shm_create(minos::Access::Read | minos::Access::Write, 1024 * 1024, &shm), true);

	// Note that the below `offset` of 65536 is the alignment required by a
	// native call to win32's `MapViewOfFile`, underlying `minos::shm_reserve`.
	byte* const mem = static_cast<byte*>(minos::shm_reserve(shm, 65536, 1024));

	TEST_UNEQUAL(mem, nullptr);

	minos::shm_unreserve(mem, 1024);

	minos::shm_close(shm);

	MINOS_TEST_END;
}

static void shm_reserve_of_shm_subrange_at_nonzero_unaligned_offset_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::ShmHandle shm;

	TEST_EQUAL(minos::shm_create(minos::Access::Read | minos::Access::Write, 1024 * 1024, &shm), true);

	byte* const mem = static_cast<byte*>(minos::shm_reserve(shm, 585410, 1024));

	TEST_UNEQUAL(mem, nullptr);

	minos::shm_unreserve(mem, 1024);

	minos::shm_close(shm);

	MINOS_TEST_END;
}

static void shm_commit_read_of_reserved_shm_succeeds_and_is_readable() noexcept
{
	MINOS_TEST_BEGIN;

	minos::ShmHandle shm;

	TEST_EQUAL(minos::shm_create(minos::Access::Read | minos::Access::Write, 1024, &shm), true);

	byte* const mem = static_cast<byte*>(minos::shm_reserve(shm, 0, 1024));

	TEST_UNEQUAL(mem, nullptr);

	TEST_EQUAL(minos::shm_commit(mem, minos::Access::Read, 1024), true);

	TEST_EQUAL(mem[0], 0);

	TEST_EQUAL(mem[1024 - 1], 0);

	minos::shm_unreserve(mem, 1024);

	minos::shm_close(shm);

	MINOS_TEST_END;
}

static void shm_commit_read_write_of_reserved_shm_succeeds_and_is_readable_and_writeable() noexcept
{
	MINOS_TEST_BEGIN;

	minos::ShmHandle shm;

	TEST_EQUAL(minos::shm_create(minos::Access::Read | minos::Access::Write, 1024 * 1024, &shm), true);

	byte* const mem = static_cast<byte*>(minos::shm_reserve(shm, 585410, 1024));

	TEST_UNEQUAL(mem, nullptr);

	TEST_EQUAL(minos::shm_commit(mem, minos::Access::Read | minos::Access::Write, 1024), true);

	TEST_EQUAL(mem[0], 0);

	TEST_EQUAL(mem[1024 - 1], 0);

	mem[0] = 0xCC;

	TEST_EQUAL(mem[0], 0xCC);

	minos::shm_unreserve(mem, 1024);

	minos::shm_close(shm);

	MINOS_TEST_END;
}

static void shm_works_across_processes() noexcept
{
	MINOS_TEST_BEGIN;

	minos::ShmHandle shm;

	TEST_EQUAL(minos::shm_create(minos::Access::Read | minos::Access::Write, 1024 * 1024, &shm), true);

	byte* const mem = static_cast<byte*>(minos::shm_reserve(shm, 0, 1024 * 1024));

	TEST_UNEQUAL(mem, nullptr);

	TEST_EQUAL(minos::shm_commit(mem, minos::Access::Read | minos::Access::Write, 1024 * 1024), true);

	memset(mem, 0xAB, 1024 * 1024);

	char8 arg_bufs[9][64];

	Range<char8> command_line[] = {
		range::from_literal_string("--shm"),
		format_handle(shm, MutRange{ arg_bufs[0] }),
		format_u64(0, MutRange{ arg_bufs[1] }), // reserve offset
		format_u64(1024 * 1024, MutRange{ arg_bufs[2] }), // reserve bytes
		format_u64(0, MutRange{ arg_bufs[3] }), // commit offset
		format_u64(1024 * 1024, MutRange{ arg_bufs[4] }), // commit bytes
		format_u64(10, MutRange{ arg_bufs[5] }), // read offset
		format_u64(0xAB, MutRange{ arg_bufs[6] }), // commit value
		format_u64(15, MutRange{ arg_bufs[7] }), // write offset
		format_u64(0xF3, MutRange{ arg_bufs[8] }), // write value
	};

	minos::GenericHandle inherited_handles[] = { shm };

	minos::ProcessHandle process;

	TEST_EQUAL(minos::process_create({}, Range{ command_line }, {}, Range{ inherited_handles }, false, &process), true);

	u32 process_result;

	TEST_EQUAL(minos::process_wait_timeout(process, TIMEOUT_TEST_MILLIS, &process_result), true);

	TEST_EQUAL(process_result, 0);

	minos::process_close(process);

	minos::shm_unreserve(mem, 1024 * 1024);

	minos::shm_close(shm);

	MINOS_TEST_END;
}


struct SemaphoreThreadParams
{
	minos::SemaphoreHandle semaphore;

	bool use_timeout;

	u32 timeout_milliseconds;
};

static u32 THREAD_PROC semaphore_wait_proc(void* param) noexcept
{
	SemaphoreThreadParams* const params = static_cast<SemaphoreThreadParams*>(param);

	if (params->use_timeout)
		return minos::semaphore_wait_timeout(params->semaphore, params->timeout_milliseconds) ? 0 : 1;

	minos::semaphore_wait(params->semaphore);

	return 0;
}

static void sem_create_creates_a_semaphore() noexcept
{
	MINOS_TEST_BEGIN;

	minos::SemaphoreHandle semaphore;

	TEST_EQUAL(minos::sempahore_create(0, &semaphore), true);

	minos::semaphore_close(semaphore);

	MINOS_TEST_END;
}

static void sem_create_with_initial_count_1_allows_1_wait() noexcept
{
	MINOS_TEST_BEGIN;

	minos::SemaphoreHandle semaphore;

	TEST_EQUAL(minos::sempahore_create(1, &semaphore), true);

	TEST_EQUAL(minos::semaphore_wait_timeout(semaphore, 1), true);

	minos::semaphore_close(semaphore);

	MINOS_TEST_END;
}

static void sem_create_with_initial_count_0_allows_no_waits() noexcept
{
	MINOS_TEST_BEGIN;

	minos::SemaphoreHandle semaphore;

	TEST_EQUAL(minos::sempahore_create(0, &semaphore), true);

	TEST_EQUAL(minos::semaphore_wait_timeout(semaphore, 1), false);

	minos::semaphore_close(semaphore);

	MINOS_TEST_END;
}

static void sem_create_with_initial_count_5_allows_5_waits() noexcept
{
	MINOS_TEST_BEGIN;

	minos::SemaphoreHandle semaphore;

	TEST_EQUAL(minos::sempahore_create(5, &semaphore), true);

	TEST_EQUAL(minos::semaphore_wait_timeout(semaphore, 1), true);

	TEST_EQUAL(minos::semaphore_wait_timeout(semaphore, 1), true);

	TEST_EQUAL(minos::semaphore_wait_timeout(semaphore, 1), true);

	TEST_EQUAL(minos::semaphore_wait_timeout(semaphore, 1), true);

	TEST_EQUAL(minos::semaphore_wait_timeout(semaphore, 1), true);

	TEST_EQUAL(minos::semaphore_wait_timeout(semaphore, 1), false);

	minos::semaphore_close(semaphore);

	MINOS_TEST_END;
}

static void sem_post_one_allows_one_wait() noexcept
{
	MINOS_TEST_BEGIN;

	minos::SemaphoreHandle semaphore;

	TEST_EQUAL(minos::sempahore_create(0, &semaphore), true);

	minos::semaphore_post(semaphore, 1);

	TEST_EQUAL(minos::semaphore_wait_timeout(semaphore, 1), true);

	minos::semaphore_close(semaphore);

	MINOS_TEST_END;
}

static void sem_post_two_allows_two_waits() noexcept
{
	MINOS_TEST_BEGIN;

	minos::SemaphoreHandle semaphore;

	TEST_EQUAL(minos::sempahore_create(0, &semaphore), true);

	minos::semaphore_post(semaphore, 2);

	TEST_EQUAL(minos::semaphore_wait_timeout(semaphore, 1), true);

	TEST_EQUAL(minos::semaphore_wait_timeout(semaphore, 1), true);

	TEST_EQUAL(minos::semaphore_wait_timeout(semaphore, 1), false);

	minos::semaphore_close(semaphore);

	MINOS_TEST_END;
}

static void sem_wait_waits_until_post() noexcept
{
	MINOS_TEST_BEGIN;

	minos::SemaphoreHandle semaphore;

	TEST_EQUAL(minos::sempahore_create(0, &semaphore), true);

	SemaphoreThreadParams params;
	params.semaphore = semaphore;
	params.use_timeout = false;

	minos::ThreadHandle thread;

	TEST_EQUAL(minos::thread_create(semaphore_wait_proc, &params, range::from_literal_string("semaphore_wait"), &thread), true);

	minos::semaphore_post(semaphore, 1);

	u32 thread_result;

	TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, &thread_result), true);

	TEST_EQUAL(thread_result, 0);

	minos::semaphore_close(semaphore);

	MINOS_TEST_END;
}

static void sem_wait_timeout_with_long_timeout_waits_until_post() noexcept
{
	MINOS_TEST_BEGIN;

	minos::SemaphoreHandle semaphore;

	TEST_EQUAL(minos::sempahore_create(0, &semaphore), true);

	SemaphoreThreadParams params;
	params.semaphore = semaphore;
	params.use_timeout = true;
	params.timeout_milliseconds = 1000;

	minos::ThreadHandle thread;

	TEST_EQUAL(minos::thread_create(semaphore_wait_proc, &params, range::from_literal_string("semaphore_wait"), &thread), true);

	minos::semaphore_post(semaphore, 1);

	u32 thread_result;

	TEST_EQUAL(minos::thread_wait_timeout(thread, TIMEOUT_TEST_MILLIS, &thread_result), true);

	TEST_EQUAL(thread_result, 0);

	minos::semaphore_close(semaphore);

	MINOS_TEST_END;
}

static void sem_wait_and_post_work_across_processes() noexcept
{
	MINOS_TEST_BEGIN;

	minos::SemaphoreHandle semaphore;

	TEST_EQUAL(minos::sempahore_create(1, &semaphore), true);

	char8 semaphore_buf[64];

	char8 timeout_buf[64];

	Range<char8> command_line[] = {
		range::from_literal_string("--semaphore-wait"),
		format_handle(semaphore, MutRange{ semaphore_buf }),
		range::from_literal_string("--timeout"),
		format_u64(TIMEOUT_TEST_MILLIS, MutRange{ timeout_buf }),
	};

	minos::GenericHandle inherited_handles[] = { semaphore };

	minos::ProcessHandle process;

	TEST_EQUAL(minos::process_create({}, Range{ command_line }, {}, Range{ inherited_handles }, false, &process), true);

	u32 process_result;

	TEST_EQUAL(minos::process_wait_timeout(process, TIMEOUT_TEST_MILLIS, &process_result), true);

	TEST_EQUAL(process_result, 0);

	minos::semaphore_close(semaphore);

	minos::process_close(process);

	MINOS_TEST_END;
}


static void directory_enumeration_create_on_empty_directory_returns_no_more_files() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::directory_create(range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_empty_dir")), true);

	minos::DirectoryEnumerationHandle enumeration;

	minos::DirectoryEnumerationResult result;

	TEST_EQUAL(minos::directory_enumeration_create(range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_empty_dir"), &enumeration, &result), minos::DirectoryEnumerationStatus::NoMoreFiles);

	minos::directory_enumeration_close(enumeration);

	MINOS_TEST_END;
}

static void directory_enumeration_on_directory_with_one_file_returns_that_file_then_no_more_files() noexcept
{
	MINOS_TEST_BEGIN;

	minos::DirectoryEnumerationHandle enumeration;

	minos::DirectoryEnumerationResult result;

	TEST_EQUAL(minos::directory_enumeration_create(range::from_literal_string("minos_fs_data/directory_with_1_file"), &enumeration, &result), minos::DirectoryEnumerationStatus::Ok);

	TEST_EQUAL(result.is_directory, false);

	TEST_EQUAL(result.bytes, 0);

	TEST_MEM_EQUAL(result.filename, "file_1", sizeof("file_1"));

	TEST_EQUAL(minos::directory_enumeration_next(enumeration, &result), minos::DirectoryEnumerationStatus::NoMoreFiles);

	minos::directory_enumeration_close(enumeration);

	MINOS_TEST_END;
}

static void directory_enumeration_on_directory_with_5_files_returns_those_files_then_no_more_files() noexcept
{
	MINOS_TEST_BEGIN;

	minos::DirectoryEnumerationHandle enumeration;

	minos::DirectoryEnumerationResult result;

	TEST_EQUAL(minos::directory_enumeration_create(range::from_literal_string("minos_fs_data/directory_with_5_files"), &enumeration, &result), minos::DirectoryEnumerationStatus::Ok);

	TEST_EQUAL(result.is_directory, false);

	TEST_EQUAL(result.bytes, 0);


	TEST_EQUAL(minos::directory_enumeration_next(enumeration, &result), minos::DirectoryEnumerationStatus::Ok);

	TEST_EQUAL(result.is_directory, false);

	TEST_EQUAL(result.bytes, 0);


	TEST_EQUAL(minos::directory_enumeration_next(enumeration, &result), minos::DirectoryEnumerationStatus::Ok);

	TEST_EQUAL(result.is_directory, false);

	TEST_EQUAL(result.bytes, 0);


	TEST_EQUAL(minos::directory_enumeration_next(enumeration, &result), minos::DirectoryEnumerationStatus::Ok);

	TEST_EQUAL(result.is_directory, false);

	TEST_EQUAL(result.bytes, 0);


	TEST_EQUAL(minos::directory_enumeration_next(enumeration, &result), minos::DirectoryEnumerationStatus::Ok);

	TEST_EQUAL(result.is_directory, false);

	TEST_EQUAL(result.bytes, 0);


	TEST_EQUAL(minos::directory_enumeration_next(enumeration, &result), minos::DirectoryEnumerationStatus::NoMoreFiles);


	minos::directory_enumeration_close(enumeration);

	MINOS_TEST_END;
}

static void directory_enumeration_on_directory_with_subdirectory_returns_that_subdirectory_then_no_more_files() noexcept
{
	MINOS_TEST_BEGIN;

	minos::DirectoryEnumerationHandle enumeration;

	minos::DirectoryEnumerationResult result;

	TEST_EQUAL(minos::directory_enumeration_create(range::from_literal_string("minos_fs_data/directory_with_1_subdirectory"), &enumeration, &result), minos::DirectoryEnumerationStatus::Ok);

	TEST_EQUAL(result.is_directory, true);

	TEST_EQUAL(result.bytes, 0);

	TEST_MEM_EQUAL(result.filename, "subdirectory", sizeof("subdirectory"));

	TEST_EQUAL(minos::directory_enumeration_next(enumeration, &result), minos::DirectoryEnumerationStatus::NoMoreFiles);

	minos::directory_enumeration_close(enumeration);

	MINOS_TEST_END;
}


static void directory_create_on_new_path_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::directory_create(range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_DIR_A")), true);

	MINOS_TEST_END;
}

static void directory_create_on_existing_path_fails() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::directory_create(range::from_literal_string("minos_fs_data")), false);

	MINOS_TEST_END;
}


static void path_remove_file_on_file_path_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_J"), minos::Access::Write, minos::ExistsMode::Fail, minos::NewMode::Create, minos::AccessPattern::Sequential, nullptr, false, &file), true);

	minos::file_close(file);

	TEST_EQUAL(minos::path_remove_file(range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_J")), true);

	MINOS_TEST_END;
}

static void path_remove_file_on_directory_path_fails() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::directory_create(range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_DIR_B")), true);

	TEST_EQUAL(minos::path_remove_file(range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_DIR_B")), false);

	MINOS_TEST_END;
}

static void path_remove_file_on_nonexistent_path_fails() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::path_remove_file(range::from_literal_string("minos_fs_data/nonexistent_path")), false);

	MINOS_TEST_END;
}


static void path_remove_directory_on_directory_path_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::directory_create(range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_DIR_C")), true);

	TEST_EQUAL(minos::path_remove_directory(range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_DIR_C")), true);

	MINOS_TEST_END;
}

static void path_remove_directory_on_file_path_fails() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileHandle file;

	TEST_EQUAL(minos::file_create(range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_K"), minos::Access::Write, minos::ExistsMode::Fail, minos::NewMode::Create, minos::AccessPattern::Sequential, nullptr, false, &file), true);

	minos::file_close(file);

	TEST_EQUAL(minos::path_remove_directory(range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME "/DELETEME_K")), false);

	MINOS_TEST_END;
}

static void path_remove_directory_on_nonexistent_path_fails() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::path_remove_directory(range::from_literal_string("minos_fs_data/nonexistent_path")), false);

	MINOS_TEST_END;
}


static void path_is_directory_on_directory_path_returns_true() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::path_is_directory(range::from_literal_string("minos_fs_data")), true);

	MINOS_TEST_END;
}

static void path_is_directory_on_file_path_returns_false() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::path_is_directory(range::from_literal_string("minos_fs_data/empty_file")), false);

	MINOS_TEST_END;
}

static void path_is_directory_on_nonexistent_path_returns_false() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::path_is_directory(range::from_literal_string("minos_fs_data/nonexistent_path")), false);

	MINOS_TEST_END;
}


static void path_is_file_on_file_path_returns_true() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::path_is_file(range::from_literal_string("minos_fs_data/empty_file")), true);

	MINOS_TEST_END;
}

static void path_is_file_on_directory_path_returns_false() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::path_is_file(range::from_literal_string("minos_fs_data")), false);

	MINOS_TEST_END;
}

static void path_is_file_on_nonexistent_path_returns_false() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_EQUAL(minos::path_is_file(range::from_literal_string("minos_fs_data/nonexistent_path")), false);

	MINOS_TEST_END;
}


static void path_to_absolute_on_absolute_path_returns_that_path() noexcept
{
	MINOS_TEST_BEGIN;

	const Range<char8> path = range::from_literal_string("/xyz/abc/d");

	char8 path_buf[1024];

	const u32 path_chars = minos::path_to_absolute(path, MutRange{ path_buf });

	#ifdef _WIN32
		const Range<char8> win32_path = range::from_literal_string(":\\xyz\\abc\\d");

		TEST_EQUAL(path_chars, win32_path.count() + 1);

		TEST_MEM_EQUAL(win32_path.begin(), path_buf + 1, win32_path.count());
	#else
		TEST_EQUAL(path_chars, path.count());

		TEST_MEM_EQUAL(path.begin(), path_buf, path.count());
	#endif
	MINOS_TEST_END;
}

static void path_to_absolute_on_absolute_path_with_dot_returns_that_path_without_dot() noexcept
{
	MINOS_TEST_BEGIN;

	const Range<char8> path = range::from_literal_string("/./a/b/c");

	char8 path_buf[1024];

	const u32 path_chars = minos::path_to_absolute(path, MutRange{ path_buf });

	#ifdef _WIN32
		const Range<char8> win32_path = range::from_literal_string(":\\a\\b\\c");

		TEST_EQUAL(path_chars, win32_path.count() + 1);

		TEST_MEM_EQUAL(win32_path.begin(), path_buf + 1, win32_path.count());
	#else
		const Range<char8> linux_path = range::from_literal_string("/a/b/c");

		TEST_EQUAL(path_chars, linux_path.count());

		TEST_MEM_EQUAL(linux_path.begin(), path_buf, linux_path.count());
	#endif
	MINOS_TEST_END;
}

static void path_to_absolute_on_absolute_path_with_double_dash_returns_that_path_with_single_dash() noexcept
{
	MINOS_TEST_BEGIN;

	const Range<char8> path = range::from_literal_string("/a//b/c");

	char8 path_buf[1024];

	const u32 path_chars = minos::path_to_absolute(path, MutRange{ path_buf });

	#ifdef _WIN32
		const Range<char8> win32_path = range::from_literal_string(":\\a\\b\\c");

		TEST_EQUAL(path_chars, win32_path.count() + 1);

		TEST_MEM_EQUAL(win32_path.begin(), path_buf + 1, win32_path.count());
	#else
		const Range<char8> linux_path = range::from_literal_string("/a/b/c");

		TEST_EQUAL(path_chars, linux_path.count());

		TEST_MEM_EQUAL(linux_path.begin(), path_buf, linux_path.count());
	#endif
	MINOS_TEST_END;
}

static void path_to_absolute_on_root_path_returns_root_path() noexcept
{
	MINOS_TEST_BEGIN;

	const Range<char8> path = range::from_literal_string("/");

	char8 path_buf[1024];

	const u32 path_chars = minos::path_to_absolute(path, MutRange{ path_buf });

	#ifdef _WIN32
		const Range<char8> win32_path = range::from_literal_string(":");

		TEST_EQUAL(path_chars, win32_path.count() + 1);

		TEST_MEM_EQUAL(win32_path.begin(), path_buf + 1, win32_path.count());
	#else
		TEST_EQUAL(path_chars, path.count());

		TEST_MEM_EQUAL(path.begin(), path_buf, path.count());
	#endif
	MINOS_TEST_END;
}

static void path_to_absolute_on_relative_path_returns_an_absolute_path() noexcept
{
	MINOS_TEST_BEGIN;

	const Range<char8> path = range::from_literal_string("a/b/c");

	char8 path_buf[1024];

	const u32 path_chars = minos::path_to_absolute(path, MutRange{ path_buf });

	char8 wd_buf[1024];

	const u32 wd_chars = minos::working_directory(MutRange{ wd_buf });

	TEST_EQUAL(path_chars, wd_chars + path.count() + 1);

	TEST_MEM_EQUAL(path_buf, wd_buf, wd_chars);

	#ifdef _WIN32
		TEST_MEM_EQUAL(path_buf + wd_chars, "\\a\\b\\c", sizeof("\\a\\b\\c") - 1);
	#else
		TEST_MEM_EQUAL(path_buf + wd_chars, "/a/b/c", sizeof("/a/b/c") - 1);
	#endif

	MINOS_TEST_END;
}

static void path_to_absolute_on_relative_path_with_dot_returns_an_absolute_path_ignoring_dot() noexcept
{
	MINOS_TEST_BEGIN;

	const Range<char8> path = range::from_literal_string("a/./b/c");

	char8 path_buf[1024];

	const u32 path_chars = minos::path_to_absolute(path, MutRange{ path_buf });

	char8 wd_buf[1024];

	const u32 wd_chars = minos::working_directory(MutRange{ wd_buf });

	TEST_EQUAL(path_chars, wd_chars + path.count() - 1);

	TEST_MEM_EQUAL(path_buf, wd_buf, wd_chars);

	#ifdef _WIN32
		TEST_MEM_EQUAL(path_buf + wd_chars, "\\a\\b\\c", sizeof("\\a\\b\\c") - 1);
	#else
		TEST_MEM_EQUAL(path_buf + wd_chars, "/a/b/c", sizeof("/a/b/c") - 1);
	#endif

	MINOS_TEST_END;
}

static void path_to_absolute_on_relative_path_with_double_dot_returns_an_absolute_path_ignoring_element_before_double_dot() noexcept
{
	MINOS_TEST_BEGIN;

	const Range<char8> path = range::from_literal_string("a/../b/c");

	char8 path_buf[1024];

	const u32 path_chars = minos::path_to_absolute(path, MutRange{ path_buf });

	char8 wd_buf[1024];

	const u32 wd_chars = minos::working_directory(MutRange{ wd_buf });

	TEST_EQUAL(path_chars, wd_chars + 4);

	TEST_MEM_EQUAL(path_buf, wd_buf, wd_chars);

	#ifdef _WIN32
		TEST_MEM_EQUAL(path_buf + wd_chars, "\\b\\c", sizeof("\\b\\c") - 1);
	#else
		TEST_MEM_EQUAL(path_buf + wd_chars, "/b/c", sizeof("/b/c") - 1);
	#endif

	MINOS_TEST_END;
}


static void path_to_absolute_relative_to_with_absolute_path_returns_that_path() noexcept
{
	MINOS_TEST_BEGIN;

	const Range<char8> path = range::from_literal_string("/this/is/a/path");

	const Range<char8> base = range::from_literal_string("/base");

	char8 path_buf[1024];

	const u32 path_chars = minos::path_to_absolute_relative_to(path, base, MutRange{ path_buf });

	#ifdef _WIN32
		const Range<char8> win32_path = range::from_literal_string(":\\this\\is\\a\\path");

		TEST_EQUAL(path_chars, win32_path.count() + 1);

		TEST_MEM_EQUAL(path_buf + 1, win32_path.begin(), win32_path.count());
	#else
		TEST_EQUAL(path_chars, path.count());

		TEST_MEM_EQUAL(path_buf, path.begin(), path.count());
	#endif

	MINOS_TEST_END;
}

static void path_to_absolute_relative_to_with_absolute_base_returns_path_appended_to_that_base() noexcept
{
	MINOS_TEST_BEGIN;

	const Range<char8> path = range::from_literal_string("this/is/a/path");

	const Range<char8> base = range::from_literal_string("/base");

	char8 path_buf[1024];

	const u32 path_chars = minos::path_to_absolute_relative_to(path, base, MutRange{ path_buf });

	#ifdef _WIN32
		const Range<char8> win32_path = range::from_literal_string(":\\base\\this\\is\\a\\path");

		TEST_EQUAL(path_chars, win32_path.count() + 1);

		TEST_MEM_EQUAL(path_buf + 1, win32_path.begin(), win32_path.count());
	#else
		const Range<char8> linux_path = range::from_literal_string("/base/this/is/a/path");

		TEST_EQUAL(path_chars, linux_path.count());

		TEST_MEM_EQUAL(path_buf, linux_path.begin(), linux_path.count());
	#endif

	MINOS_TEST_END;
}

static void path_to_absolute_relative_to_with_relative_base_returns_path_appended_to_absolute_base() noexcept
{
	MINOS_TEST_BEGIN;

	const Range<char8> path = range::from_literal_string("this/is/a/path");

	const Range<char8> base = range::from_literal_string("base");

	char8 path_buf[1024];

	const u32 path_chars = minos::path_to_absolute_relative_to(path, base, MutRange{ path_buf });

	char8 wd_buf[1024];

	const u32 wd_chars = minos::working_directory(MutRange{ wd_buf });

	TEST_MEM_EQUAL(path_buf, wd_buf, wd_chars);

	#ifdef _WIN32
		const Range<char8> expected = range::from_literal_string("\\base\\this\\is\\a\\path");
	#else
		const Range<char8> expected = range::from_literal_string("/base/this/is/a/path");
	#endif

	TEST_EQUAL(path_chars, wd_chars + expected.count());

	TEST_MEM_EQUAL(path_buf + wd_chars, expected.begin(), expected.count());

	MINOS_TEST_END;
}


// TODO: path_to_absolute_directory


static void path_get_info_on_nonexistent_path_fails() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileInfo fileinfo;

	TEST_EQUAL(minos::path_get_info(range::from_literal_string("minos_fs_data/nonexistent_path"), &fileinfo), false);

	MINOS_TEST_END;
}

static void path_get_info_on_file_path_returns_is_not_directory_and_file_size() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileInfo fileinfo;

	TEST_EQUAL(minos::path_get_info(range::from_literal_string("minos_fs_data/short_file"), &fileinfo), true);

	TEST_EQUAL(fileinfo.is_directory, false);

	TEST_EQUAL(fileinfo.bytes, 14);

	MINOS_TEST_END;
}

static void path_get_info_on_directory_path_returns_is_directory_and_0_bytes() noexcept
{
	MINOS_TEST_BEGIN;

	minos::FileInfo fileinfo;

	TEST_EQUAL(minos::path_get_info(range::from_literal_string("minos_fs_data"), &fileinfo), true);

	TEST_EQUAL(fileinfo.is_directory, true);

	TEST_EQUAL(fileinfo.bytes, 0);

	MINOS_TEST_END;
}


static void timestamp_utc_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	(void) minos::timestamp_utc();

	MINOS_TEST_END;
}

static void timestamp_ticks_per_second_succeeds_and_returns_nonzero() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_UNEQUAL(minos::timestamp_ticks_per_second(), 0);

	MINOS_TEST_END;
}


static void exact_timestamp_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	(void) minos::exact_timestamp();

	MINOS_TEST_END;
}

static void exact_timestamp_ticks_per_second_succeeds_and_returns_nonzero() noexcept
{
	MINOS_TEST_BEGIN;

	TEST_UNEQUAL(minos::exact_timestamp_ticks_per_second(), 0);

	MINOS_TEST_END;
}

static void exact_timestamp_then_sleep_10_milliseconds_then_exact_timestamp_again_has_approximately_correct_difference() noexcept
{
	MINOS_TEST_BEGIN;

	const u64 start = minos::exact_timestamp();

	minos::sleep(10);

	const u64 end = minos::exact_timestamp();

	const u64 tps = minos::exact_timestamp_ticks_per_second();

	const u64 elapsed_ms = (end - start) / (tps / 1000);

	TEST_EQUAL(elapsed_ms > 5 && elapsed_ms < 50, true);

	MINOS_TEST_END;
}


static void prepare_minos_tests() noexcept
{
	// The prefix with COMPILER_NAME is necessary so that different tests
	// running in parallel - as is done by build-all.ps1 - do not clobber each
	// other's data.
	static constexpr Range<char8> individual_directory = range::from_literal_string("minos_fs_data/dynamic_data/" COMPILER_NAME);

	if (!minos::path_is_directory(individual_directory))
	{
		if (!minos::directory_create(individual_directory))
			panic("Failed to create dynamic test file directory %.*s (0x%X)\n", static_cast<s32>(individual_directory.count()), individual_directory.begin(), minos::last_error());
	}

	// Clean up data from previous runs. Take care to order paths_to_delete so
	// that children are removed before their parents.

	static constexpr const AttachmentRange<char8, bool> paths_to_delete[] = {
		AttachmentRange{ range::from_literal_string("DELETEME_A"), false },
		AttachmentRange{ range::from_literal_string("DELETEME_B"), false },
		AttachmentRange{ range::from_literal_string("DELETEME_C"), false },
		AttachmentRange{ range::from_literal_string("DELETEME_D"), false },
		AttachmentRange{ range::from_literal_string("DELETEME_E"), false },
		AttachmentRange{ range::from_literal_string("DELETEME_F"), false },
		AttachmentRange{ range::from_literal_string("DELETEME_G"), false },
		AttachmentRange{ range::from_literal_string("DELETEME_H"), false },
		AttachmentRange{ range::from_literal_string("DELETEME_I"), false },
		AttachmentRange{ range::from_literal_string("DELETEME_J"), false },
		AttachmentRange{ range::from_literal_string("DELETEME_K"), false },
		AttachmentRange{ range::from_literal_string("DELETEME_empty_dir"), true },
		AttachmentRange{ range::from_literal_string("DELETEME_DIR_A"), true },
		AttachmentRange{ range::from_literal_string("DELETEME_DIR_B"), true },
		AttachmentRange{ range::from_literal_string("DELETEME_DIR_C"), true },
	};

	char8 path_buf[256];

	memcpy(path_buf, individual_directory.begin(), individual_directory.count());

	path_buf[individual_directory.count()] = '/';

	for (AttachmentRange<char8, bool> path : paths_to_delete)
	{
		const u64 individual_path_chars = individual_directory.count() + 1 + path.count();

		if (sizeof(path_buf) < individual_path_chars)
			panic("Cleanup path for test file %.*s was too long\n", static_cast<s32>(path.count()), path.begin());

		memcpy(path_buf + individual_directory.count() + 1, path.begin(), path.count());

		const Range<char8> individual_path = { path_buf, individual_path_chars };

		if (path.attachment())
		{
			if (!minos::path_is_directory(individual_path))
				continue;

			if (!minos::path_remove_directory(individual_path))
				panic("Failed to clean up directory %.*s from previous test run (0x%X)\n", static_cast<s32>(individual_path.count()), individual_path.begin(), minos::last_error());
		}
		else
		{
			if (!minos::path_is_file(individual_path))
				continue;

			if (!minos::path_remove_file(individual_path))
				panic("Failed to clean up file %.*s from previous test run (0x%X)\n", static_cast<s32>(individual_path.count()), individual_path.begin(), minos::last_error());
		}
	}
}

void minos_tests() noexcept
{
	TEST_MODULE_BEGIN;

	prepare_minos_tests();


	mem_reserve_succeeds_on_small_allocation();

	mem_reserve_succeeds_on_small_odd_sized_allocation();

	mem_reserve_succeeds_on_huge_allocation();

	mem_commit_with_reserved_pointer_and_exact_size_succeeds();

	mem_commit_with_reserved_pointer_and_smaller_size_succeeds();

	mem_commit_with_offset_pointer_and_exact_size_succeeds();

	mem_commit_with_offset_pointer_and_smaller_size_succeeds();

	mem_commit_repeated_on_same_memory_succeeds();

	mem_decommit_on_aligned_pointer_and_exact_size_succeeds();


	page_bytes_returns_nonzero_power_of_two();


	logical_processor_count_returns_nonzero();


	thread_create_and_thread_wait_work();

	thread_wait_timeout_succeeds_on_short_thread();

	thread_wait_timeout_times_out_on_long_thread();


	address_wait_with_4_bytes_and_wake_single_with_changed_value_wakes();

	address_wait_with_4_bytes_and_wake_single_with_unchanged_value_sleeps();

	address_wait_with_2_bytes_and_wake_single_with_changed_value_wakes();

	address_wait_with_2_bytes_and_wake_single_with_unchanged_value_sleeps();

	address_wait_with_1_byte_and_wake_single_with_changed_value_wakes();

	address_wait_with_1_byte_and_wake_single_with_unchanged_value_sleeps();

	multiple_address_wait_and_wake_all_with_changed_value_wakes_all();

	file_create_with_existing_file_path_and_read_access_opens_file();

	file_create_with_existing_file_path_and_write_access_opens_file();

	file_create_with_existing_file_path_and_readwrite_access_opens_file();

	file_create_with_existing_file_path_and_none_access_opens_file();

	file_create_with_existing_file_path_and_unbuffered_access_pattern_opens_file();

	file_create_with_existing_file_path_and_exists_mode_fail_fails();

	file_create_with_existing_file_path_and_exists_mode_truncate_succeeds();

	file_create_with_existing_file_path_and_exists_mode_open_succeeds();

	file_create_with_existing_directory_path_and_none_access_opens_file();

	file_create_with_new_file_path_and_new_mode_fail_fails();

	file_create_with_new_file_path_and_new_mode_create_succeeds();


	file_read_on_empty_file_returns_no_bytes();

	file_read_on_file_shorter_than_buffer_returns_file_size_bytes();

	file_read_on_file_longer_than_buffer_returns_buffer_size_bytes();

	file_read_unbuffered_file_with_page_alignment_and_zero_offset_on_short_file_returns_file_size_bytes();

	file_read_unbuffered_file_with_page_alignment_and_zero_offset_on_long_file_returns_buffer_size_bytes();

	file_read_unbuffered_file_with_page_alignment_and_nonzero_offset_on_medium_file_returns_remaining_file_size_bytes();

	file_read_unbuffered_file_with_page_alignment_and_nonzero_offset_on_long_file_returns_buffer_size_bytes();


	file_write_on_empty_file_appends_to_that_file();

	file_write_on_existing_file_part_overwrites_it();

	file_write_unbuffered_file_with_page_alignment_on_existing_file_part_overwrites_it();

	file_write_unbuffered_file_with_page_alignment_on_unaligned_file_end_overwrites_it_and_appends();

	file_get_info_on_file_handle_returns_not_is_directory_and_file_size();

	file_get_info_on_directory_handle_returns_is_directory();


	file_resize_to_grow_empty_file_succeeds();

	file_resize_to_grow_file_succeeds();

	file_resize_to_shrink_file_succeeds();

	file_resize_to_empty_file_succeeds();


	event_create_creates_an_event();

	event_wake_allows_wait();

	event_wait_waits_until_wake();

	event_wait_timeout_with_long_timeout_waits_until_wake();

	event_wait_timeout_with_no_wakes_times_out();

	event_wait_and_wake_work_across_processes();


	completion_create_and_completion_close_work();

	file_create_with_completion_works();

	file_read_with_completion_works();

	file_read_twice_with_completion_works();


	process_create_with_empty_exe_path_and_empty_working_directory_spawns_self_in_same_directory();

	process_create_with_empty_exe_path_and_given_working_directory_spawns_self_in_given_directory();

	process_create_with_given_exe_path_and_empty_working_directory_spawns_given_exe_in_same_directory();

	process_create_with_given_exe_path_and_given_working_directory_spawns_given_exe_in_given_directory();

	process_create_makes_inherited_handles_available_to_child();

	process_wait_timeout_on_sleeping_process_times_out();

	process_wait_waits_for_process_to_exit();

	process_wait_on_exited_process_still_works();


	shm_create_with_small_requst_succeeds();

	shm_create_with_large_requst_succeeds();

	shm_create_with_execute_access_succeeds();

	shm_reserve_of_entire_shm_succeeds();

	shm_reserve_of_shm_subrange_at_zero_offset_succeeds();

	shm_reserve_of_shm_subrange_at_nonzero_aligned_offset_succeeds();

	shm_reserve_of_shm_subrange_at_nonzero_unaligned_offset_succeeds();

	shm_commit_read_of_reserved_shm_succeeds_and_is_readable();

	shm_commit_read_write_of_reserved_shm_succeeds_and_is_readable_and_writeable();

	shm_works_across_processes();


	sem_create_creates_a_semaphore();

	sem_create_with_initial_count_1_allows_1_wait();

	sem_create_with_initial_count_0_allows_no_waits();

	sem_create_with_initial_count_5_allows_5_waits();

	sem_post_one_allows_one_wait();

	sem_post_two_allows_two_waits();

	sem_wait_waits_until_post();

	sem_wait_timeout_with_long_timeout_waits_until_post();

	sem_wait_and_post_work_across_processes();


	directory_enumeration_create_on_empty_directory_returns_no_more_files();

	directory_enumeration_on_directory_with_one_file_returns_that_file_then_no_more_files();

	directory_enumeration_on_directory_with_5_files_returns_those_files_then_no_more_files();

	directory_enumeration_on_directory_with_subdirectory_returns_that_subdirectory_then_no_more_files();


	directory_create_on_new_path_succeeds();

	directory_create_on_existing_path_fails();


	path_remove_file_on_file_path_succeeds();

	path_remove_file_on_directory_path_fails();

	path_remove_file_on_nonexistent_path_fails();


	path_remove_directory_on_directory_path_succeeds();

	path_remove_directory_on_file_path_fails();

	path_remove_directory_on_nonexistent_path_fails();


	path_is_directory_on_directory_path_returns_true();

	path_is_directory_on_file_path_returns_false();

	path_is_directory_on_nonexistent_path_returns_false();


	path_is_file_on_file_path_returns_true();

	path_is_file_on_directory_path_returns_false();

	path_is_file_on_nonexistent_path_returns_false();


	path_to_absolute_on_absolute_path_returns_that_path();

	path_to_absolute_on_absolute_path_with_dot_returns_that_path_without_dot();

	path_to_absolute_on_absolute_path_with_double_dash_returns_that_path_with_single_dash();

	path_to_absolute_on_root_path_returns_root_path();

	path_to_absolute_on_relative_path_returns_an_absolute_path();

	path_to_absolute_on_relative_path_with_dot_returns_an_absolute_path_ignoring_dot();

	path_to_absolute_on_relative_path_with_double_dot_returns_an_absolute_path_ignoring_element_before_double_dot();


	path_to_absolute_relative_to_with_absolute_path_returns_that_path();

	path_to_absolute_relative_to_with_absolute_base_returns_path_appended_to_that_base();

	path_to_absolute_relative_to_with_relative_base_returns_path_appended_to_absolute_base();


	path_get_info_on_nonexistent_path_fails();

	path_get_info_on_file_path_returns_is_not_directory_and_file_size();

	path_get_info_on_directory_path_returns_is_directory_and_0_bytes();


	timestamp_utc_succeeds();

	timestamp_ticks_per_second_succeeds_and_returns_nonzero();


	exact_timestamp_succeeds();

	exact_timestamp_ticks_per_second_succeeds_and_returns_nonzero();

	exact_timestamp_then_sleep_10_milliseconds_then_exact_timestamp_again_has_approximately_correct_difference();

	TEST_MODULE_END;
}

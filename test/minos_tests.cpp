#include "test_helpers.hpp"
#include "../infra/minos.hpp"

#define MINOS_TEST_BEGIN minos::init(); TEST_BEGIN

#define MINOS_TEST_END minos::deinit(); TEST_END

static constexpr u32 TIMEOUT_TEST_MILLIS = 50;

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

	// TODO

	MINOS_TEST_END;
}

static void process_create_with_given_exe_path_and_given_working_directory_spawns_given_exe_in_given_directory() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void process_create_makes_inherited_handles_available_to_child() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void process_create_makes_uninherited_handles_unavailable_to_child() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void process_wait_timeout_on_sleeping_process_times_out() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void process_wait_waits_for_process_to_exit() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void process_wait_on_completed_exited_still_works() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


static void shm_create_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void shm_map_of_entire_shm_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void shm_map_of_shm_subrange_at_begin_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void shm_map_of_shm_subrange_at_offset_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void shm_map_works_across_processes() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void shm_is_consistent_across_processes() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


static void sem_create_creates_a_semaphore() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void sem_create_with_initial_count_1_allows_1_wait() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void sem_create_with_initial_count_0_allows_no_waits() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void sem_create_with_initial_count_5_allows_5_waits() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void sem_post_allows_wait() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void sem_wait_waits_until_post() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void sem_wait_timeout_with_long_timeout_waits_until_post() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void sem_wait_timeout_with_no_posts_times_out() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void sem_wait_and_post_work_across_processes() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


static void directory_enumeration_create_on_empty_directory_returns_no_more_files() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void directory_enumeration_on_directory_with_one_file_returns_that_file_then_no_more_files() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void directory_enumeration_on_directory_with_5_files_returns_those_files_then_no_more_files() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void directory_enumeration_on_directory_subdirectory_returns_that_subdirectory_then_no_more_files() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


static void directory_create_on_new_path_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void directory_create_on_existing_path_fails() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


static void path_remove_file_on_file_path_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_remove_file_on_directory_path_fails() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_remove_file_on_nonexistent_path_fails() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


static void path_remove_directory_on_directory_path_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_remove_directory_on_file_path_fails() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_remove_directory_on_nonexistent_path_fails() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


static void path_is_directory_on_directory_path_returns_true() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_is_directory_on_file_path_returns_false() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_is_directory_on_nonexistent_path_returns_false() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


static void path_is_file_on_file_path_returns_true() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_is_file_on_directory_path_returns_false() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_is_file_on_nonexistent_path_returns_false() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


static void path_to_absolute_on_absolute_path_returns_that_path() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_to_absolute_on_relative_path_returns_an_absolute_path() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


static void path_to_absolute_relative_to_with_absolute_path_returns_that_path() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_to_absolute_relative_to_with_absolute_base_returns_path_appended_to_that_base() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_to_absolute_relative_to_with_relative_base_returns_path_appended_to_absolute_base() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


// TODO: path_to_absolute_directory


static void path_get_info_on_nonexistent_path_fails() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_get_info_on_file_path_returns_is_not_directory_and_file_size() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void path_get_info_on_directory_path_returns_is_directory() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


static void timestamp_utc_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void timestamp_ticks_per_second_succeeds_and_returns_nonzero() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}


static void exact_timestamp_succeeds() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void exact_timestamp_ticks_per_second_succeeds_and_returns_nonzero() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

	MINOS_TEST_END;
}

static void exact_timestamp_then_sleep_10_milliseconds_then_exact_timestamp_again_has_approximately_correct_difference() noexcept
{
	MINOS_TEST_BEGIN;

	// TODO

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

	process_create_makes_uninherited_handles_unavailable_to_child();

	process_wait_timeout_on_sleeping_process_times_out();

	process_wait_waits_for_process_to_exit();

	process_wait_on_completed_exited_still_works();

	shm_create_succeeds();

	shm_map_of_entire_shm_succeeds();

	shm_map_of_shm_subrange_at_begin_succeeds();

	shm_map_of_shm_subrange_at_offset_succeeds();

	shm_map_works_across_processes();

	shm_is_consistent_across_processes();


	sem_create_creates_a_semaphore();

	sem_create_with_initial_count_1_allows_1_wait();

	sem_create_with_initial_count_0_allows_no_waits();

	sem_create_with_initial_count_5_allows_5_waits();

	sem_post_allows_wait();

	sem_wait_waits_until_post();

	sem_wait_timeout_with_long_timeout_waits_until_post();

	sem_wait_timeout_with_no_posts_times_out();

	sem_wait_and_post_work_across_processes();


	directory_enumeration_create_on_empty_directory_returns_no_more_files();

	directory_enumeration_on_directory_with_one_file_returns_that_file_then_no_more_files();

	directory_enumeration_on_directory_with_5_files_returns_those_files_then_no_more_files();

	directory_enumeration_on_directory_subdirectory_returns_that_subdirectory_then_no_more_files();


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

	path_to_absolute_on_relative_path_returns_an_absolute_path();


	path_to_absolute_relative_to_with_absolute_path_returns_that_path();

	path_to_absolute_relative_to_with_absolute_base_returns_path_appended_to_that_base();

	path_to_absolute_relative_to_with_relative_base_returns_path_appended_to_absolute_base();


	path_get_info_on_nonexistent_path_fails();

	path_get_info_on_file_path_returns_is_not_directory_and_file_size();

	path_get_info_on_directory_path_returns_is_directory();


	timestamp_utc_succeeds();

	timestamp_ticks_per_second_succeeds_and_returns_nonzero();


	exact_timestamp_succeeds();

	exact_timestamp_ticks_per_second_succeeds_and_returns_nonzero();

	exact_timestamp_then_sleep_10_milliseconds_then_exact_timestamp_again_has_approximately_correct_difference();

	TEST_MODULE_END;
}

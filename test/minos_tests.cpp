#include "test_helpers.hpp"
#include "../infra/minos.hpp"

#define MINOS_TEST_BEGIN minos::init(); TEST_BEGIN

#define MINOS_TEST_END minos::deinit(); TEST_END

static constexpr u32 TIMEOUT_TEST_MILLIS = 50;

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

static void address_wait_and_wake_single_with_changed_value_wakes() noexcept
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

static void address_wait_and_wake_single_with_unchanged_value_sleeps() noexcept
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
		address = 0;
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


// TODO: file_create and file_close

// TODO: file_read

// TODO: file_write

// TODO: file_get_info

// TODO: file_resize


// TODO: event_create and event_close

// TODO: event_wait and event_wait_timeout and event_wake


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

	TEST_EQUAL(minos::file_read(file, buf, 1024, &overlapped), true);

	minos::CompletionResult read_result;

	TEST_EQUAL(minos::completion_wait(completion, &read_result), true);

	fprintf(stderr, "read_result.key: %" PRId64 "\nread_result.bytes: %d\n", read_result.key, read_result.bytes);

	TEST_EQUAL(read_result.key, 1234);

	TEST_EQUAL(read_result.bytes, 14);

	TEST_MEM_EQUAL(buf, "abcdefghijklmn", 14);

	minos::file_close(file);

	minos::completion_close(completion);

	MINOS_TEST_END;
}

// TODO: file_create and file_close with completion

// TODO: file_read and file_write with completion and completion_wait


// TODO: process_create and process_close

// TODO: process_wait and process_wait_timeout

// TODO: process_get_exit_code


// TODO: shm_create and shm_close

// TODO: shm_reserve


// TODO: semaphore_create and semaphore_close

// TODO: semaphore_post and semaphore_wait and semaphore_wait_timeout


// TODO: directory_enumeration_create and directory_enumeration_close

// TODO: directory_enumeration_next


// TODO: directory_create


// TODO: path_is_directory and path_is_file


// TODO: path_to_absolute


// TODO: path_to_absolute_relative_to


// TODO: path_to_absolute_directory


// TODO: path_get_info


// TODO: timestamp_utc


// TODO: timestamp_ticks_per_second


// TODO: exact_timestamp


// TODO: exact_timestamp_ticks_per_second



void minos_tests() noexcept
{
	TEST_MODULE_BEGIN;

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


	address_wait_and_wake_single_with_changed_value_wakes();

	address_wait_and_wake_single_with_unchanged_value_sleeps();

	multiple_address_wait_and_wake_all_with_changed_value_wakes_all();


	completion_create_and_completion_close_work();

	file_create_with_completion_works();

	file_read_with_completion_works();

	TEST_MODULE_END;
}

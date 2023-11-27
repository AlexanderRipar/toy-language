#include "test_global_data.hpp"

#include "../global_data.hpp"
#include "test_helpers.hpp"
#include <cstdio>
#include <cstring>

namespace test::global_data
{
	namespace string_set
	{
		using RAIIStringSet = RAIIWRAPPER(StringSet, deinit);

		struct InsertParallelThreadArgs
		{
			StringSet* s;

			u32 iteration_count;

			FILE* out_file;
		};

		union IncrementCharBuffer
		{
			char8 chars[8]{ '0', '0', '0', '0', '0', '0', '0', '0' };

			void advance() noexcept
			{
				for (u32 i = 0; i != 8; ++i)
				{
					if (chars[i] == '9')
					{
						chars[i] = 'A';

						break;
					}
					else if (chars[i] != 'F')
					{
						chars[i] += 1;

						break;
					}

					chars[i] = '0';
				}
			}

			void reset() noexcept
			{
				memset(chars, '0', sizeof(chars));
			}

			Range<char8> range() const noexcept
			{
				return Range{ chars };
			}
		};

		static u32 insert_parallel_thread_proc(void* raw_arg) noexcept
		{
			TEST_INIT;

			InsertParallelThreadArgs* arg = static_cast<InsertParallelThreadArgs*>(raw_arg);

			StringSet* s = arg->s;

			FILE* out_file = arg->out_file;

			const u32 iteration_count = arg->iteration_count;

			IncrementCharBuffer buf;

			for (u32 i = 0; i != iteration_count; ++i)
			{
				CHECK_NE(s->index_from(buf.range()), -1, "index_from running in parallel does not return -1");

				buf.advance();
			}

			TEST_RETURN;
		}



		static TESTCASE(init_deinit)
		{
			TEST_INIT;

			RAIIStringSet rs1;

			StringSet& s1 = rs1.t;

			CHECK_EQ(s1.init(), true, "Initialize unitialized StringSet");

			CHECK_EQ(s1.deinit(), true, "Deinitialize initialized StringSet");

			CHECK_EQ(s1.deinit(), true, "Deinitialize deinitialized StringSet");

			RAIIStringSet rs2{};

			StringSet& s2 = rs2.t;

			CHECK_EQ(s2.deinit(), true, "Deinitialize default-initialized StringSet");

			TEST_RETURN;
		}

		static TESTCASE(insert_and_get_single)
		{
			TEST_INIT;

			RAIIStringSet rs;

			StringSet& s = rs.t;

			CHECK_EQ(s.init(), true, "StringSet::init() returns true");

			const char8 buf[]{ "Hello there" };

			Range range{ buf, strlen(buf) };

			const s32 index1 = s.index_from(range);

			CHECK_NE(index1, -1, "StringSet::index_from does not return -1 under normal operation");

			const s32 index2 = s.index_from(range);

			CHECK_EQ(index1, index2, "Calls to StringSet::index_from with the same string return the same index");

			const Range returned_range = s.string_from(index1);

			CHECK_RANGES_EQ(range, returned_range, "Range returned from StringSet::string_from is equal to the range passed to StringSet::index_from");

			CHECK_EQ(s.deinit(), true, "StringSet::deinit() returns true");

			TEST_RETURN;
		}

		static TESTCASE(insert_and_get_multiple)
		{
			TEST_INIT;

			RAIIStringSet rs;

			StringSet& s = rs.t;

			CHECK_EQ(s.init(), true, "StringSet::init() returns true");

			const char8 buf1[]{ "String Number 1" };

			const char8 buf2[]{ "Another string yaaaay!" };

			const Range range1{ buf1, strlen(buf1) };

			const Range range2{ buf2, strlen(buf2) };

			const s32 idx1a = s.index_from(range1);

			const s32 idx1b = s.index_from(range1);

			const s32 idx2a = s.index_from(range2);

			const s32 idx1c = s.index_from(range1);

			const s32 idx2b = s.index_from(range2);

			CHECK_EQ(idx1a, idx1b, "Consecutively inserted equal strings yield equal indices");

			CHECK_NE(idx1a, idx2a, "Different inserted strings yield unequal strings");

			CHECK_EQ(idx1b, idx1c, "Non-consecutively inserted equal strings yield equal indices");

			CHECK_EQ(idx2a, idx2b, "Non-consecutively inserted equal strings yield equal indices");

			CHECK_EQ(s.deinit(), true, "StringSet::deinit() returns true");

			TEST_RETURN;
		}

		static TESTCASE(grow_data)
		{
			TEST_INIT;

			RAIIStringSet rs;

			StringSet& s = rs.t;

			CHECK_EQ(s.init(), true, "StringSet::init() returns true");

			SimpleMapDiagnostics diag;

			s.get_diagnostics(&diag);

			const auto initial_data_committed_bytes = diag.data_committed_bytes;

			IncrementCharBuffer buf;

			u32 prev_data_used_bytes = diag.data_used_bytes;

			u32 insert_count = 0;

			while (diag.data_committed_bytes == initial_data_committed_bytes)
			{
				CHECK_NE(s.index_from(buf.range()), -1, "index_from succeeds until data commit increase");

				buf.advance();

				insert_count += 1;

				s.get_diagnostics(&diag);

				const u32 curr_data_used_bytes = diag.data_used_bytes;

				CHECK_GT(curr_data_used_bytes, prev_data_used_bytes, "data_used_bytes strictly increases when new strings are passed to index_from");

				CHECK_GE(diag.data_committed_bytes, curr_data_used_bytes, "data_committed_bytes is greater than or equal to data_used_bytes");

				prev_data_used_bytes = curr_data_used_bytes;
			}

			const s32 idx1 = s.index_from(buf.range());

			CHECK_NE(idx1, -1, "index_from succeeds for new string after data commit increases");

			insert_count += 1;

			s.get_diagnostics(&diag);

			const u32 new_data_used_bytes = diag.data_used_bytes;

			IncrementCharBuffer buf2;

			while (memcmp(&buf, &buf2, sizeof(buf)) != 0)
			{
				CHECK_NE(s.index_from(buf2.range()), -1, "index_from with same strings succeeds after data commit increases");

				buf2.advance();

				s.get_diagnostics(&diag);

				CHECK_EQ(diag.data_used_bytes, new_data_used_bytes, "used bytes do not increase when calling from_index with same strings");
			}

			const s32 idx2 = s.index_from(buf.range());

			CHECK_EQ(idx1, idx2, "index_from returns same index for same string after data commit increases");

			const Range returned_range = s.string_from(idx2);

			CHECK_RANGES_EQ(buf.range(), returned_range, "string_from returns same string for same index");

			s.get_diagnostics(&diag);

			CHECK_EQ(diag.indices_used_count, insert_count, "number of indices equals number of distinct inserted strings");

			CHECK_EQ(s.deinit(), true, "StringSet::deinit() returns true");

			TEST_RETURN;
		}

		static TESTCASE(grow_indices)
		{
			TEST_INIT;

			RAIIStringSet rs;

			StringSet& s = rs.t;

			CHECK_EQ(s.init(), true, "StringSet::init() returns true");

			IncrementCharBuffer buf;

			SimpleMapDiagnostics diag;

			s.get_diagnostics(&diag);

			const u32 initial_indices_committed_count = diag.indices_committed_count;

			u32 prev_indices_used_count = diag.indices_used_count;

			const s32 initial_index = s.index_from(buf.range());

			CHECK_NE(initial_index, -1, "The first call to index_from returns a valid index");

			CHECK_RANGES_EQ(buf.range(), s.string_from(initial_index), "string_from returns the correct string");

			while (diag.indices_committed_count == initial_indices_committed_count)
			{
				CHECK_NE(s.index_from(buf.range()), -1, "index_from succeeds until indices commit increases");

				buf.advance();

				s.get_diagnostics(&diag);

				const u32 curr_indices_used_count = diag.indices_used_count;

				CHECK_GT(curr_indices_used_count, prev_indices_used_count, "indices_used_bytes strictly increases when new strings are passed to index_from");

				CHECK_GE(diag.indices_committed_count, diag.indices_used_count, "number of committed indices is greater than or equal to number of used indices");

				prev_indices_used_count = curr_indices_used_count;
			}

			const s32 index = s.index_from(buf.range());

			CHECK_NE(index, -1, "Calling index_from with a new string after rehashing indices does not fail");

			s.get_diagnostics(&diag);

			CHECK_LT(prev_indices_used_count, diag.indices_used_count, "Calling index_from with a new string after rehashing indices increases indices_used_bytes");

			CHECK_RANGES_EQ(buf.range(), s.string_from(index), "Calling string_from on index created after rehashing indices returns the correct string");

			buf.reset();

			CHECK_RANGES_EQ(buf.range(), s.string_from(initial_index), "Calling string_from after rehashing indices with an index created before returns the correct string");

			CHECK_EQ(s.deinit(), true, "StringSet::deinit() returns true");

			TEST_RETURN;
		}

		static TESTCASE(insert_parallel)
		{
			TEST_INIT;

			RAIIStringSet rs;

			StringSet& s = rs.t;

			InsertParallelThreadArgs args;
			args.iteration_count = 60000;
			args.out_file = out_file;
			args.s = &s;

			CHECK_EQ(s.init(), true, "StringSet::init returns true");

			RUN_TEST_WITH_ARGS(run_on_threads_and_wait, 32, insert_parallel_thread_proc, &args, INFINITE);

			SimpleMapDiagnostics diag;

			s.get_diagnostics(&diag);

			CHECK_EQ(diag.indices_used_count, args.iteration_count, "Number of indices equals number of distinct concurrently inserted strings");

			CHECK_EQ(s.deinit(), true, "StringSet::deinit returns true");

			TEST_RETURN;
		}

		static TESTCASE(diagnostics)
		{
			TEST_INIT;

			RAIIStringSet rs;

			StringSet& s = rs.t;

			CHECK_EQ(s.init(), true, "StringSet::init() returns true");

			IncrementCharBuffer buf;

			log(LogLevel::Info, out_file, "StringSet Statistics\n");

			for (u32 iter = 0; iter != 40; ++iter)
			{
				for (u32 i = 0; i != 50000; ++i)
				{
					CHECK_NE(s.index_from(buf.range()), -1, "StringSet::index_from succeeds");

					buf.advance();
				}

				FullMapDiagnostics diag;

				s.get_diagnostics(&diag);

				log(LogLevel::None, out_file,
					"Used indices    Allocated indices    Load factor    Max Probe Seq. Length (PSL)\n"
					"  %10u           %10u       %8.2f                        %7u\n"
					"PSL Dist | ",
					diag.simple.indices_used_count,
					diag.simple.indices_committed_count,
					static_cast<float>(diag.simple.indices_used_count) / diag.simple.indices_committed_count,
					diag.max_probe_seq_len);

				const u32 max_saved_psl = 
					diag.max_probe_seq_len < array_count(diag.probe_seq_len_counts) ?
					diag.max_probe_seq_len :
					static_cast<u32>(array_count(diag.probe_seq_len_counts));

				u32 psl = 0;

				for (u32 i = 0; i != 10; ++i)
					log(LogLevel::None, out_file, "%8u", i + 1);

				log(LogLevel::None, out_file, "\n---------+---------------------------------------------------------------------------------\n");

				while (psl != max_saved_psl)
				{
					const u32 next_psl = psl + 10 < max_saved_psl ? psl + 10 : max_saved_psl;

					log(LogLevel::None, out_file, "    %4u | ", psl);

					for (u32 i = psl; i != next_psl; ++i)
						log(LogLevel::None, out_file, "%8u", diag.probe_seq_len_counts[i]);

					log(LogLevel::None, out_file, "\n");

					psl = next_psl;
				}

				log(LogLevel::None, out_file, "\n");
			}

			TEST_RETURN;
		}

		static TESTCASE(run)
		{
			TEST_INIT;

			RUN_TEST(init_deinit);

			RUN_TEST(insert_and_get_single);

			RUN_TEST(insert_and_get_multiple);

			RUN_TEST(grow_data);

			RUN_TEST(grow_indices);

			RUN_TEST(insert_parallel);

			RUN_TEST(diagnostics);

			TEST_RETURN;
		}
	}

	namespace input_file_set
	{
		using RAIIInputFileSet = RAIIWRAPPER(InputFileSet, deinit);



		static TESTCASE(init_deinit)
		{
			TEST_INIT;

			RAIIInputFileSet rs1;

			InputFileSet& s1 = rs1.t;

			CHECK_EQ(s1.init(), true, "Initialize unitialized InputFileSet");

			CHECK_EQ(s1.deinit(), true, "Deinitialize initialized InputFileSet");

			CHECK_EQ(s1.deinit(), true, "Deinitialize deinitialized InputFileSet");

			RAIIInputFileSet rs2{};

			InputFileSet& s2 = rs2.t;

			CHECK_EQ(s2.deinit(), true, "Deinitialize default-initialized InputFileSet");

			TEST_RETURN;
		}

		static TESTCASE(insert_single)
		{
			TEST_INIT;

			TEST_TBD;

			TEST_RETURN;
		}

		static TESTCASE(insert_multiple)
		{
			TEST_INIT;

			TEST_TBD;

			TEST_RETURN;
		}

		static TESTCASE(get_single)
		{
			TEST_INIT;

			TEST_TBD;

			TEST_RETURN;
		}

		static TESTCASE(get_multiple)
		{
			TEST_INIT;

			TEST_TBD;

			TEST_RETURN;
		}

		static TESTCASE(insert_and_get_parallel)
		{
			TEST_INIT;

			TEST_TBD;

			TEST_RETURN;
		}

		static TESTCASE(diagnostics)
		{
			TEST_INIT;

			TEST_TBD;

			TEST_RETURN;
		}

		static TESTCASE(run)
		{
			TEST_INIT;

			RUN_TEST(init_deinit);

			RUN_TEST(insert_single);

			RUN_TEST(insert_multiple);

			RUN_TEST(get_single);

			RUN_TEST(get_multiple);

			RUN_TEST(insert_and_get_parallel);

			RUN_TEST(diagnostics);

			TEST_RETURN;
		}
	}

	namespace read_list
	{
		// @TODO
		static TESTCASE(run)
		{
			TEST_INIT;

			TEST_TBD;

			TEST_RETURN;
		}
	}

	namespace init
	{
		// @TODO
		static TESTCASE(run)
		{
			TEST_INIT;

			TEST_TBD;

			TEST_RETURN;
		}
	}

	TESTCASE(run)
	{
		TEST_INIT;

		RUN_TEST(string_set::run);

		RUN_TEST(input_file_set::run);

		RUN_TEST(read_list::run);

		RUN_TEST(init::run);

		TEST_RETURN;
	}
}

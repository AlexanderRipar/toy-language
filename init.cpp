#include "init.hpp"

#include <cstdio>
#include <cstdlib>

struct Arguments
{
	const char8** positional_args;
	
	u32 positional_arg_count;

	u32 thread_count;

	u32 concurrent_read_count;

	u32 read_buffer_bytes;
};

struct ArgDesc
{
	const char8* name;

	bool is_found;

	enum class Tag
	{
		EMPTY = 0,
		Switch,
		Integer,
		String,
	} tag;

	union
	{
		struct
		{
			u32 value;

			u32 min;

			u32 max;
		} integer_arg;

		struct
		{
			const char* value;
		} string_arg;
	};

	ArgDesc(const char8* name, u32 default_value, u32 min, u32 max) noexcept
		: name{ name }, tag{ Tag::Integer }, integer_arg{ default_value, min, max } {}

	ArgDesc(const char8* name, const char* default_value) noexcept
		: name{ name }, tag{ Tag::String }, string_arg{ default_value } {}
};

static bool parse_integer_arg(const char8* arg, uint* out) noexcept
{
	uint n = 0;

	do
	{
		if (*arg < '0' || *arg > '9')
			return false;

		n = n * 10 + *arg - '0';

		arg += 1;
	}
	while (*arg != '\0');

	*out = n;

	return true;
}

static ArgDesc* match_arg(const char8* arg, uint desc_count, ArgDesc* descs) noexcept
{
	for (uint i = 0; i != desc_count; ++i)
		if (strcmp(descs[i].name, arg) == 0)
			return descs + i;

	return nullptr;
}

static InitStatus parse_args(s32 argc, const char8** argv, Arguments* out) noexcept
{
	static constexpr const char8 usage_message[] = "Usage: %s [--thread-count N] [--concurrent-read-count N] [--read-buffer-bytes N] FILENAMES...\n";

	SYSTEM_INFO sysinfo;

	GetSystemInfo(&sysinfo);

	ArgDesc descs[] {
		{ "thread-count", sysinfo.dwNumberOfProcessors, 1, 1000 },
		{ "read-buffer-bytes", 1024 * 1024 * 1024, 65536, UINT32_MAX },
		{ "concurrent-read-count", 0, 1, 1024 },
	};

	ArgDesc* thread_count = descs + 0;

	ArgDesc* read_buffer_bytes = descs + 1;

	ArgDesc* concurrent_read_count = descs + 2;

	if (argc < 1)
	{
		fprintf(stderr, "Received argc == %d. Expected at least 1.\n", argc);

		return InitStatus::ExitFailure;
	}
	else if (argc == 1)
	{
		fprintf(stderr, usage_message, argv[0]);

		return InitStatus::ExitSuccess;
	}

	s32 arg_index = 1;
	
	while (arg_index != argc)
	{
		const char8* arg = argv[arg_index];

		if (arg[0] != '-' || arg[1] != '-')
			break;

		ArgDesc* desc = match_arg(arg + 2, _countof(descs), descs);

		if (desc == nullptr)
		{
			fprintf(stderr, "Unknown argument %s.\n", arg);

			return InitStatus::ExitFailure;
		}

		if (desc->is_found)
		{
			fprintf(stderr, "Argument %s supplied more than once.\n", arg);

			return InitStatus::ExitFailure;
		}

		descs->is_found = true;

		switch (desc->tag)
		{
		case ArgDesc::Tag::Switch: {

			arg_index += 1;

			break;
		}

		case ArgDesc::Tag::Integer: {

			if (arg_index + 1 == argc)
			{
				fprintf(stderr, "Missing value for argument %s.\n", arg);

				return InitStatus::ExitFailure;
			}

			uint value;

			if (!parse_integer_arg(argv[arg_index + 1], &value))
			{
				fprintf(stderr, "Non-numeric value found for argument %s.\n", arg);

				return InitStatus::ExitFailure;
			}

			if (value < desc->integer_arg.min)
			{
				fprintf(stderr, "The value %llu supplied for argument %s is smaller than the minimum of %u.\n", value, arg, desc->integer_arg.min);

				return InitStatus::ExitFailure;
			}

			if (value > desc->integer_arg.max)
			{
				fprintf(stderr, "The value %llu supplied for argument %s is greater than the maximum of %u.\n", value, arg, desc->integer_arg.max);

				return InitStatus::ExitFailure;
			}

			desc->integer_arg.value = static_cast<u32>(value);

			arg_index += 2;

			break;
		}

		case ArgDesc::Tag::String: {

			if (arg_index + 1 == argc)
			{
				fprintf(stderr, "Missing value for argument %s.\n", arg);

				return InitStatus::ExitFailure;
			}

			desc->string_arg.value = argv[arg_index + 1];

			arg_index += 2;

			break;
		}

		default: {

			assert(false);

			break;
		}
		}
	}

	if (arg_index == argc)
	{
		fprintf(stderr, "Missing positional arguments.\n");

		return InitStatus::ExitFailure;
	}

	u32 positional_arg_count = argc - arg_index;

	const char8** positional_args = argv + arg_index;

	if (!concurrent_read_count->is_found)
	{
		// Default concurrent-read-count based on number of positional args and threads.
		if (positional_arg_count < thread_count->integer_arg.value * 2)
			concurrent_read_count->integer_arg.value = positional_arg_count;
		else
			concurrent_read_count->integer_arg.value = thread_count->integer_arg.value * 2;
	}
	else if (concurrent_read_count->integer_arg.value > positional_arg_count)
	{
		// Limit concurrent-read-count to number of positional args.
		concurrent_read_count->integer_arg.value = positional_arg_count;
	}

	out->positional_args = positional_args;

	out->positional_arg_count = positional_arg_count;

	out->thread_count = thread_count->integer_arg.value;

	out->concurrent_read_count = concurrent_read_count->integer_arg.value;

	out->read_buffer_bytes = read_buffer_bytes->integer_arg.value;

	return InitStatus::Ok;
}

InitStatus init(s32 argc, const char8** argv, GlobalData* out) noexcept
{
	memset(out, 0, sizeof(*out));

	Arguments args;

	if (const InitStatus s = parse_args(argc, argv, &args); s != InitStatus::Ok)
		return s;

	fprintf(
		stderr,
		"    thread-count          %d\n"
		"    concurrent-read-count %d\n"
		"    read-buffer-bytes     %d\n"
		"    positional-arg-count  %d\n"
		"    positional-args       %s\n",
		args.thread_count,
		args.concurrent_read_count,
		args.read_buffer_bytes,
		args.positional_arg_count,
		*args.positional_args);

	for (uint i = 1; i != args.positional_arg_count; ++i)
		fprintf(stderr, "                          %s\n", args.positional_args[i]);

	out->program_name = argv[0];

	if (!out->strings.init())
	{
		fprintf(stderr, "Failed to initialize global StringSet.\n");

		return InitStatus::ExitFailure;
	}

	if (!out->input_files.init())
	{
		fprintf(stderr, "Failed to initialize global InputFileSet.\n");

		return InitStatus::ExitFailure;
	}

	if (!out->reads.init(args.concurrent_read_count, args.read_buffer_bytes))
	{
		fprintf(stderr, "Failed to initialize global ReadSet.\n");

		return InitStatus::ExitFailure;
	}

	if ((out->completion_port = CreateIoCompletionPort(nullptr, nullptr, 0, args.thread_count)) == nullptr)
	{
		fprintf(stderr, "CreateIoCompletionPort failed: %d.\n", GetLastError());

		return InitStatus::ExitFailure;
	}

	if ((out->thread_completion_event = CreateEventW(nullptr, false, false, nullptr)) == nullptr)
	{
		fprintf(stderr, "CreateEvent failed: %d.\n", GetLastError());

		return InitStatus::ExitFailure;
	}

	return InitStatus::Ok;






	/*
	SYSTEM_INFO sysinfo;

	GetSystemInfo(&sysinfo);

	m_page_bytes = sysinfo.dwAllocationGranularity;

	m_program_name = argv[0];

	if ((m_completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0)) == nullptr)
		return false;

	if (!m_strings.init())
		return false;

	if (!m_input_files.init(args.path_count))
		return false;

	for (uint i = 0; i != m_input_files.count(); ++i)
	{
		if (!open_file(args.paths[i], &m_input_files[i]))
			return false;
	}

	if (!m_reads.init(args.concurrent_read_count))
		return false;

	// Overallocate by m_page_bytes for every buffer to allow simple use of sentinels
	char8* full_read_buffer = static_cast<char8*>(VirtualAlloc(nullptr, args.concurrent_read_count * (args.read_buffer_bytes + m_page_bytes), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));

	if (full_read_buffer == nullptr)
		return false;

	Token* full_token_buffer = static_cast<Token*>(VirtualAlloc(nullptr, args.path_count * args.max_token_count * sizeof(Token), MEM_RESERVE, PAGE_READWRITE));

	if (full_token_buffer == nullptr)
		return false;

	m_read_buffer_bytes = args.read_buffer_bytes;

	for (uint i = 0; i != m_reads.count(); ++i)
	{
		m_reads[i].buffer = full_read_buffer + i * (args.read_buffer_bytes + m_page_bytes);

		m_reads[i].tokens = full_token_buffer + i * args.max_token_count;

		m_reads[i].file_handle = m_input_files[i];

		LARGE_INTEGER file_bytes;

		if (!GetFileSizeEx(m_reads[i].file_handle, &file_bytes))
			return false;

		m_reads[i].remaining_file_bytes = file_bytes.QuadPart;

		if (!CreateIoCompletionPort(m_reads[i].file_handle, m_completion_port, static_cast<ULONG_PTR>(CompletionKey::Read), 0))
			return false;

		if (!ReadFile(m_reads[i].file_handle, m_reads[i].buffer, static_cast<DWORD>(m_read_buffer_bytes), nullptr, &m_reads[i].overlapped) && GetLastError() != ERROR_IO_PENDING)
			return false;
	}

	m_next_file_index = args.concurrent_read_count;

	m_running_thread_count = args.thread_count;

	m_thread_completion_event = CreateEventW(nullptr, false, false, nullptr);

	if (m_thread_completion_event == nullptr)
		return false;

	for (uint i = 0; i != args.thread_count; ++i)
	{
		HANDLE thread_handle = CreateThread(nullptr, 0, worker_thread_proc, this, 0, nullptr);

		if (thread_handle == nullptr)
			return false;

		char16 thread_name[]{ L"read_lex_parse_worker_0000" };

		uint n = i + 1;

		uint off = _countof(thread_name) - 2;

		while (n != 0)
		{
			thread_name[off] = L'0' + (n % 10);

			n /= 10;

			off -= 1;
		}

		if (SetThreadDescription(thread_handle, thread_name))

		if (!CloseHandle(thread_handle))
			return false;
	}

	return true;
	*/
}

#ifndef TASK_MANAG0R_INCLUDE_GUARD
#define TASK_MANAG0R_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"

namespace task
{
	enum class TaskType
	{
		NONE = 0,
		Scan,
		Parse,
	};

	struct ScanTask
	{

	};

	struct ParseTask
	{

	};

	struct Task
	{
		TaskType type;

		union
		{
			ScanTask scan;

			ParseTask parse;
		};
	};

	bool init() noexcept;

	u32 request_ast_handle(Range<char8> filepath, Range<char8> relative_to) noexcept;

	bool access_ast(u32 handle, u32 symbol) noexcept;

	Task next(Task completed_task) noexcept;

	bool await_idle() noexcept;
};

#endif // TASK_MANAG0R_INCLUDE_GUARD

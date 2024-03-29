#ifndef TASK_MANAG0R_INCLUDE_GUARD
#define TASK_MANAG0R_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"

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

#endif // TASK_MANAG0R_INCLUDE_GUARD

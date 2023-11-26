#include "common.hpp"
#include "global_data.hpp"
#include "init.hpp"
#include "minwin.hpp"
#include <cstdlib>
#include <cstdio>

GlobalData global_data;

s32 main(s32 argc, const char8** argv)
{
	switch (init(argc, argv, &global_data))
	{
	case InitStatus::ExitFailure:
		return EXIT_FAILURE;
	case InitStatus::ExitSuccess:
		return EXIT_SUCCESS;
	case InitStatus::Ok:
		break;
	default:
		fprintf(stderr, "Unknown InitStatus.\n");
		return EXIT_FAILURE;
	}

	WaitForSingleObject(global_data.thread_completion_event, INFINITE);

	return EXIT_SUCCESS;
}

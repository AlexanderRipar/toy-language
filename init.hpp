#ifndef INIT_INCLUDE_GUARD
#define INIT_INCLUDE_GUARD

#include "global_data.hpp"
#include "common.hpp"

enum class InitStatus
{
	Ok,
	ExitSuccess,
	ExitFailure,
};

InitStatus init(s32 argc, const char8** argv, GlobalData* out) noexcept;

#endif // INIT_INCLUDE_GUARD

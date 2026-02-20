#ifndef HOST_COMPILER_INCLUDE_GUARD
#define HOST_COMPILER_INCLUDE_GUARD

bool perform_debugbreak_helper() noexcept;

#if defined(__clang__)
	#define COMPILER_CLANG 1
	#define COMPILER_NAME "clang"
	#define NORETURN [[noreturn]]
	#define DEBUGBREAK do { if (perform_debugbreak_helper()) __builtin_debugtrap(); } while (false)
	#if !__has_builtin(__builtin_debugtrap)
		#error("Required __builtin_debugtrap not supported by used clang version")
	#endif
#elif defined(__GNUC__)
	#include <signal.h>
	#define COMPILER_GCC 1
	#define COMPILER_NAME "gcc"
	#define NORETURN __attribute__ ((noreturn))
	#define DEBUGBREAK do { if (perform_debugbreak_helper()) raise(SIGTRAP); } while (false)
#elif defined(_MSC_VER) && !defined(__INTEL_COMPILER)
	#include <intrin.h>
	#define COMPILER_MSVC 1
	#define COMPILER_NAME "msvc"
	#define NORETURN __declspec(noreturn)
	#define DEBUGBREAK do { if (perform_debugbreak_helper()) __debugbreak(); } while (false)
#else
	#error("Unsupported compiler")
#endif

#endif // HOST_COMPILER_INCLUDE_GUARD

#ifndef UTIL_STATUS_INCLUDE_GUARD
#define UTIL_STATUS_INCLUDE_GUARD

#include <cassert>

#include "types.hpp"
#include "strview.hpp"

enum class CustomError
{
	OutOfMemory,
	BadCommandLine,
	PartialRead,
};

struct [[nodiscard]] Status
{
	enum class Kind
	{
		OK,
		Custom,
		OS,
	};

	u64 m_err_union;

	Status() noexcept : m_err_union{ 0ui64 } {}

	explicit Status(Kind kind, u32 error_code) noexcept : m_err_union{ static_cast<u64>(kind) << 32 | error_code } {}

	static Status from_os(u32 e) noexcept { return Status{ Kind::OS, e }; }

	static Status from_custom(CustomError e) noexcept
	{
		return Status{ Kind::Custom, static_cast<u32>(e) };
	}

	bool is_ok() const noexcept
	{
		return m_err_union == 0;
	}

	Kind kind() const noexcept
	{
		return static_cast<Kind>(m_err_union >> 32);
	}

	u32 error_code() const noexcept
	{
		return static_cast<u32>(m_err_union);
	}

	strview kind_name() const noexcept;

	u32 error_message(char* buf, u32 buf_size) noexcept;
};

bool operator==(Status lhs, Status rhs) noexcept
{
	return lhs.m_err_union == rhs.m_err_union;
}

bool operator!=(Status lhs, Status rhs) noexcept
{
	return lhs.m_err_union != rhs.m_err_union;
}

#ifdef STATUS_DISABLE_TRACE
#pragma detect_mismatch("status_disable_trace", "true")

#define TRY(macro_arg_) do { if (const Status macro_status_ = (macro_arg_); !macro_status.is_ok()) return macro_status; } while (false)

#define STATUS_FROM_CUSTOM(macro_error_) Status::from_custom(macro_error_)

#define STATUS_FROM_OS(macro_error_) Status::from_os(macro_error_)

#else // STATUS_DISABLE_TRACE
#pragma detect_mismatch("status_disable_trace", "false")

namespace impl
{
	struct ErrorLocation
	{
		const char* file;

		const char* function;

		u32 line_number;
	};

	__declspec(noinline) void push_error_location(const ErrorLocation& loc) noexcept;

	Status register_error(Status status, const ErrorLocation& loc) noexcept;
}

#define CONSTEXPR_LINE_NUM_CAT_HELPER2(x, y) x##y
#define CONSTEXPR_LINE_NUM_CAT_HELPER(x, y) CONSTEXPR_LINE_NUM_CAT_HELPER2(x, y)
#define CONSTEXPR_LINE CONSTEXPR_LINE_NUM_CAT_HELPER(__LINE__, u)

#define TRY(macro_arg_) do { if (const Status macro_status_ = (macro_arg_); !macro_status_.is_ok()) { static constexpr impl::ErrorLocation macro_loc_{ __FILE__, __FUNCTION__, CONSTEXPR_LINE }; impl::push_error_location(macro_loc_); return macro_status_; } } while (false)

#define STATUS_FROM_CUSTOM(macro_error_) impl::register_error(Status::from_custom(macro_error_), impl::ErrorLocation{ __FILE__, __FUNCTION__, CONSTEXPR_LINE })

#define STATUS_FROM_OS(macro_error_) impl::register_error(Status::from_os(macro_error_), impl::ErrorLocation{ __FILE__, __FUNCTION__, CONSTEXPR_LINE })

#endif // STATUS_DISABLE_TRACE

#endif // UTIL_STATUS_INCLUDE_GUARD

#include <atomic>

#include "common.hpp"
#include "job_server.hpp"

struct WorkerPool
{
private:

	std::atomic<u32> m_pending_worker_count;

	std::atomic<bool> m_success;

	JobServer* m_job_server;

	static u32 worker_proc(void* worker_pool) noexcept;

public:

	struct InitInfo
	{
		u32 worker_count;
	};

	bool init(const InitInfo& info, JobServer* job_server) noexcept;

	bool await_pending() noexcept;
};

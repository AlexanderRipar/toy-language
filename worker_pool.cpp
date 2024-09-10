#include "worker_pool.hpp"
#include "minos.hpp"

u32 WorkerPool::worker_proc(void* param) noexcept
{
	WorkerPool* const worker_pool = static_cast<WorkerPool*>(param);

	while (true)
	{
		job_proc job;

		byte job_param[MAX_JOB_DATA_SIZE];

		if (!worker_pool->m_job_server->get_job_await(&job, job_param))
		{
			if (worker_pool->m_pending_worker_count.fetch_sub(1, std::memory_order_relaxed) == 1)
				minos::address_wake_all(&worker_pool->m_pending_worker_count);

			return 0;
		}

		if (!job(worker_pool->m_job_server, job_param))
		{
			worker_pool->m_job_server->terminate();

			worker_pool->m_success.store(false, std::memory_order_relaxed);

			worker_pool->m_pending_worker_count.store(0, std::memory_order_release);

			minos::address_wake_all(&worker_pool->m_pending_worker_count);

			return 0;
		}
	}
}

bool WorkerPool::init(const InitInfo& info, JobServer* job_server) noexcept
{
	m_job_server = job_server;

	m_success.store(true, std::memory_order_relaxed);

	m_pending_worker_count.store(info.worker_count, std::memory_order_relaxed);

	for (u32 i = 0; i != info.worker_count; ++i)
	{
		static constexpr const char8 THREAD_NAME_PREFIX[] = "Generic Compiler Worker ";

		char8 thread_name[32];

		memcpy(thread_name, THREAD_NAME_PREFIX, sizeof(THREAD_NAME_PREFIX));

		char8* const end = thread_name + sizeof(THREAD_NAME_PREFIX) + (i >= 1000 ? 4 : i >= 100 ? 3 : i >= 10 ? 2 : 1);

		char8* curr = end;

		ASSERT_OR_IGNORE(curr < thread_name + sizeof(thread_name));

		u32 n = i;

		do
		{
			curr -= 1;

			*curr = '0' + (n % 10);

			n /= 10;
		}
		while (n != 0);

		if (!minos::thread_create(worker_proc, this, Range<char8>{ thread_name, end }))
		{
			m_job_server->terminate();

			return false;
		}
	}

	return true;
}

bool WorkerPool::await_pending() noexcept
{
	u32 pending = m_pending_worker_count.load(std::memory_order_acquire);

	while (pending != 0)
	{
		minos::address_wait(&m_pending_worker_count, &pending, sizeof(m_pending_worker_count));

		pending = m_pending_worker_count.load(std::memory_order_acquire);
	}

	return m_success.load(std::memory_order_relaxed);
}

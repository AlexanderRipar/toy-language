#ifndef JOB_SERVER_INCLUDE_GUARD
#define JOB_SERVER_INCLUDE_GUARD

#include "common.hpp"
#include "minos.hpp"
#include "memory.hpp"
#include "threading.hpp"
#include "ast_server.hpp"

struct JobServer;

using job_proc = bool (*) (JobServer* job_server, void* param) noexcept;

static constexpr uint MAX_JOB_DATA_SIZE = 24;



struct JobServer
{
private:

	static constexpr u32 PRIORITY_LEVEL_COUNT = 2;

	union JobEntry
	{
		struct
		{
			job_proc proc;

			byte param[MAX_JOB_DATA_SIZE];	
		};

		u32 freelist_next;
	};

	u32 m_entry_capacity;

	JobEntry* m_entries;

	u32* m_queue_buffer;

	AstServer* m_ast_server;

	ThreadsafeAwaitableRingBufferHeader<u32> m_pq;

	ThreadsafeIndexStackListHeader<JobEntry, offsetof(JobEntry, freelist_next)> m_entry_freelist;

public:

	struct InitInfo
	{
		u32 max_job_count;
	};

	MemoryRequirements get_memory_requirements(const InitInfo& info) noexcept;

	bool init(const InitInfo& info, byte* memory) noexcept;

	void submit(u32 priority, job_proc proc, u32 param_bytes, void* param) noexcept;

	bool get_job_await(job_proc* out_proc, void* out_param) noexcept;

	void register_job_source(AstServer* source) noexcept;

	void terminate() noexcept;
};

#endif // JOB_SERVER_INCLUDE_GUARD

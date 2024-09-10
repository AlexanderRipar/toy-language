#include "job_server.hpp"

MemoryRequirements JobServer::get_memory_requirements(const InitInfo& info) noexcept
{
    return { info.max_job_count * (sizeof(JobEntry) + PRIORITY_LEVEL_COUNT * sizeof(u32)), alignof(JobEntry) };
}

bool JobServer::init(const InitInfo& info, byte* memory) noexcept
{
    m_entry_capacity = info.max_job_count;

    m_entries = reinterpret_cast<JobEntry*>(memory);

    m_queue_buffer = reinterpret_cast<u32*>(memory + info.max_job_count * sizeof(JobEntry));

    m_pq.init();

    m_entry_freelist.init(m_entries, info.max_job_count);

    return true;
}

void JobServer::submit(u32 priority, job_proc proc, u32 param_bytes, void* param) noexcept
{
    ASSERT_OR_IGNORE(param_bytes <= MAX_JOB_DATA_SIZE);

    ASSERT_OR_IGNORE(priority < PRIORITY_LEVEL_COUNT);

    JobEntry* const entry = m_entry_freelist.pop(m_entries);

    if (entry == nullptr)
        minos::exit_process(101);

    entry->proc = proc;

    memcpy(entry->param, param, param_bytes);

    m_pq.enqueue(m_queue_buffer, m_entry_capacity, priority, static_cast<u32>(entry - m_entries));
}

bool JobServer::get_job_await(job_proc* out_proc, void* out_param) noexcept
{
    u32 entry_index;
    
    if (!m_pq.dequeue_await(m_queue_buffer, m_entry_capacity, PRIORITY_LEVEL_COUNT, &entry_index))
        return false;

    const JobEntry* const entry = m_entries + entry_index;

    *out_proc = entry->proc;

    memcpy(out_param, entry->param, MAX_JOB_DATA_SIZE);

    return false;
}

void JobServer::register_job_source(AstServer* source) noexcept
{
    m_ast_server = source;
}

void JobServer::terminate() noexcept
{
    m_pq.terminate();
}

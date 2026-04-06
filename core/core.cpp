#include "core.hpp"
#include "structure.hpp"

#include "../infra/types.hpp"
#include "../infra/panic.hpp"
#include "../infra/range.hpp"
#include "../infra/inplace_sort.hpp"
#include "../diag/diag.hpp"

struct IdAllocation
{
	u64 begin;

	u64 size;
};



bool comp_heap_validate_config(const Config* config, PrintSink sink) noexcept;

bool ast_pool_validate_config(const Config* config, PrintSink sink) noexcept;

bool error_sink_validate_config(const Config* config, PrintSink sink) noexcept;

bool global_value_pool_validate_config(const Config* config, PrintSink sink) noexcept;

bool identifier_pool_validate_config(const Config* config, PrintSink sink) noexcept;

bool interpreter_validate_config(const Config* config, PrintSink sink) noexcept;

bool lexical_analyser_validate_config(const Config* config, PrintSink sink) noexcept;

bool opcode_pool_validate_config(const Config* config, PrintSink sink) noexcept;

bool parser_validate_config(const Config* config, PrintSink sink) noexcept;

bool source_reader_validate_config(const Config* config, PrintSink sink) noexcept;

bool type_pool_validate_config(const Config* config, PrintSink sink) noexcept;



MemoryRequirements comp_heap_memory_requirements(const Config* config) noexcept;

MemoryRequirements ast_pool_memory_requirements(const Config* config) noexcept;

MemoryRequirements error_sink_memory_requirements(const Config* config) noexcept;

MemoryRequirements global_value_pool_memory_requirements(const Config* config) noexcept;

MemoryRequirements identifier_pool_memory_requirements(const Config* config) noexcept;

MemoryRequirements interpreter_memory_requirements(const Config* config) noexcept;

MemoryRequirements lexical_analyser_memory_requirements(const Config* config) noexcept;

MemoryRequirements opcode_pool_memory_requirements(const Config* config) noexcept;

MemoryRequirements parser_memory_requirements(const Config* config) noexcept;

MemoryRequirements source_reader_memory_requirements(const Config* config) noexcept;

MemoryRequirements type_pool_memory_requirements(const Config* config) noexcept;



void comp_heap_init(CoreData* core, MemoryAllocation allocation) noexcept;

void ast_pool_init(CoreData* core, MemoryAllocation allocation) noexcept;

void error_sink_init(CoreData* core, MemoryAllocation allocation) noexcept;

void global_value_pool_init(CoreData* core, MemoryAllocation allocation) noexcept;

void identifier_pool_init(CoreData* core, MemoryAllocation allocation) noexcept;

void interpreter_init(CoreData* core, MemoryAllocation allocation) noexcept;

void lexical_analyser_init(CoreData* core, MemoryAllocation allocation) noexcept;

void opcode_pool_init(CoreData* core, MemoryAllocation allocation) noexcept;

void parser_init(CoreData* core, MemoryAllocation allocation) noexcept;

void source_reader_init(CoreData* core, MemoryAllocation allocation) noexcept;

void type_pool_init(CoreData* core, MemoryAllocation allocation) noexcept;



using validate_config_func = bool (*) (const Config* config, PrintSink sink) noexcept;

using memory_requirements_func = MemoryRequirements (*) (const Config* config) noexcept;

using init_func = void (*) (CoreData* core, MemoryAllocation allocation) noexcept;



struct MemoryIdRequirementsAlignmentComparator
{
	static s32 compare(const MemoryIdRequirements* a, const MemoryIdRequirements* b) noexcept
	{
		return a->alignment - b->alignment;
	}
};

static u32 id_requirements_index_from_ptr(const MemoryIdRequirements* ptr, const MemoryRequirements* memory_requirements) noexcept
{
	const u32 memory_requirements_index = static_cast<u32>((reinterpret_cast<const byte*>(ptr) - reinterpret_cast<const byte*>(memory_requirements))) / sizeof(MemoryRequirements);

	const u32 id_index = static_cast<u32>(ptr - memory_requirements[memory_requirements_index].id_requirements);

	return memory_requirements_index * MAX_MEMORY_ID_REQUIREMENTS_COUNT + id_index;
}



CoreData* create_core_data(const Config* config) noexcept
{
	static constexpr validate_config_func VALIDATE_CONFIG_FUNCS[] = {
		&comp_heap_validate_config,
		&ast_pool_validate_config,
		&error_sink_validate_config,
		&global_value_pool_validate_config,
		&identifier_pool_validate_config,
		&lexical_analyser_validate_config,
		&opcode_pool_validate_config,
		&type_pool_validate_config,
		&parser_validate_config,
		&source_reader_validate_config,
		&interpreter_validate_config,
	};

	static constexpr memory_requirements_func MEMORY_REQUIREMENTS_FUNCS[] = {
		&comp_heap_memory_requirements,
		&ast_pool_memory_requirements,
		&error_sink_memory_requirements,
		&global_value_pool_memory_requirements,
		&identifier_pool_memory_requirements,
		&lexical_analyser_memory_requirements,
		&opcode_pool_memory_requirements,
		&type_pool_memory_requirements,
		&parser_memory_requirements,
		&source_reader_memory_requirements,
		&interpreter_memory_requirements,
	};

	static constexpr init_func INIT_FUNCS[] = {
		&comp_heap_init,
		&ast_pool_init,
		&error_sink_init,
		&global_value_pool_init,
		&identifier_pool_init,
		&lexical_analyser_init,
		&opcode_pool_init,
		&type_pool_init,
		&parser_init,
		&source_reader_init,
		&interpreter_init,
	};

	const bool enable_flags[] = {
		config->enable.heap,
		config->enable.ast_pool,
		config->enable.error_sink,
		config->enable.global_value_pool,
		config->enable.identifier_pool,
		config->enable.lexical_analyser,
		config->enable.opcode_pool,
		config->enable.type_pool,
		config->enable.parser,
		config->enable.source_reader,
		config->enable.interpreter,
	};

	static_assert(array_count(VALIDATE_CONFIG_FUNCS) == array_count(MEMORY_REQUIREMENTS_FUNCS));

	static_assert(array_count(VALIDATE_CONFIG_FUNCS) == array_count(INIT_FUNCS));

	static_assert(array_count(VALIDATE_CONFIG_FUNCS) == array_count(enable_flags));

	static constexpr u32 MEMBER_COUNT = static_cast<u32>(array_count(MEMORY_REQUIREMENTS_FUNCS));



	const Maybe<minos::FileHandle> config_log_file = config_open_log_file(config->logging.config, some(minos::StdFileName::StdOut));

	if (is_some(config_log_file))
		diag::print_config(get(config_log_file), config);



	bool is_valid_config = true;

	const minos::FileHandle config_validation_log_file = minos::standard_file_handle(minos::StdFileName::StdErr);

	for (u64 i = 0; i != MEMBER_COUNT; ++i)
	{
		if (enable_flags[i] && !VALIDATE_CONFIG_FUNCS[i](config, print_make_sink(config_validation_log_file)))
			is_valid_config = false;
	}

	if (!is_valid_config)
		minos::exit_process(1);



	MemoryRequirements memory_requirements[MEMBER_COUNT];

	for (u32 i = 0; i != MEMBER_COUNT; ++i)
	{
		if (enable_flags[i])
			memory_requirements[i] = MEMORY_REQUIREMENTS_FUNCS[i](config);
		else
			memory_requirements[i] = MemoryRequirements{};
	}

	MemoryIdRequirements* id_requirements_buf[MAX_MEMORY_ID_REQUIREMENTS_COUNT * MEMBER_COUNT];

	u64 id_requirements_count = 0;

	for (MemoryRequirements& req : memory_requirements)
	{
		for (u64 i = 0; i != req.id_requirements_count; ++i)
		{
			ASSERT_OR_IGNORE(id_requirements_count + req.id_requirements_count < array_count(id_requirements_buf));

			id_requirements_buf[id_requirements_count + i] = &req.id_requirements[i];
		}

		id_requirements_count += req.id_requirements_count;
	}

	const MutRange<MemoryIdRequirements*> id_requirements{ id_requirements_buf, id_requirements_count };

	inplace_sort<MemoryIdRequirements*, MemoryIdRequirementsAlignmentComparator>(id_requirements);

	const u64 page_mask = minos::page_bytes() - 1;

	u64 total_allocation_size = sizeof(CoreData);

	IdAllocation id_allocations[MEMBER_COUNT * MAX_MEMORY_ID_REQUIREMENTS_COUNT];

	for (const MemoryIdRequirements* req : id_requirements)
	{
		ASSERT_OR_IGNORE(is_pow2(req->alignment) && req->alignment <= 4096);

		total_allocation_size = (total_allocation_size + page_mask) & ~page_mask;

		const u32 reverse_index = id_requirements_index_from_ptr(req, memory_requirements);

		id_allocations[reverse_index] = IdAllocation{ total_allocation_size, req->reserve };

		total_allocation_size += req->reserve;

		if (total_allocation_size > UINT32_MAX * req->alignment)
			panic("Ids with element size % could not be allocated so that they could be indexed by an unsigned 32-bit integer in a unified address space.\n", req->alignment);
	}

	const u64 private_member_data_offset = total_allocation_size;

	for (const MemoryRequirements& req : memory_requirements)
	{
		if (!add_checked_u64(total_allocation_size, req.private_reserve, &total_allocation_size))
			panic("Size of core data exceeds size of address space.\n");
	}

	void* const memory = minos::mem_reserve(total_allocation_size);

	if (memory == nullptr)
		panic("Failed to reserve memory for `CoreData` (0x%[|X]).\n", minos::last_error());

	if (!minos::mem_commit(memory, (sizeof(CoreData) + page_mask) & ~page_mask))
		panic("Failed to commit memory for `CoreData` (0x%[|X]).\n", minos::last_error());

	CoreData* const core = static_cast<CoreData*>(memory);
	core->config = config;
	core->allocation_size = total_allocation_size;

	u64 private_allocation_begin = private_member_data_offset;

	for (u64 i = 0; i != MEMBER_COUNT; ++i)
	{
		if (!enable_flags[i])
			continue;

		MemoryAllocation allocation;
		allocation.private_data = MutRange{ static_cast<byte*>(memory) + private_allocation_begin, memory_requirements[i].private_reserve };

		private_allocation_begin += memory_requirements[i].private_reserve;

		for (u64 j = 0; j != memory_requirements[i].id_requirements_count; ++j)
		{
			const IdAllocation id_allocation = id_allocations[i * MAX_MEMORY_ID_REQUIREMENTS_COUNT + j];

			allocation.ids[j] = MutRange{ static_cast<byte*>(memory) + id_allocation.begin, id_allocation.size };
		}

		INIT_FUNCS[i](core, allocation);
	}

	return core;
}

void release_core_data(CoreData* core) noexcept
{
	minos::mem_unreserve(core, core->allocation_size);
}



bool run_compilation(CoreData* core, bool main_is_std) noexcept
{
	if (!import_prelude(core, core->config->std.prelude.filepath))
		return false;

	const Maybe<TypeId> main_file_type_id = import_file(core, core->config->entrypoint.filepath, main_is_std);

	if (is_none(main_file_type_id))
		return false;

	if (core->config->compile_all)
	{
		if (!evaluate_all_file_definitions(core, get(main_file_type_id)))
			return false;
	}
	else
	{
		const IdentifierId entrypoint_name = id_from_identifier(core, core->config->entrypoint.symbol);

		if (!evaluate_file_definition_by_name(core, get(main_file_type_id), entrypoint_name))
			return false;
	}

	return true;
}

void* address_from_core_id(CoreData* core, CoreId id) noexcept
{
	ASSERT_OR_IGNORE((static_cast<u64>(id) << COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2) >= sizeof(CoreData));

	ASSERT_OR_IGNORE((static_cast<u64>(id) << COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2) < core->allocation_size);

	return reinterpret_cast<byte*>(core) + (static_cast<u64>(id) << COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2);
}

CoreId core_id_from_address(CoreData* core, const void* memory) noexcept
{
	ASSERT_OR_IGNORE(memory >= core + 1);

	ASSERT_OR_IGNORE(memory < reinterpret_cast<byte*>(core) + core->allocation_size);

	return static_cast<CoreId>((static_cast<const byte*>(memory) - reinterpret_cast<byte*>(core)) >> COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2);
}

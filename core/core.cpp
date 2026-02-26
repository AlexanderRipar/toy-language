#include "core.hpp"
#include "structure.hpp"

#include "../infra/types.hpp"
#include "../infra/panic.hpp"
#include "../infra/range.hpp"
#include "../infra/inplace_sort.hpp"
#include "../diag/diag.hpp"

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



using memory_requirements_func = MemoryRequirements (*) (const Config* config) noexcept;

using init_func = void (*) (CoreData* core, MemoryAllocation allocation) noexcept;





struct CoreMemberInit
{
	MemoryRequirements memory_requirements;

	u32 id_offsets[4];
};

struct MemoryIdRequirementsPtrComparator
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
	static constexpr memory_requirements_func MEMORY_REQUIREMENTS_FUNCS[] = {
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

	static_assert(array_count(MEMORY_REQUIREMENTS_FUNCS) == array_count(INIT_FUNCS));

	static constexpr u32 MEMBER_COUNT = static_cast<u32>(array_count(MEMORY_REQUIREMENTS_FUNCS));



	const minos::FileHandle config_log_file = config_open_log_file(config->logging.config, some(minos::StdFileName::StdOut));

	if (config_log_file.m_rep != nullptr)
		diag::print_config(config_log_file, config);



	const u64 page_mask = minos::page_bytes() - 1;

	MemoryRequirements memory_requirements[MEMBER_COUNT];

	for (u32 i = 0; i != MEMBER_COUNT; ++i)
		memory_requirements[i] = MEMORY_REQUIREMENTS_FUNCS[i](config);

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

	inplace_sort<MemoryIdRequirements*, MemoryIdRequirementsPtrComparator>(id_requirements);

	u64 total_allocation_size = sizeof(CoreData);

	u64 id_offsets[MEMBER_COUNT * MAX_MEMORY_ID_REQUIREMENTS_COUNT];

	for (const MemoryIdRequirements* req : id_requirements)
	{
		ASSERT_OR_IGNORE(is_pow2(req->alignment) && req->alignment <= 4096);

		total_allocation_size = (total_allocation_size + page_mask) & ~page_mask;

		const u32 reverse_index = id_requirements_index_from_ptr(req, memory_requirements);

		id_offsets[reverse_index] = total_allocation_size;

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
		MemoryAllocation allocation;
		allocation.private_data = static_cast<byte*>(memory) + private_allocation_begin;
		
		for (u64 j = 0; j != memory_requirements[i].id_requirements_count; ++j)
			allocation.ids[j] = static_cast<byte*>(memory) + id_offsets[i * MAX_MEMORY_ID_REQUIREMENTS_COUNT + j];

		INIT_FUNCS[i](core, allocation);
	}



	const minos::FileHandle imported_asts_log_file = config_open_log_file(config->logging.imports.asts, some(minos::StdFileName::StdOut));

	const minos::FileHandle imported_opcodes_log_file = config_open_log_file(config->logging.imports.opcodes, some(minos::StdFileName::StdOut));

	const minos::FileHandle imported_types_log_file = config_open_log_file(config->logging.imports.types, some(minos::StdFileName::StdOut));

	return core;
}

void release_core_data(CoreData* core) noexcept
{
	minos::mem_unreserve(core, core->allocation_size);
}



bool run_compilation(CoreData* core, bool main_is_std) noexcept
{
	if (!import_prelude(&core->interp, core->config->std.prelude.filepath))
		return false;

	const Maybe<TypeId> main_file_type_id = import_file(&core->interp, core->config->entrypoint.filepath, main_is_std);

	if (is_none(main_file_type_id))
		return false;

	if (core->config->compile_all)
	{
		if (!evaluate_all_file_definitions(&core->interp, get(main_file_type_id)))
			return false;
	}
	else
	{
		const IdentifierId entrypoint_name = id_from_identifier(&core->identifiers, core->config->entrypoint.symbol);

		if (!evaluate_file_definition_by_name(&core->interp, get(main_file_type_id), entrypoint_name))
			return false;
	}

	return true;
}

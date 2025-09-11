cmake_minimum_required(VERSION 3.22)

project(compil0r)



if(MSVC)
	set(COMPILER_NAME "msvc")
elseif (${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
	set(COMPILER_NAME "gcc")
elseif (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
	set(COMPILER_NAME "clang")
endif()



set(INFRA_SOURCES
	infra/container/index_map.hpp
	infra/container/reserved_heap.hpp
	infra/container/reserved_vec.hpp
	infra/minos/minos.hpp
	infra/minos/minos_win32.cpp
	infra/minos/minos_linux.cpp
	infra/alloc_pool.hpp
	infra/alloc_pool.cpp
	infra/common.hpp
	infra/common.cpp
	infra/hash.hpp
	infra/inplace_sort.hpp
	infra/range.hpp
	infra/threading.hpp
)

set(CORE_SOURCES
	core/core.hpp
	core/ast_pool.cpp
	core/closure_pool.cpp
	core/partial_value_pool.cpp
	core/comp_values.cpp
	core/config.cpp
	core/error_sink.cpp
	core/global_value_pool.cpp
	core/identifier_pool.cpp
	core/interpreter.cpp
	core/parse.cpp
	core/source_reader.cpp
	core/type_pool.cpp
)

set(DIAG_SOURCES
	diag/diag.hpp
	diag/diag.cpp
	diag/print_ast.cpp
	diag/print_type.cpp
)

function(prepare_exe NAME)
	add_executable(${NAME} ${ARGN})

	target_compile_features(${NAME} PRIVATE cxx_std_17)

	if (MSVC)
		target_link_libraries(${NAME} PRIVATE synchronization)

		target_link_options(${NAME} PRIVATE
			"$<$<NOT:$<CONFIG:Debug>>:/DEBUG>"
			"$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>"
			"$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>"
		)

		target_compile_options(${NAME} PRIVATE
			/W4
			/wd4201
			"$<$<NOT:$<CONFIG:Debug>>:/Zi>"
		)
	else()
		target_compile_options(${NAME} PRIVATE
			-Wall
			-Wpedantic
			-Wextra
			-g
		)
	endif()
endfunction()



prepare_exe(compil0r main.cpp ${INFRA_SOURCES} ${CORE_SOURCES} ${DIAG_SOURCES})



add_subdirectory(test)

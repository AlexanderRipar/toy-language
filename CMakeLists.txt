cmake_minimum_required(VERSION 3.22)

project(compil0r)



set(INFRA_SOURCES
	infra/container/index_map.hpp
	infra/container/reserved_heap.hpp
	infra/container/reserved_vec.hpp
	infra/minos/minos.hpp
	infra/minos/minos_win32.cpp
	infra/minos/minos_linux.cpp
	infra/common.hpp
	infra/common.cpp
	infra/hash.hpp
	infra/inplace_sort.hpp
	infra/opt.hpp
	infra/range.hpp
)

set(CORE_SOURCES
	core/core.hpp
	core/core.cpp
	core/handle_pool.cpp
	core/ast_pool.cpp
	core/partial_value_pool.cpp
	core/comp_values.cpp
	core/config.cpp
	core/error_sink.cpp
	core/global_value_pool2.cpp
	core/identifier_pool.cpp
	core/interpreter2.cpp
	core/lexical_analyser.cpp
	core/opcode_pool.cpp
	core/parse.cpp
	core/source_reader.cpp
	core/type_pool.cpp
)

set(DIAG_SOURCES
	diag/diag.hpp
	diag/diag.cpp
	diag/print_ast.cpp
	diag/print_opcodes.cpp
	diag/print_type.cpp
)

set(INTERFACE_HEADERS
	core/core.hpp
	infra/common.hpp
	infra/opt.hpp
	infra/minos/minos.hpp
)

function(set_common_target_options NAME)
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
			/wd4200 # nonstandard extension used: zero-sized array in struct/union
			/wd4201 # nonstandard extension used: nameless struct/union
			/wd4611 # interaction between '_setjmp' and C++ object destruction is non-portable
			"$<$<NOT:$<CONFIG:Debug>>:/Zi>"
		)
	elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
		target_compile_options(${NAME} PRIVATE
			-Wall
			-Wpedantic
			-Wextra
			-g
		)
	elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
		target_compile_options(${NAME} PRIVATE
			-Wall
			-Wpedantic
			-Wextra
			-Wno-c99-extensions       # flexible array members are a C99 feature
			-Wno-gnu-anonymous-struct # anonymous structs are a GNU extension
			-Wno-nested-anon-types    # anonymous types declared in an anonymous union are an extension
			-g
		)
	else()
		message(FATAL_ERROR "Unknown compiler id ${CMAKE_CXX_COMPILER_ID}")
	endif()
endfunction()

function(prepare_object_library NAME)
	add_library(${NAME} OBJECT ${ARGN})

	set_common_target_options(${NAME})
endfunction()

function(prepare_executable NAME)
	add_executable(${NAME} ${ARGN})

	set_common_target_options(${NAME})

	target_link_libraries(${NAME} PRIVATE elib)
endfunction()

prepare_object_library(elib ${CORE_SOURCES} ${DIAG_SOURCES} ${INFRA_SOURCES})

prepare_executable(eval main.cpp ${INTERFACE_HEADERS})

add_subdirectory(test)

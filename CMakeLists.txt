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
	infra/alloc_pool.hpp
	infra/alloc_pool.cpp
	infra/common.hpp
	infra/common.cpp
	infra/hash.hpp
	infra/minos.hpp
	infra/minos_win32.cpp
	infra/minos_linux.cpp
	infra/range.hpp
	infra/container.hpp
	infra/threading.hpp
)

set(CORE_SOURCES
	core/error_sink.cpp
	core/config.hpp
	core/config.cpp
	core/ast_pool.cpp
	core/ast.cpp
	core/identifier_pool.cpp
	core/interpreter.cpp
	core/parse.cpp
	core/pass_data.hpp
	core/source_reader.cpp
	core/scope_pool.cpp
	core/token.cpp
	core/type_pool.cpp
	core/type_pool2.cpp
	core/typechecker.cpp
	core/value_pool.cpp
	core/ast_attach.hpp
	core/ast_helper.hpp
)

set(DIAG_SOURCES
	diag/diag.hpp
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

cmake_minimum_required(VERSION 3.22)

project(compil0r CXX)

if (WIN32)
	enable_language(ASM_MASM)
else()
	enable_language(ASM)
endif()


set(INFRA_SOURCES
	infra/container/id_map.hpp
	infra/container/reserved_vec.hpp
	infra/minos/minos.hpp
	infra/minos/minos_win32.cpp
	infra/minos/minos_linux.cpp
	infra/print/print.hpp
	infra/print/print.cpp
	infra/toml/toml.hpp
	infra/toml/toml.cpp
	infra/common.cpp
	infra/assert.hpp
	infra/hash.hpp
	infra/host_compiler.hpp
	infra/inplace_sort.hpp
	infra/math.hpp
	infra/opt.hpp
	infra/panic.hpp
	infra/range.hpp
	infra/template_helpers.hpp
	infra/types.hpp
)

set(CORE_SOURCES
	core/core.hpp
	core/core.cpp
	core/ast_pool.cpp
	core/comp_heap.cpp
	core/comp_values.cpp
	core/config.cpp
	core/error_sink.cpp
	core/foreign_function_interface.cpp
	core/identifier_pool.cpp
	core/interpreter.cpp
	core/lexical_analyser.cpp
	core/opcode_pool.cpp
	core/parse.cpp
	core/shadow_store.cpp
	core/source_reader.cpp
	core/temp_stack.cpp
	core/type_pool.cpp
)

set(DIAG_SOURCES
	diag/diag.hpp
	diag/print_ast.cpp
	diag/print_config.cpp
	diag/print_opcodes.cpp
	diag/print_type.cpp
)

set(INTERFACE_HEADERS
	core/core.hpp
	infra/minos/minos.hpp
)

if (WIN32)
	set(FFI_ASM_SOURCES
		core/ffi_native/x64_win32_perform_native_call.asm
	)
else()
	set(FFI_ASM_SOURCES
		core/ffi_native/x64_sysv_perform_native_call.s
	)
endif()

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
			"$<$<COMPILE_LANGUAGE:CXX>:/W4>"
			"$<$<COMPILE_LANGUAGE:CXX>:/wd4200>" # nonstandard extension used: zero-sized array in struct/union
			"$<$<COMPILE_LANGUAGE:CXX>:/wd4201>" # nonstandard extension used: nameless struct/union
			"$<$<COMPILE_LANGUAGE:CXX>:/wd4611>" # interaction between '_setjmp' and C++ object destruction is non-portable
			"$<$<COMPILE_LANGUAGE:CXX>:$<$<NOT:$<CONFIG:Debug>>:/Zi>>"
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

			# Suppress `clang: warning: argument unused during compilation: '-D NDEBUG'` for assembly
			# in release builds. No idea how to get rid of the `-DNDEBUG`, but hey, this works I guess.
			"$<$<COMPILE_LANGUAGE:ASM>:$<$<NOT:$<CONFIG:Debug>>:-Wno-unused-command-line-argument>>"
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

prepare_object_library(elib ${CORE_SOURCES} ${DIAG_SOURCES} ${INFRA_SOURCES} ${FFI_ASM_SOURCES})

prepare_executable(eval main.cpp ${INTERFACE_HEADERS})

add_subdirectory(test)

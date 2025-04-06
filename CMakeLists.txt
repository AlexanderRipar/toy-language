cmake_minimum_required(VERSION 3.22)

add_subdirectory(test)

project(compil0r)



add_executable(
	compil0r main.cpp


	infra/alloc_pool.hpp infra/alloc_pool.cpp
	infra/common.hpp infra/common.cpp
	infra/hash.hpp
	infra/minos.hpp infra/minos_win32.cpp infra/minos_linux.cpp
	infra/range.hpp
	infra/container.hpp
	infra/threading.hpp

	core/error.hpp core/error.cpp
	core/config.hpp core/config.cpp

	core/ast_pool.cpp
	core/ast.cpp
	core/identifier_pool.cpp
	core/interpreter.cpp
	core/parse.cpp
	core/pass_data.hpp
	core/read.cpp
	core/scope_pool.cpp
	core/token.cpp
	core/type_pool.cpp
	core/type_pool2.cpp
	core/typechecker.cpp
	core/value_pool.cpp

	core/ast_attach.hpp core/ast_helper.hpp

	diag/diag.hpp
	diag/print_ast.cpp
	diag/print_type.cpp
)

target_compile_features(compil0r PRIVATE cxx_std_17)

if (MSVC)
	target_link_libraries(compil0r PRIVATE synchronization)

	target_link_options(compil0r PRIVATE
		"$<$<NOT:$<CONFIG:Debug>>:/DEBUG>"
		"$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>"
		"$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>"
	)

	target_compile_options(compil0r PRIVATE
		/W4
		/wd4201
		"$<$<NOT:$<CONFIG:Debug>>:/Zi>"
	)
else()
	target_compile_options(compil0r PRIVATE
		-Wall
		-Wpedantic
		-Wextra
		-g
	)
endif()

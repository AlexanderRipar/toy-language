cmake_minimum_required(VERSION 3.22)

add_subdirectory(test)

project(compil0r)



add_executable(
	compil0r main.cpp


	infra/alloc_pool.hpp infra/alloc_pool.cpp
	infra/common.hpp infra/common.cpp
	infra/hash.hpp
	infra/minos.hpp infra/minos.cpp
	infra/range.hpp
	infra/container.hpp
	infra/threading.hpp

	error.hpp error.cpp
	config.hpp config.cpp

	ast_pool.cpp
	identifier_pool.cpp
	interpreter.cpp
	parse.cpp
	pass_data.hpp
	read.cpp
	scope_pool.cpp
	token.cpp
	type_pool.cpp
	typechecker.cpp
	value_pool.cpp
	ast.cpp

	ast_attach.hpp ast_helper.hpp

	diag/diag.hpp
	diag/print_ast.cpp
	diag/print_type.cpp
)

target_link_libraries(compil0r PRIVATE synchronization)

target_compile_features(compil0r PRIVATE cxx_std_17)

if (MSVC)
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

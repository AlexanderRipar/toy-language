cmake_minimum_required(VERSION 3.22)

project(test)

add_executable(test test_main.cpp
	test_helpers.hpp
	minos_tests.cpp
	ast_tests.cpp
	type_pool2_tests.cpp


	../infra/alloc_pool.hpp ../infra/alloc_pool.cpp
	../infra/common.hpp ../infra/common.cpp
	../infra/minos.hpp ../infra/minos_win32.cpp ../infra/minos_linux.cpp
	../infra/hash.hpp
	../infra/range.hpp
	../infra/container.hpp
	../infra/threading.hpp

	../error.hpp ../error.cpp
	../config.hpp ../config.cpp

	../ast_pool.cpp
	../ast.cpp
	../identifier_pool.cpp
	../interpreter.cpp
	../parse.cpp
	../pass_data.hpp
	../read.cpp
	../scope_pool.cpp
	../token.cpp
	../type_pool.cpp
	../type_pool2.cpp
	../typechecker.cpp
	../value_pool.cpp

	../ast_attach.hpp ../ast_helper.hpp

	../diag/diag.hpp
	../diag/print_ast.cpp
	../diag/print_type.cpp
)


target_compile_features(test PRIVATE cxx_std_17)

if (MSVC)
	target_link_libraries(test PRIVATE synchronization)

	target_link_options(test PRIVATE
		"$<$<NOT:$<CONFIG:Debug>>:/DEBUG>"
		"$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>"
		"$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>"
	)

	target_compile_options(test PRIVATE
		/W4
		/wd4201
		"$<$<NOT:$<CONFIG:Debug>>:/Zi>"
	)
else()
	target_compile_options(test PRIVATE
		-Wall
		-Wpedantic
		-Wextra
		-g
	)
endif()

add_custom_target(run-tests COMMAND test --ignore-debugbreaks WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_dependencies(run-tests test)

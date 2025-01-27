cmake_minimum_required(VERSION 3.22)

project(test)

add_executable(test test_main.cpp
	test_helpers.hpp
	ast2_tests.cpp


	../infra/alloc_pool.hpp ../infra/alloc_pool.cpp
	../infra/common.hpp ../infra/common.cpp
	../infra/minos.hpp ../infra/minos.cpp
	../infra/hash.hpp
	../infra/range.hpp
	../infra/container.hpp
	../infra/threading.hpp

	../error.hpp ../error.cpp
	../config.hpp ../config.cpp

	../identifier_pool.cpp
	../parse.cpp
	../pass_data.hpp
	../read.cpp
	../token.cpp
	../ast2.hpp ../ast2.cpp

	../diag/diag.hpp
	../diag/print_ast.cpp
)

target_link_libraries(test PRIVATE synchronization)

target_compile_features(test PRIVATE cxx_std_17)

if (MSVC)
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

add_custom_target(run-tests COMMAND test --ignore-debugbreaks)

add_dependencies(run-tests test)

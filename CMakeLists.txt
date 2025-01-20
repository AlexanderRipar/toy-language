cmake_minimum_required(VERSION 3.22)

add_subdirectory(test)

project(compil0r)



add_executable(
	compil0r main.cpp

	ast2.hpp ast2.cpp

	infra/common.hpp infra/common.cpp
	infra/hash.hpp
	infra/minos.hpp infra/minos.cpp
	infra/range.hpp
	infra/container.hpp
	infra/threading.hpp

	error.hpp error.cpp
	config.hpp config.cpp

	pass/ast.hpp
	pass/pass_data.hpp pass/pass_data.cpp
	pass/read.cpp
	pass/parse.cpp

	diag/inc.hpp
	diag/print_ast.cpp
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

cmake_minimum_required(VERSION 3.22)

project(compil0r)



add_executable(
	compil0r main.cpp
	common.hpp
	range.hpp
	memory.hpp
	threading.hpp
	token.hpp
	minos.hpp minos.cpp
	config.hpp config.cpp
	task_manag0r.hpp task_manag0r.cpp
	parse.hpp parse.cpp
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

add_subdirectory(test)

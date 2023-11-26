cmake_minimum_required(VERSION 3.22)

project(compil0r)



add_executable(
	compil0r main.cpp
	common.hpp
	minwin.hpp
	range.hpp
	token.hpp
	global_data.hpp global_data.cpp
	init.hpp init.cpp
	lex0r.hpp lex0r.cpp
)

target_compile_features(compil0r PRIVATE cxx_std_17)

if (MSVC)
	target_compile_options(compil0r PRIVATE /W4 /wd4201)
else()
	target_compile_options(compil0r PRIVATE -Wall -Wpedantic -Wextra)
endif()

add_subdirectory(test)

cmake_minimum_required(VERSION 3.22)

project(compilator)



add_executable(compilator main.cpp
	tok/tok_gen.hpp tok/tok_gen.cpp
	ast/ast_gen.hpp ast/ast_gen.cpp
	ast/ast_fmt.hpp ast/ast_fmt.cpp
	ast/ast_data_structure.hpp ast/ast_data_structure.cpp
	util/types.hpp
	util/strutil.hpp
	util/strview.hpp
	util/vec.hpp
	util/str.hpp
	util/minimal_os.hpp
)

target_compile_features(compilator PRIVATE cxx_std_17)

if (MSVC)
	target_compile_options(compilator PRIVATE /W4 /wd4201)
else()
	target_compile_options(compilator PRIVATE -Wall -Wpedantic -Wextra)
endif()

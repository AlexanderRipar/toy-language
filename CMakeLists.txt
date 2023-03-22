cmake_minimum_required(VERSION 3.22)

project(compilator)



add_executable(compilator main.cpp
	token_gen.hpp token_gen.cpp
	ast_gen.hpp ast_gen.cpp
	ast_fmt.hpp ast_fmt.cpp
	ast_data_structure.hpp ast_data_structure.cpp
	util/types.hpp
	util/strutil.hpp
	util/strview.hpp
	util/vec.hpp
	util/refptr.hpp
	util/ownptr.hpp
	util/str.hpp
)

target_compile_features(compilator PRIVATE cxx_std_17)

if (MSVC)
	target_compile_options(compilator PRIVATE /W4 /wd4201)
else()
	target_compile_options(compilator PRIVATE -Wall -Wpedantic -Wextra)
endif()

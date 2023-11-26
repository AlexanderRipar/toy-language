cmake_minimum_required(VERSION 3.22)

add_executable(
	test test_main.cpp
	test_helpers.hpp test_helpers.cpp
	test_global_data.hpp test_global_data.cpp ../global_data.hpp ../global_data.cpp
)

target_compile_features(test PRIVATE cxx_std_17)

target_compile_definitions(test PRIVATE TEST)

if (MSVC)
	target_compile_options(test PRIVATE /W4 /wd4201)
else()
	target_compile_options(test PRIVATE -Wall -Wpedantic -Wextra)
endif()

add_custom_command(
  OUTPUT test_output.txt
  DEPENDS test
  COMMAND test test_output.txt
  VERBATIM
)

# add_custom_target(
# 	run_test ALL
# 	DEPENDS test_output.txt
# )

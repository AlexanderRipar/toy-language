cmake_minimum_required(VERSION 3.22)

add_executable(test
	main.cpp
	tests.hpp
	helpers.hpp helpers.cpp
	test_threading.cpp
	../common.hpp
	../range.hpp
	../threading.hpp
	../token.hpp
	../minos.hpp ../minos.cpp
#	../config.hpp ../config.cpp
#	../task_manag0r.hpp ../task_manag0r.cpp
)

target_link_libraries(test PRIVATE synchronization)

target_compile_features(test PRIVATE cxx_std_17)

target_compile_definitions(test PRIVATE TEST)

if (MSVC)
	target_link_options(test PRIVATE
		"$<$<NOT:$<CONFIG:Debug>>:/DEBUG>"
		"$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>"
		"$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>"
	)

	target_compile_options(test PRIVATE
		/W4
		/wd4201
		/Zi
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

add_custom_command(
  OUTPUT test_output.txt
  DEPENDS test
  COMMAND test --timeout 5000 --logfile test_output.txt
  VERBATIM
)

add_custom_target(
	run_test ALL
	DEPENDS test_output.txt
)

add_subdirectory(generator)

set(PROGRAMS_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)
set(SHADERS_DIR ${CMAKE_SOURCE_DIR}/shaders)

file(GLOB SHADER_SOURCES
	${SHADERS_DIR}/*.vert
	${SHADERS_DIR}/*.frag
)

add_custom_command(
	OUTPUT ${PROGRAMS_CPP_DIR}
	COMMAND ${CMAKE_COMMAND} -E make_directory ${PROGRAMS_CPP_DIR}
	COMMENT "Creating ${PROGRAMS_CPP_DIR}"
)

set(list_txt ${CMAKE_CURRENT_SOURCE_DIR}/list.txt)
set(output_cpp ${PROGRAMS_CPP_DIR}/programs.cpp)
set(output_h ${PROGRAMS_CPP_DIR}/programs.h)
add_custom_command(
	OUTPUT ${output_cpp} ${output_h}
	VERBATIM
	COMMAND ProgramsGen ${list_txt} ${SHADERS_DIR} ${PROGRAMS_CPP_DIR}
	DEPENDS ProgramsGen ${list_txt} ${SHADER_SOURCES} ${PROGRAMS_CPP_DIR}
	COMMENT "Generating programs cpp files"
)
list(APPEND PROGRAMS_GENERATED ${output_cpp} ${output_h})

add_custom_target(programs_generated ALL DEPENDS ${PROGRAMS_GENERATED})

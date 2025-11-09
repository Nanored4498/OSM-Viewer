project(ProgramsGen)

file(GLOB PROTO_COMP_SOURCES
	*.cpp
	${CMAKE_SOURCE_DIR}/src/glad/*.c
)

add_executable(${PROJECT_NAME} ${PROTO_COMP_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	${OPENGL_LIBRARIES}
	glfw
)
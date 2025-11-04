project(ProgramsGen)

file(GLOB PROTO_COMP_SOURCES
	*.cpp
)

add_executable(${PROJECT_NAME} ${PROTO_COMP_SOURCES})

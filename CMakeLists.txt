cmake_minimum_required(VERSION 3.14)

project(OSM)

# Compilation options
set(CMAKE_CXX_STANDARD 23)
if(CMAKE_CXX_COMPILER_ID MATCHES GNU OR CMAKE_CXX_COMPILER_ID MATCHES Clang)
	set(CMAKE_CXX_FLAGS_DEBUG "-g -Og")
	set(CMAKE_CXX_FLAGS_RELEASE "-O3")
	add_compile_options(
		-Wall
		-Wextra
		-Werror
		-fdiagnostics-color
	)
endif()

# Find packages
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(ZLIB REQUIRED)

# Subdirectories
add_subdirectory(src/proto)
add_subdirectory(src/programs)

# Sources
file(GLOB SOURCES
	src/*.cpp
	src/data/*.cpp
	src/glad/*.c
	src/programs/generated/*.cpp
	src/proto/*.cpp
	src/proto/generated/*.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

add_dependencies(${PROJECT_NAME}
	proto_generated
	programs_generated
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	MAP_DIR=\"${CMAKE_SOURCE_DIR}/map\"
	SHADER_DIR=\"${CMAKE_SOURCE_DIR}/shaders\"
	FONT_DIR=\"${CMAKE_SOURCE_DIR}/fonts\"
)

target_include_directories(${PROJECT_NAME} PRIVATE
	src
	src/proto
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	${OPENGL_LIBRARIES}
	${ZLIB_LIBRARIES} # TODO: Remove
	glfw
)

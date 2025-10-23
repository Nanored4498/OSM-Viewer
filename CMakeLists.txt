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

add_subdirectory(src/proto)

# Sources
file(GLOB SOURCES
	src/*.cpp
	src/glad/*.c
	src/proto/*.cpp
	src/proto/generated/*.cpp
)

# Find packages
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(${PROJECT_NAME} ${SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE
	SHADER_DIR=\"${CMAKE_SOURCE_DIR}/shaders\"
)

target_include_directories(${PROJECT_NAME} PRIVATE
	src
	src/proto
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	${OPENGL_LIBRARIES}
	${ZLIB_LIBRARIES}
	glfw
)

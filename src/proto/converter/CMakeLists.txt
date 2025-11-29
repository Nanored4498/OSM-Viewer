add_subdirectory(enums)

project(Converter)

file(GLOB CONV_SOURCES
	*.cpp
	enums/enums.cpp
	${CMAKE_SOURCE_DIR}/src/data/*.cpp
	${CMAKE_SOURCE_DIR}/src/proto/*.cpp
	${CMAKE_SOURCE_DIR}/src/proto/generated/*.cpp
)

add_executable(${PROJECT_NAME} ${CONV_SOURCES})

add_dependencies(${PROJECT_NAME}
	proto_generated
	enums_generated
)

target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/proto
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	${ZLIB_LIBRARIES}
)
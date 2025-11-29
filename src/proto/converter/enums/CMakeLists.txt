project(EnumsGen)

add_executable(${PROJECT_NAME} gen.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE
	src
)

set(enum_cpp ${CMAKE_CURRENT_SOURCE_DIR}/enums.cpp)
set(enum_h ${CMAKE_CURRENT_SOURCE_DIR}/enums.h)

add_custom_command(
	OUTPUT ${enum_cpp} ${enum_h}
	VERBATIM
	COMMAND ${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS ${PROJECT_NAME}
	COMMENT "Generating enums files"
)

add_custom_target(enums_generated ALL DEPENDS ${enum_cpp} ${enum_h})
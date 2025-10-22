add_subdirectory(compiler)

file(GLOB PROTO_SOURCES
	*.proto
)

set(PROTO_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)

add_custom_command(
	OUTPUT ${PROTO_CPP_DIR}
	COMMAND ${CMAKE_COMMAND} -E make_directory ${PROTO_CPP_DIR}
	COMMENT "Creating ${PROTO_CPP_DIR}"
)

foreach(source IN LISTS PROTO_SOURCES)
	get_filename_component(FILENAME ${source} NAME_WLE)
	set(output_cpp ${PROTO_CPP_DIR}/${FILENAME}.pb.cpp)
	set(output_h ${PROTO_CPP_DIR}/${FILENAME}.pb.h)
	add_custom_command(
		OUTPUT ${output_cpp} ${output_h}
    	VERBATIM
    	COMMAND Proto2CPP ${source} ${PROTO_CPP_DIR}
    	DEPENDS Proto2CPP ${source} ${PROTO_CPP_DIR}
    	COMMENT "Compiling ${source}"
  	)
	list(APPEND PROTO_GENERATED ${output_cpp} ${output_h})
endforeach()

add_custom_target(proto_generated ALL DEPENDS ${PROTO_GENERATED})

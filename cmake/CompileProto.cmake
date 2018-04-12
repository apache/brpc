function(compile_proto OUT_HDRS OUT_SRCS DESTDIR HDR_OUTPUT_DIR PROTO_DIR PROTO_FILES)
  foreach(P ${PROTO_FILES})
    string(REPLACE .proto .pb.h HDR ${P})
    set(HDR_RELATIVE ${HDR})
    set(HDR ${DESTDIR}/${HDR})
    string(REPLACE .proto .pb.cc SRC ${P})
    set(SRC ${DESTDIR}/${SRC})
    list(APPEND HDRS ${HDR})
    list(APPEND SRCS ${SRC})
    add_custom_command(
      OUTPUT ${HDR} ${SRC}
      COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTOC_FLAGS} -I${PROTO_DIR} --cpp_out=${DESTDIR} ${PROTO_DIR}/${P}
      COMMAND ${CMAKE_COMMAND} -E copy ${HDR} ${HDR_OUTPUT_DIR}/${HDR_RELATIVE}
      DEPENDS ${PROTO_DIR}/${P}
    )
  endforeach()
  set(${OUT_HDRS} ${HDRS} PARENT_SCOPE)
  set(${OUT_SRCS} ${SRCS} PARENT_SCOPE)
endfunction()
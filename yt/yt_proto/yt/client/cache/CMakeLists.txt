
# This file was generated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.


get_built_tool_path(
  TOOL_protoc_bin
  TOOL_protoc_dependency
  contrib/tools/protoc/bin
  protoc
)
get_built_tool_path(
  TOOL_cpp_styleguide_bin
  TOOL_cpp_styleguide_dependency
  contrib/tools/protoc/plugins/cpp_styleguide
  cpp_styleguide
)

add_library(yt-client-cache-proto)
target_include_directories(yt-client-cache-proto PUBLIC
  ${CMAKE_BINARY_DIR}/yt
)
target_link_libraries(yt-client-cache-proto PUBLIC
  contrib-libs-linux-headers
  contrib-libs-cxxsupp
  yutil
  yt_proto-yt-core
  contrib-libs-protobuf
)
target_proto_messages(yt-client-cache-proto PRIVATE
  ${CMAKE_SOURCE_DIR}/yt/yt_proto/yt/client/cache/proto/config.proto
)
target_proto_addincls(yt-client-cache-proto
  ./yt
  ${CMAKE_SOURCE_DIR}/yt
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/yt
  ${CMAKE_SOURCE_DIR}/yt
  ${CMAKE_SOURCE_DIR}/contrib/libs/protobuf/src
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/contrib/libs/protobuf/src
)
target_proto_outs(yt-client-cache-proto
  --cpp_out=${CMAKE_BINARY_DIR}/yt
  --cpp_styleguide_out=${CMAKE_BINARY_DIR}/yt
)


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

add_library(server-lib-exec_node)
target_compile_options(server-lib-exec_node PRIVATE
  -Wdeprecated-this-capture
)
target_link_libraries(server-lib-exec_node PUBLIC
  contrib-libs-linux-headers
  contrib-libs-cxxsupp
  yutil
  yt-yt-ytlib
  server-lib-job_agent
  server-lib-job_proxy
  yt-library-containers
  server-lib-misc
  contrib-libs-protobuf
)
target_proto_messages(server-lib-exec_node PRIVATE
  ${CMAKE_SOURCE_DIR}/yt/yt/server/lib/exec_node/proto/supervisor_service.proto
)
target_sources(server-lib-exec_node PRIVATE
  ${CMAKE_SOURCE_DIR}/yt/yt/server/lib/exec_node/public.cpp
  ${CMAKE_SOURCE_DIR}/yt/yt/server/lib/exec_node/config.cpp
  ${CMAKE_SOURCE_DIR}/yt/yt/server/lib/exec_node/helpers.cpp
  ${CMAKE_SOURCE_DIR}/yt/yt/server/lib/exec_node/job_report.cpp
)
target_proto_addincls(server-lib-exec_node
  ./
  ${CMAKE_SOURCE_DIR}/
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/yt
  ${CMAKE_SOURCE_DIR}/contrib/libs/protobuf/src
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/contrib/libs/protobuf/src
)
target_proto_outs(server-lib-exec_node
  --cpp_out=${CMAKE_BINARY_DIR}/
  --cpp_styleguide_out=${CMAKE_BINARY_DIR}/
)

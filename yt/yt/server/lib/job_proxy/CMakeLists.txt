
# This file was generated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(server-lib-job_proxy)
target_compile_options(server-lib-job_proxy PRIVATE
  -Wdeprecated-this-capture
)
target_link_libraries(server-lib-job_proxy PUBLIC
  contrib-libs-linux-headers
  contrib-libs-cxxsupp
  yutil
  yt-yt-core
  library-dns_over_rpc-client
  server-lib-misc
)
target_sources(server-lib-job_proxy PRIVATE
  ${CMAKE_SOURCE_DIR}/yt/yt/server/lib/job_proxy/config.cpp
)

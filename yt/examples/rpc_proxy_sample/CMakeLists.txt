
# This file was generated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_executable(rpc_proxy_example)
target_link_libraries(rpc_proxy_example PUBLIC
  contrib-libs-linux-headers
  contrib-libs-cxxsupp
  yutil
  library-cpp-cpuid_check
  yt-yt-core
  yt-yt-client
  yt-library-program
  cpp-getopt-small
)
target_link_options(rpc_proxy_example PRIVATE
  -ldl
  -lrt
  -Wl,--no-as-needed
  -fPIC
  -fPIC
  -lpthread
  -lrt
  -ldl
  -lutil
)
target_sources(rpc_proxy_example PRIVATE
  ${CMAKE_SOURCE_DIR}/yt/examples/rpc_proxy_sample/main.cpp
)
target_allocator(rpc_proxy_example
  cpp-ytalloc-impl
)
vcs_info(rpc_proxy_example)

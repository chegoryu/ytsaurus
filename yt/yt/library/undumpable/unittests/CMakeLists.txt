
# This file was generated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_executable(yt-yt-library-undumpable-unittests)
target_compile_options(yt-yt-library-undumpable-unittests PRIVATE
  -Wdeprecated-this-capture
)
target_link_libraries(yt-yt-library-undumpable-unittests PUBLIC
  contrib-libs-linux-headers
  contrib-libs-cxxsupp
  yutil
  library-cpp-cpuid_check
  cpp-testing-gtest
  cpp-testing-gtest_main
  yt-library-undumpable
)
target_link_options(yt-yt-library-undumpable-unittests PRIVATE
  -ldl
  -lrt
  -Wl,--no-as-needed
  -fPIC
  -fPIC
  -lpthread
  -lrt
  -ldl
)
target_sources(yt-yt-library-undumpable-unittests PRIVATE
  ${CMAKE_SOURCE_DIR}/yt/yt/library/undumpable/unittests/undumpable_ut.cpp
)
add_test(
  NAME
  yt-yt-library-undumpable-unittests
  COMMAND
  yt-yt-library-undumpable-unittests
)
set_property(
  TEST
  yt-yt-library-undumpable-unittests
  PROPERTY
  LABELS
  SMALL
)
set_property(
  TEST
  yt-yt-library-undumpable-unittests
  PROPERTY
  PROCESSORS
  1
)
target_allocator(yt-yt-library-undumpable-unittests
  cpp-malloc-tcmalloc
  contrib-libs-tcmalloc
)
vcs_info(yt-yt-library-undumpable-unittests)

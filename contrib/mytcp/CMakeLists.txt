check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/mytcp-examples-test-suite.cc
        )
endif()

build_lib(
    LIBNAME mytcp
    SOURCE_FILES model/mytcp.cc
    HEADER_FILES model/mytcp.h
    LIBRARIES_TO_LINK ${libcore}
    TEST_SOURCES ${examples_as_tests_sources}
)

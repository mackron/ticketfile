cmake_minimum_required(VERSION 3.10)
project(ticketfile
    VERSION 1.0
)

# Compile options
set(COMPILE_OPTIONS)

# Warnings
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND COMPILE_OPTIONS -Wall -Wextra -Wpedantic)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    #list(APPEND COMPILE_OPTIONS /W4)
endif()


# Common interface.
add_library(ticketfile_common INTERFACE)
target_compile_options(ticketfile_common INTERFACE ${COMPILE_OPTIONS})


# ticketfile
add_executable(ticketfile
    source/cli/ticketfile.c
)
target_link_libraries(ticketfile PRIVATE ticketfile_common)
set_target_properties(ticketfile PROPERTIES OUTPUT_NAME "ticket")


# Tests
include(CTest)
if(BUILD_TESTING)
    add_test(NAME ticketfile_test COMMAND ticketfile --test)
    set_tests_properties(ticketfile_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()


# Install
install(TARGETS ticketfile DESTINATION bin)

cmake_minimum_required(VERSION 3.10)

# Extract version from shared version header.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/source/ticketfile_version.h" TICKETFILE_VERSION_SOURCE)
string(REGEX MATCH "#define TICKETFILE_VERSION_MAJOR[ \t]+([0-9]+)" _major_match "${TICKETFILE_VERSION_SOURCE}")
set(TICKETFILE_VERSION_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define TICKETFILE_VERSION_MINOR[ \t]+([0-9]+)" _minor_match "${TICKETFILE_VERSION_SOURCE}")
set(TICKETFILE_VERSION_MINOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define TICKETFILE_VERSION_PATCH[ \t]+([0-9]+)" _patch_match "${TICKETFILE_VERSION_SOURCE}")
set(TICKETFILE_VERSION_PATCH "${CMAKE_MATCH_1}")
set(TICKETFILE_VERSION "${TICKETFILE_VERSION_MAJOR}.${TICKETFILE_VERSION_MINOR}.${TICKETFILE_VERSION_PATCH}")

project(ticketfile VERSION ${TICKETFILE_VERSION})

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
    target_compile_definitions(ticketfile PRIVATE TICKETFILE_ENABLE_TESTS)

    add_test(NAME ticketfile_test COMMAND ticketfile --test)
    set_tests_properties(ticketfile_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()


# Install
install(TARGETS ticketfile DESTINATION bin)

cmake_minimum_required(VERSION 3.22)
project(solution LANGUAGES CXX)

# ---- C++ standard ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if nothing set
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DDEBUG)
endif()

# ---- dbcppp: local submodule if present, else FetchContent ----
include(FetchContent)

# Disable third-party tests/examples/tools
# set(BUILD_TESTING OFF CACHE BOOL "Disable global testing" FORCE)
set(DBCPPP_BUILD_TESTING OFF CACHE BOOL "Disable dbcppp tests" FORCE)
set(DBCPPP_BUILD_TESTS OFF CACHE BOOL "Disable dbcppp tests (alt name)" FORCE)
set(DBCPPP_BUILD_EXAMPLES OFF CACHE BOOL "Disable dbcppp examples" FORCE)
set(DBCPPP_BUILD_TOOLS OFF CACHE BOOL "Disable dbcppp CLI tools" FORCE)

set(DBCPPP_LOCAL_DIR "${CMAKE_SOURCE_DIR}/external/dbcppp")
if(EXISTS "${DBCPPP_LOCAL_DIR}/CMakeLists.txt")
  message(STATUS "Using local dbcppp at ${DBCPPP_LOCAL_DIR}")
  add_subdirectory("${DBCPPP_LOCAL_DIR}" external_dbcppp EXCLUDE_FROM_ALL)
else()
  message(STATUS "Fetching dbcppp via FetchContent")
  FetchContent_Declare(
    dbcppp
    GIT_REPOSITORY https://github.com/xR3b0rn/dbcppp.git
    GIT_TAG 6d3d3a4b2db77a3c0a2c3e9d4f2b4a7c8d40ff0a
  )
  FetchContent_MakeAvailable(dbcppp)
endif()

# ---- Detect dbcppp target (do this BEFORE creating our libs that link it) ----
set(DBCPPP_TARGET "")
if (TARGET dbcppp::dbcppp)
  set(DBCPPP_TARGET dbcppp::dbcppp)
elseif (TARGET dbcppp)
  set(DBCPPP_TARGET dbcppp)
elseif (TARGET dbcppp_shared)
  set(DBCPPP_TARGET dbcppp_shared)
elseif (TARGET dbcppp_static)
  set(DBCPPP_TARGET dbcppp_static)
else()
  message(FATAL_ERROR "dbcppp target not found")
endif()

# ---- Reusable library for decoding (used by main and tests) ----
# (Use CMAKE_CURRENT_SOURCE_DIR because this CMakeLists.txt is in solution/)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/can_decode.cpp")
  message(FATAL_ERROR "Missing source: ${CMAKE_CURRENT_SOURCE_DIR}/src/can_decode.cpp")
endif()

add_library(solution_lib
  ${CMAKE_CURRENT_SOURCE_DIR}/src/can_decode.cpp
)
target_include_directories(solution_lib PUBLIC
  ${CMAKE_SOURCE_DIR}/solution
)
target_link_libraries(solution_lib PUBLIC ${DBCPPP_TARGET})
if (MSVC)
  target_compile_options(solution_lib PRIVATE /W4)
else()
  target_compile_options(solution_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Pick main.cpp from solution/ or repo root ----
if(EXISTS "${CMAKE_SOURCE_DIR}/solution/main.cpp")
  set(SRC "${CMAKE_SOURCE_DIR}/solution/main.cpp")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/main.cpp")
  set(SRC "${CMAKE_SOURCE_DIR}/main.cpp")
else()
  message(FATAL_ERROR "main.cpp not found (looked in solution/ and repo root)")
endif()

# ---- Main executable ----
add_executable(answer ${SRC})
set_target_properties(answer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/solution"
)
target_link_libraries(answer PRIVATE solution_lib)

# ---- Tests (Catch2 v3) ----
include(CTest)
enable_testing()

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.6.0
)
FetchContent_MakeAvailable(Catch2)

add_executable(solution_tests
  ${CMAKE_SOURCE_DIR}/tests/test_decode.cpp
)
target_include_directories(solution_tests PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/solution
)
target_link_libraries(solution_tests PRIVATE
  solution_lib
  Catch2::Catch2WithMain
)
add_test(NAME solution_tests COMMAND solution_tests)

# ---- Stage 4 (no dbcppp) ----
add_executable(answer_stage4
  ${CMAKE_CURRENT_SOURCE_DIR}/main_stage4.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dbc_simple.cpp
)
set_target_properties(answer_stage4 PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/solution"
)
target_include_directories(answer_stage4 PRIVATE
  ${CMAKE_SOURCE_DIR}/solution
)
if (MSVC)
  target_compile_options(answer_stage4 PRIVATE /W4)
else()
  target_compile_options(answer_stage4 PRIVATE -Wall -Wextra -Wpedantic)
endif()


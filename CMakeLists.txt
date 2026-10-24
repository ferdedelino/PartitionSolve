cmake_minimum_required(VERSION 3.16)
project(partitionsolve LANGUAGES CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(partitionsolve
    src/main.cpp
    src/hypergraph_split.cpp
    src/prime_graph_split.cpp
    src/dual_graph_split.cpp
    src/PartitionPropagator.cpp
    src/ClauseLearner.cpp
)
target_compile_options(partitionsolve PRIVATE -Wall -Wextra -UNDEBUG)


include(FetchContent)
include(ExternalProject)


# ---- CaDiCaL ----
FetchContent_Declare(
  cadical
  GIT_REPOSITORY https://github.com/arminbiere/cadical
  GIT_TAG        rel-3.0.1
)
FetchContent_MakeAvailable(cadical)
# CaDiCaL has no CMakeLists.txt, so this only downloads it.
# The folder is available as ${cadical_SOURCE_DIR}.

ExternalProject_Add(cadical_build
    SOURCE_DIR        ${cadical_SOURCE_DIR}
    BUILD_IN_SOURCE   1
    CONFIGURE_COMMAND ./configure
    BUILD_COMMAND     make -j4
    INSTALL_COMMAND   ""
    BUILD_BYPRODUCTS  ${cadical_SOURCE_DIR}/build/libcadical.a)

add_library(cadical_lib STATIC IMPORTED)
set_target_properties(cadical_lib PROPERTIES
    IMPORTED_LOCATION ${cadical_SOURCE_DIR}/build/libcadical.a
    INTERFACE_INCLUDE_DIRECTORIES ${cadical_SOURCE_DIR}/src)
add_dependencies(cadical_lib cadical_build)


# --- metis ---
find_path(METIS_INCLUDE_DIR metis.h)
find_library(METIS_LIBRARY metis)

if(NOT METIS_INCLUDE_DIR OR NOT METIS_LIBRARY)
  message(FATAL_ERROR "Metis not found")
endif()

# ---- Mt-KaHyPar ----
include(FetchContent)

# Options must be set BEFORE FetchContent_MakeAvailable
set(KAHYPAR_DOWNLOAD_TBB   ON CACHE BOOL "" FORCE)
set(KAHYPAR_DOWNLOAD_BOOST ON CACHE BOOL "" FORCE)
set(KAHYPAR_DISABLE_HWLOC  ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  MtKaHyPar EXCLUDE_FROM_ALL
  GIT_REPOSITORY https://github.com/kahypar/mt-kahypar
  GIT_TAG        v1.7
)
FetchContent_MakeAvailable(MtKaHyPar)


# --- Link libraries ---
target_include_directories(partitionsolve PRIVATE ${METIS_INCLUDE_DIR})
target_link_libraries(partitionsolve PRIVATE cadical_lib MtKaHyPar::mtkahypar ${METIS_LIBRARY})
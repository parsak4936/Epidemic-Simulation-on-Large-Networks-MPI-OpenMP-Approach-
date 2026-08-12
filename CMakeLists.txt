cmake_minimum_required(VERSION 3.16)
project(EpidemicHPC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

find_package(MPI REQUIRED)
find_package(OpenMP REQUIRED)

# the plain baseline, one process one thread
add_executable(sequentialSimulation
    src/mainSequential.cpp src/graph.cpp src/networkGenerators.cpp
    src/seirModel.cpp src/measurement.cpp)
target_include_directories(sequentialSimulation PRIVATE src)

# measures the degree distribution of the generated networks
add_executable(degreeStats
    src/degreeStats.cpp
    src/graph.cpp
    src/networkGenerators.cpp)
target_include_directories(degreeStats PRIVATE src)

# the intervention experiment: the same epidemic with task C vaccinating the most central
add_executable(responseSimulation
    src/mainResponse.cpp
    src/graph.cpp
    src/networkGenerators.cpp
    src/seirModel.cpp
    src/analysisTasks.cpp
    src/measurement.cpp)
target_include_directories(responseSimulation PRIVATE src)
target_link_libraries(responseSimulation PRIVATE OpenMP::OpenMP_CXX)

# the same epidemic split across processes and threads
add_executable(parallelSimulation
    src/mainParallel.cpp src/graph.cpp src/networkGenerators.cpp
    src/seirModel.cpp src/measurement.cpp)
target_include_directories(parallelSimulation PRIVATE src)
target_link_libraries(parallelSimulation PRIVATE MPI::MPI_CXX OpenMP::OpenMP_CXX)

# METIS is optional. if it is installed the "metis" partitioner becomes available,
# otherwise the program still builds and uses the block partition.
find_library(METIS_LIBRARY metis)
find_path(METIS_INCLUDE_DIR metis.h)
if(METIS_LIBRARY AND METIS_INCLUDE_DIR)
  message(STATUS "Found METIS: ${METIS_LIBRARY}")
  target_compile_definitions(parallelSimulation PRIVATE USE_METIS)
  target_include_directories(parallelSimulation PRIVATE ${METIS_INCLUDE_DIR})
  target_link_libraries(parallelSimulation PRIVATE ${METIS_LIBRARY})
else()
  message(STATUS "METIS not found, the block partition will be used")
endif()

# the four tasks running at the same time on different processes
add_executable(taskScheduler
    src/mainScheduler.cpp src/graph.cpp src/networkGenerators.cpp
    src/seirModel.cpp src/analysisTasks.cpp)
target_include_directories(taskScheduler PRIVATE src)
target_link_libraries(taskScheduler PRIVATE MPI::MPI_CXX OpenMP::OpenMP_CXX)

# prints the speedup table from results/timings.csv
add_executable(timingReport src/timingReport.cpp)
target_include_directories(timingReport PRIVATE src)

# times each task on its own at different thread counts
add_executable(taskBenchmark
    src/mainTaskBenchmark.cpp src/graph.cpp src/networkGenerators.cpp
    src/seirModel.cpp src/analysisTasks.cpp src/measurement.cpp)
target_include_directories(taskBenchmark PRIVATE src)
target_link_libraries(taskBenchmark PRIVATE OpenMP::OpenMP_CXX)

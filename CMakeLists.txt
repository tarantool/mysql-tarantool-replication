cmake_minimum_required(VERSION 2.8)
project(replicator)

set(CMAKE_VERBOSE_MAKEFILE on)
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY true)

set(COPTS_FLAGS "-pipe -O2 -fomit-frame-pointer -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COPTS_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COPTS_FLAGS}")

set(REPLICATOR_ROOT "${CMAKE_SOURCE_DIR}")
set(REPLICATOR_SRC
	"${REPLICATOR_ROOT}/dbreader.cpp"
	"${REPLICATOR_ROOT}/main.cpp"
	"${REPLICATOR_ROOT}/tpwriter.cpp"
)

set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(TARANTOOL_C_EMBEDDED ON)

add_subdirectory(lib/libslave)
add_subdirectory(lib/yaml-cpp)
add_subdirectory(lib/tarantool-c)

include_directories(
	BEFORE SYSTEM
	"${REPLICATOR_ROOT}/lib/libslave/include"
	"${REPLICATOR_ROOT}/lib/libslave/include/boost_1_59_0/boost"
)
include_directories(
	${REPLICATOR_ROOT}
	"${REPLICATOR_ROOT}/lib/libslave"
	"${REPLICATOR_ROOT}/lib/yaml-cpp/include"
	"${REPLICATOR_ROOT}/lib/tarantool-c/include"
	"${REPLICATOR_ROOT}/lib/tarantool-c/third_party/msgpuck"
)

find_library(LPTHREAD pthread)

add_executable(rp ${REPLICATOR_SRC})
set_target_properties(rp PROPERTIES COMPILE_FLAGS "-std=c++11" LINK_FLAGS "-Wl,-s -Wl,--gc-sections" OUTPUT_NAME replicatord)
target_link_libraries(rp rt dl tnt slave_a yaml-cpp ${LPTHREAD})

install(TARGETS rp RUNTIME DESTINATION sbin)
install(FILES replicatord.yml DESTINATION etc)

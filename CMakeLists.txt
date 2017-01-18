CMAKE_MINIMUM_REQUIRED (VERSION 2.8)
PROJECT (replicator)

set(CMAKE_VERBOSE_MAKEFILE on)
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY true)

set(REPLICATOR_NAME "replicatord")
set(REPLICATOR_ROOT "${CMAKE_SOURCE_DIR}")
set(REPLICATOR_CFLAGS "-DTB_LOCAL=${REPLICATOR_ROOT}/lib/tarantool-c/lib -std=c++0x -g")
set(REPLICATOR_SRC
    ${REPLICATOR_ROOT}/dbreader.cpp
    ${REPLICATOR_ROOT}/main.cpp
    ${REPLICATOR_ROOT}/tpwriter.cpp
)

add_subdirectory(lib/tarantool-c)
add_subdirectory(lib/libslave)
add_subdirectory(lib/yaml-cpp)

include(FindPackageHandleStandardArgs)
find_path(IMYSQL mysql/mysql.h)
find_package_handle_standard_args(Mysql DEFAULT_MSG IMYSQL)

include_directories(
	"${REPLICATOR_ROOT}"
	"${REPLICATOR_ROOT}/lib/libslave"
	"${REPLICATOR_ROOT}/lib/yaml-cpp/include"
	"${REPLICATOR_ROOT}/lib/tarantool-c/include"
	"${REPLICATOR_ROOT}/lib/tarantool-c/third_party/msgpuck"
	"${IMYSQL}/mysql"
)

find_library(LMYSQL_CLIENT_R perconaserverclient_r PATH_SUFFIXES mysql)
find_library(LPTHREAD pthread)
find_library(LBOOST_SYSTEM_MT boost_system)
find_library(LBOOST_SERIALIZATION_MT boost_serialization)

add_executable(rp ${REPLICATOR_SRC})
set_target_properties(rp PROPERTIES COMPILE_FLAGS "${REPLICATOR_CFLAGS}")
set_target_properties(rp PROPERTIES OUTPUT_NAME ${REPLICATOR_NAME})
target_link_libraries(rp tnt slave_a yaml-cpp)
target_link_libraries(rp ${LMYSQL_CLIENT_R} ${LPTHREAD} ${LBOOST_SYSTEM_MT} ${LBOOST_SERIALIZATION_MT} rt dl ssl crypto z)

install(TARGETS rp RUNTIME DESTINATION sbin)
install(FILES replicatord.cfg DESTINATION etc)

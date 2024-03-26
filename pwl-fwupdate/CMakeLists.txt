cmake_minimum_required(VERSION 3.10)

INCLUDE_DIRECTORIES(inc)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/includes)

file(GLOB FB_SRC fastboot/*.cpp)

add_executable(pwl_fwupdate pwl_fwupdate.c fb_programing.c ${FB_SRC} ${PROJECT_SOURCE_DIR}/common/common.c)

target_link_libraries(pwl_fwupdate ${GLIB_LIBRARIES} pthread)

target_include_directories(pwl_fwupdate PRIVATE ${GLIB_INCLUDE_DIRS})

project(pwlLinuxPkg VERSION 1.0)

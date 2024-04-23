cmake_minimum_required(VERSION 3.10)

INCLUDE_DIRECTORIES(inc)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/includes)
INCLUDE_DIRECTORIES(/usr/include/gio-unix-2.0)
INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR}/)

file(GLOB FB_SRC fastboot/*.cpp)

add_compile_options(-Wno-ignored-attributes)

add_executable(pwl_fwupdate pwl_fwupdate.c fb_programing.c ${FB_SRC} ${PROJECT_SOURCE_DIR}/common/common.c ${PROJECT_BINARY_DIR}/CoreGdbusGenerated.c)

target_link_libraries(pwl_fwupdate ${GLIB_LIBRARIES} pthread)

target_include_directories(pwl_fwupdate PRIVATE ${GLIB_INCLUDE_DIRS})

project(pwlLinuxPkg VERSION 1.0)

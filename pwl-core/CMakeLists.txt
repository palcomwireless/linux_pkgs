cmake_minimum_required(VERSION 3.10)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/includes)
INCLUDE_DIRECTORIES(/usr/include/gio-unix-2.0)

INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR}/ /usr/local/include/libmbim-glib /usr/include/libmbim-glib)

link_directories(${PROJECT_BINARY_DIR}/)

add_executable(pwl_core pwl_core.c ${PROJECT_SOURCE_DIR}/common/common.c ${PROJECT_BINARY_DIR}/CoreGdbusGenerated.c)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0)

target_link_libraries(pwl_core ${GLIB_LIBRARIES} libmbim-glib.so)

target_include_directories(pwl_core PRIVATE ${GLIB_INCLUDE_DIRS})

project(pwlLinuxPkg VERSION 1.0 LANGUAGES C)

cmake_minimum_required(VERSION 3.10)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/includes)
INCLUDE_DIRECTORIES(/usr/include/gio-unix-2.0)

INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR}/)

link_directories(${PROJECT_BINARY_DIR}/)

add_executable(pwl_pref pwl_pref.c ${PROJECT_SOURCE_DIR}/common/common.c ${PROJECT_BINARY_DIR}/CoreGdbusGenerated.c)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0)

target_link_libraries(pwl_pref ${GLIB_LIBRARIES} pthread)

target_include_directories(pwl_pref PRIVATE ${GLIB_INCLUDE_DIRS})

project(pwlLinuxPkg VERSION 1.0 LANGUAGES C)

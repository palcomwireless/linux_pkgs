cmake_minimum_required(VERSION 3.10)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/includes)
INCLUDE_DIRECTORIES(/usr/include/gio-unix-2.0)

add_compile_options(-DAT_CHANNEL)

INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR}/ /usr/local/include/libmbim-glib /usr/include/libmbim-glib)

link_directories(${PROJECT_BINARY_DIR}/)

add_executable(pwl_madpt
               pwl_madpt.c
               pwl_mbimadpt.c
               pwl_mbimdeviceadpt.c
               pwl_atchannel.c
               ${PROJECT_SOURCE_DIR}/common/common.c
               ${PROJECT_BINARY_DIR}/CoreGdbusGenerated.c)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0)

target_link_libraries(pwl_madpt ${GLIB_LIBRARIES} libmbim-glib.so)

target_include_directories(pwl_madpt PRIVATE ${GLIB_INCLUDE_DIRS})

project(pwlLinuxPkg VERSION 1.0 LANGUAGES C)

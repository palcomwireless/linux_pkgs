cmake_minimum_required(VERSION 3.10)
project(pwlLinuxPkgs C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3")

set(DEB_EXTRA "${PROJECT_SOURCE_DIR}/deb_extra")

add_subdirectory(gdbus)
add_subdirectory(pwl-core)
add_subdirectory(pwl-madpt)
add_subdirectory(pwl-fwupdate)
add_subdirectory(pwl-pref)

install(CODE "execute_process(COMMAND bash ${DEB_EXTRA}/install)")

set(DEFAULT_INSTALL_PREFIX "")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX ${DEFAULT_INSTALL_PREFIX} CACHE PATH "default install prefix" FORCE)
endif()
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")

install(DIRECTORY "${PROJECT_BINARY_DIR}/release/dpkg/opt/pwl" FILE_PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ DESTINATION "${CMAKE_INSTALL_PREFIX}/opt")
install(DIRECTORY "${PROJECT_BINARY_DIR}/release/dpkg/lib/systemd/system/" DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/systemd/system")
install(DIRECTORY "${PROJECT_BINARY_DIR}/release/dpkg/usr/share/dbus-1/system.d/" DESTINATION "${CMAKE_INSTALL_PREFIX}/usr/share/dbus-1/system.d")
install(DIRECTORY "${PROJECT_BINARY_DIR}/release/dpkg/usr/lib/udev/rules.d/" DESTINATION "${CMAKE_INSTALL_PREFIX}/usr/lib/udev/rules.d")
install(DIRECTORY "${PROJECT_BINARY_DIR}/release/dpkg/etc/systemd/system/ModemManager.service.d/" DESTINATION "${CMAKE_INSTALL_PREFIX}/etc/systemd/system/ModemManager.service.d")
install(FILES "${PROJECT_SOURCE_DIR}/common/mcc_mnc_list.csv" DESTINATION "${CMAKE_INSTALL_PREFIX}/opt/pwl/")

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "pwl-linux-pkgs")
set(CPACK_PACKAGE_VERSION "4.7.10")
set(CPACK_DEBIAN_PACKAGE_SECTION "base")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "PWL")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "PWL Linux service packages")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "standard")
# madpt uses mbim_message_compal_at_command_*, new in libmbim 1.32; on 1.31
# every service fails to load (colleague report 2026-10-02).
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libmbim-glib4 (>= 1.32)")
# Canonical's split packages install the same /opt/pwl tree (the Dell 24.04d
# image puts pwl-pc-services 4.7.5 on at first boot); with Replaces as well,
# dpkg takes them off when this package goes on.
set(PWL_CANONICAL_PKGS "pwl-pc-services, pwl-core-service, pwl-fwupdate-service, pwl-madpt-service, pwl-pref-service, pwl-unlock-service, pwl-test-quick-suspend-resume")
set(CPACK_DEBIAN_PACKAGE_CONFLICTS "${PWL_CANONICAL_PKGS}")
set(CPACK_DEBIAN_PACKAGE_REPLACES "${PWL_CANONICAL_PKGS}")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${DEB_EXTRA}/postinst;${DEB_EXTRA}/postrm;${DEB_EXTRA}/preinst;${DEB_EXTRA}/prerm")
include(CPack)

# Generate uninstall target
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)

add_custom_target(uninstall
  "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")

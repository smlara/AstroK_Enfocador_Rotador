cmake_minimum_required(VERSION 3.18)
# La versión de los drivers INDI va aparte del firmware y de la app de
# Windows, como esas dos entre sí. De aquí salen la de los drivers
# (DRIVER_VERSION), la de indi_astrok.xml y la del paquete .deb.
project(indi-astrok VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# Las propiedades INDI::PropertyNumber del enfocador y del rotador son de INDI 2.
pkg_check_modules(INDI REQUIRED libindi>=2.0)
# BLE: BlueZ por D-Bus, con GIO.
pkg_check_modules(GIO REQUIRED gio-2.0 gio-unix-2.0)
find_library(INDI_DRIVER_LIBRARY indidriver HINTS ${INDI_LIBRARY_DIRS} REQUIRED)

add_library(astrok_comun STATIC
    astrok_canal.cpp
    astrok_ble.cpp
    astrok_equipo.cpp
    astrok_comun.cpp)
target_include_directories(astrok_comun PUBLIC ${INDI_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS})
target_link_libraries(astrok_comun PUBLIC ${INDI_DRIVER_LIBRARY} ${GIO_LINK_LIBRARIES} Threads::Threads)
target_compile_definitions(astrok_comun PUBLIC
    ASTROK_VERSION_MAYOR=${PROJECT_VERSION_MAJOR}
    ASTROK_VERSION_MENOR=${PROJECT_VERSION_MINOR})

add_executable(indi_astrok_focuser astrok_focuser.cpp)
target_link_libraries(indi_astrok_focuser astrok_comun)

add_executable(indi_astrok_rotator astrok_rotator.cpp)
target_link_libraries(indi_astrok_rotator astrok_comun)

configure_file(indi_astrok.xml.in ${CMAKE_CURRENT_BINARY_DIR}/indi_astrok.xml @ONLY)

install(TARGETS indi_astrok_focuser indi_astrok_rotator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Donde INDI busca la lista de drivers (/usr/share/indi), para que los
# clientes los ofrezcan.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/indi_astrok.xml DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/indi)

# ---- Paquete .deb (cpack -G DEB), el que se publica en bin/ ----
# Se compila en la máquina de destino (una Raspberry con PINS, arm64): las
# dependencias las saca dpkg-shlibdeps de las bibliotecas contra las que se ha
# enlazado, así que valen las versiones de esa distribución.
option(ASTROK_DEB_SHLIBDEPS "Compute .deb dependencies with dpkg-shlibdeps" ON)
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME indi-astrok)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "INDI drivers for the AstroK focuser and rotator (USB and Bluetooth LE)")
set(CPACK_PACKAGE_CONTACT "Sergio Martinez")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_PACKAGE_SECTION science)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://astrok.eu")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ${ASTROK_DEB_SHLIBDEPS})
# libindi1 no trae fichero shlibs (medido en PINS, INDI 2.2.3), así que
# dpkg-shlibdeps no la pone sola. Se suma a las que sí saca.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libindi1 (>= 2.0)")
# Sin bluetoothd no hay BLE, pero por USB funciona igual.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS bluez)
# indi-astrok_1.0.0_arm64.deb
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)

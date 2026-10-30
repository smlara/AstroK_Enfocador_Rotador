cmake_minimum_required(VERSION 3.18)
project(indi-astrok CXX)

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

add_executable(indi_astrok_focuser astrok_focuser.cpp)
target_link_libraries(indi_astrok_focuser astrok_comun)

add_executable(indi_astrok_rotator astrok_rotator.cpp)
target_link_libraries(indi_astrok_rotator astrok_comun)

install(TARGETS indi_astrok_focuser indi_astrok_rotator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Donde INDI busca la lista de drivers (/usr/share/indi), para que los
# clientes los ofrezcan.
install(FILES indi_astrok.xml DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/indi)

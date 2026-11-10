cmake_minimum_required(VERSION 3.19)
project(GJGTableQty LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg PrintSupport Network)
set(STEELTALLY_UPDATE_URL "" CACHE STRING "HTTPS manifest endpoint (empty disables updater)")
set(STEELTALLY_UPDATE_PUBLIC_KEY "" CACHE STRING "Base64 Windows BCRYPT_RSAPUBLIC_BLOB; public key only")
if(STEELTALLY_UPDATE_URL AND NOT STEELTALLY_UPDATE_URL MATCHES "^https://")
    message(FATAL_ERROR "Updater URL must be HTTPS")
endif()
# Keep generated C++ safe: URLs/keys may not terminate raw string literals.
if(STEELTALLY_UPDATE_URL MATCHES "[\"\\\\\r\n]" OR STEELTALLY_UPDATE_PUBLIC_KEY MATCHES "[^A-Za-z0-9+/=]")
    message(FATAL_ERROR "Invalid updater build configuration")
endif()
configure_file(src/updateconfig.h.in generated/updateconfig.h @ONLY)

qt_add_executable(gjg_table_qty MANUAL_FINALIZATION
    resources.qrc
    src/main.cpp
    src/updater.cpp
    src/mainwindow.cpp
    src/tableinputpanel.cpp
    src/reporter.cpp
    src/xlsx.cpp
    src/workspaceextras.cpp
    src/sectioncalculator.cpp
)

target_include_directories(gjg_table_qty PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(WIN32)
    target_link_libraries(gjg_table_qty PRIVATE bcrypt)
endif()
target_link_libraries(gjg_table_qty PRIVATE
    Qt6::Network
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::PrintSupport
)

# Qt imports the matching static platform/image plugins when Qt is static.
set_target_properties(gjg_table_qty PROPERTIES WIN32_EXECUTABLE TRUE)
get_target_property(QT_CORE_LIBRARY_TYPE Qt6::Core TYPE)
if(QT_CORE_LIBRARY_TYPE STREQUAL "STATIC_LIBRARY")
    # Desktop runtime and the existing headless regression suite both need a platform.
    qt_import_plugins(gjg_table_qty INCLUDE
        Qt6::QWindowsIntegrationPlugin
        Qt6::QOffscreenIntegrationPlugin
        Qt6::QSvgPlugin
        Qt6::QSvgIconPlugin
    )
endif()
qt_finalize_executable(gjg_table_qty)

# Built-in catalogs, icons and themes are embedded by resources.qrc.

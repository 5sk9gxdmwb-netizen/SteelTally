cmake_minimum_required(VERSION 3.19)
project(GJGTableQty LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg PrintSupport)

qt_add_executable(gjg_table_qty MANUAL_FINALIZATION
    resources.qrc
    src/main.cpp
    src/mainwindow.cpp
    src/tableinputpanel.cpp
    src/reporter.cpp
    src/xlsx.cpp
    src/workspaceextras.cpp
    src/sectioncalculator.cpp
)

target_link_libraries(gjg_table_qty PRIVATE
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

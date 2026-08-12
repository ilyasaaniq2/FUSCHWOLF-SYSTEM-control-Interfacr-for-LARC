cmake_minimum_required(VERSION 3.16)
project(larc_system)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 and QTermWidget6 (Added OpcUa, Network, and Sql)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Mqtt OpcUa Sql)
find_package(qtermwidget6 REQUIRED)

add_executable(larc_system
        main.cpp
        mainwindow.cpp
        mainwindow.h
)

# Link the libraries (Added Qt6::OpcUa, Qt6::Network, and Qt6::Sql)
target_link_libraries(larc_system PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::Mqtt
        Qt6::OpcUa
        Qt6::Sql
        qtermwidget6
)
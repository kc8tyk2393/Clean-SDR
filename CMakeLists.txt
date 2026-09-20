cmake_minimum_required(VERSION 3.21)
project(Brick2SDR VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Multimedia
    Concurrent
)

add_subdirectory(third_party/wdsp)

qt_add_executable(Brick2SDR
    src/main.cpp
    src/core/RadioModel.cpp
    src/protocol/Protocol2.cpp
    src/dsp/DspEngine.cpp
    src/audio/AudioEngine.cpp
    src/cat/CatEngine.cpp
    src/ui/MainWindow.cpp
    src/ui/ConsoleWidgets.cpp
    src/ui/PanafallWidget.cpp
    src/ui/SetupDialog.cpp
    src/ui/Dialogs.cpp
)

target_include_directories(Brick2SDR PRIVATE src)

target_link_libraries(Brick2SDR PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::Concurrent
    wdsp
)

brick2_copy_wdsp_runtime(Brick2SDR)

if(WIN32)
    target_link_libraries(Brick2SDR PRIVATE ws2_32)
    set_target_properties(Brick2SDR PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS Brick2SDR RUNTIME DESTINATION bin)

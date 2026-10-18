cmake_minimum_required(VERSION 3.21)

project(590Ctrl VERSION 0.10 DESCRIPTION "Kenwood TS-590SG control" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

set(QT_COMPONENTS Core Widgets SerialPort)
if(BUILD_TESTING)
    list(APPEND QT_COMPONENTS Test)
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS ${QT_COMPONENTS})
qt_standard_project_setup()

# Radio protocol, transport and state model. No widget dependencies so it can
# be unit tested headless and reused by other front ends.
qt_add_library(ts590core STATIC
    src/core/CatCodec.h src/core/CatCodec.cpp
    src/core/CatTransport.h
    src/core/SerialTransport.h src/core/SerialTransport.cpp
    src/core/SimulatorTransport.h src/core/SimulatorTransport.cpp
    src/core/CatLink.h src/core/CatLink.cpp
    src/core/Radio.h src/core/Radio.cpp
    src/core/EqPresetLibrary.h src/core/EqPresetLibrary.cpp
    src/core/EqAutoLoader.h src/core/EqAutoLoader.cpp
)
target_include_directories(ts590core PUBLIC src/core)
target_link_libraries(ts590core PUBLIC Qt6::Core Qt6::SerialPort)

qt_add_executable(590Ctrl
    src/app/main.cpp
    src/app/MainWindow.h src/app/MainWindow.cpp
    src/app/FrequencyDisplay.h src/app/FrequencyDisplay.cpp
    src/app/EqualizerPanel.h src/app/EqualizerPanel.cpp
    src/app/CatMonitor.h src/app/CatMonitor.cpp
    src/app/WheelSteps.h
    src/app/SMeter.h src/app/SMeter.cpp
    src/app/PowerButton.h src/app/PowerButton.cpp
    src/app/AppStyle.h src/app/AppStyle.cpp
)
target_link_libraries(590Ctrl PRIVATE ts590core Qt6::Widgets)
target_compile_definitions(590Ctrl PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
set_target_properties(590Ctrl PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME 590Ctrl
    MACOSX_BUNDLE_GUI_IDENTIFIER com.radiopathology.590ctrl
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 KC2RGW Chris Esser"
)

if(BUILD_TESTING)
    foreach(test tst_catcodec tst_catlink tst_radio tst_eqpresets)
        qt_add_executable(${test} tests/${test}.cpp)
        target_link_libraries(${test} PRIVATE ts590core Qt6::Test)
        add_test(NAME ${test} COMMAND ${test})
    endforeach()
endif()

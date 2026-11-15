cmake_minimum_required(VERSION 3.21)

project(590Ctrl VERSION 0.16.0 DESCRIPTION "Kenwood TS-590SG control" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

set(QT_COMPONENTS Core Widgets SerialPort Network WebSockets Multimedia)
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
    src/core/MenuTable.h src/core/MenuTable.cpp
    src/core/EqPresetLibrary.h src/core/EqPresetLibrary.cpp
    src/core/EqAutoLoader.h src/core/EqAutoLoader.cpp
    src/core/Qrz.h src/core/Qrz.cpp
    src/core/TciServer.h src/core/TciServer.cpp
    src/core/Resampler.h src/core/Resampler.cpp
    src/core/RadioAudio.h src/core/RadioAudio.cpp
    src/core/UpdateChecker.h src/core/UpdateChecker.cpp
    src/core/CwWordFormer.h src/core/CwWordFormer.cpp
    src/core/DeepCwDecoder.h src/core/DeepCwDecoder.cpp
)
# The words the CW word former may split run-together copy into.
qt_add_resources(ts590core "cwwords" PREFIX "/" BASE resources FILES resources/cw-words.txt)

# The DeepCW decoder runs its model in ONNX Runtime, which is loaded at run
# time: only the headers are needed to build. scripts/fetch-onnxruntime.sh
# puts it in third_party/onnxruntime. Without it there's no DeepCW.
set(ONNXRUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime" CACHE PATH
    "ONNX Runtime (include/ and lib/), for the DeepCW decoder")
if(EXISTS "${ONNXRUNTIME_DIR}/include/onnxruntime_cxx_api.h")
    set(DEEPCW ON)
    target_compile_definitions(ts590core PRIVATE HAVE_ONNXRUNTIME)
    target_include_directories(ts590core SYSTEM PRIVATE "${ONNXRUNTIME_DIR}/include")
    # The library itself, not the symlinks to it.
    file(GLOB ONNXRUNTIME_LIBRARY
        "${ONNXRUNTIME_DIR}/lib/libonnxruntime.*.*.*.dylib"
        "${ONNXRUNTIME_DIR}/lib/libonnxruntime.so.*.*.*"
        "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll")
    set(DEEPCW_MODEL_FILES
        resources/deepcw/model.onnx resources/deepcw/model.onnx.json
        resources/deepcw/LICENSE resources/deepcw/README.md)
    set(ONNXRUNTIME_NOTICES "${ONNXRUNTIME_DIR}/LICENSE" "${ONNXRUNTIME_DIR}/ThirdPartyNotices.txt")
    message(STATUS "DeepCW: ONNX Runtime from ${ONNXRUNTIME_DIR}")
else()
    set(DEEPCW OFF)
    message(STATUS "DeepCW: off (no ONNX Runtime in ${ONNXRUNTIME_DIR}; see scripts/fetch-onnxruntime.sh)")
endif()
target_include_directories(ts590core PUBLIC src/core)
target_link_libraries(ts590core PUBLIC Qt6::Core Qt6::SerialPort Qt6::Network Qt6::WebSockets Qt6::Multimedia)

set(APP_SOURCES
    src/app/MainWindow.h src/app/MainWindow.cpp
    src/app/FrequencyDisplay.h src/app/FrequencyDisplay.cpp
    src/app/EqualizerPanel.h src/app/EqualizerPanel.cpp
    src/app/MenuPanel.h src/app/MenuPanel.cpp
    src/app/CatMonitor.h src/app/CatMonitor.cpp
    src/app/WheelSteps.h
    src/app/SMeter.h src/app/SMeter.cpp
    src/app/MiniWindow.h src/app/MiniWindow.cpp
    src/app/PowerButton.h src/app/PowerButton.cpp
    src/app/AppStyle.h src/app/AppStyle.cpp
)
qt_add_executable(590Ctrl
    src/app/main.cpp
    ${APP_SOURCES}
    resources/590Ctrl.icns
)
# Dock/Finder icon on macOS; the PNG is the window icon elsewhere.
set_source_files_properties(resources/590Ctrl.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
qt_add_resources(590Ctrl "icon" PREFIX "/" FILES resources/590Ctrl.png)
# The .exe's own icon on Windows. (resources/590Ctrl.* are made by scripts/make-icon.py.)
if(WIN32)
    target_sources(590Ctrl PRIVATE resources/590Ctrl.rc)
endif()
target_link_libraries(590Ctrl PRIVATE ts590core Qt6::Widgets Qt6::Multimedia)
target_compile_definitions(590Ctrl PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
# DeepCW's model and ONNX Runtime go inside the Mac bundle.
if(DEEPCW AND APPLE)
    target_sources(590Ctrl PRIVATE ${DEEPCW_MODEL_FILES} ${ONNXRUNTIME_LIBRARY} ${ONNXRUNTIME_NOTICES})
    set_source_files_properties(${DEEPCW_MODEL_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/DeepCw)
    set_source_files_properties(${ONNXRUNTIME_LIBRARY} PROPERTIES MACOSX_PACKAGE_LOCATION Frameworks)
    set_source_files_properties(${ONNXRUNTIME_NOTICES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/onnxruntime)
endif()
set_target_properties(590Ctrl PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME 590Ctrl
    MACOSX_BUNDLE_ICON_FILE 590Ctrl.icns
    # Qt's template plus the microphone prompt text the TCI audio needs.
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER com.radiopathology.590ctrl
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 KC2RGW Chris Esser"
)

# For the self-contained Linux packages and the Windows zip: `cmake --install`
# copies the Qt libraries and plugins in beside the program.
install(TARGETS 590Ctrl BUNDLE DESTINATION . RUNTIME DESTINATION bin)
qt_generate_deploy_app_script(TARGET 590Ctrl OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})

if(BUILD_TESTING)
    set(TESTS tst_catcodec tst_catlink tst_radio tst_eqpresets tst_qrz tst_tciserver tst_resampler tst_updatechecker
        tst_cwwordformer)
    if(DEEPCW)
        list(APPEND TESTS tst_deepcw)
    endif()
    foreach(test ${TESTS})
        qt_add_executable(${test} tests/${test}.cpp)
        target_link_libraries(${test} PRIVATE ts590core Qt6::Test)
        add_test(NAME ${test} COMMAND ${test})
    endforeach()
    if(DEEPCW)
        # The model and the library where the decoder looks for them.
        set(DEEPCW_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/deepcw")
        file(MAKE_DIRECTORY "${DEEPCW_TEST_DIR}")
        foreach(file ${ONNXRUNTIME_LIBRARY} resources/deepcw/model.onnx resources/deepcw/model.onnx.json)
            get_filename_component(path "${file}" ABSOLUTE)
            get_filename_component(name "${file}" NAME)
            file(CREATE_LINK "${path}" "${DEEPCW_TEST_DIR}/${name}" SYMBOLIC)
        endforeach()
        set_tests_properties(tst_deepcw PROPERTIES ENVIRONMENT "CTRL590_DEEPCW_DIR=${DEEPCW_TEST_DIR}")
    endif()

    # Renders the main window to PNG files without a radio or a screen, on
    # the simulator and with throwaway settings. See tools/ui-shots/README.md.
    qt_add_executable(ui-shots tools/ui-shots/main.cpp ${APP_SOURCES})
    target_include_directories(ui-shots PRIVATE src/app)
    target_link_libraries(ui-shots PRIVATE ts590core Qt6::Widgets Qt6::Multimedia)
    target_compile_definitions(ui-shots PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
endif()

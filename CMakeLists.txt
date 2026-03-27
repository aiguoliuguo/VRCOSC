cmake_minimum_required(VERSION 3.16)

project(VRCOSC VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        homepage.cpp
        homepage.h
        lighthouse_types.h
        lighthouse_service.cpp
        lighthouse_service.h
        lighthousepage.cpp
        lighthousepage.h
        oscsettingspage.cpp
        oscsettingspage.h
        settingspage.cpp
        settingspage.h
        steamvr_monitor_service.cpp
        steamvr_monitor_service.h
        openhw_ipc.h
        app.rc
)

set(OPENHW_HELPER_SOURCES
        openhw_helper.cpp
        openhw_ipc.h
        openhw_helper.rc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(VRCOSC
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET VRCOSC APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(VRCOSC SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(VRCOSC
            ${PROJECT_SOURCES}
        )
    endif()
endif()

# ElaWidgetTools 配置
add_executable(VRCOSC_OpenHardwareMonitorHelper WIN32
    ${OPENHW_HELPER_SOURCES}
)
set_target_properties(VRCOSC_OpenHardwareMonitorHelper PROPERTIES
    OUTPUT_NAME "VRCOSC.OpenHardwareMonitorHelper"
)
target_link_libraries(VRCOSC_OpenHardwareMonitorHelper PRIVATE shell32)

set(ELA_ROOT "D:/QTProject/ElaWidgetTools/ElaWidgetTools")
set(ELA_INCLUDE_DIR "${ELA_ROOT}/include")
set(ELA_LIB "${ELA_ROOT}/lib/ElaWidgetTools.lib")
set(ELA_DLL "${ELA_ROOT}/bin/ElaWidgetTools.dll")
set(OPENHW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/extern/OpenHardwareMonitor")
set(OPENHW_PREBUILT_DIR "${OPENHW_ROOT}/prebuilt")
set(OPENHW_PREBUILT_LIB_X64 "${OPENHW_PREBUILT_DIR}/OpenHardwareMonitorLib_x64.dll")
set(OPENHW_PREBUILT_LIB_ANYCPU "${OPENHW_PREBUILT_DIR}/OpenHardwareMonitorLib.dll")
set(OPENHW_LIB_PROJECT "${OPENHW_ROOT}/OpenHardwareMonitorLib/OpenHardwareMonitorLib.csproj")
set(OPENHW_LIB_OUTPUT_DIR "${OPENHW_ROOT}/OpenHardwareMonitorLib/bin/net472")
set(OPENHW_LIB_OUTPUT_X64 "${OPENHW_LIB_OUTPUT_DIR}/OpenHardwareMonitorLib_x64.dll")
set(OPENHW_BRIDGE_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/openhw_bridge/OpenHardwareMonitorBridge.vcxproj")
set(OPENHW_BRIDGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/openhw_bridge/")
set(OPENHW_MSBUILD "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/amd64/MSBuild.exe")
file(GLOB WINSDK_CPPWINRT_DIRS "C:/Program Files (x86)/Windows Kits/10/Include/*/cppwinrt")
list(SORT WINSDK_CPPWINRT_DIRS COMPARE NATURAL ORDER DESCENDING)
list(LENGTH WINSDK_CPPWINRT_DIRS WINSDK_CPPWINRT_DIR_COUNT)
if(WINSDK_CPPWINRT_DIR_COUNT GREATER 0)
    list(GET WINSDK_CPPWINRT_DIRS 0 WINSDK_CPPWINRT_DIR)
endif()

target_include_directories(VRCOSC PRIVATE ${ELA_INCLUDE_DIR})
if(DEFINED WINSDK_CPPWINRT_DIR)
    target_include_directories(VRCOSC PRIVATE "${WINSDK_CPPWINRT_DIR}")
else()
    message(WARNING "C++/WinRT headers not found. Lighthouse BLE integration may fail to build.")
endif()
target_link_libraries(VRCOSC PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network ${ELA_LIB} shell32 runtimeobject iphlpapi)

# C++/WinRT 模板头文件会产生大量符号，必须启用 /bigobj 避免编译器符号表溢出
# 同时添加预编译头（PCH）以大幅减少 C++/WinRT 头文件的重复编译开销，
# 防止 MSVC 因内存耗尽而崩溃（进而导致 Qt Creator 崩溃）
if(MSVC)
    target_compile_options(VRCOSC PRIVATE /bigobj)
    target_compile_options(VRCOSC_OpenHardwareMonitorHelper PRIVATE /bigobj)
endif()

target_precompile_headers(VRCOSC PRIVATE
    # Qt 核心头文件
    <QObject>
    <QString>
    <QVector>
    <QSet>
    <QWidget>
    <QTimer>
    <QThread>
    # Windows/WinRT 头文件（编译代价最高，优先预编译）
    <windows.h>
    <winrt/base.h>
    <winrt/Windows.Devices.Bluetooth.h>
    <winrt/Windows.Devices.Enumeration.h>
    <winrt/Windows.Foundation.Collections.h>
)

if(EXISTS "${OPENHW_PREBUILT_LIB_X64}")
    set(OPENHW_PREBUILT_REFERENCE_LIB "${OPENHW_PREBUILT_LIB_X64}")
    set(OPENHW_REFERENCE_DIR "${OPENHW_PREBUILT_DIR}")
elseif(EXISTS "${OPENHW_PREBUILT_LIB_ANYCPU}")
    set(OPENHW_PREBUILT_REFERENCE_LIB "${OPENHW_PREBUILT_LIB_ANYCPU}")
    set(OPENHW_REFERENCE_DIR "${OPENHW_PREBUILT_DIR}")
elseif(EXISTS "${OPENHW_LIB_OUTPUT_X64}")
    set(OPENHW_PREBUILT_REFERENCE_LIB "${OPENHW_LIB_OUTPUT_X64}")
    set(OPENHW_REFERENCE_DIR "${OPENHW_LIB_OUTPUT_DIR}")
endif()

if(DEFINED OPENHW_PREBUILT_REFERENCE_LIB AND EXISTS "${OPENHW_BRIDGE_PROJECT}" AND EXISTS "${OPENHW_MSBUILD}")
    set(OPENHW_RUNTIME_SOURCE_DIR "${OPENHW_REFERENCE_DIR}")
    if(EXISTS "${OPENHW_LIB_OUTPUT_DIR}/SergiyE.Common.dll")
        set(OPENHW_RUNTIME_SOURCE_DIR "${OPENHW_LIB_OUTPUT_DIR}")
    elseif(EXISTS "${OPENHW_REFERENCE_DIR}/SergiyE.Common.dll")
        set(OPENHW_RUNTIME_SOURCE_DIR "${OPENHW_REFERENCE_DIR}")
    endif()
    add_custom_target(OpenHardwareMonitorBridgeBuild ALL
        COMMAND "${OPENHW_MSBUILD}" "${OPENHW_BRIDGE_PROJECT}"
            /t:Build
            /p:Configuration=Release
            /p:Platform=x64
            /p:OHM_LIB_DIR="${OPENHW_REFERENCE_DIR}"
            /p:OutDir="${OPENHW_BRIDGE_OUTPUT_DIR}"
        BYPRODUCTS "${OPENHW_BRIDGE_OUTPUT_DIR}/OpenHardwareMonitorBridge.dll"
        USES_TERMINAL
        COMMENT "Build OpenHardwareMonitor bridge"
    )

    add_dependencies(VRCOSC OpenHardwareMonitorBridgeBuild)
    add_dependencies(VRCOSC_OpenHardwareMonitorHelper OpenHardwareMonitorBridgeBuild)
elseif(EXISTS "${OPENHW_BRIDGE_PROJECT}")
    message(WARNING "OpenHardwareMonitor prebuilt DLL not found. Skip bridge build until extern/OpenHardwareMonitor/prebuilt contains OpenHardwareMonitorLib_x64.dll or OpenHardwareMonitorLib.dll.")
endif()

add_dependencies(VRCOSC VRCOSC_OpenHardwareMonitorHelper)

# 构建后自动将 ElaWidgetTools.dll 复制到输出目录
add_custom_command(TARGET VRCOSC POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ELA_DLL}"
        "$<TARGET_FILE_DIR:VRCOSC>"
    COMMENT "复制 ElaWidgetTools.dll 到输出目录"
)

add_custom_command(TARGET VRCOSC POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:VRCOSC_OpenHardwareMonitorHelper>"
        "$<TARGET_FILE_DIR:VRCOSC>/VRCOSC.OpenHardwareMonitorHelper.exe"
    COMMENT "Copy OpenHardwareMonitor helper"
)

if(DEFINED OPENHW_RUNTIME_SOURCE_DIR)
    add_custom_command(TARGET VRCOSC POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENHW_BRIDGE_OUTPUT_DIR}/OpenHardwareMonitorBridge.dll"
            "$<TARGET_FILE_DIR:VRCOSC>/OpenHardwareMonitorBridge.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OPENHW_RUNTIME_SOURCE_DIR}"
            "$<TARGET_FILE_DIR:VRCOSC>"
        COMMENT "Copy OpenHardwareMonitor bridge runtime"
    )

    add_custom_command(TARGET VRCOSC_OpenHardwareMonitorHelper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENHW_BRIDGE_OUTPUT_DIR}/OpenHardwareMonitorBridge.dll"
            "$<TARGET_FILE_DIR:VRCOSC_OpenHardwareMonitorHelper>/OpenHardwareMonitorBridge.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OPENHW_RUNTIME_SOURCE_DIR}"
            "$<TARGET_FILE_DIR:VRCOSC_OpenHardwareMonitorHelper>"
        COMMENT "Copy OpenHardwareMonitor helper runtime"
    )

    add_custom_target(OpenHardwareMonitorRuntimeCopy ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENHW_BRIDGE_OUTPUT_DIR}/OpenHardwareMonitorBridge.dll"
            "$<TARGET_FILE_DIR:VRCOSC>/OpenHardwareMonitorBridge.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OPENHW_RUNTIME_SOURCE_DIR}"
            "$<TARGET_FILE_DIR:VRCOSC>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENHW_BRIDGE_OUTPUT_DIR}/OpenHardwareMonitorBridge.dll"
            "$<TARGET_FILE_DIR:VRCOSC_OpenHardwareMonitorHelper>/OpenHardwareMonitorBridge.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OPENHW_RUNTIME_SOURCE_DIR}"
            "$<TARGET_FILE_DIR:VRCOSC_OpenHardwareMonitorHelper>"
        COMMENT "Sync OpenHardwareMonitor runtime"
    )
    add_dependencies(OpenHardwareMonitorRuntimeCopy OpenHardwareMonitorBridgeBuild VRCOSC VRCOSC_OpenHardwareMonitorHelper)
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.VRCOSC)
endif()
set_target_properties(VRCOSC PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS VRCOSC VRCOSC_OpenHardwareMonitorHelper
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(VRCOSC)
endif()

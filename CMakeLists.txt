cmake_minimum_required(VERSION 3.16)

project(VRCOSC VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        homepage.cpp
        homepage.h
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
set(ELA_ROOT "D:/QTProject/ElaWidgetTools/ElaWidgetTools")
set(ELA_INCLUDE_DIR "${ELA_ROOT}/include")
set(ELA_LIB "${ELA_ROOT}/lib/ElaWidgetTools.lib")
set(ELA_DLL "${ELA_ROOT}/bin/ElaWidgetTools.dll")

target_include_directories(VRCOSC PRIVATE ${ELA_INCLUDE_DIR})
target_link_libraries(VRCOSC PRIVATE Qt${QT_VERSION_MAJOR}::Widgets ${ELA_LIB} pdh dxgi gdi32 wbemuuid sensorsapi)

# 构建后自动将 ElaWidgetTools.dll 复制到输出目录
add_custom_command(TARGET VRCOSC POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ELA_DLL}"
        "$<TARGET_FILE_DIR:VRCOSC>"
    COMMENT "复制 ElaWidgetTools.dll 到输出目录"
)

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
install(TARGETS VRCOSC
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(VRCOSC)
endif()

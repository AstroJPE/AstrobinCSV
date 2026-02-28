cmake_minimum_required(VERSION 3.22)
project(AstrobinCSV VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_OSX_ARCHITECTURES arm64;x86_64)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/xisfheaderreader.cpp
    src/acquisitiontableview.cpp
    src/logparser/pixinsightlogparser.cpp
    src/logparser/sirillogparser.cpp
    src/models/csvtablemodel.cpp
    src/dialogs/managelocations.cpp
    src/dialogs/managefilters.cpp
    src/dialogs/managetargets.cpp
    src/dialogs/aboutdialog.cpp
    src/dialogs/copycsv.cpp
    src/filterwebscraper.cpp
    src/settings/appsettings.cpp
    src/xisfresolveworker.cpp
    src/logparser/calibrationlogparser.cpp
    src/xisfmasterframereader.cpp
    src/debuglogger.cpp
    src/dialogs/debugresultdialog.cpp
)

set(HEADERS
    src/mainwindow.h
    src/xisfheaderreader.h
    src/acquisitiontableview.h
    src/logparser/logparserbase.h
    src/logparser/pixinsightlogparser.h
    src/logparser/sirillogparser.h
    src/models/acquisitiongroup.h
    src/models/acquisitionrow.h
    src/models/csvtablemodel.h
    src/models/targetgroup.h
    src/dialogs/managelocations.h
    src/dialogs/managefilters.h
    src/dialogs/managetargets.h
    src/dialogs/aboutdialog.h
    src/dialogs/copycsv.h
    src/filterwebscraper.h
    src/settings/appsettings.h
    src/xisfresolveworker.h
    src/logparser/calibrationlogparser.h
    src/xisfmasterframereader.h
    src/debuglogger.h
    src/dialogs/debugresultdialog.h
)

if(APPLE)
    set(APP_ICON resources/icons/AstrobinCSV.icns)
    set_source_files_properties(${APP_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
elseif(WIN32)
    # The .rc file embeds the .ico into the Windows executable.
    # Create resources/icons/AstrobinCSV.ico and resources/icons/AstrobinCSV.rc
    # with the content:  IDI_ICON1 ICON "AstrobinCSV.ico"
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/AstrobinCSV.rc")
        set(APP_ICON resources/icons/AstrobinCSV.rc)
    else()
        set(APP_ICON "")
    endif()
else()
    set(APP_ICON "")
endif()

add_executable(AstrobinCSV
    ${SOURCES}
    ${HEADERS}
    resources/app.qrc
    ${APP_ICON}
)

target_include_directories(AstrobinCSV PRIVATE src)

# Expose the CMake project version to the C++ source as a string macro.
# Update the version in the project() call above and it propagates
# automatically to QApplication::applicationVersion() and the About dialog.
target_compile_definitions(AstrobinCSV PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(AstrobinCSV PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

if(APPLE)
    set_target_properties(AstrobinCSV PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "AstrobinCSV"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.astrobincsv.AstrobinCSV"
        MACOSX_BUNDLE_ICON_FILE "AstrobinCSV.icns"
    )
endif()

if(WIN32)
    set_target_properties(AstrobinCSV PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

cmake_minimum_required(VERSION 3.16)

project(QtEdit VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)

qt_add_executable(qtedit
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/editor.cpp
    src/editor.h
    src/syntaxhighlighter.cpp
    src/syntaxhighlighter.h
)

qt_add_resources(qtedit "app_resources"
    PREFIX "/"
    FILES
        resources/appicon.svg
)

target_link_libraries(qtedit PRIVATE Qt6::Widgets)

include(GNUInstallDirs)
install(TARGETS qtedit
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)
install(FILES qtedit.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES resources/appicon.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME qtedit.svg
)
install(DIRECTORY resources/icons/hicolor
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons
)
install(FILES packaging/io.github.gtedit.QtEdit.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)
install(FILES README.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

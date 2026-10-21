cmake_minimum_required(VERSION 3.16)

project(rockIDclient VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_definitions(NOMINMAX)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Network Multimedia)

qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(rockIDclient
    main.cpp
    src/rockclient.h
    src/rockclient.cpp
    src/secretbox.h
    src/secretbox.cpp
    src/rockfeatures.cpp
    src/randombytes.cpp
    src/x25519check.cpp
    third_party/tweetnacl.c
    third_party/miniz.c
    third_party/miniz_tdef.c
    third_party/miniz_tinfl.c
)

set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(rockIDclient
    URI rockid
    VERSION 1.0
    QML_FILES
        Theme.qml
        Avatar.qml
        Main.qml
        LoginPage.qml
        MessengerPage.qml
        NewChatPopup.qml
        InfoPanel.qml
        ToolsSheet.qml
)

target_include_directories(rockIDclient PRIVATE src third_party)
target_compile_definitions(rockIDclient PRIVATE MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO MINIZ_NO_TIME)

target_link_libraries(rockIDclient
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Network
        Qt6::Multimedia
        bcrypt
)

set_target_properties(rockIDclient PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME rockIDclient
)

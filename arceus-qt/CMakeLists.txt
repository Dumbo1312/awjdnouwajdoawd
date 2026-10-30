cmake_minimum_required(VERSION 3.19)
project(ArceusAI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(POLICY CMP0071)
    cmake_policy(SET CMP0071 NEW)
endif()

find_package(Qt6 6.4 COMPONENTS Core Gui Widgets Network REQUIRED)

set(ARCEUS_SOURCES
    src/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/app/Theme.cpp
    src/app/Theme.h
    src/app/ChatView.cpp
    src/app/ChatView.h
    src/app/MessageWidget.cpp
    src/app/MessageWidget.h
    src/app/SettingsDialog.cpp
    src/app/SettingsDialog.h
    src/app/PresetEditor.cpp
    src/app/PresetEditor.h
    src/core/Settings.cpp
    src/core/Settings.h
    src/core/Chat.cpp
    src/core/Chat.h
    src/core/Preset.cpp
    src/core/Preset.h
    src/core/Provider.cpp
    src/core/Provider.h
    src/core/Tools.cpp
    src/core/Tools.h
    src/core/Defaults.cpp
    src/core/Defaults.h
    src/net/ApiClient.cpp
    src/net/ApiClient.h
    src/net/OpenAICompatClient.cpp
    src/net/OpenAICompatClient.h
    src/net/AnthropicClient.cpp
    src/net/AnthropicClient.h
    src/net/GeminiClient.cpp
    src/net/GeminiClient.h
    src/net/HuggingFaceClient.cpp
    src/net/HuggingFaceClient.h
    src/net/ApiRouter.cpp
    src/net/ApiRouter.h
    resources/arceus.qrc
)

if(WIN32)
    list(APPEND ARCEUS_SOURCES resources/arceus.rc)
endif()

qt_add_executable(ArceusAI WIN32 ${ARCEUS_SOURCES})

target_include_directories(ArceusAI PRIVATE src)

target_link_libraries(ArceusAI PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

set_target_properties(ArceusAI PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME "ArceusAI"
)

if(WIN32)
    install(TARGETS ArceusAI RUNTIME DESTINATION .)
else()
    install(TARGETS ArceusAI RUNTIME DESTINATION bin)
endif()

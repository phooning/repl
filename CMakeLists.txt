cmake_minimum_required(VERSION 3.24)

project(repl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets WebChannel WebEngineWidgets)

qt_add_executable(repl
    src/main.cpp
    src/app/ConsolePane.cpp
    src/app/ConsolePane.h
    src/app/EditorPane.cpp
    src/app/EditorPane.h
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/editor/DiagnosticModel.cpp
    src/editor/DiagnosticModel.h
    src/runtime/TsRunner.cpp
    src/runtime/TsRunner.h
)

target_include_directories(repl PRIVATE src)

target_link_libraries(repl PRIVATE
    Qt6::Widgets
    Qt6::WebChannel
    Qt6::WebEngineWidgets
)

set(REPL_BUILT_MONACO_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/build-assets/monaco/vs")
set(REPL_NODE_MONACO_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/node_modules/monaco-editor/min/vs")
set(REPL_MONACO_SOURCE "")
if(EXISTS "${REPL_BUILT_MONACO_SOURCE}/loader.js")
    set(REPL_MONACO_SOURCE "${REPL_BUILT_MONACO_SOURCE}")
elseif(EXISTS "${REPL_NODE_MONACO_SOURCE}/loader.js")
    set(REPL_MONACO_SOURCE "${REPL_NODE_MONACO_SOURCE}")
endif()

if(EXISTS "${REPL_MONACO_SOURCE}/loader.js")
    add_custom_command(TARGET repl POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:repl>/assets/monaco"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${REPL_MONACO_SOURCE}"
            "$<TARGET_FILE_DIR:repl>/assets/monaco/vs"
        VERBATIM
    )
else()
    message(WARNING "Local Monaco assets were not found. Run `pnpm install` and `pnpm build:editor-assets` before building.")
endif()

set(REPL_EDITOR_ASSETS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/build-assets/editor")
set(REPL_MONACO_VIM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/node_modules/monaco-vim/dist/monaco-vim.umd.js")
if(EXISTS "${REPL_EDITOR_ASSETS_SOURCE}/monaco-vim.js")
    add_custom_command(TARGET repl POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:repl>/assets/editor"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${REPL_EDITOR_ASSETS_SOURCE}"
            "$<TARGET_FILE_DIR:repl>/assets/editor"
        VERBATIM
    )
elseif(EXISTS "${REPL_MONACO_VIM_SOURCE}")
    add_custom_command(TARGET repl POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:repl>/assets/editor"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${REPL_MONACO_VIM_SOURCE}"
            "$<TARGET_FILE_DIR:repl>/assets/editor/monaco-vim.js"
        VERBATIM
    )
else()
    message(WARNING "Local monaco-vim asset was not found. Run `pnpm install` and `pnpm build:editor-assets` before building.")
endif()

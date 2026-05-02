cmake_minimum_required(VERSION 3.10)

project(game_engine_dragoiuc LANGUAGES C CXX)

if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2_PKG REQUIRED sdl2)
endif()

# Controls C++ version
set(CPP_VERSION 17)
set(CMAKE_CXX_STANDARD ${CPP_VERSION})
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_definitions(
        _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
    )
endif()

set(WINDOWS_RELEASE_GAME "finalGame" CACHE STRING
    "Game folder under resources/ to bundle for Windows releases")
set(WINDOWS_RELEASE_GAME_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/${WINDOWS_RELEASE_GAME}")

if(WIN32)
    set(WINDOWS_RUNTIME_DLLS
        "${CMAKE_CURRENT_SOURCE_DIR}/dlls/SDL2.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/dlls/SDL2_image.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/dlls/SDL2_mixer.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/dlls/SDL2_ttf.dll"
    )
endif()

if(WIN32 AND MSVC)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
    include(InstallRequiredSystemLibraries)
endif()


add_library(engine_src
    src/ActorID.cpp
    src/AudioDB.cpp
    src/DB.cpp
    src/Engine.cpp
    src/EngineUtils.cpp
    src/GameEditor.cpp
    src/ImGuiLayer.cpp
    src/ImageDB.cpp
    src/Input.cpp
    src/ResourcePaths.cpp
    src/Renderer.cpp
    src/SceneDB.cpp
    src/TextDB.cpp
)

add_library(imgui STATIC
    libraries/imgui/imgui.cpp
    libraries/imgui/imgui_demo.cpp
    libraries/imgui/imgui_draw.cpp
    libraries/imgui/imgui_tables.cpp
    libraries/imgui/imgui_widgets.cpp
    libraries/imgui/backends/imgui_impl_sdl2.cpp
    libraries/imgui/backends/imgui_impl_sdlrenderer2.cpp
    libraries/imgui/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(imgui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/libraries/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/libraries/imgui/backends
)

if(WIN32)
    target_include_directories(imgui
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/SDL2
    )
else()
    target_include_directories(imgui
        PUBLIC
            ${SDL2_PKG_INCLUDE_DIRS}
    )
    target_compile_options(imgui
        PRIVATE
            ${SDL2_PKG_CFLAGS_OTHER}
    )
endif()

target_link_libraries(imgui
    PUBLIC
        SDL2
)


target_include_directories(engine_src
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/libraries
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/libraries/lua/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libraries/box2d/include
        ${CMAKE_CURRENT_SOURCE_DIR}/SDL2
        ${CMAKE_CURRENT_SOURCE_DIR}/SDL2_image
        ${CMAKE_CURRENT_SOURCE_DIR}/SDL2_mixer
        ${CMAKE_CURRENT_SOURCE_DIR}/SDL2_ttf
)

file(GLOB BOX2D_SOURCES CONFIGURE_DEPENDS "libraries/box2d/src/*.cpp")
add_library(box2d STATIC ${BOX2D_SOURCES})
target_include_directories(box2d
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/libraries/box2d/include
)

file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "libraries/lua/src/*.c")
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/libraries/lua/include
)

target_link_libraries(engine_src
    PUBLIC
        box2d
        imgui
        lua
)

add_executable(game_engine_dragoiuc
    src/main.cpp
)

target_link_libraries(game_engine_dragoiuc
    PRIVATE
        engine_src
)

if(WIN32)
    target_link_directories(game_engine_dragoiuc
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/SDL2/lib
            ${CMAKE_CURRENT_SOURCE_DIR}/SDL2_image/lib
            ${CMAKE_CURRENT_SOURCE_DIR}/SDL2_mixer/lib
            ${CMAKE_CURRENT_SOURCE_DIR}/SDL2_ttf/lib
    )

    target_link_libraries(game_engine_dragoiuc
        PRIVATE
            SDL2main
            SDL2
            SDL2_image
            SDL2_mixer
            SDL2_ttf
    )

    add_custom_command(TARGET game_engine_dragoiuc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:game_engine_dragoiuc>/resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${WINDOWS_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:game_engine_dragoiuc>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${WINDOWS_RELEASE_GAME_SOURCE_DIR}"
            "$<TARGET_FILE_DIR:game_engine_dragoiuc>/resources/${WINDOWS_RELEASE_GAME}"
        COMMENT "Copying Windows runtime DLLs and ${WINDOWS_RELEASE_GAME} resources"
    )
else()
    target_link_libraries(game_engine_dragoiuc
        PRIVATE
            SDL2
            SDL2_image
            SDL2_mixer
            SDL2_ttf
    )
endif()

install(TARGETS game_engine_dragoiuc
    RUNTIME DESTINATION .
)

install(DIRECTORY "${WINDOWS_RELEASE_GAME_SOURCE_DIR}"
    DESTINATION resources
)

if(WIN32)
    install(FILES ${WINDOWS_RUNTIME_DLLS}
        DESTINATION .
    )
endif()

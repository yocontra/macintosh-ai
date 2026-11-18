cmake_minimum_required(VERSION 3.20)

# Project name can be configured from the command line with -DAPP_NAME=YourAppName
if(NOT DEFINED APP_NAME)
    set(APP_NAME "App" CACHE STRING "Name of the application")
endif()

project(${APP_NAME} LANGUAGES C CXX)

# Version string compiled into the About box; set from `git describe` by the
# Makefile and the CI workflow.
set(APP_VERSION_STR "dev" CACHE STRING "Version string compiled into the application")

set(SRC_C
    src/main.c
    src/prefs.c
    src/error.c
    src/ui/menu.c
    src/ui/event.c
    src/ui/splash_window.c
    src/ui/chat_window.c
    src/ui/about_window.c
    src/ui/window_manager.c
    src/ui/utils.c
    src/chatbot/markov.c
    src/chatbot/markov_data.c
    src/chatbot/model_manager.c
    src/chatbot/template.c
    src/chatbot/template_data.c
    src/sound/beepbop.c
    src/sound/tetris.c
)
set(SRC_CPP
    src/chatbot/api_parse.cpp
    src/chatbot/api_client.cpp
)
set(RESOURCE_FILES
    src/main.r
)

set(SRC_FILES ${SRC_C} ${SRC_CPP})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The TLS/HTTP engine (Claude 68) with the Open Transport transport. It is
# configured with -DNET68K_TRANSPORT (ot|mactcp) and -DNET68K_UNIVERSAL_DIR,
# which the Makefile passes.
add_subdirectory(libs/net68k)

# 68k System 7 target: the engine's P-256 assembly needs a 68020, there is no
# FPU to rely on, and -ffunction-sections/-fdata-sections are what make the
# -Wl,-gc-sections on the link line actually discard unreachable code.
add_compile_options(
    -mcpu=68020 -msoft-float -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

add_application(${APP_NAME}
    ${SRC_FILES}
    ${RESOURCE_FILES}
    CREATOR "McAI"
)

target_compile_definitions(${APP_NAME} PRIVATE APP_VERSION_STR="${APP_VERSION_STR}")

target_link_libraries(${APP_NAME} net68k)

set_target_properties(${APP_NAME} PROPERTIES
    LINKER_LANGUAGE CXX
    LINK_FLAGS "-Wl,--allow-multiple-definition -Wl,-gc-sections"
)

if(NOT DEFINED LAUNCH_APPL)
    set(LAUNCH_APPL "LaunchAPPL" CACHE STRING "Path to the LaunchAPPL executable")
endif()

add_custom_target(run
    DEPENDS ${APP_NAME}
    COMMAND ${LAUNCH_APPL} -e minivmac ${CMAKE_CURRENT_BINARY_DIR}/${APP_NAME}.bin
    COMMENT "Launching ${APP_NAME} in Mini vMac"
)

cmake_minimum_required(VERSION 3.10)

# Project name can be configured from command line with -DAPP_NAME=YourAppName
if(NOT DEFINED APP_NAME)
    set(APP_NAME "App" CACHE STRING "Name of the application")
endif()

project(${APP_NAME} LANGUAGES C CXX)

# Set source and resource files
set(SRC_C
    src/main.c
    src/ui/menu.c
    src/ui/event.c
    src/error.c
    src/chatbot/markov.c
    src/chatbot/markov_data.c
    src/chatbot/model_manager.c
    src/chatbot/template.c
    src/chatbot/template_data.c
    src/chatbot/openai.c
    src/sound/beepbop.c
    src/sound/tetris.c
    src/ui/splash_window.c
    src/ui/chat_window.c
    src/ui/about_window.c
    src/ui/window_manager.c
    src/ui/utils.c
)
set(SRC_CPP
)
set(HEADER_FILES
    src/ui/menu.h
    src/ui/event.h
    src/error.h
    src/constants.h
    src/chatbot/markov.h
    src/chatbot/markov_data.h
    src/chatbot/template.h
    src/chatbot/template_data.h
    src/chatbot/openai.h
    src/sound/beepbop.h
    src/sound/tetris.h
    src/ui/splash_window.h
    src/ui/chat_window.h
    src/ui/about_window.h
    src/ui/window_manager.h
    src/ui/utils.h
)
set(RESOURCE_FILES
    src/main.r
)

# Configuration options
option(USE_MINIVMAC "Use Mini vMac for running the application" ON)
option(ENABLE_CLANG_FORMAT "Enable clang-format formatting" ON)
option(DEBUG "Enable debug output with DebugStr calls" OFF)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set path to LaunchAPPL if provided
if(NOT DEFINED LAUNCH_APPL)
    set(LAUNCH_APPL "LaunchAPPL" CACHE STRING "Path to the LaunchAPPL executable")
endif()

# Find clang-format if enabled
if(ENABLE_CLANG_FORMAT)
    find_program(CLANG_FORMAT clang-format)
    if(CLANG_FORMAT)
        message(STATUS "Found clang-format: ${CLANG_FORMAT}")
        
        # Get all source files
        set(FORMAT_SOURCES ${SRC_C} ${SRC_CPP} ${HEADER_FILES})
        
        # Add custom target for formatting
        add_custom_target(format
            COMMENT "Running clang-format to format the code..."
            COMMAND ${CLANG_FORMAT} -i -style=file ${FORMAT_SOURCES}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
    else()
        message(WARNING "clang-format not found. Code formatting will not be available.")
    endif()
endif()

# Custom target to run the application in Mini vMac or Basilisk II
if(USE_MINIVMAC)
    # Make sure the launcher command is properly quoted
    set(LAUNCHER_CMD ${LAUNCH_APPL})
    set(LAUNCHER_ARGS "-e" "minivmac")
    set(LAUNCHER_DESC "Mini vMac")
else()
    set(LAUNCHER_CMD "BasiliskII")
    set(LAUNCHER_ARGS "")
    set(LAUNCHER_DESC "Basilisk II")
endif()

# Combine source files
set(SRC_FILES ${SRC_C} ${SRC_CPP})

# Define build target
if(APPLE)
    # Modern macOS build with Carbon
    add_executable(${APP_NAME} MACOSX_BUNDLE
        ${SRC_FILES}
        ${RESOURCE_FILES}
    )
    target_link_libraries(${APP_NAME} "-framework Carbon")
else()
    # Retro68 build for classic Mac OS
    add_application(${APP_NAME}
        ${SRC_FILES}
        ${RESOURCE_FILES}
    )

    # Add DEBUG definition if enabled
    if(DEBUG)
        target_compile_definitions(${APP_NAME} PRIVATE DEBUG=1)
    endif()

    # Save 200KB+ of code by removing unused stuff
    set_target_properties(${APP_NAME} PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
    
    # Our dependencies
    target_link_libraries(${APP_NAME} 
        MacHTTP
        cJSON
    )
    
    # Add custom targets
    add_custom_target(build
        DEPENDS ${APP_NAME}
        COMMENT "Building application"
    )
    
    add_custom_target(run
        DEPENDS ${APP_NAME}
        COMMAND ${LAUNCHER_CMD} ${LAUNCHER_ARGS} ${CMAKE_CURRENT_BINARY_DIR}/${APP_NAME}.bin
        COMMENT "Running in ${LAUNCHER_DESC}"
    )
endif()
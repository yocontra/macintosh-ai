cmake_minimum_required(VERSION 3.27)

# Project name can be configured from command line with -DAPP_NAME=YourAppName
if(NOT DEFINED APP_NAME)
    set(APP_NAME "App" CACHE STRING "Name of the application")
endif()

project(${APP_NAME} LANGUAGES C CXX)

# Set source and resource files
set(SRC_C
    src/main.c
)
set(SRC_CPP
    # Add C++ files here, for example:
    # src/ai_interface.cpp
)
set(RESOURCE_FILES
    src/main.r
)

# Configuration options
option(USE_MINIVMAC "Use Mini vMac for running the application" ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set path to LaunchAPPL if provided
if(NOT DEFINED LAUNCH_APPL)
    set(LAUNCH_APPL "LaunchAPPL" CACHE STRING "Path to the LaunchAPPL executable")
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

    # Save 200KB+ of code by removing unused stuff
    set_target_properties(${APP_NAME} PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
    
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
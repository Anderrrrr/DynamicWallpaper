cmake_minimum_required(VERSION 3.15)
project(DynamicWallpaper VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    src/main.cpp
    src/VideoPlayer.cpp
)

set(HEADERS
    src/VideoPlayer.h
)

# Create a Windows application (no console)
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

# Link necessary Windows SDK libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    user32
    mf
    mfplat
    mfuuid
    shlwapi
    ole32
    gdi32
    strmiids
    evr
)

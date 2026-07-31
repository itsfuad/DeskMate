cmake_minimum_required(VERSION 3.16)
project(deskmate_preview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DESKMATE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(deskmate-preview
  src/main.cpp
  src/ArduinoCompat.cpp
  src/ClockPreview.cpp
  src/FirmwarePreview.cpp
  src/GfxPreview.cpp
  src/NetPreview.cpp
  src/PreviewFramebuffer.cpp
  src/PreviewScenarios.cpp
  src/RadarPreview.cpp
  vendor/Adafruit_GFX/Adafruit_GFX.cpp
  ${DESKMATE_ROOT}/src/TileRenderer.cpp
  ${DESKMATE_ROOT}/src/features/weather/WeatherMode.cpp
  ${DESKMATE_ROOT}/src/features/github/GithubMode.cpp
  ${DESKMATE_ROOT}/src/features/network/NetworkMode.cpp
  ${DESKMATE_ROOT}/src/features/radar/RadarMode.cpp
)

target_compile_definitions(deskmate-preview PRIVATE
  DESKMATE_PREVIEW
  ARDUINO=10819
)

target_include_directories(deskmate-preview PRIVATE
  include
  vendor/Adafruit_GFX
  ${DESKMATE_ROOT}/src
  ${DESKMATE_ROOT}/src/features/weather
  ${DESKMATE_ROOT}/src/features/github
  ${DESKMATE_ROOT}/src/features/network
  ${DESKMATE_ROOT}/src/features/radar
)

find_package(X11)
if(X11_FOUND)
  target_compile_definitions(deskmate-preview PRIVATE DESKMATE_HAVE_X11)
  target_include_directories(deskmate-preview PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(deskmate-preview PRIVATE ${X11_LIBRARIES})
else()
  message(WARNING "X11 development files were not found. The preview will build in headless-only mode.")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(deskmate-preview PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    -Wno-nonnull-compare
  )
endif()

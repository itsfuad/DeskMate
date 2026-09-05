cmake_minimum_required(VERSION 3.16)
project(deskmate_emulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DESKMATE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(DESKMATE_SHARED_SOURCES
  ${DESKMATE_ROOT}/src/app/main.cpp
  ${DESKMATE_ROOT}/src/core/Clock.cpp
  ${DESKMATE_ROOT}/src/core/Connectivity.cpp
  ${DESKMATE_ROOT}/src/core/HttpRequest.cpp
  ${DESKMATE_ROOT}/src/core/JsonScanner.cpp
  ${DESKMATE_ROOT}/src/core/JsonWriter.cpp
  ${DESKMATE_ROOT}/src/core/PollScheduler.cpp
  ${DESKMATE_ROOT}/src/core/Settings.cpp
  ${DESKMATE_ROOT}/src/display/IconData.cpp
  ${DESKMATE_ROOT}/src/display/Icons.cpp
  ${DESKMATE_ROOT}/src/display/TileRenderer.cpp
  ${DESKMATE_ROOT}/src/features/weather/WeatherMode.cpp
  ${DESKMATE_ROOT}/src/features/github/GithubMode.cpp
  ${DESKMATE_ROOT}/src/features/network/NetworkMode.cpp
  ${DESKMATE_ROOT}/src/features/radar/RadarClient.cpp
  ${DESKMATE_ROOT}/src/features/radar/RadarMode.cpp
  ${DESKMATE_ROOT}/src/features/radar/RadarTrail.cpp
  ${DESKMATE_ROOT}/src/ota/OtaUpdate.cpp
  ${DESKMATE_ROOT}/src/web/WebPortal.cpp
)

add_executable(deskmate-emulator
  src/main.cpp
  src/ArduinoCompat.cpp
  src/EmulatorPlatform.cpp
  src/EmulatorStorage.cpp
  src/EmulatorWebServer.cpp
  src/GfxEmulator.cpp
  src/NetEmulator.cpp
  src/EmulatorDisplay.cpp
  vendor/Adafruit_GFX/Adafruit_GFX.cpp
  ${DESKMATE_SHARED_SOURCES}
)

add_executable(deskmate-radar-trail-test
  src/RadarTrailTest.cpp
  ${DESKMATE_ROOT}/src/features/radar/RadarTrail.cpp
)

add_executable(deskmate-radar-client-test
  tests/RadarClientIntegrationTest.cpp
  src/ArduinoCompat.cpp
  src/EmulatorPlatform.cpp
  ${DESKMATE_ROOT}/src/core/HttpRequest.cpp
  ${DESKMATE_ROOT}/src/core/JsonScanner.cpp
  ${DESKMATE_ROOT}/src/features/radar/RadarClient.cpp
  ${DESKMATE_ROOT}/src/features/radar/RadarTrail.cpp
)

add_executable(deskmate-json-scanner-test
  tests/JsonScannerTest.cpp
  src/ArduinoCompat.cpp
  src/EmulatorPlatform.cpp
  ${DESKMATE_ROOT}/src/core/JsonScanner.cpp
)

add_executable(deskmate-json-writer-test
  tests/JsonWriterTest.cpp
  src/ArduinoCompat.cpp
  src/EmulatorPlatform.cpp
  ${DESKMATE_ROOT}/src/core/JsonScanner.cpp
  ${DESKMATE_ROOT}/src/core/JsonWriter.cpp
)

add_executable(deskmate-settings-test
  tests/SettingsTest.cpp
  src/ArduinoCompat.cpp
  src/EmulatorPlatform.cpp
  src/EmulatorStorage.cpp
  ${DESKMATE_ROOT}/src/core/JsonScanner.cpp
  ${DESKMATE_ROOT}/src/core/JsonWriter.cpp
  ${DESKMATE_ROOT}/src/core/Settings.cpp
)

add_executable(deskmate-connectivity-test
  tests/ConnectivitySchedulerTest.cpp
  src/ArduinoCompat.cpp
  src/EmulatorPlatform.cpp
  ${DESKMATE_ROOT}/src/core/Connectivity.cpp
  ${DESKMATE_ROOT}/src/core/JsonScanner.cpp
  ${DESKMATE_ROOT}/src/core/PollScheduler.cpp
)

foreach(target deskmate-emulator deskmate-radar-trail-test
               deskmate-radar-client-test deskmate-connectivity-test
               deskmate-json-scanner-test deskmate-json-writer-test
               deskmate-settings-test)
  target_compile_definitions(${target} PRIVATE DESKMATE_EMULATOR ARDUINO=10819)
  target_include_directories(${target} PRIVATE
    include
    vendor/Adafruit_GFX
    ${DESKMATE_ROOT}/src
    ${DESKMATE_ROOT}/src/app
    ${DESKMATE_ROOT}/src/boards
    ${DESKMATE_ROOT}/src/core
    ${DESKMATE_ROOT}/src/display
    ${DESKMATE_ROOT}/src/ota
    ${DESKMATE_ROOT}/src/platform
    ${DESKMATE_ROOT}/src/web
    ${DESKMATE_ROOT}/src/features/weather
    ${DESKMATE_ROOT}/src/features/github
    ${DESKMATE_ROOT}/src/features/network
    ${DESKMATE_ROOT}/src/features/radar
  )
endforeach()

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(X11)
target_link_libraries(deskmate-emulator PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_link_libraries(deskmate-radar-client-test PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_link_libraries(deskmate-connectivity-test PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_link_libraries(deskmate-json-scanner-test PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_link_libraries(deskmate-json-writer-test PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_link_libraries(deskmate-settings-test PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

if(X11_FOUND)
  target_compile_definitions(deskmate-emulator PRIVATE DESKMATE_HAVE_X11)
  target_include_directories(deskmate-emulator PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(deskmate-emulator PRIVATE ${X11_LIBRARIES})
else()
  message(WARNING "X11 was not found. The emulator will build headless-only.")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  foreach(target deskmate-emulator deskmate-radar-trail-test
                 deskmate-radar-client-test deskmate-connectivity-test
                 deskmate-json-scanner-test deskmate-json-writer-test
                 deskmate-settings-test)
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-nonnull-compare
      -Wno-array-bounds)
  endforeach()
endif()

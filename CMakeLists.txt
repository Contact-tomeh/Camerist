cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/camera.cpp
    src/camera-dshow.cpp
    src/camera-stub.cpp
    src/store.cpp
    src/obs-bridge.cpp
    src/server.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

# cpp-httplib (bibliothèque externe) déclenche des avertissements sans gravité :
# ils ne doivent pas bloquer la compilation.
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

if(WIN32)
  # DirectShow (pilotage des caméras) et sockets (serveur local)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE strmiids uuid ole32 oleaut32 ws2_32)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

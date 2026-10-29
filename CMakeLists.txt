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
    src/core/timer-engine.hpp
    src/core/timer-engine.cpp
    src/core/timer-data.hpp
    src/core/action-executor.hpp
    src/core/action-executor.cpp
    src/utils/obs-utils.hpp
    src/utils/obs-utils.cpp
    src/utils/format-utils.hpp
    src/utils/format-utils.cpp
    src/widgets/obs-dock-wrapper.hpp
    src/widgets/tminus-dock.hpp
    src/widgets/tminus-dock.cpp
    src/widgets/timer-widget.hpp
    src/widgets/timer-widget.cpp
    src/widgets/timer-settings-dialog.hpp
    src/widgets/timer-settings-dialog.cpp
    src/widgets/action-list-widget.hpp
    src/widgets/action-list-widget.cpp
    src/widgets/drag-reorder.hpp
    src/widgets/drag-reorder.cpp
    src/widgets/button-style.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

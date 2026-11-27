cmake_minimum_required(VERSION 3.21)
project(PokitSuiteNative VERSION 0.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(MSVC)
  add_compile_options(/utf-8)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Bluetooth)

set(DOKIT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dokit")
file(GLOB_RECURSE DOKIT_HEADERS CONFIGURE_DEPENDS
  "${DOKIT_ROOT}/include/*.h"
  "${DOKIT_ROOT}/src/lib/*.h")
add_library(QtPokit SHARED
  ${DOKIT_HEADERS}
  ${DOKIT_ROOT}/src/lib/abstractpokitservice.cpp
  ${DOKIT_ROOT}/src/lib/calibrationservice.cpp
  ${DOKIT_ROOT}/src/lib/dataloggerservice.cpp
  ${DOKIT_ROOT}/src/lib/deviceinfoservice.cpp
  ${DOKIT_ROOT}/src/lib/dsoservice.cpp
  ${DOKIT_ROOT}/src/lib/multimeterservice.cpp
  ${DOKIT_ROOT}/src/lib/pokitdevice.cpp
  ${DOKIT_ROOT}/src/lib/pokitdiscoveryagent.cpp
  ${DOKIT_ROOT}/src/lib/pokitmeter.cpp
  ${DOKIT_ROOT}/src/lib/pokitpro.cpp
  ${DOKIT_ROOT}/src/lib/pokitproducts.cpp
  ${DOKIT_ROOT}/src/lib/statusservice.cpp)
target_include_directories(QtPokit PUBLIC ${DOKIT_ROOT}/include PRIVATE ${DOKIT_ROOT}/src)
target_link_libraries(QtPokit PRIVATE Qt6::Core Qt6::Bluetooth)
target_compile_definitions(QtPokit PRIVATE QTPOKIT_LIBRARY QT_NO_KEYWORDS)

qt_add_executable(PokitSuiteNative
  src/main.cpp
  src/metertools.cpp
  src/metertools.h
  src/scopeexport.cpp
  src/scopeexport.h
  src/pokitbridge.cpp
  src/pokitbridge.h
  src/pokitcontroller.cpp
  src/pokitcontroller.h)

qt_add_qml_module(PokitSuiteNative
  URI PokitSuite
  VERSION 1.0
  QML_FILES qml/Main.qml qml/MeterWorkspace.qml)

target_link_libraries(PokitSuiteNative PRIVATE
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Bluetooth QtPokit)
if(WIN32)
  target_link_libraries(PokitSuiteNative PRIVATE ole32)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(SwitchQueryTest tests/switchquery_test.cpp)
  target_include_directories(SwitchQueryTest PRIVATE src ${DOKIT_ROOT}/src/lib)
  target_link_libraries(SwitchQueryTest PRIVATE Qt6::Core Qt6::Bluetooth QtPokit)
  add_test(NAME switch-query COMMAND SwitchQueryTest)
  add_executable(ScopeExportTest tests/scopeexport_test.cpp src/scopeexport.cpp)
  target_include_directories(ScopeExportTest PRIVATE src)
  target_link_libraries(ScopeExportTest PRIVATE Qt6::Core Qt6::Gui)
  add_test(NAME scope-export COMMAND ScopeExportTest)
  if(NOT WIN32)
    set_tests_properties(scope-export PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
  add_executable(ScopeChannelsTest tests/scopechannels_test.cpp)
  target_include_directories(ScopeChannelsTest PRIVATE src)
  target_link_libraries(ScopeChannelsTest PRIVATE Qt6::Core)
  add_test(NAME scope-channels COMMAND ScopeChannelsTest)
  add_executable(MeterToolsTest tests/metertools_test.cpp src/metertools.cpp src/metertools.h)
  target_include_directories(MeterToolsTest PRIVATE src)
  target_link_libraries(MeterToolsTest PRIVATE Qt6::Core Qt6::Gui)
  add_test(NAME meter-tools COMMAND MeterToolsTest)
  if(NOT WIN32)
    set_tests_properties(meter-tools PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()



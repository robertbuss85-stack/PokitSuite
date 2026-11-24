cmake_minimum_required(VERSION 3.21)
project(PokitSuiteNative VERSION 0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

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
  src/pokitcontroller.cpp
  src/pokitcontroller.h)

qt_add_qml_module(PokitSuiteNative
  URI PokitSuite
  VERSION 1.0
  QML_FILES qml/Main.qml)

target_link_libraries(PokitSuiteNative PRIVATE
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Bluetooth QtPokit)



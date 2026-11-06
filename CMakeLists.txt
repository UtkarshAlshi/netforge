cmake_minimum_required(VERSION 3.20)
project(NetForge VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(NETFORGE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
  if(NETFORGE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()
add_library(netforge_core src/ethernet.cpp src/ipv4.cpp src/tcp.cpp src/scheduler.cpp src/config.cpp src/appliance.cpp)
target_include_directories(netforge_core PUBLIC include)
add_executable(netforge src/main.cpp src/linux_io.cpp)
target_link_libraries(netforge PRIVATE netforge_core netforge_workers)
enable_testing()
add_executable(udp_probe tools/udp_probe.cpp)
add_executable(tcp_probe tools/tcp_probe.cpp)
add_executable(icmp_probe tools/icmp_probe.cpp)
foreach(component ethernet ipv4 tcp scheduler appliance)
  add_executable(${component}_test tests/${component}_test.cpp)
  target_link_libraries(${component}_test PRIVATE netforge_core)
  if(component STREQUAL "appliance")
    add_test(NAME ${component} COMMAND ${component}_test ${CMAKE_CURRENT_SOURCE_DIR}/examples/appliance.conf)
  else()
    add_test(NAME ${component} COMMAND ${component}_test)
  endif()
endforeach()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME cli COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.py
  $<TARGET_FILE:netforge> ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME control COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/control_test.py
  $<TARGET_FILE:netforge> ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME durable COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/durable_test.py
  $<TARGET_FILE:netforge> ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME health COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/health_test.py ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME health_controller COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/health_controller_test.py $<TARGET_FILE:netforge> ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME appliance_indexed COMMAND appliance_test ${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed-appliance.conf)

add_executable(xdp_test tests/xdp_test.cpp)
target_link_libraries(xdp_test PRIVATE netforge_core)
add_test(NAME xdp_model COMMAND xdp_test)
add_executable(nf-xdp-compile backends/xdp/compile.cpp)
target_link_libraries(nf-xdp-compile PRIVATE netforge_core)
add_test(NAME xdp_compile COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/xdp_compile_test.py
  $<TARGET_FILE:nf-xdp-compile> ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
add_library(netforge_workers src/workers.cpp)
target_link_libraries(netforge_workers PUBLIC netforge_core Threads::Threads)
add_executable(workers_test tests/workers_test.cpp)
target_link_libraries(workers_test PRIVATE netforge_workers)
add_test(NAME workers COMMAND workers_test ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME fleet_journal COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/fleet_journal_test.py
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME worker_agent COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/worker_agent_test.py
    $<TARGET_FILE:netforge> ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME audit COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/audit_test.py ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME metrics_view COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/metrics_view_test.py)

# Retain the repository's original M1 parser laboratory alongside the appliance.
add_library(netforge_packet src/common/packet_parser.cpp)
target_include_directories(netforge_packet PUBLIC src)
add_executable(packet_decode tools/packet_decode.cpp)
target_link_libraries(packet_decode PRIVATE netforge_packet)
add_executable(packet_parser_tests tests/unit/packet_parser_tests.cpp)
target_link_libraries(packet_parser_tests PRIVATE netforge_packet)
add_test(NAME legacy_packet_parser COMMAND packet_parser_tests)

add_executable(dpdk_policy_test tests/dpdk_policy_test.cpp)
target_link_libraries(dpdk_policy_test PRIVATE netforge_core)
add_test(NAME dpdk_policy_binding COMMAND dpdk_policy_test ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME capture_supervisor COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture_agent_test.py
    $<TARGET_FILE:nf-xdp-compile> ${CMAKE_CURRENT_SOURCE_DIR})
endif()

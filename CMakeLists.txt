cmake_minimum_required(VERSION 3.20)
project(netforge LANGUAGES CXX)

option(NETFORGE_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(netforge_packet src/common/packet_parser.cpp)
target_include_directories(netforge_packet PUBLIC src)
target_compile_options(netforge_packet PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

if (NETFORGE_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(netforge_packet PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(netforge_packet PUBLIC -fsanitize=address,undefined)
endif()

add_executable(packet_decode tools/packet_decode.cpp)
target_link_libraries(packet_decode PRIVATE netforge_packet)

enable_testing()
add_executable(packet_parser_tests tests/unit/packet_parser_tests.cpp)
target_link_libraries(packet_parser_tests PRIVATE netforge_packet)
add_test(NAME packet_parser_tests COMMAND packet_parser_tests)

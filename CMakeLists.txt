cmake_minimum_required(VERSION 3.20)
project(cmb_single_module_proto LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(common_lib
    common/frame.cpp
    common/crc32.cpp
    common/logger.cpp
    common/metrics.cpp
    common/latency_stats.cpp
)

target_include_directories(common_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(sender
    sender/main.cpp
    sender/data_simulator.cpp
    sender/frame_encoder.cpp
    sender/tcp_sender.cpp
)
target_link_libraries(sender PRIVATE common_lib Threads::Threads)

add_executable(receiver
    receiver/main.cpp
    receiver/frame_parser.cpp
    receiver/loss_detector.cpp
    receiver/tcp_receiver.cpp
    receiver/storage_writer.cpp
)
target_link_libraries(receiver PRIVATE common_lib)

if(WIN32)
    target_link_libraries(sender PRIVATE ws2_32)
    target_link_libraries(receiver PRIVATE ws2_32)
endif()

add_executable(frame_tests
    tests/frame_tests.cpp
)
target_link_libraries(frame_tests PRIVATE common_lib)

add_executable(stats_tests
    tests/stats_tests.cpp
    receiver/loss_detector.cpp
)
target_link_libraries(stats_tests PRIVATE common_lib)

add_executable(parser_tests
    tests/parser_tests.cpp
    receiver/frame_parser.cpp
)
target_link_libraries(parser_tests PRIVATE common_lib)

add_executable(local_smoke_tests
    tests/local_smoke_tests.cpp
    sender/data_simulator.cpp
    sender/frame_encoder.cpp
    sender/tcp_sender.cpp
    receiver/frame_parser.cpp
    receiver/tcp_receiver.cpp
)
target_link_libraries(local_smoke_tests PRIVATE common_lib Threads::Threads)

if(WIN32)
    target_link_libraries(local_smoke_tests PRIVATE ws2_32)
endif()

add_executable(local_process_smoke
    tests/local_process_smoke.cpp
)
target_link_libraries(local_process_smoke PRIVATE common_lib Threads::Threads)

if(WIN32)
    target_link_libraries(local_process_smoke PRIVATE ws2_32)
endif()

enable_testing()
add_test(NAME frame_tests COMMAND frame_tests)
add_test(NAME stats_tests COMMAND stats_tests)
add_test(NAME parser_tests COMMAND parser_tests)
add_test(NAME local_smoke_tests COMMAND local_smoke_tests)
add_test(NAME local_process_smoke COMMAND local_process_smoke)

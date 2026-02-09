cmake_minimum_required(VERSION 3.10)

project(libzv LANGUAGES C)

option(BUILD_TESTS "Build tests" ON)
option(ASAN "Enable AddressSanitizer" OFF)

include_directories(include)

set(LIB_SRCS
    include/zv/acceptor.h
    include/zv/array.h
    include/zv/atomic.h
    include/zv/buffer.h
    include/zv/channel.h
    include/zv/config.h
    include/zv/connector.h
    include/zv/export.h
    include/zv/log.h
    include/zv/loop.h
    include/zv/os_compat.h
    include/zv/os_socket.h
    include/zv/os_thread.h
    include/zv/os_time.h
    include/zv/semaphore.h
    include/zv/stream_defragger.h
    include/zv/thread_pool.h
    include/zv/timer_pool.h
    src/acceptor.c
    src/array.c
    src/buffer.c
    src/channel.c
    src/channel_in_queue.h
    src/channel_in_queue.c
    src/channel_out_queue.h
    src/channel_out_queue.c
    src/config.c
    src/connector.c
    src/log.c
    src/loop.c
    src/loop_int.h
    src/loop_poll.c
    src/loop_select.c
    src/misc.h
    src/os_compat.c
    src/os_socket.c
    src/os_thread.c
    src/os_time.c
    src/semaphore.c
    src/stream_defragger.c
    src/thread_pool.c
    src/timer_pool.c
)

# Disable tests for the buffer set library
set(_BUILD_TESTS ${BUILD_TESTS})
set(BUILD_TESTS OFF)
add_subdirectory(thd/buffer_set)
set(BUILD_TESTS ${_BUILD_TESTS})

set(LIBRARIES buffer_set)

add_library(zv SHARED ${LIB_SRCS})
target_compile_definitions(zv PRIVATE BUILDING_ZV_DLL)
target_include_directories(zv PRIVATE thd/buffer_set/include)
target_link_libraries(zv buffer_set)

if(ASAN)
    if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|GNU|Clang")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
    elseif(MSVC)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /fsanitize=address")
    else()
        message(SEND_ERROR "AddressSanitizer support requires clang, gcc, or msvc.")
    endif()
endif()

if(UNIX)
    target_link_libraries(zv -lpthread)
endif()

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    # gettid() on Linux available in unistd.h only with _GNU_SOURCE
    target_compile_definitions(zv PUBLIC _GNU_SOURCE)
endif()

if(WIN32)
    set_property(TARGET zv PROPERTY C_STANDARD 11)
    target_compile_definitions(zv PRIVATE WIN32_LEAN_AND_MEAN)
    target_link_libraries(zv ws2_32)
endif()

if(BUILD_TESTS)

    set(TEST_SRCS
        tests/acceptor1.c
        tests/channel_close1.c
        tests/channel_close2.c
        tests/channel_recv.c
        tests/channel_recv_suspend.c
        tests/channel_send_peer_close.c
        tests/connector1.c
        tests/connector2.c
        tests/connector3.c
        tests/helpers.h
        tests/helpers.c
        tests/ipv6.c
        tests/main.c
        tests/stream_defragger1.c
        tests/timer_pool_test1.c
        tests/timer_pool_test2.c
        tests/timer_pool_test3.c
        tests/timer_pool_test4.c
        tests/timer_pool_test5.c
    )

    set(BENCHMARK_SRCS
        tests/benchmark-tcp_broadcast.c
        tests/benchmark-tcp_echo_latency.c
        tests/benchmark-tcp_ping_pongs.c
        tests/benchmark-thread_pool_latency.c
        tests/benchmark-thread_pool_throughput.c
        tests/benchmark-udp_echo_latency.c
        tests/benchmarks.c
        tests/helpers.c
        tests/histogram.c
        tests/tcp_broadcast_server.c
        tests/tcp_echo_server.c
        tests/udp_echo_server.c
    )

    add_executable(zv_tests ${TEST_SRCS})
    add_executable(benchmarks ${BENCHMARK_SRCS})

    foreach(TARGET benchmarks zv_tests)
        add_dependencies(${TARGET} zv)
        target_link_libraries(${TARGET} zv)
        if(WIN32)
            set_property(TARGET ${TARGET} PROPERTY C_STANDARD 11)
            target_compile_definitions(${TARGET} PRIVATE WIN32_LEAN_AND_MEAN)
        endif()
    endforeach()

    include(CTest)
    add_test(NAME zv_tests COMMAND zv_tests)
endif()

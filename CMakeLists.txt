cmake_minimum_required(VERSION 3.16)
project(adsb-decoder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLADERF REQUIRED libbladeRF)

set(PROTO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/proto/adsb.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_SRC})

add_executable(adsb-decoder
    src/main.cpp
    src/sdr.cpp
    src/preamble.cpp
    src/decoder.cpp
    src/cpr.cpp
    src/aircraft.cpp
    src/server.cpp
    ${PROTO_SRCS}
)
target_include_directories(adsb-decoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${BLADERF_INCLUDE_DIRS}
)

target_link_libraries(adsb-decoder PRIVATE
    protobuf::libprotobuf
    ${BLADERF_LIBRARIES}
    pthread
)
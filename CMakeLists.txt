cmake_minimum_required(VERSION 3.16)
project(order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(order_book
    src/main.cpp
    src/order_book.cpp
    src/gateway.cpp
    src/market_data.cpp
    src/analytics.cpp
    src/order_server.cpp
)
target_include_directories(order_book PRIVATE src)
target_link_libraries(order_book PRIVATE pthread)

enable_testing()

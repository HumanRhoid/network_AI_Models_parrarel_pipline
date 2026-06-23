cmake_minimum_required(VERSION 3.10)
project(net)

set(CMAKE_C_STANDARD 11)

add_executable(server server/server.c)
add_executable(worker worker/worker.c)
add_executable(client client/client.c)
include_directories(${CMAKE_SOURCE_DIR})
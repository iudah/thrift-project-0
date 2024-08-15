cmake_minimum_required(VERSION 3.20)

project(Thrift-Project)

add_executable(thrift main.c)

install(TARGETS thrift DESTINATION bin)

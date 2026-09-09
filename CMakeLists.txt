cmake_minimum_required(VERSION 3.25)

project(harry_potter_vr LANGUAGES C CXX)

include(CTest)
add_subdirectory(src/wand)
add_subdirectory(src/quest)

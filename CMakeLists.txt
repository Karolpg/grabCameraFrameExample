cmake_minimum_required(VERSION 2.4)
set(TARGET_NAME grabCameraFrameExample)
project(${TARGET_NAME})

set(SOURCES main.cpp)

add_executable(${TARGET_NAME} ${SOURCES})

find_package(PNG)

target_link_libraries(${TARGET_NAME} gstreamer-1.0 
                                     gobject-2.0 
                                     glib-2.0 
                                     pthread
                                     PNG::PNG)

target_include_directories(${TARGET_NAME} PRIVATE /usr/include/gstreamer-1.0 
                                                  /usr/include/glib-2.0 
                                                  /usr/lib/x86_64-linux-gnu/glib-2.0/include)

target_compile_options(${TARGET_NAME} PRIVATE -fpermissive)

set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 17)

#  pkg-config --cflags --libs gstreamer-1.0

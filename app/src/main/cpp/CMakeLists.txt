cmake_minimum_required(VERSION 3.22.1)
project(engine)

set(CMAKE_CXX_STANDARD 17)

set(GLUE ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue)
add_library(native_app_glue STATIC ${GLUE}/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${GLUE})

add_library(engine SHARED main.cpp)
set_target_properties(engine PROPERTIES LINK_FLAGS "-u ANativeActivity_onCreate")
target_link_libraries(engine native_app_glue android EGL GLESv3 log)

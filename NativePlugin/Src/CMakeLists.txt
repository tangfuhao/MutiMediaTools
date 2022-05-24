cmake_minimum_required(VERSION 3.7)
project(MediaTools VERSION 0.1.0)

include(CTest)
enable_testing()


file(GLOB APP_SOURCES ./MediaCodec/*.cpp )

add_library(MediaTools SHARED ${APP_SOURCES}
./Platform/RenderAPI_OpenGLCoreES.cpp
./Platform/RenderingPlugin.cpp
./Platform/RenderAPI.cpp )


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSUPPORT_OPENGL_UNIFIED=1 -DUNITY_LINUX=1 -fPIC")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Include/MediaCodec)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Include/Platform)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Include/Platform/Unity)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Video_Codec_SDK_11.1.5/Samples/AppEncode/AppEncGL)


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Video_Codec_SDK_11.1.5/Samples)

target_link_libraries(MediaTools AppEncGL)











set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)



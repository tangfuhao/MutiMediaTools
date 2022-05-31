cmake_minimum_required(VERSION 3.7)
project(MediaTools VERSION 0.1.0)

include(CTest)
enable_testing()

if(WIN32)
else()
find_package(PkgConfig)
pkg_search_module(avcodec REQUIRED libavcodec)
MESSAGE(STATUS "avcodec dirs:" ${avcodec_INCLUDE_DIRS})
MESSAGE(STATUS "avcodec lib:" ${avcodec_LIBRARIES})
include_directories(${avcodec_INCLUDE_DIRS})
link_directories(${avcodec_LIBRARIES})

pkg_search_module(avfilter REQUIRED libavfilter)
MESSAGE(STATUS "avfilter dirs:" ${avfilter_INCLUDE_DIRS})
MESSAGE(STATUS "avfilter lib:" ${avfilter_LIBRARIES})
include_directories(${avfilter_INCLUDE_DIRS})
link_directories(${avfilter_LIBRARIES})

pkg_search_module(swresample REQUIRED libswresample)
MESSAGE(STATUS "swresample dirs:" ${swresample_INCLUDE_DIRS})
MESSAGE(STATUS "swresample lib:" ${swresample_LIBRARIES})
include_directories(${swresample_INCLUDE_DIRS})
link_directories(${swresample_LIBRARIES})


pkg_search_module(swscale REQUIRED libswscale)
MESSAGE(STATUS "swscale dirs:" ${swscale_INCLUDE_DIRS})
MESSAGE(STATUS "swscale lib:" ${swscale_LIBRARIES})
include_directories(${swscale_INCLUDE_DIRS})
link_directories(${swscale_LIBRARIES})


pkg_search_module(avformat REQUIRED libavformat)
MESSAGE(STATUS "avformat dirs:" ${avformat_INCLUDE_DIRS})
MESSAGE(STATUS "avformat lib:" ${avformat_LIBRARIES})
include_directories(${avformat_INCLUDE_DIRS})
link_directories(${avformat_LIBRARIES})

pkg_search_module(avutil REQUIRED libavutil)
MESSAGE(STATUS "avutil dirs:" ${avutil_INCLUDE_DIRS})
MESSAGE(STATUS "avutil lib:" ${avutil_LIBRARIES})
include_directories(${avutil_INCLUDE_DIRS})
link_directories(${avutil_LIBRARIES})

pkg_search_module(avdevice REQUIRED libavdevice)
MESSAGE(STATUS "avdevice dirs:" ${avdevice_INCLUDE_DIRS})
MESSAGE(STATUS "avdevice lib:" ${avdevice_LIBRARIES})
include_directories(${avdevice_INCLUDE_DIRS})
link_directories(${avdevice_LIBRARIES})
endif()


if(WIN32)
set(CODEC_SOURCES ./MediaCodec/NVENCCodecAPI.cpp ./MediaCodec/NVENCWrapper.cpp)
else()
file(GLOB CODEC_SOURCES ./MediaCodec/*.cpp )
set(RENDER_API_SROUCE ./Platform/RenderAPI_OpenGLCoreES.cpp  ./Platform/RenderingPlugin.cpp ./Platform/RenderAPI.cpp)
file(GLOB_RECURSE PLAYER_SOURCES ./MediaPlayer/*.c )
endif()

if(WIN32)
    add_library(MediaTools SHARED ${CODEC_SOURCES} ${RENDER_API_SROUCE} )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSUPPORT_OPENGL_UNIFIED=1 -DUNITY_WIN=1 -fPIC")
else()
    add_library(MediaTools SHARED ${CODEC_SOURCES} ${RENDER_API_SROUCE} ${PLAYER_SOURCES})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSUPPORT_OPENGL_UNIFIED=1 -DUNITY_LINUX=1 -fPIC")
endif()



include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Include/MediaCodec)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Include/Platform)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Include/Platform/Unity)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Include/MediaPlayer)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Include/MediaPlayer/mediaplayer)


if(WIN32)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Video_Codec_SDK_11.1.5/Samples/AppEncode/AppEncCuda)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Video_Codec_SDK_11.1.5/Samples)
else()
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Video_Codec_SDK_11.1.5/Samples/AppEncode/AppEncGL)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Video_Codec_SDK_11.1.5/Samples)
endif()




if(WIN32)
target_link_libraries(MediaTools AppEncCuda ${avcodec_LIBRARIES} ${avfilter_LIBRARIES} ${swresample_LIBRARIES} ${swscale_LIBRARIES} ${avformat_LIBRARIES} ${avutil_LIBRARIES} ${avdevice_LIBRARIES} -lpthread)
else()
target_link_libraries(MediaTools AppEncGL ${avcodec_LIBRARIES} ${avfilter_LIBRARIES} ${swresample_LIBRARIES} ${swscale_LIBRARIES} ${avformat_LIBRARIES} ${avutil_LIBRARIES} ${avdevice_LIBRARIES})
endif()











set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)



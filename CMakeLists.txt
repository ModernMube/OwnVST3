# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

# Project information and version
project(ownvst3 
    VERSION 1.0.2
    DESCRIPTION "A lightweight C++ wrapper for loading and working with VST3 plugins."
    HOMEPAGE_URL "https://github.com/ModernMube/OwnVST3Host"
)

# C++14 standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_GENERATOR_PLATFORM x64)

# Create dynamic library
option(BUILD_SHARED_LIBS "Build shared library" ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# VST3 SDK path
set(VST3_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/sdk" CACHE PATH "VST3 SDK root directory")

# Platform-specific settings
if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
elseif(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Target macOS architectures")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
    add_definitions(-DRELEASE) 
elseif(UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()

add_definitions(-DVST3_WRAPPER_EXPORTS)
add_definitions(-DOWN_VST3_HOST_EXPORTS)

if(WIN32)
    # VST3 SDK sources WINDOWS
    set(VST3_SDK_SOURCES
        ${VST3_SDK_ROOT}/base/source/baseiids.cpp
        ${VST3_SDK_ROOT}/base/source/fbuffer.cpp
        ${VST3_SDK_ROOT}/base/source/fdebug.cpp
        ${VST3_SDK_ROOT}/base/source/fdynlib.cpp
        ${VST3_SDK_ROOT}/base/source/fobject.cpp
        ${VST3_SDK_ROOT}/base/source/fstreamer.cpp
        ${VST3_SDK_ROOT}/base/source/fstring.cpp
        ${VST3_SDK_ROOT}/base/source/timer.cpp
        ${VST3_SDK_ROOT}/base/source/updatehandler.cpp
        ${VST3_SDK_ROOT}/base/thread/source/fcondition.cpp
        ${VST3_SDK_ROOT}/base/thread/source/flock.cpp
        ${VST3_SDK_ROOT}/pluginterfaces/base/coreiids.cpp
        ${VST3_SDK_ROOT}/pluginterfaces/base/funknown.cpp
        ${VST3_SDK_ROOT}/pluginterfaces/base/ustring.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/commoniids.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/commonstringconvert.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/pluginview.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/threadchecker_win32.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/connectionproxy.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/eventlist.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/hostclasses.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/parameterchanges.cpp    
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/plugprovider.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/processdata.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/utility/stringconvert.cpp 
        ${VST3_SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp
    )
elseif(APPLE)
    # VST3 SDK sources MAC
    set(VST3_SDK_SOURCES
        ${VST3_SDK_ROOT}/base/source/baseiids.cpp
        ${VST3_SDK_ROOT}/base/source/fbuffer.cpp
        ${VST3_SDK_ROOT}/base/source/fdebug.cpp
        ${VST3_SDK_ROOT}/base/source/fdynlib.cpp
        ${VST3_SDK_ROOT}/base/source/fobject.cpp
        ${VST3_SDK_ROOT}/base/source/fstreamer.cpp
        ${VST3_SDK_ROOT}/base/source/fstring.cpp
        ${VST3_SDK_ROOT}/base/source/timer.cpp
        ${VST3_SDK_ROOT}/base/source/updatehandler.cpp
        ${VST3_SDK_ROOT}/base/thread/source/fcondition.cpp
        ${VST3_SDK_ROOT}/base/thread/source/flock.cpp
        ${VST3_SDK_ROOT}/pluginterfaces/base/coreiids.cpp
        ${VST3_SDK_ROOT}/pluginterfaces/base/funknown.cpp
        ${VST3_SDK_ROOT}/pluginterfaces/base/ustring.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/commoniids.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/commonstringconvert.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/pluginview.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/threadchecker_mac.mm
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/connectionproxy.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/eventlist.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/hostclasses.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/parameterchanges.cpp    
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/plugprovider.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/processdata.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/utility/stringconvert.cpp 
        ${VST3_SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp
    )
elseif(LINUX)
    # VST3 SDK sources LINUX
    set(VST3_SDK_SOURCES
        ${VST3_SDK_ROOT}/base/source/baseiids.cpp
        ${VST3_SDK_ROOT}/base/source/fbuffer.cpp
        ${VST3_SDK_ROOT}/base/source/fdebug.cpp
        ${VST3_SDK_ROOT}/base/source/fdynlib.cpp
        ${VST3_SDK_ROOT}/base/source/fobject.cpp
        ${VST3_SDK_ROOT}/base/source/fstreamer.cpp
        ${VST3_SDK_ROOT}/base/source/fstring.cpp
        ${VST3_SDK_ROOT}/base/source/timer.cpp
        ${VST3_SDK_ROOT}/base/source/updatehandler.cpp
        ${VST3_SDK_ROOT}/base/thread/source/fcondition.cpp
        ${VST3_SDK_ROOT}/base/thread/source/flock.cpp
        ${VST3_SDK_ROOT}/pluginterfaces/base/coreiids.cpp
        ${VST3_SDK_ROOT}/pluginterfaces/base/funknown.cpp
        ${VST3_SDK_ROOT}/pluginterfaces/base/ustring.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/commoniids.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/commonstringconvert.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/pluginview.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/threadchecker_linux.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/connectionproxy.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/eventlist.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/hostclasses.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_linux.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/parameterchanges.cpp    
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/plugprovider.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/processdata.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/utility/stringconvert.cpp 
        ${VST3_SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp
    )
endif()


# Platform-specific sources
if(WIN32)
    list(APPEND VST3_SDK_SOURCES
#        ${VST3_SDK_ROOT}/base/source/fplatform_win32.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp
    )
elseif(APPLE)
    list(APPEND VST3_SDK_SOURCES
#       ${VST3_SDK_ROOT}/base/source/fplatform_mac.mm
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_mac.mm
    )
elseif(UNIX)
    list(APPEND VST3_SDK_SOURCES
#       ${VST3_SDK_ROOT}/base/source/fplatform_linux.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_linux.cpp
    )
endif()

# VST3 SDK include directories
include_directories(
    ${VST3_SDK_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Add -fobjc-arc flag for Objective-C/Objective-C++ files
if(APPLE)
    set_source_files_properties(
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_mac.mm
        ${VST3_SDK_ROOT}/public.sdk/source/common/threadchecker_mac.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
endif()

# Source files
set(SOURCES 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ownvst3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ownvst3_wrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ownvst3_test.cpp
)

# Header files
set(HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ownvst3.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ownvst3_wrapper.h
)

# Create library
add_library(ownvst3 ${SOURCES} ${HEADERS} ${VST3_SDK_SOURCES})

# Create executable
add_executable(ownvst3_test ${SOURCES} ${HEADERS} ${VST3_SDK_SOURCES})

# Platform-specific links
if(WIN32)
    target_link_libraries(ownvst3 PRIVATE ole32 uuid)
elseif(APPLE)
    find_library(CORE_FOUNDATION CoreFoundation)
    find_library(COCOA Cocoa)
    target_link_libraries(ownvst3 PRIVATE ${CORE_FOUNDATION} ${COCOA})
elseif(UNIX)
    target_link_libraries(ownvst3 PRIVATE dl pthread)
endif()

# Platform-specific links for the test executable
if(WIN32)
    target_link_libraries(ownvst3_test PRIVATE ole32 uuid)
elseif(APPLE)
    find_library(CORE_FOUNDATION CoreFoundation)
    find_library(COCOA Cocoa)
    target_link_libraries(ownvst3_test PRIVATE ${CORE_FOUNDATION} ${COCOA})
elseif(UNIX)
    target_link_libraries(ownvst3_test PRIVATE dl pthread)
endif()

# Export headers
set_target_properties(ownvst3 PROPERTIES
    PUBLIC_HEADER "${HEADERS}"
)

# Installation rules
include(GNUInstallDirs)
install(TARGETS ownvst3
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
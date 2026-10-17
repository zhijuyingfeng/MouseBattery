cmake_minimum_required(VERSION 3.21)

project(MouseBattery VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

option(MOUSEBATTERY_USE_HIDAPI
    "Use the hidapi backend on Linux as well (mainly for testing)" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

set(MOUSEBATTERY_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/batterygauge.cpp
    src/batterygauge.h
    src/batterydevice.cpp
    src/batterydevice.h
    src/batteryreader.cpp
    src/batteryreader.h
)

if(WIN32 OR APPLE OR MOUSEBATTERY_USE_HIDAPI)
    list(APPEND MOUSEBATTERY_SOURCES src/batterydevice_hidapi.cpp)
    set(MOUSEBATTERY_HIDAPI_BACKEND TRUE)
else()
    list(APPEND MOUSEBATTERY_SOURCES src/batterydevice_linux.cpp)
    set(MOUSEBATTERY_HIDAPI_BACKEND FALSE)
endif()

qt_add_executable(MouseBattery ${MOUSEBATTERY_SOURCES})

target_link_libraries(MouseBattery PRIVATE Qt6::Widgets)

target_compile_options(MouseBattery PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

set_target_properties(MouseBattery PROPERTIES
    OUTPUT_NAME "MouseBattery"
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

if(APPLE)
    set_target_properties(MouseBattery PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "MouseBattery"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.zhijuyingfeng.mousebattery"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
    )
    # hidapi's macOS backend is a static library in this build.
    find_package(Threads REQUIRED)
    target_link_libraries(MouseBattery PRIVATE
        Threads::Threads
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework AppKit"
    )
endif()

if(MOUSEBATTERY_HIDAPI_BACKEND)
    find_package(hidapi CONFIG QUIET)

    if(NOT hidapi_FOUND)
        include(FetchContent)

        # hidapi still declares cmake_minimum_required(VERSION 3.1.3).
        # CMake 4 removes compatibility with < 3.5 unless this is set.
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

        set(HIDAPI_WITH_TESTS OFF CACHE BOOL "" FORCE)
        set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "" FORCE)
        set(HIDAPI_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

        if(UNIX AND NOT APPLE)
            set(HIDAPI_WITH_HIDRAW ON CACHE BOOL "" FORCE)
            set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)
        endif()

        FetchContent_Declare(hidapi
            GIT_REPOSITORY https://github.com/libusb/hidapi.git
            GIT_TAG hidapi-0.14.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(hidapi)
    endif()

    if(TARGET hidapi::hidapi)
        target_link_libraries(MouseBattery PRIVATE hidapi::hidapi)
    elseif(TARGET hidapi::hidapi-hidraw)
        target_link_libraries(MouseBattery PRIVATE hidapi::hidapi-hidraw)
    elseif(TARGET hidapi::hidapi-libusb)
        target_link_libraries(MouseBattery PRIVATE hidapi::hidapi-libusb)
    elseif(TARGET hidapi::winapi)
        target_link_libraries(MouseBattery PRIVATE hidapi::winapi)
    elseif(TARGET hidapi::darwin)
        target_link_libraries(MouseBattery PRIVATE hidapi::darwin)
    else()
        message(FATAL_ERROR "hidapi was found but no usable hidapi target is available")
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS MouseBattery
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

include(CTest)
if(BUILD_TESTING)
    add_executable(protocol_test
        tests/protocol_test.cpp
        src/batterydevice.cpp
        src/batterydevice.h
    )
    target_include_directories(protocol_test PRIVATE src)
    target_link_libraries(protocol_test PRIVATE Qt6::Core)
    target_compile_options(protocol_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
    add_test(NAME protocol_test COMMAND protocol_test)
endif()

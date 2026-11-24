cmake_minimum_required(VERSION 3.16)
project(ShecanKillSwitch VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

add_executable(ShecanKillSwitch WIN32
        main.cpp
        firewallmanager.cpp
        firewallmanager.h
        toggleswitch.cpp
        toggleswitch.h
        mainwindow.cpp
        mainwindow.h
        resources.qrc
)

target_include_directories(ShecanKillSwitch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ShecanKillSwitch PRIVATE
        Qt6::Widgets
        Qt6::Concurrent
        Ws2_32
        Iphlpapi
        Ole32
        OleAut32
        Dwmapi
)

if(WIN32)
    target_compile_definitions(ShecanKillSwitch PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    if(MINGW)
        set(APP_RC_OBJ "${CMAKE_CURRENT_BINARY_DIR}/app_rc.obj")
        add_custom_command(
                OUTPUT "${APP_RC_OBJ}"
                COMMAND "${CMAKE_RC_COMPILER}" -O coff --preprocessor type "app.rc" "${APP_RC_OBJ}"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/app.rc" "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                VERBATIM)
        target_sources(ShecanKillSwitch PRIVATE "${APP_RC_OBJ}")
        target_link_options(ShecanKillSwitch PRIVATE "-Wl,--allow-multiple-definition")

        get_filename_component(MINGW_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        foreach(runtime_dll IN ITEMS
                libstdc++-6.dll
                libgcc_s_seh-1.dll
                libwinpthread-1.dll)
            if(NOT EXISTS "${MINGW_RUNTIME_DIR}/${runtime_dll}")
                message(FATAL_ERROR "Required MinGW runtime DLL not found: ${MINGW_RUNTIME_DIR}/${runtime_dll}")
            endif()
            add_custom_command(TARGET ShecanKillSwitch POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_RUNTIME_DIR}/${runtime_dll}"
                    "$<TARGET_FILE_DIR:ShecanKillSwitch>/${runtime_dll}"
                    VERBATIM)
        endforeach()
    else()
        target_sources(ShecanKillSwitch PRIVATE app.rc)
    endif()
endif()
# =============================================================================
#  User-managed CMake entries for additional sources and include paths.
#
#  This file is managed by the "CMake Build List Manager" VS Code extension.
#  Do not edit manually while the extension is active.
#
#  To use this file, add the following line to your root CMakeLists.txt
#  (preferably near the end, before any add_subdirectory calls):
#
#      include(cmake_manager_gen/ManagedLists.cmake)
#
#  The extension will also prompt you to add this line automatically.
# =============================================================================

set(USER_SOURCES
    User/device/motor2204.cpp
    User/app/pc_link.cpp
    User/app/app_main.cpp
)

set(USER_HEADERS
    User/device
    User/driver
    User/app
    User/app
)

# The variables above are applied to the "${CMAKE_PROJECT_NAME}" target.
if(TARGET ${CMAKE_PROJECT_NAME})
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${USER_SOURCES}
    )

    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${USER_HEADERS}
    )
endif()
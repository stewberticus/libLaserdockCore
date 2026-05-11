cmake_minimum_required(VERSION 3.11)

add_executable(ld_network_demo
    src/main.cpp
)

target_link_libraries(ld_network_demo
    PRIVATE
        laserdocklib
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
)

set_target_properties(ld_network_demo PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# macOS: let the linker find the laserdocklib dylib at runtime from its build location
if(APPLE)
    set_target_properties(ld_network_demo PROPERTIES
        BUILD_RPATH "$<TARGET_FILE_DIR:laserdocklib>"
    )
endif()

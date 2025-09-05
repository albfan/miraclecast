set(CMAKE_C_FLAGS "-std=gnu11 ${CMAKE_C_FLAGS}")

add_subdirectory(shared)
add_subdirectory(wifi)
add_subdirectory(dhcp)
add_subdirectory(ctl)
add_subdirectory(uibc)

INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/shared)

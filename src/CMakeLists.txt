set(CMAKE_C_FLAGS "-std=gnu11 ${CMAKE_C_FLAGS}")

add_subdirectory(shared)
add_subdirectory(wifi)
add_subdirectory(dhcp)
add_subdirectory(ctl)
add_subdirectory(uibc)
add_subdirectory(disp)

set(miracled_SRCS miracled.h miracled.c)
add_executable(miracled ${miracled_SRCS})
target_link_libraries(miracled miracle-shared)
target_link_libraries(miracled m)
install(TARGETS miracled DESTINATION bin)

INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/shared)

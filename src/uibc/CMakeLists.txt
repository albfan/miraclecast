set(miracle-uibcctl_SRCS miracle-uibcctl.h
        miracle-uibcctl.c)

add_executable(miracle-uibcctl ${miracle-uibcctl_SRCS})
target_link_libraries(miracle-uibcctl PRIVATE miracle-shared)
target_link_libraries(miracle-uibcctl PUBLIC m)

install(TARGETS miracle-uibcctl DESTINATION bin)

INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/shared)

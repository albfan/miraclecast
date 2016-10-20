pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_PBUTILS REQUIRED gstreamer-pbutils-1.0)

include_directories(${GST_INCLUDE_DIRS} ${GST_PBUTILS_INCLUDE_DIRS})

add_executable(miracle-sender sender)

target_link_libraries(miracle-sender ${GST_LIBRARIES} ${GST_PBUTILS_LIBRARIES})

install(TARGETS miracle-sender DESTINATION bin)

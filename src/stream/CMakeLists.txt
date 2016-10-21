pkg_check_modules(GTK REQUIRED gtk+-3.0)
pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_PBUTILS REQUIRED gstreamer-pbutils-1.0)

include_directories(
	${GTK_INCLUDE_DIRS}
	${GST_INCLUDE_DIRS}
	${GST_PBUTILS_INCLUDE_DIRS}
)

add_executable(miracle-sender sender)

target_link_libraries(
	miracle-sender
	${GTK_LIBRARIES}
	${GST_LIBRARIES}
	${GST_PBUTILS_LIBRARIES})

install(TARGETS miracle-sender DESTINATION bin)

pkg_check_modules(GDK REQUIRED gdk-3.0>=3.20)
pkg_check_modules(GIO REQUIRED gio-2.0>=2.30)
pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_PBUTILS REQUIRED gstreamer-pbutils-1.0)
find_program(GDBUS_CODEGEN_EXEC NAMES gdbus-codegen)

if(NOT GDBUS_CODEGEN_EXEC)
	message(ERROR gdbus-codegen not found)
endif()

include_directories(
	${CMAKE_CURRENT_BINARY_DIR}
	${GDK_INCLUDE_DIRS}
	${GST_INCLUDE_DIRS}
	${GST_PBUTILS_INCLUDE_DIRS}
	${GIO_INCLUDE_DIRS}
)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sender-iface.c
						  ${CMAKE_CURRENT_BINARY_DIR}/sender-iface.h
				   COMMAND ${GDBUS_CODEGEN_EXEC}
				   ARGS --generate-c-code sender-iface
						--annotate org.freedesktop.miracle.Sender org.gtk.GDBus.C.Name Sender
						--interface-prefix org.freedesktop.Miracle
						${CMAKE_CURRENT_SOURCE_DIR}/sender-iface.xml
				   DEPENDS sender-iface.xml
				   COMMENT "generating sender interface"
)

add_definitions(-DG_LOG_USE_STRUCTURED)

add_library(sender-iface STATIC ${CMAKE_CURRENT_BINARY_DIR}/sender-iface.c)

add_executable(miracle-sender sender)

target_link_libraries(miracle-sender sender-iface
									 ${GDK_LIBRARIES}
									 ${GIO_LIBRARIES}
									 ${GST_LIBRARIES}
									 ${GST_PBUTILS_LIBRARIES})

install(TARGETS miracle-sender DESTINATION bin)

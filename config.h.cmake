#ifndef CONFIG_H
#define CONFIG_H

#cmakedefine ENABLE_SYSTEMD

#cmakedefine BUILD_BINDIR "@BUILD_BINDIR@"
#cmakedefine RELY_UDEV @RELY_UDEV@
#cmakedefine IP_BINARY @IP_BINARY@

#cmakedefine PACKAGE_STRING "@PACKAGE_STRING@"

#endif // CONFIG_H

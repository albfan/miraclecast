/*
 * SHL - Log/Debug Interface
 *
 * Copyright (c) 2010-2013 David Herrmann <dh.herrmann@gmail.com>
 * Dedicated to the Public Domain
 */

/*
 * Log/Debug Interface
 * This interface provides basic logging to stderr.
 *
 * Define BUILD_ENABLE_DEBUG before including this header to enable
 * debug-messages for this file.
 */

#ifndef SHL_LOG_H
#define SHL_LOG_H

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

enum log_severity {
#ifndef LOG_FATAL
	LOG_FATAL = 0,
#endif
#ifndef LOG_ALERT
	LOG_ALERT = 1,
#endif
#ifndef LOG_CRITICAL
	LOG_CRITICAL = 2,
#endif
#ifndef LOG_ERROR
	LOG_ERROR = 3,
#endif
#ifndef LOG_WARNING
	LOG_WARNING = 4,
#endif
#ifndef LOG_NOTICE
	LOG_NOTICE = 5,
#endif
#ifndef LOG_INFO
	LOG_INFO = 6,
#endif
#ifndef LOG_DEBUG
	LOG_DEBUG = 7,
#endif
#ifndef LOG_TRACE
	LOG_TRACE = 8,
#endif
	LOG_SEV_NUM,
};

/*
 * Max Severity
 * Messages with severities between log_max_sev and LOG_SEV_NUM (exclusive)
 * are not logged, but discarded.
 * Default: LOG_NOTICE
 */

extern unsigned int log_max_sev;

/*
 * Defines the debug configuration for gstreamer
 */
extern char* gst_debug;

/*
 * Timestamping
 * Call this to initialize timestamps and cause all log-messages to be prefixed
 * with a timestamp. If not called, no timestamps are added.
 */

void log_init_time(void);

/*
 * Log-Functions
 * These functions pass a log-message to the log-subsystem. Handy helpers are
 * provided below. You almost never use these directly.
 *
 * log_submit:
 * Submit the message to the log-subsystem. This is the backend of all other
 * loggers.
 *
 * log_format:
 * Same as log_submit but first converts the arguments into a va_list object.
 *
 * log_llog:
 * Same as log_submit but used as connection to llog.
 *
 * log_dummyf:
 * Dummy logger used for gcc var-arg validation.
 */

__attribute__((format(printf, 6, 0)))
void log_submit(const char *file,
		int line,
		const char *func,
		const char *subs,
		unsigned int sev,
		const char *format,
		va_list args);

__attribute__((format(printf, 6, 7)))
void log_format(const char *file,
		int line,
		const char *func,
		const char *subs,
		unsigned int sev,
		const char *format,
		...);

__attribute__((format(printf, 7, 0)))
void log_llog(void *data,
	      const char *file,
	      int line,
	      const char *func,
	      const char *subs,
	      unsigned int sev,
	      const char *format,
	      va_list args);

unsigned int log_parse_arg(char *optarg);

static inline __attribute__((format(printf, 2, 3)))
void log_dummyf(unsigned int sev, const char *format, ...)
{
}

/*
 * Default values
 * All helpers automatically pick-up the file, line, func and subsystem
 * parameters for a log-message. file, line and func are generated with
 * __FILE__, __LINE__ and __func__ and should almost never be replaced.
 * The subsystem is by default an empty string. To overwrite this, add this
 * line to the top of your source file:
 *   #define LOG_SUBSYSTEM "mysubsystem"
 * Then all following log-messages will use this string as subsystem. You can
 * define it before or after including this header.
 *
 * If you want to change one of these, you need to directly use log_submit and
 * log_format. If you want the defaults for file, line and func you can use:
 *   log_format(LOG_DEFAULT_BASE, subsys, sev, format, ...);
 * If you want all default values, use:
 *   log_format(LOG_DEFAULT, sev, format, ...);
 *
 * If you want to change a single value, this is the default line that is used
 * internally. Adjust it to your needs:
 *   log_format(__FILE__, __LINE__, __func__, LOG_SUBSYSTEM, LOG_ERROR,
 *              "your format string: %s %d", "some args", 5, ...);
 *
 * log_printf is the same as log_format(LOG_DEFAULT, sev, format, ...) and is
 * the most basic wrapper that you can use.
 */

#ifndef LOG_SUBSYSTEM
extern const char *LOG_SUBSYSTEM;
#endif

#define LOG_DEFAULT_BASE __FILE__, __LINE__, __func__
#define LOG_DEFAULT LOG_DEFAULT_BASE, LOG_SUBSYSTEM

#define log_printf(sev, format, ...) \
	log_format(LOG_DEFAULT, (sev), (format), ##__VA_ARGS__)

/*
 * Helpers
 * These pick up all the default values and submit the message to the
 * log-subsystem. The log_debug() function produces zero-code if
 * BUILD_ENABLE_DEBUG is not defined. Therefore, it can be heavily used for
 * debugging and will not have any side-effects.
 * Even if disabled, parameters are evaluated! So it only produces zero code
 * if there are no side-effects and the compiler can optimized it away.
 */

#ifdef BUILD_ENABLE_DEBUG
	#define log_debug(format, ...) \
		log_printf(LOG_DEBUG, (format), ##__VA_ARGS__)
	#define log_trace(format, ...) \
		log_printf(LOG_TRACE, (format), ##__VA_ARGS__)
#else
	#define log_debug(format, ...) \
		log_dummyf(LOG_DEBUG, (format), ##__VA_ARGS__)
	#define log_trace(format, ...) \
		log_dummyf(LOG_TRACE, (format), ##__VA_ARGS__)
#endif

#define log_info(format, ...) \
	log_printf(LOG_INFO, (format), ##__VA_ARGS__)
#define log_notice(format, ...) \
	log_printf(LOG_NOTICE, (format), ##__VA_ARGS__)
#define log_warning(format, ...) \
	log_printf(LOG_WARNING, (format), ##__VA_ARGS__)
#define log_error(format, ...) \
	log_printf(LOG_ERROR, (format), ##__VA_ARGS__)
#define log_critical(format, ...) \
	log_printf(LOG_CRITICAL, (format), ##__VA_ARGS__)
#define log_alert(format, ...) \
	log_printf(LOG_ALERT, (format), ##__VA_ARGS__)
#define log_fatal(format, ...) \
	log_printf(LOG_FATAL, (format), ##__VA_ARGS__)

#define log_EINVAL() \
	(log_error("invalid arguments"), -EINVAL)
#define log_vEINVAL() \
	((void)log_EINVAL())

#define log_EFAULT() \
	(log_error("internal operation failed"), -EFAULT)
#define log_vEFAULT() \
	((void)log_EFAULT())

#define log_ENOMEM() \
	(log_error("out of memory"), -ENOMEM)
#define log_vENOMEM() \
	((void)log_ENOMEM())

#define log_EPIPE() \
	(log_error("fd closed unexpectedly"), -EPIPE)
#define log_vEPIPE() \
	((void)log_EPIPE())

#define log_ERRNO() \
	(log_error("syscall failed (%d): %m", errno), -errno)
#define log_vERRNO() \
	((void)log_ERRNO())

#define log_ERR(_r) \
	(errno = -(_r), log_error("syscall failed (%d): %m", (_r)), (_r))
#define log_vERR(_r) \
	((void)log_ERR(_r))

#define log_EUNMANAGED() \
	(log_error("interface unmanaged"), -EFAULT)
#define log_vEUNMANAGED() \
	((void)log_EUNMANAGED())

#endif /* SHL_LOG_H */

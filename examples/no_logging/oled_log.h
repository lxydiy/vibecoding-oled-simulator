#ifndef OLED_LOG_H
#define OLED_LOG_H

/* Standalone production replacement for include/oled_log.h.
 * No includes, declarations, storage, output, or argument evaluation.
 * Place this directory first on the application's include search path, or
 * copy this file into the hardware project's include directory.
 * Existing application sources keep: #include "oled_log.h"
 */
#define OLED_LOG_NONE    0
#define OLED_LOG_ERROR   1
#define OLED_LOG_WARN    2
#define OLED_LOG_INFO    3
#define OLED_LOG_DEBUG   4
#define OLED_LOG_VERBOSE 5

#ifndef OLED_LOG_LEVEL
#define OLED_LOG_LEVEL OLED_LOG_NONE
#endif
#ifndef OLED_LOG_MIN_LEVEL
#define OLED_LOG_MIN_LEVEL OLED_LOG_ERROR
#endif
#ifndef OLED_LOG_LOCAL_LEVEL
#define OLED_LOG_LOCAL_LEVEL OLED_LOG_LEVEL
#endif

#define oled_log_write(...) ((void)0)
#define OLED_LOGE(...) ((void)0)
#define OLED_LOGW(...) ((void)0)
#define OLED_LOGI(...) ((void)0)
#define OLED_LOGD(...) ((void)0)
#define OLED_LOGV(...) ((void)0)
#endif

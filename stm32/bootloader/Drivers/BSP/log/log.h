/*
 * log.h
 *
 *  Created on: Jul 21, 2026
 *      Author: Muhmmad Salman
 */

#ifndef LOG_H
#define LOG_H




#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** Compile-time master switch.
 *  Set to 0 to strip the LOG_ERROR/WARN/INFO/DEBUG macros below down to
 *  nothing — their arguments (including format-string literals) are not
 *  evaluated and cost zero flash/RAM. This is independent of the runtime
 *  Log_Enable()/Log_SetLevel() controls further down; flip this for
 *  release builds where you want logging gone, not just quiet. */
#define LOG_ENABLED   1

typedef enum {
    LOG_LEVEL_NONE  = 0,  /**< nothing is logged */
    LOG_LEVEL_ERROR,      /**< unrecoverable / serious problems only */
    LOG_LEVEL_WARN,       /**< + recoverable problems, unexpected states */
    LOG_LEVEL_INFO,       /**< + high-level status / progress messages */
    LOG_LEVEL_DEBUG,      /**< + verbose, developer-only detail */
} Log_Level_t;

/**
 * @brief  Initialize the logging UART (GPIO + peripheral config)
 * @note   Call once at startup, before any other Log_* call. If skipped
 *         or if init fails internally, later Log_* calls are silently
 *         no-ops rather than crashing.
 */
void Log_Init(void);

/** @brief  Set the minimum severity that gets printed (default: LOG_LEVEL_INFO) */
void Log_SetLevel(Log_Level_t level);

/** @brief  Get the currently configured level */
Log_Level_t Log_GetLevel(void);

/** @brief  Runtime on/off switch, independent of LOG_ENABLED and the level */
void Log_Enable(bool enable);

/** @brief  Query the runtime on/off switch */
bool Log_IsEnabled(void);

/* =========================================================================
 * Typed output primitives — the building blocks. Safe to call directly
 * when you want a specific type without going through a format string.
 * ========================================================================= */

/** @brief  Log a plain string.        e.g. Log_String(LOG_LEVEL_INFO, "MAIN", "Boot complete"); */
void Log_String(Log_Level_t level, const char *tag, const char *str);

/** @brief  Log a labeled signed integer. e.g. Log_Number(LOG_LEVEL_DEBUG, "TOUCH", "x", 512); */
void Log_Number(Log_Level_t level, const char *tag, const char *label, int32_t value);

/** @brief  Log a labeled float, `decimals` digits after the point (0-6).
 *  @note   Does its own float->string conversion — does not depend on
 *          newlib's %f support, so it works even without the
 *          "-u _printf_float" linker flag some toolchains need. */
void Log_Float(Log_Level_t level, const char *tag, const char *label, float value, uint8_t decimals);

/** @brief  printf-style logging. e.g. Log_Printf(LOG_LEVEL_INFO, "MAIN", "count=%d", n);
 *  @note   Avoid %f here (see Log_Float note above) — %d/%u/%x/%s are fine. */
void Log_Printf(Log_Level_t level, const char *tag, const char *fmt, ...);

/* =========================================================================
 * Convenience macros — preferred for most call sites, since these are
 * the ones LOG_ENABLED strips to nothing in a release build.
 * ========================================================================= */
#if LOG_ENABLED
    #define LOG_ERROR(tag, fmt, ...)  Log_Printf(LOG_LEVEL_ERROR, (tag), (fmt), ##__VA_ARGS__)
    #define LOG_WARN(tag, fmt, ...)   Log_Printf(LOG_LEVEL_WARN,  (tag), (fmt), ##__VA_ARGS__)
    #define LOG_INFO(tag, fmt, ...)   Log_Printf(LOG_LEVEL_INFO,  (tag), (fmt), ##__VA_ARGS__)
    #define LOG_DEBUG(tag, fmt, ...)  Log_Printf(LOG_LEVEL_DEBUG, (tag), (fmt), ##__VA_ARGS__)
#else
    #define LOG_ERROR(tag, fmt, ...)  do { (void)(tag); (void)(fmt); } while (0)
    #define LOG_WARN(tag, fmt, ...)   do { (void)(tag); (void)(fmt); } while (0)
    #define LOG_INFO(tag, fmt, ...)   do { (void)(tag); (void)(fmt); } while (0)
    #define LOG_DEBUG(tag, fmt, ...)  do { (void)(tag); (void)(fmt); } while (0)
#endif

#ifdef __cplusplus
}
#endif



#endif /* LOG_H */

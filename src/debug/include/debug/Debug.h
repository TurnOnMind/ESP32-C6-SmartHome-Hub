#ifndef DEBUG_DEBUG_H_
#define DEBUG_DEBUG_H_

#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif

#ifndef DEBUG_TAG
#define DEBUG_TAG "DEBUG"
#endif

#define DEBUG_BOOL_STR(value) ((value) ? "true" : "false")

#if DEBUG_LEVEL >= 1
#define DEBUG_FUNC_ENTER() ESP_LOGD(DEBUG_TAG, "ENTER %s", __func__)
#define DEBUG_FUNC_EXIT() ESP_LOGD(DEBUG_TAG, "EXIT %s", __func__)
#define DEBUG_FUNC_EXIT_RC(rc) ESP_LOGD(DEBUG_TAG, "EXIT %s rc=%" PRId64, __func__, (int64_t)(rc))
#else
#define DEBUG_FUNC_ENTER() ((void)0)
#define DEBUG_FUNC_EXIT() ((void)0)
#define DEBUG_FUNC_EXIT_RC(rc) ((void)(rc))
#endif

#if DEBUG_LEVEL >= 2
#define DEBUG_PARAM_INT(name, value) ESP_LOGD(DEBUG_TAG, "PARAM %s=%d", name, (int)(value))
#define DEBUG_PARAM_UINT(name, value) ESP_LOGD(DEBUG_TAG, "PARAM %s=%u", name, (unsigned int)(value))
#define DEBUG_PARAM_BOOL(name, value) ESP_LOGD(DEBUG_TAG, "PARAM %s=%s", name, DEBUG_BOOL_STR(value))
#define DEBUG_PARAM_STR(name, value) ESP_LOGD(DEBUG_TAG, "PARAM %s=\"%s\"", name, (value) ? (value) : "<null>")
#define DEBUG_PARAM_PTR(name, value) ESP_LOGD(DEBUG_TAG, "PARAM %s=%p", name, (const void*)(value))
#else
#define DEBUG_PARAM_INT(name, value) ((void)(value))
#define DEBUG_PARAM_UINT(name, value) ((void)(value))
#define DEBUG_PARAM_BOOL(name, value) ((void)(value))
#define DEBUG_PARAM_STR(name, value) ((void)(value))
#define DEBUG_PARAM_PTR(name, value) ((void)(value))
#endif

#if DEBUG_LEVEL >= 3
#define DEBUG_PROFILE() ESP_LOGD(DEBUG_TAG, "PROFILE %s @%lldus", __func__, (long long)esp_timer_get_time())
#else
#define DEBUG_PROFILE() ((void)0)
#endif

#if DEBUG_LEVEL >= 4
#include "esp_heap_caps.h"
#define DEBUG_MEM_SNAPSHOT(label)                                                                           \
  do {                                                                                                      \
    size_t _int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);                                        \
    size_t _ext_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);                                          \
    ESP_LOGD(DEBUG_TAG, "MEM[%s] internal=%u ext=%u", label, (unsigned)(_int_free), (unsigned)(_ext_free)); \
  } while (0)
#else
#define DEBUG_MEM_SNAPSHOT(label) ((void)(label))
#endif

#if DEBUG_LEVEL >= 5
#define DEBUG_STACK_TRACE() ESP_LOGD(DEBUG_TAG, "STACK %s", __func__)
#else
#define DEBUG_STACK_TRACE() ((void)0)
#endif

#endif  // DEBUG_DEBUG_H_

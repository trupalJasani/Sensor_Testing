/**
 * @file  esp_log.h (HOST TEST STUB)
 * @brief Replaces ESP-IDF logging when edge_ai.c is compiled on a PC.
 *        Define HOST_TEST_VERBOSE to print the log lines.
 */
#ifndef HOST_TEST_ESP_LOG_STUB_H
#define HOST_TEST_ESP_LOG_STUB_H

#include <stdio.h>

#ifdef HOST_TEST_VERBOSE
#define HOST_LOG_ENABLED 1
#else
#define HOST_LOG_ENABLED 0
#endif

#define HOST_LOG(level, tag, fmt, ...)                                        \
    do                                                                        \
    {                                                                         \
        if (HOST_LOG_ENABLED)                                                 \
        {                                                                     \
            printf("%s %s: " fmt "\n", level, tag, ##__VA_ARGS__);            \
        }                                                                     \
    } while (0)

#define ESP_LOGE(tag, fmt, ...) HOST_LOG("E", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) HOST_LOG("W", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) HOST_LOG("I", tag, fmt, ##__VA_ARGS__)

#endif /* HOST_TEST_ESP_LOG_STUB_H */

#pragma once
// Host-test stand-in for lib/Logging: prints, so failures show the sync log.
#include <cstdio>
#define LOG_INF(tag, ...) (std::printf("[%s] ", tag), std::printf(__VA_ARGS__), std::printf("\n"))
#define LOG_ERR LOG_INF
#define LOG_DBG LOG_INF

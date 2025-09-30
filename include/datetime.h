#pragma once

#include <time.h>

void UTCDateTimeString(char* buffer, int size, time_t timestamp);
time_t FileCreationTime(const char* path);
time_t FileModificationTime(const char* path);
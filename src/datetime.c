#include "datetime.h"

#include <sys/stat.h>

void UTCDateTimeString(char* buffer, int size, time_t timestamp) {
    strftime(buffer, size, "%a, %e %b %Y %H:%M:%S GMT", gmtime(&timestamp));
}

time_t FileCreationTime(const char *path) {
    struct stat attr;
    stat(path, &attr);
    return attr.st_ctime;
}

time_t FileModificationTime(const char *path) {
    struct stat attr;
    stat(path, &attr);
    return attr.st_mtime;
}
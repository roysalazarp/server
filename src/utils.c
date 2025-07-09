#include <string.h>
#include <stdio.h>
#include <sys/sysinfo.h>

#include "../../include/ark/shared.h"
#include "utils.h"

void bzero(void *p, size_t n) { memset(p, 0, n); }

unsigned long available_ram() {
    struct sysinfo info;
    
    if (sysinfo(&info) == 0) {
        printf("Total RAM: %lu bytes\n", info.totalram);
        printf("Free RAM:  %lu bytes\n", info.freeram);
        printf("Buffer RAM:  %lu bytes\n", info.bufferram);
        
        return info.freeram;
    }

    return 0;
}

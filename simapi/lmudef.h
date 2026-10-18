#ifndef _LMUDEF_H
#define _LMUDEF_H

#include <stdbool.h>
#include "../include/lmu.h"


#define LMU_FILE "LMU_Data"

typedef struct
{
    bool has_telemetry;
    void* telemetry_map_addr;
    int fd_telemetry;
    struct LMUObject lmu_telemetry;
    double prev_elapsed_time;
    double prev_deflection[4];
    uint64_t prev_frame_mtick;
    int game_pid;
    long long empty_since;
    bool bridged;
    bool warned;
}
LMUMap;

#endif

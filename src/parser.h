#pragma once
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

enum supported_command
{
    CMD_DELAY_ALL = 0,
    CMD_DELAY_ONE,
    CMD_TOGGLE_EXEC_ALL,
    CMD_TOGGLE_EXEC_ONE,
    CMD_PWM,
    CMD_UNSUPPORTED
};

typedef struct 
{
    enum supported_command cmd;
    size_t args_count;
    uint32_t arg1;
    uint32_t arg2;
} parser_data_t;


parser_data_t ParseLine(const char * line);
/* used to debugging */
const char * CommandToString(enum supported_command cmd);

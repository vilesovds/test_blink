#include "parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char * toggleAllStr= "toggleExecAll";
static const char * pwmStr = "pwm";
static const char * toggleExecStr = "toggleExec";
static const char * delayStr = "delay";
static const char * delayAllStr = "delayAll";

static const char * delimiter = ":";

const char * CommandToString(enum supported_command cmd)
{
    switch(cmd) {
        case CMD_DELAY_ONE:
            return "DELAY_ONE";
        case CMD_DELAY_ALL:
            return "DELAY_ALL";
        case CMD_UNSUPPORTED:
            return "UNSUPPORTED";
        case CMD_TOGGLE_EXEC_ALL:
            return "TOGGLE_EXEC_ALL";
        case CMD_TOGGLE_EXEC_ONE:
            return "TOGGLE_EXEC_ONE";
        case CMD_PWM:
            return "PWM";
    }
    return "Unknown";
}

parser_data_t ParseLine(const char * line)
{
    parser_data_t results = {.cmd = CMD_UNSUPPORTED, .args_count = 0};
    if (0 == strcmp(toggleAllStr, line)) {
        results.cmd = CMD_TOGGLE_EXEC_ALL;
    } else {
        size_t i = 0;
        char * token = strtok((char *)line, delimiter);
        while (token != NULL) {
            if (0 == i) {
                if (0 == strcmp(pwmStr, token)) {
                    results.cmd = CMD_PWM;
                } else if (0 == strcmp(toggleExecStr, token)) {
                    results.cmd = CMD_TOGGLE_EXEC_ONE;
                } else if (0 == strcmp(toggleAllStr, token)) {
                    results.cmd = CMD_TOGGLE_EXEC_ALL;
                } else if (0 == strcmp(delayAllStr, token)) {
                    results.cmd = CMD_DELAY_ALL;
                } else if (0 == strcmp(delayStr, token)) {
                    results.cmd = CMD_DELAY_ONE;
                }
            } else if (1 == i) {
                results.arg1 = atoi(token);
            } else if (2 == i) {
                results.arg2 = atoi(token);
            }
            results.args_count = i;
		    ++i;
		 	token = strtok(NULL, delimiter);
        }
    } 
    return results;
}

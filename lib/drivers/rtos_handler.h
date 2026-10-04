#pragma once

#ifdef FREERTOS_ENABLED

    #include "FreeRTOS.h"
    #include "task.h"
    #include "semphr.h"
    #ifndef USING_FREERTOS
    #define USING_FREERTOS
    #endif

#else
    #ifndef NO_RTOS
    #define NO_RTOS
    #endif
#endif
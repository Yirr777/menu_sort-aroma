#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void actInitialize(void);
    void actFinalize(void);
    uint8_t actGetSlotNo(void);
    uint32_t actGetPersistentIdEx(uint8_t slot);

#ifdef __cplusplus
}
#endif

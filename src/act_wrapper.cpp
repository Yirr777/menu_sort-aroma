#include <nn/act.h>
#include "act_wrapper.h"

void actInitialize(void)
{
    nn::act::Initialize();
}

void actFinalize(void)
{
    nn::act::Finalize();
}

uint8_t actGetSlotNo(void)
{
    return nn::act::GetSlotNo();
}

uint32_t actGetPersistentIdEx(uint8_t slot)
{
    return nn::act::GetPersistentIdEx(slot);
}

#include "controller.h"

static PadState g_pad;

void controllerInit()
{
    padConfigureInput(
        1,
        HidNpadStyleTag_NpadHandheld |
        HidNpadStyleTag_NpadFullKey |
        HidNpadStyleTag_NpadJoyDual |
        HidNpadStyleTag_NpadJoyLeft |
        HidNpadStyleTag_NpadJoyRight
    );

    padInitializeDefault(&g_pad);
}

void controllerUpdate()
{
    padUpdate(&g_pad);
}

PadState* controllerGetPad()
{
    return &g_pad;
}

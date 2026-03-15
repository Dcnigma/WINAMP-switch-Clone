#pragma once
#include <switch.h>

void controllerInit();
void controllerUpdate();

PadState* controllerGetPad();

void controllerHandlePlayerControls();

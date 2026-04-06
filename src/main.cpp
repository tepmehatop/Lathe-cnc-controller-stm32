/**
 * @file  main.cpp
 * @brief ELS — Электронная Гитара Токарного Станка
 *        Точка входа Arduino (STM32duino)
 */

#include <Arduino.h>
#include "Core/els_main.h"

void setup() {
    ELS_Init();
}

void loop() {
    ELS_Loop();
}

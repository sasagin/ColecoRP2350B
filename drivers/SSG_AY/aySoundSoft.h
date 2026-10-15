#pragma once
#include "inttypes.h"
#include "stdbool.h" 

void AY_select_reg(uint8_t N_reg);
uint8_t AY_get_reg();
void AY_set_reg(uint8_t val);
// void set_joy_val(uint8_t joy_in1,uint8_t joy_in2);
// void tape_in_load(uint8_t val);
uint16_t get_AY_Out(uint8_t delta);
void AY_reset();




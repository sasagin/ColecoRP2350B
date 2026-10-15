#include "aySoundSoft.h"
#include "stdbool.h"
#include "string.h"
#include "inttypes.h"
#include "hardware/gpio.h"

#define MAX_AMPLS 2 

static uint8_t N_sel_reg=0;			
// static uint8_t c_chip=0;
// static uint8_t sound_mode=0;
// static uint8_t ampls_mode=0;
uint8_t outs[3];

static uint8_t voltbl1[MAX_AMPLS][32] = {
  {0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09,
   0x0B, 0x0D, 0x0F, 0x12,
   0x16, 0x1A, 0x1F, 0x25, 0x2D, 0x35, 0x3F, 0x4C, 0x5A, 0x6A, 0x7F, 0x97,
   0xB4, 0xD6, 0xEB, 0xFF},
  {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x05, 0x05, 0x07, 0x07,
   0x0B, 0x0B, 0x0F, 0x0F,
   0x16, 0x16, 0x1F, 0x1F, 0x2D, 0x2D, 0x3F, 0x3F, 0x5A, 0x5A, 0x7F, 0x7F,
   0xB4, 0xB4, 0xFF, 0xFF}
};

uint8_t* ampls0 = voltbl1[0];

typedef struct ay_regs_t {
	uint16_t ay_R1_R0;
	uint16_t ay_R3_R2;
	uint16_t ay_R5_R4;
	uint8_t ay_R6;
	uint8_t ay_R7;
	uint8_t ay_R8;
	uint8_t ay_R9;
	uint8_t ay_R10;
	uint16_t ay_R12_R11;
	uint32_t ay_R12_R11_sh;
	uint8_t ay_R13;
	uint8_t ay_R14;
	uint8_t ay_R15;

	bool is_envelope_begin;
	bool bools[4];
	uint32_t main_ay_count_env;
	uint32_t chA_count;
	uint32_t chB_count;
	uint32_t chC_count;
	uint32_t noise_ay_count;
	uint8_t ampl_ENV;
	bool is_env_inv_enum; //инверсия последовательности огибающей
	uint32_t envelope_ay_count;
	uint8_t joy_input[2];

} ay_regs_t;

static ay_regs_t chip;

void AY_select_reg(uint8_t N_reg){
	N_sel_reg=N_reg;
}

void AY_reset() {
	//printf("AY RST\n");
	chip.ay_R1_R0=0;
	chip.ay_R3_R2=0;
	chip.ay_R5_R4=0;
	chip.ay_R6=0;
	chip.ay_R7=0xff;
	chip.ay_R8=0;
	chip.ay_R9=0;
	chip.ay_R10=0;
	chip.ay_R12_R11=0;
	chip.ay_R12_R11_sh=0;
	chip.ay_R13=0;
	chip.ay_R14=0xff;
	chip.ay_R15=0;

	chip.is_envelope_begin=false;
	chip.bools[0]=false;
	chip.bools[1]=false;
	chip.bools[2]=false;
	chip.bools[3]=false;
	chip.chA_count=0;
	chip.chB_count=0;
	chip.chC_count=0;
	chip.noise_ay_count=0;
	chip.main_ay_count_env=0;
	chip.ampl_ENV=0;
	chip.is_env_inv_enum=true;
	chip.envelope_ay_count=0;
	chip.joy_input[0] = 0;
	chip.joy_input[1] = 0;
};
// save buttons active joystick
// void set_joy_val(uint8_t joy_in1,uint8_t joy_in2){
// 	chip.joy_input[0] = joy_in1;
// 	chip.joy_input[1] = joy_in2;
// }
// tape in to A.7 PSG
// void tape_in_load(uint8_t val){
// 	chip.ay_R14 = ((chip.ay_R14 & 0x7f) | (val));
// }

uint8_t AY_get_reg(){

	uint8_t j0 = chip.joy_input[0];
	uint8_t j1 = chip.joy_input[1];

	switch (N_sel_reg){
		case 0: return (chip.ay_R1_R0);
		case 1: return (chip.ay_R1_R0>>8);
		case 2: return (chip.ay_R3_R2);
		case 3: return (chip.ay_R3_R2>>8);
		case 4: return (chip.ay_R5_R4);
		case 5: return (chip.ay_R5_R4>>8);
		case 6: return chip.ay_R6;
		case 7: return chip.ay_R7;
		case 8: return chip.ay_R8;
		case 9: return chip.ay_R9;
		case 10: return chip.ay_R10;
		case 11: return (chip.ay_R12_R11);
		case 12: return (chip.ay_R12_R11>>8);
		case 13: return chip.ay_R13;
		case 14:
				// j0 = (chip.ay_R15 & 0x10)? 0xFF:j0;
				// j0 = (j0 & ((chip.ay_R15<<4) | 0xCF));
				// j1 = (chip.ay_R15 & 0x20)? 0xFF:j1;
				// j1 = (j1 & ((chip.ay_R15<<2) | 0xCF));

				// chip.ay_R14 = chip.ay_R14 & 0x80;		//save bit tape_in

		 		// return chip.ay_R14 |= ((chip.ay_R15 & 0x40)?j1:j0) & 0x7F;
				// return 0xff;
				return chip.ay_R14;

		case 15: return chip.ay_R15;
		default: return 0;
	}	
};

void AY_set_reg(uint8_t val){
	switch (N_sel_reg){
		case 0:
		chip.ay_R1_R0=(chip.ay_R1_R0&0xff00)|val;
		break;
		case 1:
		chip.ay_R1_R0=(chip.ay_R1_R0&0xff)|((val&0xf)<<8);
		break;
		case 2:
		chip.ay_R3_R2=(chip.ay_R3_R2&0xff00)|val;
		break;
		case 3:
		chip.ay_R3_R2=(chip.ay_R3_R2&0xff)|((val&0xf)<<8);
		break;
		case 4:
		chip.ay_R5_R4=(chip.ay_R5_R4&0xff00)|val;
		break;
		case 5:
		chip.ay_R5_R4=(chip.ay_R5_R4&0xff)|((val&0xf)<<8);
		break;
		case 6:
		chip.ay_R6=val&0x1f;
		break;
		case 7:
		chip.ay_R7=val;
		break;
		case 8:
		chip.ay_R8=val&0x1f;
		break;
		case 9:
		chip.ay_R9=val&0x1f;
		break;
		case 10:
		chip.ay_R10=val&0x1f;
		break;
		case 11:
		chip.ay_R12_R11=(chip.ay_R12_R11&0xff00)|val;
		chip.ay_R12_R11_sh=chip.ay_R12_R11<<1;
		break;
		case 12:
		chip.ay_R12_R11=(chip.ay_R12_R11&0xff)|(val<<8);
		chip.ay_R12_R11_sh=chip.ay_R12_R11<<1;
		break;
		case 13:
		chip.ay_R13=val&0xf;
		chip.is_envelope_begin=true;
		break;
		case 14:
		chip.ay_R14=val;
		break;
		case 15:
		chip.ay_R15 = val;
		break;
		default:
		break;
	}	
};

//------------------------
bool get_random(){
	static uint32_t S = 0x00000001;
	if (S & 0x00000001) {
		S = ((S ^ 0xea000001) >> 1) | 0x80000000;
		return true;
	} else {
		S >>= 1;
		return false;
	};	
}

uint16_t get_AY_Out(uint8_t delta){
	/*
		N регистра	Назначение или содержание	Значение	
		0, 2, 4 	Нижние 8 бит частоты голосов А, В, С 	0 - 255
		1, 3, 5 	Верхние 4 бита частоты голосов А, В, С 	0 - 15
		6 			Управление частотой генератора шума 	0 - 31
		7 			Управление смесителем и вводом/выводом 	0 - 255
		8, 9, 10 	Управление амплитудой каналов А, В, С 	0 - 15
		11 			Нижние 8 бит управления периодом пакета 	0 - 255
		12 			Верхние 8 бит управления периодом пакета 	0 - 255
		13 			Выбор формы волнового пакета 	0 - 15
		14, 15 		Регистры портов ввода/вывода 	0 - 255
		
		R7
		
		7 	  6 	  5 	  4 	  3 	  2 	  1 	  0
		порт В	порт А	шум С	шум В	шум А	тон С	тон В	тон А
		управление
		вводом/выводом	выбор канала для шума	выбор канала для тона
	*/
	
	//копирование прошлого значения каналов для более быстрой работы
	
	register bool chA_bitOut=(chip.bools[0]);
	register bool chB_bitOut=(chip.bools[1]);
	register bool chC_bitOut=(chip.bools[2]);
	
	//#define nR7 (~chips[g_chip].ay_R7)
	
	uint8_t nR7=(~chip.ay_R7);
	
	/*
		Если установлен "ТОН" в канале X, то 
		{
		увеличиваем chX_count на 5(delta);
		Если ( (chX_count >= значений в регистрах R1_R0(делитель частоты)) И (R1_R0(делитель частоты >=5(delta))
		{
		
		}
		}
	*/
	
	//nR7 - инвертированый R7 для прямой логики - 1 Вкл, 0 - Выкл
	
	if (nR7&0x1) {
		chip.chA_count+=delta;
		if ((chip.chA_count>=chip.ay_R1_R0) && (chip.ay_R1_R0>=delta)){
			(chip.bools[0])^=1;
			chip.chA_count=chip.chA_count-chip.ay_R1_R0;
		}
	} else {
		chA_bitOut=1;
		chip.chA_count=0;
	}; /*Тон A*/
	if (nR7&0x2) {
		chip.chB_count+=delta;
		if ((chip.chB_count>=chip.ay_R3_R2) && (chip.ay_R3_R2>=delta) ) {
			(chip.bools[1])^=1;
			chip.chB_count=chip.chB_count-chip.ay_R3_R2;
		}
	} else {
		chB_bitOut=1;
		chip.chB_count=0;
	}; /*Тон B*/
	if (nR7&0x4) {
		chip.chC_count+=delta;
		if (chip.chC_count>=chip.ay_R5_R4 && (chip.ay_R5_R4>=delta) ) {
			(chip.bools[2])^=1;
			chip.chC_count=chip.chC_count-chip.ay_R5_R4;
		}
	} else {
		chC_bitOut=1;
		chip.chC_count=0;
	}; /*Тон C*/
	
	//проверка запрещения тона в каналах
	if (chip.ay_R7&0x1) chA_bitOut=1; 
	if (chip.ay_R7&0x2) chB_bitOut=1;
	if (chip.ay_R7&0x4) chC_bitOut=1;
	
	//добавление шума, если разрешён шумовой канал
	if (nR7&0x38){//есть шум хоть в одном канале
		chip.noise_ay_count+=delta;
		if (chip.noise_ay_count>=(chip.ay_R6<<1)) {(chip.bools[3])=get_random();chip.noise_ay_count=0;}//отдельный счётчик для шумового // R6 - частота шума
		if(!(chip.bools[3])){//если бит шума ==1, то он не меняет состояние каналов
			if ((chA_bitOut)&&(nR7&0x08)) chA_bitOut=0;//шум в канале A
			if ((chB_bitOut)&&(nR7&0x10)) chB_bitOut=0;//шум в канале B
			if ((chC_bitOut)&&(nR7&0x20)) chC_bitOut=0;//шум в канале C
		};
	}
	
	//вычисление амплитуды огибающей
	if ((chip.ay_R8&0x10)|(chip.ay_R9&0x10)|(chip.ay_R10&0x10)){
		
		chip.main_ay_count_env+=delta;
		
		#define env_count_32 (chip.envelope_ay_count&0x01f)
		
		if (chip.is_envelope_begin) {chip.envelope_ay_count=0;chip.main_ay_count_env=0;chip.is_envelope_begin=false;chip.is_env_inv_enum=true;};
		
		if (((chip.main_ay_count_env)>=(chip.ay_R12_R11<<1))){//без операции деления
			
			chip.envelope_ay_count++;
			chip.main_ay_count_env-=chip.ay_R12_R11<<1;
			if (env_count_32==0) chip.is_env_inv_enum=!chip.is_env_inv_enum;
			switch (chip.ay_R13){
				case (0b0000):
				case (0b0001):
				case (0b0010):
				case (0b0011):
				case (0b1001):
				if (chip.envelope_ay_count<32) chip.ampl_ENV=ampls0[31-env_count_32]; else {chip.ampl_ENV=ampls0[0];};
				break;
				case (0b0100):
				case (0b0101):
				case (0b0110):
				case (0b0111):
				case (0b1111):
				if (chip.envelope_ay_count<32) chip.ampl_ENV=ampls0[env_count_32]; else {chip.ampl_ENV=ampls0[0];}
				break;
				case (0b1000):
				chip.ampl_ENV=ampls0[31-env_count_32]; 
				break;
				case (0b1100):
				chip.ampl_ENV=ampls0[env_count_32]; 
				break;
				case (0b1010):
				if (chip.is_env_inv_enum) chip.ampl_ENV=ampls0[31-env_count_32]; else chip.ampl_ENV=ampls0[env_count_32];
				break;
				case (0b1110):
				if (!chip.is_env_inv_enum) chip.ampl_ENV=ampls0[31-env_count_32]; else chip.ampl_ENV=ampls0[env_count_32];
				break;
				case (0b1011):
				if (chip.envelope_ay_count<32) chip.ampl_ENV=ampls0[31-env_count_32]; else {chip.ampl_ENV=ampls0[31];}
				break;
				case (0b1101):
				if (chip.envelope_ay_count<32) chip.ampl_ENV=ampls0[env_count_32]; else {chip.ampl_ENV=ampls0[31];}
				break;
				default:
				break;
			}
		}
	}
// output channel	
	outs[0]=chA_bitOut?((chip.ay_R8&0xF0)?chip.ampl_ENV:ampls0[chip.ay_R8<<1]):0; //outA
	outs[1]=chB_bitOut?((chip.ay_R9&0xF0)?chip.ampl_ENV:ampls0[chip.ay_R9<<1]):0; //outB
	outs[2]=chC_bitOut?((chip.ay_R10&0xF0)?chip.ampl_ENV:ampls0[chip.ay_R10<<1]):0; //outC

	return outs[0] + outs[1] + outs[2];	
};
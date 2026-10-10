#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
// #include "hardware/i2c.h"
#include "hardware/timer.h"

#include "hardware/vreg.h"
#include <hardware/structs/qmi.h>
#include <hardware/structs/xip.h>

#include "ff.h"
#include "f_util.h"

#include "Z80pinout.h"
#include "VDP_int_Z80.pio.h"

/* JOYPAD BUTTONS

Button      1REG        2REG
--------------------------------
"UP"        0xFE
"RIGHT"     0xFD
"DOWN"      0xFB
"LEFT"      0xF7
"FIRE"      0xBF
"ARM"                   0xBF
"1"                     0xFD
"2"                     0xF7
"3"                     0xFC
"4"                     0xF2
"5"                     0xF3
"6"                     0xFE
"7"                     0xF5
"8"                     0xF1    
"9"                     0xFB
"0"                     0xFA
"Rs"                    0xF6

*/

// NES-compatible gamepad (shift register) pins
#define PIN_NES_JOYPAD_CLOCK 32
#define PIN_NES_JOYPAD_LATCH 33
#define PIN_NES_JOYPAD_DATA1  34
#define PIN_NES_JOYPAD_DATA2  35
#define NES_JOYPAD_SHIFT_COUNT 8

//RESET
#define RESET_PIN      44
#define RESET_MASK (1ULL << RESET_PIN)

// /VDP_RD 
#define VDP_RD      36
#define VDP_RD_MASK (1ULL << VDP_RD)

// /VDP_WR 
#define VDP_WR      37
#define VDP_WR_MASK (1ULL << VDP_WR)

// /AUD_W
#define AUD_W       39
#define AUD_W_MASK (1ULL << AUD_W)

#define ENCODER_IN_A    43
#define ENCODER_IN_A_MASK   (1ULL << ENCODER_IN_A)

#define ENCODER_IN_B    45
#define ENCODER_IN_B_MASK   (1ULL << ENCODER_IN_B)

#define VDP_INT_PIO     pio0
#define VDP_INT_SM      0


bool encoderFront;
uint8_t encoder1;
uint8_t encoder2;
int encoder_counter = 0;

uint8_t __aligned(4) coleco_bios[8192];
uint8_t __aligned(4) coleco_cartrige[0x20000];
uint8_t __aligned(4) coleco_ram[1024];
uint8_t __aligned(4) SGM_ram[32768];

size_t romsize;
static unsigned char* rom_name;

uint8_t upper_bank = 1;
uint8_t lower_bank = 0;
bool page0 = false;
bool page1 = false;

volatile uint8_t reg_1 = 0x01; //
volatile uint8_t reg_2 = 0x02; //

static uint8_t coleco_joysticks [4] = {0xff,0xff,0xff,0xff};
volatile uint8_t data_joysticks[4];
// uint8_t *joy_data = data_joysticks;
bool en_1;
volatile bool start_read_joypad;

void inInit(uint gpio) {
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_IN);
    gpio_pull_up(gpio);
}

void outInit(uint gpio,bool set_up_out) {
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_OUT);
    gpio_put(gpio, set_up_out);
}

// Read NES-compatible gamepad via latch/clocked serial interface.
// Returns an active-high button mask after converting from active-low bits.
uint16_t read_joystick_data() {
    uint8_t serial_data1 = 0;
    uint8_t serial_data2 = 0;
    uint16_t out_data;

    // Latch current button states into the controller's shift register
    gpio_put(PIN_NES_JOYPAD_LATCH, 1);
    busy_wait_us(20);
    gpio_put(PIN_NES_JOYPAD_LATCH, 0);
    busy_wait_us(20);

    // Shift in 8 bits (standard NES controller protocol)
    for (int i = 0; i < NES_JOYPAD_SHIFT_COUNT; i++) {
        gpio_put(PIN_NES_JOYPAD_CLOCK, 0);
        busy_wait_us(20);
        serial_data1 <<= 1;
        serial_data1 |= gpio_get(PIN_NES_JOYPAD_DATA1);
        serial_data2 <<= 1;
        serial_data2 |= gpio_get(PIN_NES_JOYPAD_DATA2);       
        gpio_put(PIN_NES_JOYPAD_CLOCK, 1);
        busy_wait_us(20);
    }
    out_data = (serial_data2<<8)|serial_data1;
    return out_data;
};

//convert NES to COLECO joypad
#define     NES_UP              (1 << 3)
#define     NES_DOWN            (1 << 2)
#define     NES_LEFT            (1 << 1)
#define     NES_RIGHT           (1 << 0)
#define     NES_A               (1 << 7)
#define     NES_B               (1 << 6)
#define     NES_SELECT          (1 << 4)
#define     NES_START           (1 << 5)

#define     UP_BUTTON        0xFE
#define     RIGHT_BUTTON     0xFD
#define     DOWN_BUTTON      0xFB
#define     LEFT_BUTTON      0xF7
#define     FIRE_BUTTON      0xBF
#define     ARM_BUTTON       0xBF
#define     BUTTON_1         0xFD
#define     BUTTON_2         0xF7
#define     BUTTON_3         0xFC
#define     BUTTON_4         0xF2
#define     BUTTON_5         0xF3
#define     BUTTON_6         0xFE
#define     BUTTON_7         0xF5
#define     BUTTON_8         0xF1    
#define     BUTTON_9         0xFB
#define     BUTTON_0         0xFA
#define     BUTTON_Rs        0xF6
#define     BUTTON_STAR      0xF9

void convert_nes_to_coleco_joypad(uint8_t nes_joy_data,uint8_t num_joypad){

    // Convert active-low layout from controller: pressed -> clear bit.
    uint8_t indx = num_joypad*2;

    coleco_joysticks[0+indx] = 0xFF;
    coleco_joysticks[1+indx] = 0xFF;

    if (((nes_joy_data & NES_START)|(nes_joy_data & NES_UP)) == 0) {coleco_joysticks[1+indx] &= BUTTON_1; return;}
    if (((nes_joy_data & NES_START)|(nes_joy_data & NES_DOWN)) == 0) {coleco_joysticks[1+indx] &= BUTTON_2; return;}
    if (((nes_joy_data & NES_START)|(nes_joy_data & NES_LEFT)) == 0) {coleco_joysticks[1+indx] &= BUTTON_3; return;}
    if (((nes_joy_data & NES_START)|(nes_joy_data & NES_RIGHT)) == 0) {coleco_joysticks[1+indx] &= BUTTON_4; return;}
    if (((nes_joy_data & NES_START)|(nes_joy_data & NES_A)) == 0) {coleco_joysticks[1+indx] &= BUTTON_5; return;}
    if (((nes_joy_data & NES_START)|(nes_joy_data & NES_B)) == 0) {coleco_joysticks[1+indx] &= BUTTON_6; return;}        

    if (((nes_joy_data & NES_SELECT)|(nes_joy_data & NES_UP)) == 0) {coleco_joysticks[1+indx] &= BUTTON_7; return;}
    if (((nes_joy_data & NES_SELECT)|(nes_joy_data & NES_DOWN)) == 0) {coleco_joysticks[1+indx] &= BUTTON_8; return;} 
    if (((nes_joy_data & NES_SELECT)|(nes_joy_data & NES_LEFT)) == 0) {coleco_joysticks[1+indx] &= BUTTON_9; return;} 
    if (((nes_joy_data & NES_SELECT)|(nes_joy_data & NES_RIGHT)) == 0) {coleco_joysticks[1+indx] &= BUTTON_0; return;} 
    if (((nes_joy_data & NES_SELECT)|(nes_joy_data & NES_A)) == 0) {coleco_joysticks[1+indx] &= BUTTON_STAR; return;} 
    if (((nes_joy_data & NES_SELECT)|(nes_joy_data & NES_B)) == 0) {coleco_joysticks[1+indx] &= BUTTON_Rs; return;} 
    
    if ((nes_joy_data & NES_UP) == 0) {coleco_joysticks[0+indx] &= UP_BUTTON;}
    if ((nes_joy_data & NES_DOWN) == 0) {coleco_joysticks[0+indx] &= DOWN_BUTTON;}
    if ((nes_joy_data & NES_LEFT) == 0) {coleco_joysticks[0+indx] &= LEFT_BUTTON;}
    if ((nes_joy_data & NES_RIGHT) == 0) {coleco_joysticks[0+indx] &= RIGHT_BUTTON;}
    if ((nes_joy_data & NES_B) == 0) {coleco_joysticks[0+indx] &= FIRE_BUTTON;}

    if ((nes_joy_data & NES_A) == 0) coleco_joysticks[1+indx] &= ARM_BUTTON;

}

void Z80_pin_setup() {
    inInit(MREQ_PIN);
    inInit(WR_PIN);
    inInit(RD_PIN);
    inInit(IORQ_PIN);
    // inInit(M1_PIN);
    inInit(PIN_NES_JOYPAD_DATA1);
    inInit(PIN_NES_JOYPAD_DATA2);    

    inInit(ENCODER_IN_A);
    inInit(ENCODER_IN_B);

    outInit(RESET_PIN,0);
    outInit(WAIT_PIN, 1);
    // outInit(NMI_PIN, 1);
    outInit(INT_PIN, 1);
    outInit(VDP_RD,1);
    outInit(VDP_WR,1);
    outInit(AUD_W,1);
    outInit(PIN_NES_JOYPAD_CLOCK,1);
    outInit(PIN_NES_JOYPAD_LATCH,1);
}
// selector VDP_INT to NMI for coleco mode
void VDP_INT_init() {
    PIO piopio = VDP_INT_PIO;
    uint smsm  = VDP_INT_SM;
    pio_set_gpio_base(piopio, 16);
    const uint pio_offset = pio_add_program(piopio, &vdpintz80_program);
    vdpintz80_program_init(piopio, smsm, pio_offset);
    pio_sm_set_enabled(piopio, smsm, true);
}

// Z80 write-side address decode: ioport,.
__always_inline static inline void write_coleco_io(const uint16_t address, const uint8_t value) {
                    uint8_t portIO = (uint8_t)(address & 0x00ff);

                    if (portIO == 0x53) { page1 = (value & 0x01); }            // upper page RAM 0x4000 - 0x7FFF
                    if (portIO == 0x7F) { page0 = (~value & 0x02); }           // lower page RAM 0x0000 - 0x3FFF 

                    portIO = portIO & 0xE0;
                    if(portIO == 0xC0) {en_1 = true; }         
                    if(portIO == 0x80) {en_1 = false;
                        // start_read_joypad=true;
                    }
    // pin WR for  pico VDP 9918                     
                    if(portIO == 0xA0) {gpio_put(VDP_WR,0);}
    // pin WR for  SN76489                     
                    if(portIO == 0xE0) {gpio_put(AUD_W,0);}

}

// Z80 read-side address decode: RAM, ROM banks,and BIOS.
__always_inline static inline uint8_t read_coleco_memory(const uint16_t address) {

        switch (address & 0xE000)
    {
        case 0x0000:
        {
            return page0 ? SGM_ram[address] : coleco_bios[address];
        }
        case 0x2000:
        case 0x4000:
        {
            return page1 ? SGM_ram[address] : 0xFF;
        }
        case 0x6000:
        {
            return page1 ? SGM_ram[address] : coleco_ram[address & 0x03FF];
        }
        case 0x8000:
        case 0xA000:
        case 0xC000:
        case 0xE000:
        {
            // if(address&0xbfe0 == 0xbfe0 ){lower_bank = (uint8_t)address & CNT_BANKS;}     // select lower bank memory 
            if(lower_bank != 0){
                if((address&0xffc0) == 0xffc0 ){upper_bank = (uint8_t)address & 0x0007;
                    reg_2 = upper_bank;
                    return 0xff;}     // select upper bank memory 
            }else{
//-------------------------- CartridgeActivisionCart               
                // if(address >= 0xFF90 ) {                
                //     if ((address == 0xFF90) || (address == 0xFFA0) || (address == 0xFFB0)){
                //        upper_bank = (address >> 4) & 0x0003;
                //        reg_2 = upper_bank; 
                //     }                   
                //     return 0xff;}     // select upper bank memory   
            }
//-----------------------------
            // 0x8000-0xFFFF: ROM Cartrige
            if ((address & 0xC000) == 0x8000) {
        
            uint32_t bank_address = (uint32_t)(address&0x3fff)+(uint32_t)((lower_bank)<<14);
            return coleco_cartrige[bank_address];
            }

            if ((address & 0xC000) == 0xC000) {
                uint32_t bank_address = (uint32_t)(address&0x3fff)+(uint32_t)((upper_bank)<<14);                 // upper bank
                return coleco_cartrige[bank_address];
            }
        }
    // Default open-bus value
    default:
    return 0xFF;
    }
}

__always_inline static inline uint8_t write_coleco_memory(const uint16_t address,const uint8_t value){
    switch (address & 0xE000)
    {
        case 0x0000:
        {
            if (page0)
                SGM_ram[address] = value;
            break;
        }
        case 0x2000:
        case 0x4000:
        {
            if (page1)
                SGM_ram[address] = value;
            break;
        }
        case 0x6000:
        {
            if (page1)
                SGM_ram[address] = value;
            else
                coleco_ram[address & 0x03FF] = value;
            break;
        }
        case 0xE000:{
                if(address >= 0xFF90 ) {                
                    if ((address == 0xFF90) || (address == 0xFFA0) || (address == 0xFFB0)){
                       upper_bank = (address >> 4) & 0x0003;
                       reg_2 = upper_bank; 
                    }
                }            
        }
        // case 0x8000:
        break;
    }
}

void __time_critical_func(Z80_loop)() {
    while(true){
            gpio_set_dir_in_masked(DATA_MASK);

        //------------memory ok-----------------

        while(!(gpio_get_all64() & MREQ_MASK)) {                                    // memory read write
                uint16_t address = (uint16_t)((gpio_get_all()) & 0x0000ffff);  
                while(!(gpio_get_all() & READ_MASK)) {                                  //  read from         
                    const uint32_t data = read_coleco_memory(address) << 16;
                    gpio_put_masked(DATA_MASK,data);
                    gpio_set_dir_out_masked(DATA_MASK);
                }

            gpio_set_dir_in_masked(DATA_MASK); 
                            
                while(!(gpio_get_all() & WRITE_MASK)) {                                 // write to ram                            
                        // : Goleco RAM
                    uint8_t data_to_ram = (uint8_t)((gpio_get_all() >> 16) & 0x000000FF);
                    write_coleco_memory(address,data_to_ram);                 
                }         
        }
    
    // io port
        while(!(gpio_get_all64() & IORQ_MASK)) {
            uint16_t address = (gpio_get_all()) ;              // mask io port address
            uint8_t portIO = (uint8_t)(0x00FF & address);

    // read from IO             
            while(!(gpio_get_all() & READ_MASK)) {                              // read from IO

                    if((portIO & 0xE2) == 0xE0) {                              // first joypad == 0xE0/ second == 0xE2
                        uint8_t temp;
                        if(en_1){temp = data_joysticks[0];}else{temp = data_joysticks[1];}  
                        const uint32_t data = ((uint32_t)temp) << 16 ; 
                        gpio_put_masked(DATA_MASK,data);           
                        gpio_set_dir_out_masked(DATA_MASK);
                        } 
                    if((portIO & 0xE2) == 0xE2) {                              // first joypad == 0xE0/ second == 0xE2
                        uint8_t temp;
                        if(en_1){temp = data_joysticks[2];}else{temp = data_joysticks[3];}                         
                        const uint32_t data = ((uint32_t)temp) << 16 ;
                        gpio_put_masked(DATA_MASK,data);           
                        gpio_set_dir_out_masked(DATA_MASK);
                        }
    // pin RD for  pico VDP 9918                                                  
                    if((portIO & 0xE0) == 0xA0) {gpio_put(VDP_RD,0);}                                                 
            }
    // write to IO        
            while(!(gpio_get_all64() & WRITE_MASK)) {                              // write to IO

                uint8_t datawritetoio = (uint8_t)((gpio_get_all() >> 16)&0x000000ff);
                write_coleco_io(address,datawritetoio);
                }
            gpio_set_dir_in_masked(DATA_MASK);

        }
        gpio_put(VDP_WR,1);
        gpio_put(VDP_RD,1);
        gpio_put(AUD_W,1);
    }
}

uint8_t checkROM(size_t application_len ){
        uint32_t headerROM = 0;
        uint32_t offsetROM = 0;
        uint8_t start_bank = 0;
        uint16_t lenght_rom = (uint16_t)application_len; 
        bool ROMisOK;

        uint8_t bank_qntt = lenght_rom / 0x4000 ;     
        headerROM = coleco_cartrige[offsetROM + 1] | (coleco_cartrige[offsetROM + 0] << 8);
        ROMisOK = (headerROM == 0xAA55) || (headerROM == 0x55AA);

        if(ROMisOK){start_bank = 0;  return start_bank; }
        else if(lenght_rom > 0x8000){

                offsetROM = lenght_rom - 0x4000;
           
                headerROM = coleco_cartrige[offsetROM + 1] | (coleco_cartrige[offsetROM + 0] << 8);
                ROMisOK = (headerROM == 0xAA55) || (headerROM == 0x55AA);

                if(ROMisOK){start_bank = bank_qntt - 1;return start_bank; }
        }

    }

// Обработчик таймера 8ms
bool my_timer_callback(struct repeating_timer *t) {
                start_read_joypad = true; 
    return true; // продолжаем повторение
}

int main() {


    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_60);

    qmi_hw->m[0].timing = 0x60007304; // 4x FLASH divisor

    sleep_ms(100);
    // if (!set_sys_clock_hz(CPU_FREQ_MHZ * MHZ, 0) ) {
        set_sys_clock_hz(315 * MHZ, 1); // fallback to failsafe clocks
    // }

    stdio_init_all();

    
	struct repeating_timer timer_8ms;
    
    // Запускаем таймер: интервал 8 мс, немедленный старт
    if (!add_repeating_timer_ms(-8, my_timer_callback, NULL, &timer_8ms)) {
        printf("Failed to start timer\n");
        return -1;
    }

    // clock for CPU
 

    // Initialize pins
    gpio_init_mask(ADDR_MASK | DATA_MASK);
    gpio_set_dir_in_masked(ADDR_MASK | DATA_MASK);
    Z80_pin_setup();

    // Initialize select out interrupt from VDP to Z80

    VDP_INT_init();

// sleep_ms(5000);    

FATFS fs;
FIL file_sd;
UINT br;
FRESULT res;

// 1. Монтируем SD
if (FR_OK != f_mount(&fs, "", 1)) {
    printf("SD Card not mounted!\n");
    while (1) {}  // вешаем плату, чтобы видеть ошибку
}

// ---------------------------------------------------------
// Загрузка BIOS (всегда фиксированный размер для Coleco)
// ---------------------------------------------------------
const size_t bios_size = 0x2000;  // 8 КБ — стандартный размер BIOS ColecoVision
res = f_open(&file_sd, "/COL/BIOS/main.rom", FA_READ);
if (res != FR_OK) {
    printf("Cannot open BIOS file!\n");
    while (1) {}
}
res = f_read(&file_sd, coleco_bios, bios_size, &br);
f_close(&file_sd);

if (res != FR_OK || br != bios_size) {
    printf("BIOS read error or size mismatch: expected %lu, got %lu\n",
           (unsigned long)bios_size, (unsigned long)br);
    while (1) {}
}
printf("BIOS loaded: %lu bytes\n", (unsigned long)br);

// ---------------------------------------------------------
// Загрузка ROM картриджа (универсально до 128 КБ)
// ---------------------------------------------------------
const char *rom_path = "/COL/CART/game.rom";  // путь к файлу
size_t max_rom_size = 131072;                // 128 КБ = 128 * 1024

// Сначала узнаём реальный размер файла на SD
FILINFO fno;
res = f_stat(rom_path, &fno);
if (res != FR_OK) {
    printf("ROM file not found: %s\n", rom_path);
    // continue;
}

size_t file_size = fno.fsize;
if (file_size == 0 || file_size > max_rom_size) {
    printf("Invalid ROM size: %lu bytes (max %lu)\n",
           (unsigned long)file_size, (unsigned long)max_rom_size);
    // continue;   
}

// Открываем и читаем ровно file_size байт
res = f_open(&file_sd, rom_path, FA_READ);
if (res != FR_OK) {
    printf("Cannot open ROM file!\n");
    // continue;
}
res = f_read(&file_sd, coleco_cartrige, file_size, &br);
f_close(&file_sd);

if (res != FR_OK || br != file_size) {
    printf("ROM read error: expected %lu, got %lu\n",
           (unsigned long)file_size, (unsigned long)br);
    // continue;
}
printf("ROM loaded: %lu bytes from %s\n", (unsigned long)file_size, rom_path);

// Теперь можно вызывать твой checkROM с реальным размером файла
lower_bank = checkROM(file_size);
if (lower_bank == 0) {
    upper_bank = 1;
} else {
    upper_bank = 0;
}


// START

printf("Start");

    multicore_launch_core1(Z80_loop);

    busy_wait_ms(250);
        
    gpio_put(RESET_PIN,1);    


encoder1 = 0x4f;

    while (true) {
    //decode joypad buttons

        if(start_read_joypad){
            // resieveI2C();
            uint16_t data_joy = read_joystick_data();
            convert_nes_to_coleco_joypad((uint8_t)(data_joy & 0x00ff),0);
            convert_nes_to_coleco_joypad((uint8_t)((data_joy & 0xff00)>>8),1);            
            // uint8_t temp = 0xff;
            for (size_t i = 0; i < 4; i++){
                data_joysticks[i] = coleco_joysticks[i] & 0x4F;
            }
            start_read_joypad = false;                       
        }
    
    // decode movement encoder
    if((gpio_get_all64() & ENCODER_IN_A_MASK) && encoderFront){
        encoderFront = false;
// encoder1 &= ~(1 << 7);              // D7        
        if(gpio_get_all64() & ENCODER_IN_B_MASK){
            encoder_counter++;
            if(encoder_counter > 6 ){
                encoder_counter = 0;
                encoder1 |= (1 << 5);               // D5 direction
                encoder1 |= (1 << 7);              // D7
                encoder1 |= (1 << 4);               // D4
                gpio_put(INT_PIN,0);
                busy_wait_us(100);
                gpio_put(INT_PIN,1);
                encoder1 &= ~(1 << 4);
            }       
        }else{
            encoder_counter--;
            if(encoder_counter < 0 ){
                encoder_counter = 6;
                encoder1 &= ~(1 << 5);              // direction
                encoder1 |= (1 << 7);              // D7
                encoder1 |= (1 << 4);               // D4            
                gpio_put(INT_PIN,0);
                busy_wait_us(100);
                gpio_put(INT_PIN,1);
                encoder1 &= ~(1 << 4);
            }
        }
    }
    if(!(gpio_get_all64() & ENCODER_IN_A_MASK)){
            encoderFront = true;
            encoder1 &= ~(1 << 7);            
    }

        data_joysticks[0] |= encoder1 & 0xB0;

        // sendI2C(~encoder_counter,data_joysticks[0]);
  
    }//while
}//main

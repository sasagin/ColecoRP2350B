#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
// #include "hardware/i2c.h"
#include "hardware/timer.h"
#include "hardware/dma.h"

#include "hardware/vreg.h"
#include <hardware/structs/qmi.h>
#include <hardware/structs/xip.h>

#include "ff.h"
#include "f_util.h"

#include "drivers/SSG_AY/aySoundSoft.h"

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

#define i2c_PCF8575_port (i2c0)
#define CLOCK_I2C_400kHz (400)
#define I2C_PCF8575_ADDR (0x20)
#define PICO_I2C_PCF8575_SDA_PIN (0)
#define PICO_I2C_PCF8575_SCL_PIN (1)

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

// /AUD_W
#define SGM_PSG       45
#define SGM_PSG_MASK (1ULL << SGM_PSG)

#define ENCODER_IN_A    42
#define ENCODER_IN_A_MASK   (1ULL << ENCODER_IN_A)

#define ENCODER_IN_B    43
#define ENCODER_IN_B_MASK   (1ULL << ENCODER_IN_B)

#define VDP_INT_PIO     pio0
#define VDP_INT_SM      0


bool encoderFront;
uint8_t encoder1;
uint8_t encoder2;
int encoder_counter = 0;

uint8_t __aligned(4) coleco_bios[8192];
uint8_t __aligned(4) coleco_cartrige[8][0x4000];
uint8_t __aligned(4) coleco_ram[1024];
uint8_t __aligned(4) SGM_ram[0x8000];

uint8_t upper_bank = 1;
uint8_t lower_bank = 0;
uint8_t bank_qntt;

bool SGM_page0 = false;
bool SGM_page1 = false;
bool SGM_on = 0;
uint16_t sample_to_send = 0;

// for test
volatile uint8_t reg_1 ; //
volatile uint8_t reg_2 ; //
volatile uint8_t reg_3 ; //
volatile uint16_t reg_4 ; //
volatile bool start_reg ;


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

// Z80 read-side address decode: ioport,.
__always_inline static inline uint8_t read_coleco_io(const uint16_t address){

    uint8_t portIO = (uint8_t)(address & 0x00ff);

    if (portIO == 0x52) { return AY_get_reg();}                         //SGM audio read register

    portIO &= 0xE2;
    if(portIO == 0xE0) {return en_1 ? data_joysticks[0] : data_joysticks[1]; }
    if(portIO == 0xE2) {return en_1 ? data_joysticks[2] : data_joysticks[3]; }

    return 0xFF;
}

// Z80 write-side address decode: ioport,.

__always_inline static inline void write_coleco_io(const uint16_t address, const uint8_t value) {

    uint8_t portIO = (uint8_t)(address & 0x00ff);
    //SGM                
        if (portIO == 0x50) { SGM_on=1; AY_select_reg(value); return;}           //SGM audio select register
        if (portIO == 0x51) {AY_set_reg(value); return;}                         //SGM audio set register        
        if (portIO == 0x53) { SGM_page1 = (value & 0x01); return;}              // upper page RAM 0x4000 - 0x7FFF
        if (portIO == 0x7F) { SGM_page0 = (~value & 0x02); return; }            // lower page RAM 0x0000 - 0x3FFF 

        portIO = portIO & 0xE0;
    //select joystick        
        if(portIO == 0xC0) {en_1 = true; return;}         
        if(portIO == 0x80) {en_1 = false;return;}
    // pin WR for  pico VDP 9918                     
        if(portIO == 0xA0) {gpio_put(VDP_WR,0);return;}
    // pin WR for  SN76489                     
        if(portIO == 0xE0) {gpio_put(AUD_W,0);return;}
}

// Z80 read-side address decode: RAM, ROM banks,and BIOS.
__always_inline static inline uint8_t read_coleco_memory(const uint16_t address) {
        switch (address & 0xE000)
    {
        case 0x0000:
            {
                return SGM_page0 ? SGM_ram[address] : coleco_bios[address];
            }
        case 0x2000:              
        case 0x4000:
            {             
                return SGM_page1 ? SGM_ram[address] : 0xFF;
            }        
        case 0x6000:
            {               
                return SGM_page1 ? SGM_ram[address] : coleco_ram[address & 0x03FF];
            }
        case 0x8000:
        case 0xA000:
            {
                return coleco_cartrige[lower_bank][address&0x3fff];     // 0x8000-0xBFFF: ROM Cartrige
            }
        case 0xC000:
        case 0xE000:
            {
            if(lower_bank != 0){
                if((address&0xffc0 ) == 0xffc0 ){upper_bank = (uint8_t)(address & (bank_qntt-1));} 
            }else if(address >= 0xFF90 ){
//-------------------------- CartridgeActivisionCart                      
                if ((address == 0xFF90) || (address == 0xFFA0) || (address == 0xFFB0)){upper_bank = (uint8_t)(address & (bank_qntt-1));}      
            }
//-----------------------------
            return coleco_cartrige[upper_bank][address&0x3fff];     // 0xС000-0xFFFF: ROM Cartrige
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
            if (SGM_page0) {SGM_ram[address] = value;}         
        }
        break;        
        case 0x2000:       
        case 0x4000:
        {
            if (SGM_page1){ SGM_ram[address] = value;}
        }
        break;
        case 0x6000:
        {
            if (SGM_page1){         
                SGM_ram[address] = value;
            } else {
                coleco_ram[address & 0x03FF] = value;         
                }
        }
        break;
        case 0x8000:
        case 0xC000:
        case 0xE000:
        break;
    }
}

void __time_critical_func(Z80_loop)() {
    while(true){
            gpio_set_dir_in_masked(DATA_MASK);

        //------------memory ok-----------------

        while(!(gpio_get_all64() & MREQ_MASK)) {                                    // memory read write
                uint16_t address = (uint16_t)((gpio_get_all()) & ADDR_MASK);  
                while(!(gpio_get_all() & READ_MASK)) {                                  //  read from         
                    const uint32_t data = (read_coleco_memory(address) << 16);
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
            uint16_t address = (uint16_t)((gpio_get_all()) & ADDR_MASK);              // mask io port address
            uint8_t portIO = (uint8_t)(0x00E0 & address);

    // read from IO             
            while(!(gpio_get_all() & READ_MASK)) {                              // read from IO  
                const uint32_t data = ((uint32_t)read_coleco_io(address)) << 16 ;
                if (portIO == 0xA0){                    // bus for vdp or io
                        gpio_put(VDP_RD,0);
                    }else{
                        gpio_put_masked(DATA_MASK,data);           
                        gpio_set_dir_out_masked(DATA_MASK);                        
                    }
            }
    // write to IO        
            while(!(gpio_get_all64() & WRITE_MASK)) {                              // write to IO
                if (portIO == 0xA0){
                    gpio_put(VDP_WR,0);
                }else{
                uint8_t datawritetoio = (uint8_t)((gpio_get_all() >> 16 )&0x000000ff);
                write_coleco_io(address,datawritetoio);
                }
            gpio_set_dir_in_masked(DATA_MASK);
            }
        }
        gpio_put(VDP_WR,1);
        gpio_put(VDP_RD,1);
        gpio_put(AUD_W,1);
    }
}

uint8_t checkROM( size_t  application_len ){
        uint32_t headerROM = 0;
        uint8_t start_bank = 0;
        uint32_t lenght_rom = (uint32_t)application_len; 
        bool ROMisOK;

        uint8_t bank_qntt = lenght_rom / 0x4000 ;     
        headerROM = coleco_cartrige[0][0] | (coleco_cartrige[0][1] << 8);
        ROMisOK = (headerROM == 0xAA55) || (headerROM == 0x55AA);
        printf("Lenght ROM = %05x   Header bank 0 = %04x\n",application_len, headerROM);

        if(ROMisOK){ start_bank = 0;  return start_bank;
         }else{
            headerROM = coleco_cartrige[bank_qntt-1][1] | (coleco_cartrige[bank_qntt-1][0] << 8);
            ROMisOK = (headerROM == 0xAA55) || (headerROM == 0x55AA);
            if(ROMisOK){start_bank = bank_qntt - 1;return start_bank; }
        }
        printf("ROM failure \n");
        return 0x00;
}

// void load_cartridge(uint8_t* rom_data, size_t total_size) {

//     const size_t BANK_SIZE = 0x4000; // 16 КБ
//     bank_qntt = total_size / BANK_SIZE ;
//     size_t remainder = total_size - (size_t)(bank_qntt * BANK_SIZE);
//     if(total_size <= 0x20000){
//         for (size_t i = 0; i < bank_qntt; i ++) { memcpy(coleco_cartrige[i],rom_data + (i*BANK_SIZE),BANK_SIZE);}

//         if(remainder != 0) {memcpy(coleco_cartrige[bank_qntt],rom_data + (bank_qntt*BANK_SIZE),remainder);}

//         lower_bank = checkROM(total_size);
//         if(lower_bank == 0){upper_bank = 1;} else { upper_bank = 0;}

//     }else{printf("ROM too big for... 128 kB \n");}
// }


// Обработчик прерывания таймера для SGM  PSG AY-8910

bool SGM_PSG_callback(struct repeating_timer *t) {      
        if(SGM_on){
            sample_to_send = (get_AY_Out(1)<<3)+0x10;
        }       
    return true; // Продолжаем повторение
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

        // Initialize pins
    gpio_init_mask(ADDR_MASK | DATA_MASK);
    gpio_set_dir_in_masked(ADDR_MASK | DATA_MASK);
    Z80_pin_setup();

    // Initialize select out interrupt from VDP to Z80

    VDP_INT_init();

// sleep_ms(3000);

        struct repeating_timer timer_8ms;           // timer for input
        struct repeating_timer timerAY;             // timer for SGM PSG AY8910
        
        // Период в микросекундах: 1 000 000 / 58 000 ≈ 17,24 мкс
        int64_t period_us = 1000000LL / 110000;
        // Запускаем таймер с заданным периодом
        if (!add_repeating_timer_us(-period_us, SGM_PSG_callback, NULL, &timerAY)) {
            printf("Failed to start timer\n");
            return -1;
        }
    
        // Запускаем таймер: интервал 8 мс, немедленный старт
        if (!add_repeating_timer_ms(-8, my_timer_callback, NULL, &timer_8ms)) {
            printf("Failed to start timer\n");
            return -1;
        }
//---------------
    const uint PWM_PIN = SGM_PSG;
    const float SYS_CLK = 315000000.0f;
    // const float PWM_FREQ = 44100.0f;
    const float PWM_FREQ = 88200.0f;
    // const uint SAMPLE_FREQ = 33000;

    // 1. Настройка PWM (44 кГц)
    gpio_set_function(PWM_PIN, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PWM_PIN);
    uint chan = pwm_gpio_to_channel(PWM_PIN);
    uint32_t top = (uint32_t)(SYS_CLK / PWM_FREQ) - 1;
    pwm_set_wrap(slice, top);
    pwm_set_enabled(slice, true);

    // Адрес регистра для записи уровня ШИМ (CC - Compare Counter)
    volatile void* pwm_cc_reg = (void*)&pwm_hw->slice[slice].cc;

    // 2. Настройка DMA
    int dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16); // 16 бит
    channel_config_set_read_increment(&c, false);          // Читаем из одного места
    channel_config_set_write_increment(&c, false);         // Пишем в один регистр
    
    // Синхронизация по таймеру (DREQ_TIMER0)
    int timer_dreq = dma_get_timer_dreq(0); 
    channel_config_set_dreq(&c, timer_dreq);

    // Настраиваем таймер на 33 кГц (315МГц / 10000 = 31.5кГц)
    // Либо используем встроенный механизм dma_timer:
    dma_timer_set_fraction(0, 1, 10000); // Настройка делителя таймера 0

    dma_channel_configure(
        dma_chan,
        &c,
        pwm_cc_reg,        // Куда (PWM CC register)
        &sample_to_send,   // Откуда (Ваша переменная)
        0xFFFFFFFF,        // Сколько раз (бесконечно в режиме зацикливания)
        true               // Старт
    );
    
    FATFS fs;
    FIL file_sd;
    UINT br;
    FRESULT res;      
 
        // Mount SD card filesystem
    if (FR_OK != f_mount(&fs, "", 1)) {
        // while (!stdio_usb_connected()) { tight_loop_contents(); }
        printf("SD Card not inserted or SD Card error!");
    }
    
        // Load BIOS
    res = f_open(&file_sd, "/COL/BIOS/main.rom", FA_READ);
    if (res != FR_OK) {
        printf("Cannot open BIOS!\n");
    }

    res = f_read(&file_sd, coleco_bios, 0x2000, &br);
    f_close(&file_sd);

    if (res != FR_OK || br == 0) {
        printf("BIOS read error!\n");
        // reset_usb_boot(0, 0);
    }
    printf("BIOS loaded: %u bytes\n", br);

    // Load program

    res = f_open(&file_sd, "/COL/CART/game.rom", FA_READ);
    if (res != FR_OK) {
        printf("Cannot open cartridge!\n");
        // return;
    }

    size_t cart_size = 0;
    for (int i = 0; i < 8; i++) {
        res = f_read(&file_sd, coleco_cartrige[i], 0x4000, &br);
        if (res != FR_OK || br == 0) break;
        cart_size += br;
        if (br < 0x4000) break;  // файл кончился
    }
    f_close(&file_sd);

    printf("Cartridge loaded: %u bytes, %u pages\n",cart_size, (cart_size + 0x3FFF) / 0x4000); 

    bank_qntt = (cart_size + 0x3FFF) / 0x4000;

    lower_bank = checkROM(cart_size);
    if(lower_bank == 0){upper_bank = 1;} else { upper_bank = 0;}
    // load_cartridge(thexder_rom,sizeof(thexder_rom));

    AY_reset();                 //reset SGM audio

    multicore_launch_core1(Z80_loop);

    busy_wait_ms(500);
        
    gpio_put(RESET_PIN,1);    

encoder1 = 0x4f;

//--------------------------------------
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


if(start_reg){     
printf("Lower bank = %02x   Upper bank = %02x \n", lower_bank, upper_bank);
start_reg = 0;}

//---------------------------------  
    }//while

}//main

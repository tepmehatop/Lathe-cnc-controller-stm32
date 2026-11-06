/**
 * @file  e2_core.cpp
 * @brief Ядро 7e2 (Arduino Mega) на STM32F407 — основной скетч.
 *
 * Это копия Core/e2/orig/7e2_Mod_LCD4.3_v1.3.ino. Изменено только то, что
 * касается железа AVR; каждое изменение помечено «[STM32]».
 *   - Параметры станка (ENC_LINE_PER_REV, SCREW_Z, ...) перенесены в Core/els_config.h.
 *   - Макросы портов/таймеров AVR реализованы в e2_hal.h, пины — в e2_pins.h.
 *   - ISR(...) стали обычными функциями e2_isr_*(): их вызывают прерывания STM32.
 * Остальные файлы скетча (Menu, Feed, Thread, ...) подключаются в конце
 * без изменений, в том же порядке, в каком их склеивает Arduino IDE.
 */
#include <Arduino.h>
#include "../els_config.h"
#include "e2_hal.h"
#include "e2_proto.h"
#include "e2_lcd.h"
#include "../../Drivers/drv_dro.h"
#include "../../Drivers/drv_scales.h"

// [STM32] Координаты от блока DRO / линеек — в тех же переменных, что DRO_HS800.ino
int32_t dro_pos_x = 0;
int32_t dro_pos_y = 0;
void e2_pos_update(void);
void e2_service(void);
void Display_Watch(void);

// [STM32] Механика, под которую рассчитаны таблицы конусов и резьб ниже (отметка gen_e2_tables.py).
// При другой механике (els_config.h или настройки с экрана) прошивка пересчитывает таблицы сама
// при старте — e2_mech_apply(); при этой — таблицы автора как есть.
#define E2_TABLES_ENC       1800    // линий энкодера
#define E2_TABLES_Z_STEPS   800     // шагов мотора Z на оборот винта (с микрошагом)
#define E2_TABLES_SCREW_Z   200     // шаг винта Z, 0.01 мм
#define E2_TABLES_X_STEPS   800
#define E2_TABLES_SCREW_X   100

/////////////////////////////////////////////////////////////////////////////////////////////////
// ***** Cone *****
struct cone_info_type
{
  byte Cs_Div;
  int  Cm_Div;
  char Cone_Print[6];
};
cone_info_type Cone_Info[] =   // [STM32] не const: пересчёт под механику (e2_mech_apply)
{
  //****начало таблицы конусов**** 
     { 1, 0, " 45\5" },		
     { 1, 7321, " 30\5" },		
     { 57, 2900, "  1\5" },		
     { 28, 6363, "  2\5" },		
     { 19, 811, "  3\5" },		
     { 14, 3007, "  4\5" },		
     { 11, 4301, "  5\5" },		
     { 9, 5144, "  6\5" },		
     { 8, 1443, "  7\5" },		
     { 7, 1154, "  8\5" },		
     { 6, 3138, "  9\5" },		
     { 5, 6713, " 10\5" },		
     { 5, 1446, " 11\5" },		
     { 4, 7046, " 12\5" },		
     { 4, 3315, " 13\5" },		
     { 4, 108, " 14\5" },		
     { 3, 7321, " 15\5" },		
     { 3, 4874, " 16\5" },		
     { 3, 2709, " 17\5" },		
     { 3, 777, " 18\5" },		
     { 2, 9042, " 19\5" },		
     { 2, 7475, " 20\5" },		
     { 2, 6051, " 21\5" },		
     { 2, 4751, " 22\5" },		
     { 2, 3559, " 23\5" },		
     { 2, 2460, " 24\5" },		
     { 2, 1445, " 25\5" },		
     { 2, 503, " 26\5" },		
     { 1, 9626, " 27\5" },		
     { 1, 8807, " 28\5" },		
     { 1, 8040, " 29\5" },			
     { 1, 6643, " 31\5" },		
     { 1, 6003, " 32\5" },		
     { 1, 5399, " 33\5" },		
     { 1, 4826, " 34\5" },		
     { 1, 4281, " 35\5" },		
     { 1, 3764, " 36\5" },		
     { 1, 3270, " 37\5" },		
     { 1, 2799, " 38\5" },		
     { 1, 2349, " 39\5" },		
     { 1, 1918, " 40\5" },		
     { 1, 1504, " 41\5" },		
     { 1, 1106, " 42\5" },		
     { 1, 724, " 43\5" },		
     { 1, 355, " 44\5" },		
     { 38, 4240,  " KM0"  },		
     { 40, 940,  " KM1"  },		
     { 40, 400,  " KM2"  },		
     { 39, 8440,  " KM3"  },		
     { 38, 5080,  " KM4"  },		
     { 38, 40,  " KM5"  },		
     { 38, 3600,  " KM6"  },		
     { 8, 0, " 1:4" },		
     { 10, 0, " 1:5" },		
     { 14, 0, " 1:7" },		
     { 20, 0, "1:10" },		
     { 32, 0, "1:16" },		
     { 40, 0, "1:20" },		
     { 48, 0, "1:24" },		
     { 60, 0, "1:30" },		
     { 100, 0, "1:50" },		
     { 18, 2857, "7:64" },		
     { 16, 6667, "3:25" },

//****конец таблицы конусов****
};
#define TOTAL_CONE (sizeof(Cone_Info) / sizeof(Cone_Info[0]))


// ***** Threads *****
struct thread_info_type
{
  byte Ks_Div_Z;
  int  Km_Div_Z;
  byte Ks_Div_X;
  int  Km_Div_X;
  char Thread_Print[7];
  float Step;
  byte Pass;
  int Limit_Print;     
};
thread_info_type Thread_Info[] =   // [STM32] не const: пересчёт под механику (e2_mech_apply)
{  
//****начало таблицы резьб****                                                                                                           
// Резьба метрическая                                                                                               					
{ 45, 0, 22, 5000, "0.20mm", 0.2, 4, 980 },					
{ 36, 0, 18, 0, "0.25mm", 0.25, 4, 840 },					
{ 30, 0, 15, 0, "0.30mm", 0.3, 4, 740 },					
{ 25, 7143, 12, 8571, "0.35mm", 0.35, 5, 680 },					
{ 22, 5000, 11, 2500, "0.40mm", 0.4, 5, 640 },					
{ 20, 0, 10, 0, "0.45mm", 0.45, 6, 600 },					
{ 18, 0, 9, 0, "0.50mm", 0.5, 6, 560 },					
{ 15, 0, 7, 5000, "0.60mm", 0.6, 7, 520 },					
{ 12, 8571, 6, 4286, "0.70mm", 0.7, 8, 480 },					
{ 12, 0, 6, 0, "0.75mm", 0.75, 8, 460 },					
{ 11, 2500, 5, 6250, "0.80mm", 0.8, 8, 460 },					
{ 9, 0, 4, 5000, "1.00mm", 1, 10, 420 },					
{ 7, 2000, 3, 6000, "1.25mm", 1.25, 12, 400 },					
{ 6, 0, 3, 0, "1.50mm", 1.5, 14, 380 },					
{ 5, 1429, 2, 5714, "1.75mm", 1.75, 16, 360 },					
{ 4, 5000, 2, 2500, "2.00mm", 2, 18, 360 },					
{ 3, 6000, 1, 8000, "2.50mm", 2.5, 22, 80 },					
{ 3, 0, 1, 5000, "3.00mm", 3, 26, 80 },					
{ 2, 2500, 1, 1250, "4.00mm", 4, 34, 80 },					
{ 2, 0, 1, 0, "4.50mm", 4.5, 38, 80 },					
// Резьба дюймовая					
{ 2, 1260, 1, 630, " 6tpi ", 4.233, 36, 80  },					
{ 2, 4803, 1, 2402, " 7tpi ", 3.629, 31, 80  },					
{ 2, 8346, 1, 4173, " 8tpi ", 3.175, 27, 80  },					
{ 3, 1890, 1, 5945, " 9tpi ", 2.822, 25, 80  },					
{ 3, 5433, 1, 7717, "10tpi ", 2.54, 22, 80  },					
{ 3, 8976, 1, 9488, "11tpi ", 2.309, 20, 80  },					
{ 4, 2520, 2, 1260, "12tpi ", 2.117, 19, 340  },					
{ 4, 6063, 2, 3031, "13tpi ", 1.954, 18, 360  },					
{ 4, 9606, 2, 4803, "14tpi ", 1.814, 17, 360  },					
{ 5, 6693, 2, 8346, "16tpi ", 1.588, 15, 360  },					
{ 6, 3780, 3, 1890, "18tpi ", 1.411, 13, 380  },					
{ 6, 7323, 3, 3661, "19tpi ", 1.337, 13, 380  },					
{ 7, 866, 3, 5433, "20tpi ", 1.27, 12, 400  },					
{ 7, 7953, 3, 8976, "22tpi ", 1.155, 11, 400  },					
{ 8, 5039, 4, 2520, "24tpi ", 1.058, 10, 420  },					
{ 9, 2126, 4, 6063, "26tpi ", 0.977, 10, 420  },					
{ 9, 5669, 4, 7835, "27tpi ", 0.941, 10, 420  },					
{ 9, 9213, 4, 9606, "28tpi ", 0.907, 9, 440  },					
{ 11, 3386, 5, 6693, "32tpi ", 0.794, 8, 460  },					
{ 14, 1732, 7, 866, "40tpi ", 0.635, 7, 500  },					
{ 15, 5906, 7, 7953, "44tpi ", 0.577, 7, 520  },					
{ 17, 79, 8, 5039, "48tpi ", 0.529, 6, 540  },					
{ 19, 8425, 9, 9213, "56tpi ", 0.454, 6, 580  },					
{ 21, 2598, 10, 6299, "60tpi ", 0.423, 5, 620  },					
{ 22, 6772, 11, 3386, "64tpi ", 0.397, 5, 640  },					
{ 25, 5118, 12, 7559, "72tpi ", 0.353, 5, 680  },					
{ 28, 3465, 14, 1732, "80tpi ", 0.318, 5, 720  },					
// Трубная резьба 55 градусов (G)					
{ 9, 9213, 4, 9606, "G 1/16", 0.907, 9, 440 },					
{ 9, 9213, 4, 9606, "G  1/8", 0.907, 9, 440  },					
{ 6, 7323, 3, 3661, "G  1/4", 1.337, 13, 380  },					
{ 6, 7323, 3, 3661, "G  3/8", 1.337, 13, 380 },					
{ 4, 9606, 2, 4803, "G  1/2", 1.814, 17, 360 },					
{ 4, 9606, 2, 4803, "G  3/4", 1.814, 17, 360 },					
{ 3, 8976, 1, 9488, "G 1   ", 2.309, 20, 80 },					
{ 3, 8976, 1, 9488, "G1 1/4", 2.309, 20, 80 },					
{ 3, 8976, 1, 9488, "G1 1/2", 2.309, 20, 80 },					
{ 3, 8976, 1, 9488, "G 2   ", 2.309, 20, 80 },					
// Трубная резьба 60 градусов (K)					
{ 9, 5669, 4, 7835, "K 1/16", 0.941, 10, 420 },					
{ 9, 5669, 4, 7835, "K  1/8", 0.941, 10, 420 },					
{ 6, 3780, 3, 1890, "K  1/4", 1.411, 13, 380 },					
{ 6, 3780, 3, 1890, "K  3/8", 1.411, 13, 380 },					
{ 4, 9606, 2, 4803, "K  1/2", 1.814, 17, 360 },					
{ 4, 9606, 2, 4803, "K  3/4", 1.814, 17, 360 },					
{ 4, 748, 2, 374, "K 1   ", 2.209, 20, 340 },					
{ 4, 748, 2, 374, "K1 1/4", 2.209, 20, 340 },					
{ 4, 748, 2, 374, "K1 1/2", 2.209, 20, 340 },					
{ 4, 748, 2, 374, "K 2   ", 2.209, 20, 340 },					

//****окончание таблицы резьб****      
};
#define TOTAL_THREADS (sizeof(Thread_Info) / sizeof(Thread_Info[0]))


#define Thrd_Accel_Err Thread_Info[0].Ks_Div_Z                 // неверно задано ускорение
//static_assert(Thrd_Accel_Err + THRD_ACCEL <= 255, "Неверно задано значение THRD_ACCEL");
//////////////////////////////////////////////////////////////////////////////////////////////////

// Проверка корректности конфигурации оси Z
#define CHECK_MIN_FEED_Z  (uint32_t)(ENC_LINE_PER_REV / ((float)MOTOR_Z_STEP_PER_REV * McSTEP_Z * MIN_FEED / SCREW_Z) /2)
static_assert(CHECK_MIN_FEED_Z <= 255, "Неверно задано значение MIN_FEED");

#define CHECK_MAX_FEED_Z  (uint32_t)(ENC_LINE_PER_REV / ((float)MOTOR_Z_STEP_PER_REV * McSTEP_Z * MAX_FEED / SCREW_Z) /2)
static_assert(CHECK_MAX_FEED_Z > 1, "Неверно задано значение MAX_FEED");

#define CHECK_MIN_aFEED_Z  250000 / ((uint32_t)MIN_aFEED * MOTOR_Z_STEP_PER_REV * McSTEP_Z / ((uint32_t)60 * SCREW_Z / 100) * 2) -1
static_assert(CHECK_MIN_aFEED_Z <= 65535, "Неверно задано значение MIN_aFEED");

#define CHECK_MAX_aFEED_Z  250000 / ((uint32_t)MAX_aFEED * MOTOR_Z_STEP_PER_REV * McSTEP_Z / ((uint32_t)60 * SCREW_Z / 100) * 2) -1
static_assert(CHECK_MAX_aFEED_Z > 1, "Неверно задано значение MAX_aFEED");

// Проверка корректности конфигурации оси X
#define CHECK_MIN_FEED_X  (uint32_t)(ENC_LINE_PER_REV / ((float)MOTOR_X_STEP_PER_REV * McSTEP_X * MIN_FEED / SCREW_X) /2)
static_assert(CHECK_MIN_FEED_X <= 255, "Неверно задано значение MIN_FEED");

#define CHECK_MAX_FEED_X  (uint32_t)(ENC_LINE_PER_REV / ((float)MOTOR_X_STEP_PER_REV * McSTEP_X * MAX_FEED / SCREW_X) /2)
static_assert(CHECK_MAX_FEED_X > 1, "Неверно задано значение MAX_FEED");

#define CHECK_MIN_aFEED_X  250000 / ((uint32_t)MIN_aFEED * MOTOR_X_STEP_PER_REV * McSTEP_X / ((uint32_t)60 * SCREW_X / 100) * 2) -1
static_assert(CHECK_MIN_aFEED_X <= 65535, "Неверно задано значение MIN_aFEED");

#define CHECK_MAX_aFEED_X  250000 / ((uint32_t)MAX_aFEED * MOTOR_X_STEP_PER_REV * McSTEP_X / ((uint32_t)60 * SCREW_X / 100) * 2) -1
static_assert(CHECK_MAX_aFEED_X > 1, "Неверно задано значение MAX_aFEED");
//////////////////////////////////////////////////////////

// [STM32] Дальше механика — из настроек (флеш / экран), исходные значения — из els_config.h
#include "e2_mech.h"
E2Mech e2_mech = E2_MECH_CONFIG;


// ***** MY CONSTANT *****
#define CW               0
#define CCW              1
#define ON               1
#define OFF              0

// [STM32] <avr/pgmspace.h>, <util/delay.h> не нужны: _delay_ms() — в e2_hal.h
// === LCD через I2C ===
// Библиотека: LCDI2C_Multilingual от locple (поддержка русского языка!)
// GitHub: https://github.com/locple/LCDI2C_Multilingual
// Подключение: SDA = Pin 11, SCL = Pin 12 (bit-bang I2C, пины 20/21 освобождены под энкодер шпинделя)
// Адрес I2C: 0x27 (если не работает - попробуйте 0x3F)
// [STM32] LCD2004 по I2C1 (PB6/PB7), вывод через теневой буфер — см. e2_lcd.h
E2Lcd lcd(LCD_I2C_ADDR, 20, 4);  // (адрес, столбцы, строки); адрес — в els_config.h

char LCD_Row_1[21];
char LCD_Row_2[21];
char LCD_Row_3[21];
char LCD_Row_4[21];
char LCD_Row_5[21];
char LCD_Row_6[21];
char LCD_Row_7[21];
byte strelka_vlevo[8] = 
{
  B00000,
  B00100,
  B01000,
  B11111,
  B01000,
  B00100,
  B00000,
  B00000,
};
byte strelka_vpravo[8] = 
{
  0b00000,
  0b00100,
  0b00010,
  0b11111,
  0b00010,
  0b00100,
  0b00000,
  0b00000
};
byte strelka_vverh[8] = 
{
  B00000,
  B00100,
  B01110,
  B10101,
  B00100,
  B00100,
  B00000,
  B00000,
};
byte strelka_vniz[8] = 
{
  B00000,
  B00100,
  B00100,
  B10101,
  B01110,
  B00100,
  B00000,
  B00000,
};
byte znak_diametr[8] = 
{
  0b00001,
  0b01110,
  0b10011,
  0b10101,
  0b11001,
  0b01110,
  0b10000,
  0b00000
};
byte znak_gradus[8] = 
{
  0b01100,
  0b10010,
  0b10010,
  0b01100,
  0b00000,
  0b00000,
  0b00000,
  0b00000
};






// ***** Stepper Motor *****








// ***** Tacho *****


// ***** Encoder *****
#define ENC_TICK              (ENC_LINE_PER_REV * 2)    // Рабочее кол-во импульсов
 


// ***** Hand_Coder *****            // Z/X: Input-E4,E5, подтяжка-E4,E5, X1/X10: Input-J0,J1, подтяжка-J0,J1.
                                     

byte Hand_Axis_Old = 0;

byte Hand_Scale_Old = 0;

//***** Limit Buttons & LEDs *****

byte Limit_Button_Old = 0;


#define Limit_Pos_Max          1073741824
#define Limit_Pos_Min         -1073741824


//////////////////

byte Button_Old = 0;

byte Button_Sel_Old = 0;
bool key_sel_flag = false;

//////////////////

byte Joy_Old = 0;

////////////////////
byte Submode_Old = 0;


// ***** Mode *****
byte Mode_Old = 0;

#define DELAY_ENTER_KEYCYCLE 2        // Задержка для входа в режим автонажатия кнопок меню. Значения от 1 до разумного максимума
#define DELAY_INTO_KEYCYCLE  0        // Интервал срабатывания автонажатия кнопок меню.      Значения от 1 до DELAY_ENTER_KEYCYCLE
#define LINEIKA (MOTOR_X_STEP_PER_REV * McSTEP_X / LIN_X )
#define TENS    (MOTOR_Z_STEP_PER_REV * McSTEP_Z / SCREW_Z )

enum Mode
{
  Mode_Feed = 1,
  Mode_aFeed,
  Mode_Thread,
  Mode_Cone_L,
  Mode_Cone_R,
  Mode_Sphere,
  Mode_Divider,
  Mode_Reserve
};

enum Sub_Mode_Thread
{
  Sub_Mode_Thread_Int = 1,
  Sub_Mode_Thread_Man,
  Sub_Mode_Thread_Ext,
};

enum Sub_Mode_Feed
{
  Sub_Mode_Feed_Int = 1,
  Sub_Mode_Feed_Man,
  Sub_Mode_Feed_Ext,
};

enum Sub_Mode_aFeed
{
  Sub_Mode_aFeed_Int = 1,
  Sub_Mode_aFeed_Man,
  Sub_Mode_aFeed_Ext,
};

enum Sub_Mode_Cone
{
  Sub_Mode_Cone_Int = 1,
  Sub_Mode_Cone_Man,
  Sub_Mode_Cone_Ext,
};

enum Sub_Mode_Sphere
{
  Sub_Mode_Sphere_Int = 1,
  Sub_Mode_Sphere_Man,
  Sub_Mode_Sphere_Ext,
};


//***** Ускоренное перемещение *****

//***** РГИ перемещение *****

// ***** aFeed *****

// ***** Feed *****

// ***** Interrupts *****











////////////////////////////////////////////////////////////
#define Ena_INT_Thrd()        do {Disable_INT_OCR2A();\
                                  Disable_INT_OCR2B();\
                                  Disable_INT_OCR4A();\
                                  Disable_INT_OCR4B();\
                                  Disable_INT_OCR5A();\
                                  Disable_INT_OCR5B();\
                                  Enable_INT0();} while(0)

#define Ena_INT_Z_Feed()      do {Disable_INT0();\
                                  Disable_INT_OCR2A();\
                                  Disable_INT_OCR2B();\
                                  Disable_INT_OCR4A();\
                                  Disable_INT_OCR4B();\
                                  Disable_INT_OCR5B();\
                                  Enable_INT_OCR5A();} while(0)
                                  
#define Ena_INT_X_Feed()      do {Disable_INT0();\
                                  Disable_INT_OCR2A();\
                                  Disable_INT_OCR2B();\
                                  Disable_INT_OCR4A();\
                                  Disable_INT_OCR4B();\
                                  Disable_INT_OCR5A();\
                                  Enable_INT_OCR5B();} while(0)

#define Ena_INT_Z_aFeed()     do {Disable_INT0();\
                                  Disable_INT_OCR2A();\
                                  Disable_INT_OCR2B();\
                                  Disable_INT_OCR4B();\
                                  Disable_INT_OCR5A();\
                                  Disable_INT_OCR5B();\
                                  Enable_INT_OCR4A();} while(0)

#define Ena_INT_X_aFeed()     do {Disable_INT0();\
                                  Disable_INT_OCR2A();\
                                  Disable_INT_OCR2B();\
                                  Disable_INT_OCR4A();\
                                  Disable_INT_OCR5A();\
                                  Disable_INT_OCR5B();\
                                  Enable_INT_OCR4B();} while(0)
                                     
#define Ena_INT_Z_Rapid()     do {Disable_INT0();\
                                  Disable_INT_OCR2B();\
                                  Disable_INT_OCR4A();\
                                  Disable_INT_OCR4B();\
                                  Disable_INT_OCR5A();\
                                  Disable_INT_OCR5B();\
                                  Enable_INT_OCR2A();} while(0)
                                     
#define Ena_INT_X_Rapid()     do {Disable_INT0();\
                                  Disable_INT_OCR2A();\
                                  Disable_INT_OCR4A();\
                                  Disable_INT_OCR4B();\
                                  Disable_INT_OCR5A();\
                                  Disable_INT_OCR5B();\
                                  Enable_INT_OCR2B();} while(0)
                                                                       

// ***** My Flags *****

bool spindle_flag = OFF;
bool feed_Z_flag = OFF;
bool feed_X_flag = OFF;

bool rapid_step_Z_flag = OFF;
bool rapid_step_X_flag = OFF;
bool rapid_Z_flag = OFF;
bool rapid_X_flag = OFF;

bool limit_Left_flag = OFF;
bool limit_Right_flag = OFF;
bool limit_Front_flag = OFF;
bool limit_Rear_flag = OFF;

bool limit_button_flag = OFF;
bool button_up_flag = OFF;
bool button_down_flag = OFF;
bool button_left_flag = OFF;
bool button_right_flag = OFF;

bool a_flag = false;
bool b_flag = false;
bool c_flag = false;
bool d_flag = false;
bool cycle_flag = false;

bool err_0_flag = true;
bool err_1_flag = false;
bool err_2_flag = false;
bool Complete_flag = false;

bool ConR_Thr_flag = false;
bool ConL_Thr_flag = false;
bool CThr_flag = false;
bool hand_X = OFF;
bool hand_Z = OFF;
bool flag_hand_X = OFF;
bool flag_hand_Z = OFF;
bool X_flag = OFF;                    // временный
bool Z_flag = OFF;                    // временный
bool flag_Scale_x1 = OFF;             // возможно только для отладки
bool flag_Scale_x10 = OFF;            // возможно только для отладки
bool control_flag = OFF;
bool flag_j = OFF;

// ***** MY VARIABLES *****
int Tacho_Count = 0;
int Tacho_Count_Old =0;
int Spindle_Count = 0;
long Motor_Z_Pos = 0;
long Motor_X_Pos = 0;


byte KeyCycle = 0;
byte Blinc_Count = 0;
byte KCs_Divisor = 0;
float KCm_Divisor = 0;
float divider = 0;
int TENSION_Z = 0;
int Step = 0;
int Ph = 1;
int Gewinde_flag = 0;
int McStep_Lag = 0; 
int Pass_Fin = 0;
int Thr_Pass_Summ = 0;
int OTSKOK_Z = REBOUND_Z;
int SelectMenu = 1;
int Speed_Limit = 0;
long Size_X_mm = 0;
long MSize_X_mm = 0;
long Size_Z_mm = 0;
long OTSKOK_Z_mm = 0;
long TENSION_Z_mm = 0;
long X_pos = 0;
long MX_pos = 0;
long Z_pos = 0;
unsigned long Spindle_Angle = 0;
unsigned long Required_Angle = 0;
unsigned long Infeed_ValueX = 0;
unsigned long Infeed_ValueC = 0;
unsigned long Infeed_ValueZ = 0;
unsigned long Infeed_Value = 0;


int Enc_Pos = 0;
volatile long Hand_Count = 0;
bool g_emul_rapid = false;  // UART emulation: true = Rapid button held (EMUL:BTN_RAPID toggle)
long Hand_Count_Old = 0;
long Hand_Count_New = 0;
long Hand_Z_Pos = 0;
long Hand_X_Pos = 0;

byte Scale = HC_SCALE_1;

int Ks_Count = 0;
int Km_Count = 0;
int KKs_Divisor = 0;
int Ks_Divisor = 0;
int THRD_CACCEL = THRD_ACCEL;
int tmp_Ks_Divisor = THRD_CACCEL;
int tmp_Cs_Divisor = THRD_CACCEL;
int KKm_Divisor = 0;
int Km_Divisor = 0;
uint16_t Feed_Divisor = 0;
uint16_t aFeed_Divisor = 0;

int Cs_Count = 0;
int Cm_Count = 0;
int Cs_Divisor = 0;
int Cm_Divisor = 0;

byte tmp_Accel = THRD_CACCEL;
byte Repeat_Count = 0;

int Brake_Compens = 0;

byte Mode = Mode_Feed;
byte Sub_Mode_Thread = Sub_Mode_Thread_Man;
byte Sub_Mode_Feed = Sub_Mode_Feed_Man;
byte Sub_Mode_aFeed = Sub_Mode_aFeed_Man;
byte Sub_Mode_Cone = Sub_Mode_Cone_Man;
byte Sub_Mode_Sphere = Sub_Mode_Sphere_Man;
byte Thread_Step = 0;
byte Cone_Step = 0;

long Limit_Pos_Left = Limit_Pos_Max;
long Limit_Pos_Right = Limit_Pos_Min;
long Limit_Pos_Front = Limit_Pos_Max;
long Limit_Pos_Rear = Limit_Pos_Min;
volatile long Limit_Pos = 0;
volatile long Limit_Pos_HC = 0;

uint16_t Feed_mm = 0;
uint16_t aFeed_mm = 0;

uint16_t Start_Speed = ENC_LINE_PER_REV / ((uint32_t)MOTOR_Z_STEP_PER_REV * McSTEP_Z * MIN_FEED / SCREW_Z) /FEED_ACCEL;
uint16_t max_OCR5A = ENC_LINE_PER_REV / ((uint32_t)MOTOR_Z_STEP_PER_REV * McSTEP_Z * MIN_FEED / SCREW_Z) /FEED_ACCEL;  // Начальная скорость подачи при разгоне/торможении
uint16_t max_OCR4A = (250000 / ((uint32_t)MIN_aFEED * MOTOR_Z_STEP_PER_REV * McSTEP_Z / ((uint32_t)60 * SCREW_Z / 100) * 2) - 1) /FEED_ACCEL;

byte Total_Tooth = 1;
byte Current_Tooth = 1;

long Pass_Total = 1;
long Pass_Nr = 1;
long Null_X_Pos = 0;
long Null_Z_Pos = 0;
int Ap = 0;
long Pass_Total_Sphr = 1;

int ADC_Feed = 0;
long Sum_ADC = 0;
int ADC_Array[16];
byte x = 0;

long Control_Count = 0;


// Sphere

const int Cutter_Width_array[] = {100, 125, 150, 175, 200, 225, 250, 275, 300};
#define TOTAL_CUTTER_WIDTH (sizeof(Cutter_Width_array) / sizeof(Cutter_Width_array[0]))
byte Cutter_Step = 4;
int Cutter_Width = Cutter_Width_array[Cutter_Step];

const int Cutting_Width_array[] = {10, 25, 50, 75, 100, 125, 150, 175, 200};
#define TOTAL_CUTTING_STEP (sizeof(Cutting_Width_array) / sizeof(Cutting_Width_array[0]))
byte Cutting_Step = 2;
int Cutting_Width = Cutting_Width_array[Cutting_Step];

long Sph_R_mm = 1000;
long Sph_R = 0;
long Bar_R_mm = 0;
long Bar_R = 0;
long SphBar = 0;


uint16_t max_OCR3A = HC_START_SPEED_1;
uint16_t min_OCR3A = HC_MAX_SPEED_1;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup()
{
  e2_mech_load();           // [STM32] механика: флеш или els_config.h (BACKLOG-028) — до всего остального
  // ========== SERIAL EMULATION - ИНИЦИАЛИЗАЦИЯ В ПЕРВУЮ ОЧЕРЕДЬ! ==========
  // ВАЖНО: Serial инициализируется ПЕРВЫМ, чтобы видеть отладку даже если LCD зависнет
  Serial_Emulation_Init();  // Инициализация эмуляции кнопок через Serial Monitor
  // =========================================================================

  // ========== НОВЫЙ ДИСПЛЕЙ ESP32-S3 (SoftwareSerial Pin51=TX, Pin52=RX, 57600) =========
  Display_UART_Init();
  // =========================================================================
#if USE_DRO_HS800
  DRV_DRO_Init();                              // [STM32] вместо DRO_Init() из DRO_HS800.ino
#elif ELS_POS_SOURCE == POS_SOURCE_SCALES
  DRV_Scales_Init();                           // [STM32] линейки напрямую
#endif

  _delay_ms(500);

  // Инициализация I2C LCD
  lcd.init();        // Инициализация I2C дисплея
  lcd.backlight();   // Включить подсветку

  lcd.setCursor(0, 0);
  lcd.print("  www.chipmaker.ru  ");
  lcd.setCursor(0, 1);
  lcd.print(" Система управления ");
  lcd.setCursor(0, 2);
  lcd.print("  Токарным Станком  ");
  lcd.setCursor(0, 3);
  lcd.print("         7e2 V3.3");


  // [STM32] DDRG = B11111111 — порт G Mega не используется

  lcd.createChar(1, strelka_vlevo);
  lcd.createChar(2, strelka_vpravo);
  lcd.createChar(3, strelka_vverh);
  lcd.createChar(4, strelka_vniz);
  lcd.createChar(5, znak_gradus);
  lcd.createChar(6, znak_diametr);



  // [STM32] TIMSK0 = 0 на Mega отключал Timer0 (millis); здесь millis() нужен — оставлен

  // ***** Timer0 ***** // ***** Тахометр *****
//  TCCR0A = (1<<COM0B0)|(1<<WGM01); // Toggle OC0B on Compare Match // CTC Mode2
//  TCCR0B = (1<<CS00);     // No Prescaler
//  OCR0A = 89; // 1800/10(выходных пульсов)/2-1 = 89
//  TIMSK0 = (1<<OCIE0B);

  Encoder_Init();
  Hand_Init();
  Motor_Init();

  e2_timers_init();                            // [STM32] TIM6/TIM7/TIM12 и прерывания энкодеров
  INT0_Init();
  INT2_Init();

  Timer2_Init();
  OCR2A = MIN_RAPID_MOTION;

  Timer3_Init();
  OCR3A = max_OCR3A;

  Timer4_Init();
  OCR4A = max_OCR4A;

  Timer5_Init();
  OCR5A = max_OCR5A;

  Ena_INT_Z_Feed();

  Limit_Init();

  Limit_Rear_LED_Off();
  Limit_Right_LED_Off();
  Limit_Front_LED_Off();
  Limit_Left_LED_Off();  
  
  Limit_Left_LED_On();
  Limit_Rear_LED_Off();
  _delay_ms(100);
  Limit_Front_LED_On();
  Limit_Left_LED_Off();
  _delay_ms(100);
  Limit_Right_LED_On();
  Limit_Front_LED_Off();
  _delay_ms(100);  
  Limit_Rear_LED_On();
  Limit_Right_LED_Off();
  _delay_ms(100);
  Limit_Left_LED_On();
  Limit_Rear_LED_Off();
  _delay_ms(100);
  Limit_Front_LED_On();
  Limit_Left_LED_Off();
  _delay_ms(100);
  Limit_Right_LED_On();
  Limit_Front_LED_Off();
  _delay_ms(100);  
  Limit_Rear_LED_On();
  Limit_Right_LED_Off();
  _delay_ms(100);
  Limit_Left_LED_On();
  Limit_Rear_LED_Off();
  _delay_ms(100);
  Limit_Front_LED_On();
  Limit_Left_LED_Off();
  _delay_ms(100);
  Limit_Right_LED_On();
  Limit_Front_LED_Off();
  _delay_ms(100);  
  Limit_Rear_LED_On();
  Limit_Right_LED_Off();
  _delay_ms(100);
   Limit_Left_LED_On();
  Limit_Rear_LED_Off();
  _delay_ms(100);
  Limit_Front_LED_On();
  Limit_Left_LED_Off();
  _delay_ms(100);
  Limit_Right_LED_On();
  Limit_Front_LED_Off();
  _delay_ms(100);  
  Limit_Rear_LED_On();
  Limit_Right_LED_Off();
  _delay_ms(100);
  Limit_Left_LED_On();
  Limit_Rear_LED_Off();
  _delay_ms(100);
  Limit_Front_LED_On();
  Limit_Left_LED_Off();
  _delay_ms(100);
  Limit_Right_LED_On();
  Limit_Front_LED_Off();
  _delay_ms(100);  
  Limit_Rear_LED_On();
  Limit_Right_LED_Off();
  _delay_ms(100);
  Limit_Left_LED_On();
  Limit_Rear_LED_Off();
  _delay_ms(100);
  Limit_Front_LED_On();
  Limit_Left_LED_Off();
  _delay_ms(100);
  Limit_Right_LED_On();
  Limit_Front_LED_Off();
  _delay_ms(100);  
  Limit_Rear_LED_On();
  Limit_Right_LED_Off();
  _delay_ms(100);
  
  Limit_Rear_LED_Off();
  Limit_Right_LED_Off();
  Limit_Front_LED_Off();
  Limit_Left_LED_Off();

  Menu_Buttons_Init();
  Joy_Init();
  Mode_Switch_Init();

  Beeper_Init();
  Beeper_Off();

#if !USE_FEED_POT
  // [STM32] Без переменника подачи (USE_FEED_POT 0 в els_config.h) подачи задаются
  // кнопками/дисплеем; стартовые значения — середина диапазона
  Feed_mm  = (MIN_FEED + MAX_FEED) / 2;
  aFeed_mm = (MIN_aFEED + MAX_aFEED) / 20 * 10;
#endif

  Spindle_Dir = CW;
  Motor_Z_Dir = CW;
  Joy_Z_flag = OFF;
  Step_Z_flag = OFF;
  Motor_X_Dir = CW;
  Joy_X_flag = OFF;
  Step_X_flag = OFF;

  // [STM32] Timer1 (CTC, /1024, OCR1A = 625 → 40 мс) — опрос клавиатуры по micros(), см. e2_hal.cpp
  CLEAR_KEYB_TIMER;

  Motor_Z_RemovePulse();
  Motor_X_RemovePulse();

  // Инициализация аналогового джойстика HW-504 (временно для тестов)
  // Analog_Joystick_Init();  // ОТКЛЮЧЕНО - не работает корректно

  // Инициализация матричной клавиатуры HW-834 (временно для тестов)
  // Keypad4x4_Init();  // ОТКЛЮЧЕНО - не работает

  // Финальное сообщение (строки хранятся во Flash памяти через F())
  Serial.println(F("========================================"));
  Serial.println(F("===== SETUP() ЗАВЕРШЕН! ================"));
  Serial.println(F("Система готова. Введите HELP"));
  Serial.println(F(""));

  // [STM32] Display_UART_Init() в начале setup() шлёт состояние до того, как заданы подачи
  // (дисплей получал 0.00). Повторно — уже с готовыми значениями, на случай если
  // перезапустился только STM32 и дисплей не пришлёт READY.
  Display_Send_All();
}


// [STM32] Ввод и сброс осей при любом источнике координат. У автора «Сброс» в подрежиме
// «Ввод * Сброс осей» обнуляет счётчики шагов X_pos/Z_pos (они только для показа, циклы их не
// читают). При линейках напрямую показания берутся с линейки — для них свой ноль (смещение).
// Диаметр — отдельный ноль, как MX_pos у автора. С блоком DRO оси задаются на самом блоке.
#if ELS_POS_SOURCE == POS_SOURCE_SCALES
static int32_t e2_scale_off[2] = {0, 0};   // показание = линейка − смещение (индекс ScaleAxis)
static int32_t e2_diam_off = 0;            // диаметр = 2 × линейка X − смещение
#endif
#if ELS_POS_SOURCE != POS_SOURCE_STEPS
static int32_t e2_diam_um = 0;             // диаметр, мкм (DRO и линейки)
#endif

bool e2_axis_editable(void) { return ELS_POS_SOURCE != POS_SOURCE_DRO_UART; }

void e2_axis_set(char axis, int32_t um)
{
#if ELS_POS_SOURCE == POS_SOURCE_SCALES
   if (axis == 'D' && DRV_Scales_Present(SCALE_AXIS_X)) {
      e2_diam_off = 2 * DRV_Scales_GetUm(SCALE_AXIS_X) - um;
      return;
   }
   ScaleAxis ax = axis == 'Z' ? SCALE_AXIS_Z : SCALE_AXIS_X;
   if (axis != 'D' && DRV_Scales_Present(ax)) {
      e2_scale_off[ax] = DRV_Scales_GetUm(ax) - um;
      return;
   }
#elif ELS_POS_SOURCE == POS_SOURCE_DRO_UART
   (void)axis; (void)um;
   return;
#endif
   // По шагам: обратный пересчёт к loop() (позиция = −шаги / шагов на 0.01 мм × 10 мкм)
   float spu_x = (float)MOTOR_X_STEP_PER_REV / SCREW_X * McSTEP_X;
   float spu_z = (float)MOTOR_Z_STEP_PER_REV / SCREW_Z * McSTEP_Z;
   if (axis == 'Z')      Z_pos  = -lroundf(um / 10.0f * spu_z);
   else if (axis == 'X') X_pos  = -lroundf(um / 10.0f * spu_x);
   else if (axis == 'D') MX_pos = -lroundf(um / 10.0f * spu_x / 2);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop()
{
  // ========== SERIAL EMULATION ==========
  Serial_Emulation_Process();  // Обработка команд из Serial Monitor
  // ======================================

  // ========== НОВЫЙ ДИСПЛЕЙ: приём команд + периодическая отправка позиции =
  Display_UART_Process();
  static uint32_t s_disp_pos_timer = 0;
  if (millis() - s_disp_pos_timer >= 250) {      // каждые 250мс
    s_disp_pos_timer = millis();
    Display_On_Position_Change();
    Display_Send_Int("ANGLE", Spindle_Angle);     // угол шпинделя (нужен для M7)
  }
  // Периодический PING каждые 10 сек для авторесинка (если дисплей перезагрузился)
  static uint32_t s_disp_ping_timer = 0;
  if (millis() - s_disp_ping_timer >= 10000) {
    s_disp_ping_timer = millis();
    Display_Send_CMD("PING", nullptr);
  }
  // =========================================================================

  // ========== АНАЛОГОВЫЙ ДЖОЙСТИК ==========
  // Read_Analog_Joystick();  // ОТКЛЮЧЕНО - не работает корректно
  // =========================================

  // ========== МАТРИЧНАЯ КЛАВИАТУРА ==========
  // Read_Keypad4x4();  // ОТКЛЮЧЕНО - не работает
  // ==========================================

  Spindle();
#if USE_FEED_POT
  Read_ADC_Feed();
#endif
  if (KEYB_TIMER_FLAG != 0) Menu();
  if (Mode == Mode_Divider) Print(); 
  lcd.flush();                                 // [STM32] вывод изменений на LCD2004 (не дольше 2 мс)
  e2_service();                                // [STM32] обороты, LCD2004-строка для тестов, линейки
 
#if ELS_POS_SOURCE != POS_SOURCE_STEPS
  e2_pos_update();                             // [STM32] вместо DRO_Process(): dro_pos_x/y от блока DRO или линеек
  {
    // Print.ino показывает минус когда переменная ПОЛОЖИТЕЛЬНАЯ (инвертированная логика).
    // Инвертируем оба знака чтобы на LCD и ESP32 отображалось то же что на HS800-2.
    int32_t new_x = -(dro_pos_x / 10L);
    int32_t new_z = -(dro_pos_y / 10L);
    int32_t new_d = -(e2_diam_um / 10L);
    if (new_x != Size_X_mm || new_z != Size_Z_mm || new_d != MSize_X_mm) {
      Size_X_mm  = new_x;
      Size_Z_mm  = new_z;
      MSize_X_mm = new_d;                      // [STM32] диаметр со своим нулём (было −dro_pos_x/5)
      Display_On_Position_Change();
    }
  }
#else
  MSize_X_mm = MX_pos / ((float)MOTOR_X_STEP_PER_REV / SCREW_X / 2 * McSTEP_X);   //перерасчёт в миллиметры диаметр заготовки
  Size_X_mm  = X_pos  / ((float)MOTOR_X_STEP_PER_REV / SCREW_X     * McSTEP_X);   //перерасчёт в миллиметры X
  Size_Z_mm  = Z_pos  / ((float)MOTOR_Z_STEP_PER_REV / SCREW_Z     * McSTEP_Z);   //перерасчёт в миллиметры Z
#endif
  OTSKOK_Z_mm  = OTSKOK_Z  / ((float)MOTOR_Z_STEP_PER_REV / SCREW_Z * McSTEP_Z);  //перерасчёт в миллиметры отскок
  TENSION_Z_mm = TENSION_Z / ((float)MOTOR_Z_STEP_PER_REV / SCREW_Z * McSTEP_Z);  //перерасчёт в миллиметры натяг
  
  Spindle_Angle = Enc_Pos * 360000 / ENC_TICK;
  Required_Angle = 360000 * (Current_Tooth - 1) / Total_Tooth;
       
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// *****  Thread ***** ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void e2_isr_int0(void)   // [STM32] было ISR(INT0_vect)
{
   TachoRemovePulse();

   if (!Enc_Ch_A)
   {
      if (!Enc_Ch_B)
      {
         Spindle_Dir = CW;
         if (++Enc_Pos == ENC_TICK)
         {                                           
            Enc_Pos = 0;
            TachoSetPulse();
            if (Joy_Z_flag == ON) {Step_Z_flag = ON;}
            else if (Joy_X_flag == ON) {Step_X_flag = ON;}
         }
      }
      else
      {
        Spindle_Dir = CCW;
        if (--Enc_Pos < 0)
        { 
           Enc_Pos = ENC_TICK - 1;
           TachoSetPulse();
           if (Joy_Z_flag == ON) {Step_Z_flag = ON;}
           else if (Joy_X_flag == ON) {Step_X_flag = ON;}
        }
      }
   }
   
   else
   {
      if (!Enc_Ch_B) 
      {
         Spindle_Dir = CCW;
         if (--Enc_Pos < 0)
         { 
            Enc_Pos = ENC_TICK - 1;
            TachoSetPulse();
            if (Joy_Z_flag == ON) {Step_Z_flag = ON;}
            else if (Joy_X_flag == ON) {Step_X_flag = ON;}
         }
      }
      else 
      {
         Spindle_Dir = CW;
         if (++Enc_Pos == ENC_TICK)
         {                                           
            Enc_Pos = 0;
            TachoSetPulse();
            if (Joy_Z_flag == ON) {Step_Z_flag = ON;}
            else if (Joy_X_flag == ON) {Step_X_flag = ON;}
         }
      }
   }  
   
   if (Step_Z_flag == ON)
   {   Motor_Z_RemovePulse();
  
      if ( (Motor_Z_Dir == CW && Motor_Z_Pos > Limit_Pos) || (Motor_Z_Dir == CCW && Motor_Z_Pos < Limit_Pos) || (!Joy_Z_flag) )
      {
         if (tmp_Ks_Divisor < tmp_Accel)
         {
            Ks_Count++;
            if (Ks_Count > tmp_Ks_Divisor)
            {
               Motor_Z_SetPulse();
               if (Motor_Z_Dir == CW) {Motor_Z_Pos ++; Z_pos ++;}
               else {Motor_Z_Pos --; Z_pos --;}
               Ks_Count = 0;
               if (++Repeat_Count == REPEAt)
               {
                  Repeat_Count = 0;
                  tmp_Ks_Divisor ++;
                  tmp_Cs_Divisor ++;
               }
            }
         }  
         else {Step_Z_flag = OFF;  if (CThr_flag == true) {Step_X_flag = OFF;}} 
      }

      else
      {
         Ks_Count++;
         if (Ks_Count > tmp_Ks_Divisor)
         {
            Motor_Z_SetPulse();
            if (Motor_Z_Dir == CW) {Motor_Z_Pos ++; Z_pos ++;}
            else {Motor_Z_Pos --; Z_pos --;}
         
            if (tmp_Ks_Divisor > Ks_Divisor)
            {
               Ks_Count = 0;
               if (++Repeat_Count == REPEAt)
               {
                  Repeat_Count = 0;
                  tmp_Ks_Divisor --;
                  tmp_Cs_Divisor --;
               }
            }
            else
            {  
               Km_Count = Km_Count + Km_Divisor;
               if (Km_Count > Km_Divisor)
               {
                  Km_Count = Km_Count - 10000;
                  Ks_Count = 0;
               }
               else {Ks_Count = 1;}
            }
         }
      }
   //////////////////////////////////////////////////////
   if (CThr_flag == true)
   
   {  Motor_X_RemovePulse();
          
       if (tmp_Ks_Divisor < tmp_Accel)
         {
         Cs_Count++;
       if (Cs_Count > tmp_Cs_Divisor)
         {
         Motor_X_SetPulse();

       if (Motor_X_Dir == CW) {Motor_X_Pos ++; X_pos ++; MX_pos ++;}
       else {Motor_X_Pos --; X_pos --; MX_pos --;}
      
         Cm_Count = Cm_Count + Cm_Divisor;
         if (Cm_Count > Cm_Divisor)
         {
            Cm_Count = Cm_Count - 10000;
            Cs_Count = 0;
         }
         else {Cs_Count = 1;}
      }
     }  
    }
   } 
    ///////////////////////////////////////////////////////////
   if (Step_X_flag == ON)
   {  Motor_X_RemovePulse();
      if ( (Motor_X_Dir == CW && Motor_X_Pos > Limit_Pos) || (Motor_X_Dir == CCW && Motor_X_Pos < Limit_Pos) || (!Joy_X_flag) )
      {
         if (tmp_Ks_Divisor < tmp_Accel)
         {
            Ks_Count++;
            if (Ks_Count > tmp_Ks_Divisor)
            {
               Motor_X_SetPulse();
               if (Motor_X_Dir == CW) {Motor_X_Pos ++; X_pos ++; MX_pos ++;}
               else {Motor_X_Pos --; X_pos --; MX_pos --;}
               Ks_Count = 0;
               if (++Repeat_Count == REPEAt)
               {
                  Repeat_Count = 0;
                  tmp_Ks_Divisor ++;
               }
            }
         }  
         else {Step_X_flag = OFF;}
      }

      else
      {
         Ks_Count++;
         if (Ks_Count > tmp_Ks_Divisor)
         {
            Motor_X_SetPulse();
            if (Motor_X_Dir == CW) {Motor_X_Pos ++; X_pos ++; MX_pos ++;}
            else {Motor_X_Pos --; X_pos --; MX_pos --;}
         
            if (tmp_Ks_Divisor > Ks_Divisor)
            {
               Ks_Count = 0;
               if (++Repeat_Count == REPEAt)
               {
                  Repeat_Count = 0;
                  tmp_Ks_Divisor --;
               }
            }
            else
            {  
               Km_Count = Km_Count + Km_Divisor;
               if (Km_Count > Km_Divisor)
               {
                  Km_Count = Km_Count - 10000;
                  Ks_Count = 0;
               }
               else {Ks_Count = 1;}
            }
         }
      }
   }
}



// [STM32] пустой ISR(INT1_vect) не нужен


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// *****  Tacho ***** ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// [STM32] пустой ISR(TIMER0_COMPB_vect) не нужен


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// *****  Feed & Cone ***** //////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void e2_isr_t5a(void)   // [STM32] было ISR(TIMER5_COMPA_vect)
{
     if (!Enc_Ch_A)
   {
      if (!Enc_Ch_B)
      {
         Spindle_Dir = CW;
         if (++Enc_Pos == ENC_TICK)
         {                                           
            Enc_Pos = 0;
            TachoSetPulse();
            if (Joy_Z_flag == ON) {Step_Z_flag = ON;}
            else if (Joy_X_flag == ON) {Step_X_flag = ON;}
         }
      }
      else
      {
        Spindle_Dir = CCW;
        if (--Enc_Pos < 0)
        { 
           Enc_Pos = ENC_TICK - 1;
           TachoSetPulse();
           if (Joy_Z_flag == ON) {Step_Z_flag = ON;}
           else if (Joy_X_flag == ON) {Step_X_flag = ON;}
        }
      }
   }
   
   else
   {
      if (!Enc_Ch_B) 
      {
         Spindle_Dir = CCW;
         if (--Enc_Pos < 0)
         { 
            Enc_Pos = ENC_TICK - 1;
            TachoSetPulse();
            if (Joy_Z_flag == ON) {Step_Z_flag = ON;}
            else if (Joy_X_flag == ON) {Step_X_flag = ON;}
         }
      }
      else 
      {
         Spindle_Dir = CW;
         if (++Enc_Pos == ENC_TICK)
         {                                           
            Enc_Pos = 0;
            TachoSetPulse();
            if (Joy_Z_flag == ON) {Step_Z_flag = ON;}
            else if (Joy_X_flag == ON) {Step_X_flag = ON;}
         }
      }
   }
   if (Joy_Z_flag == ON) {Motor_X_RemovePulse();}
   TachoRemovePulse();
   Tacho_Count = Tacho_Count + (OCR5A+1);
   if (Tacho_Count > ENC_LINE_PER_REV)
   {
      TachoSetPulse();
      Tacho_Count = Tacho_Count - ENC_LINE_PER_REV;
   }
   
   if ( (Motor_Z_Dir == CW && Motor_Z_Pos > Limit_Pos) || (Motor_Z_Dir == CCW && Motor_Z_Pos < Limit_Pos) || (!feed_Z_flag) )   
   {
      if (OCR5A < max_OCR5A)
      {
         Motor_Z_InvertPulse();
         if (!Read_Z_State)
         {
            OCR5A++;
            if (Motor_Z_Dir == CW) {Motor_Z_Pos ++; Z_pos ++;}
            else {Motor_Z_Pos --; Z_pos --;}
         }
      }
      else
      {             
         Step_Z_flag = OFF;
         Step_X_flag = OFF;
      }
   }
   
   else
   {
      Step_Z_flag = ON;
      Motor_Z_InvertPulse();
      if (!Read_Z_State)
      {
         if (Motor_Z_Dir == CW) {Motor_Z_Pos ++; Z_pos ++;}
         else {Motor_Z_Pos --; Z_pos --;}
      
         if (OCR5A > Feed_Divisor) {OCR5A--;}
         else if (OCR5A < Feed_Divisor) {OCR5A ++;}
      }
   }
   
   if (Step_X_flag == ON)
   {
      if (++Cs_Count > Cs_Divisor)
      {
         Motor_X_SetPulse();

         if (Motor_X_Dir == CW) {Motor_X_Pos ++; X_pos ++; MX_pos ++;}
         else {Motor_X_Pos --; X_pos --; MX_pos --;}

         Cm_Count = Cm_Count + Cm_Divisor;
         if (Cm_Count > Cm_Divisor)
         {
            Cm_Count = Cm_Count - 10000;
            Cs_Count = 0;
         }
         else {Cs_Count = 1;}
      }
   }
}

////////////////////////////////////////////////////////////
void e2_isr_t5b(void)   // [STM32] было ISR(TIMER5_COMPB_vect)
{
   TachoRemovePulse();   
   Tacho_Count = Tacho_Count + (OCR5A+1);
   if (Tacho_Count > ENC_LINE_PER_REV)
   {
      TachoSetPulse();
      Tacho_Count = Tacho_Count - ENC_LINE_PER_REV;
   }
  
   if ( (Motor_X_Dir == CW && Motor_X_Pos > Limit_Pos) || (Motor_X_Dir == CCW && Motor_X_Pos < Limit_Pos) || (!feed_X_flag) )
   {
      if (OCR5A < max_OCR5A)
      {
         Motor_X_InvertPulse();
         if (!Read_X_State)
         {
            OCR5A++;
            if (Motor_X_Dir == CW) {Motor_X_Pos ++; X_pos ++; MX_pos ++;}
            else {Motor_X_Pos --; X_pos --; MX_pos --;}
         }
      }
      else {Step_X_flag = OFF;}
   }
   
   else 
   {
      Step_X_flag = ON;
      Motor_X_InvertPulse();
      {
         if (!Read_X_State)
         {
            if (Motor_X_Dir == CW) {Motor_X_Pos ++; X_pos ++; MX_pos ++;}
            else {Motor_X_Pos --; X_pos --; MX_pos --;}
      
            if (OCR5A > Feed_Divisor) {OCR5A--;}
            else if (OCR5A < Feed_Divisor) {OCR5A ++;}
         }
      }
   }
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** Rapid Feed & Rapid Cone ***** ///////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void e2_isr_t2a(void)   // [STM32] было ISR(TIMER2_COMPA_vect)
{
   Motor_X_RemovePulse();
   if ( (Motor_Z_Dir == CW && Motor_Z_Pos > Limit_Pos) || (Motor_Z_Dir == CCW && Motor_Z_Pos < Limit_Pos) || (!rapid_Z_flag) )  
   {
      if (OCR2A < MIN_RAPID_MOTION)
      {
         Motor_Z_InvertPulse();
         if (!Read_Z_State)
         {
            if (Motor_Z_Dir == CW) { Motor_Z_Pos ++; Z_pos ++;}
            else { Motor_Z_Pos --; Z_pos --;}

            if (++Repeat_Count == REPEAt)
            {
               Repeat_Count = 0;
               OCR2A ++;
            }
         }
      }
      else
      {
         rapid_step_Z_flag = OFF;
         Step_X_flag = OFF;
      }
   }
  
   else
   {
      rapid_step_Z_flag = ON;
      Motor_Z_InvertPulse();
      if (!Read_Z_State)
      {
         if (Motor_Z_Dir == CW) { Motor_Z_Pos ++; Z_pos ++;}
         else { Motor_Z_Pos --; Z_pos --;}

         if (OCR2A > MAX_RAPID_MOTION)
         {
            if (++Repeat_Count == REPEAt)
            {
               Repeat_Count = 0;
               OCR2A --;
            }
         }
      }
   }
   
   ///////////////////////////////////////////////////////
   if (Step_X_flag == ON)
   {
      if (++Cs_Count > Cs_Divisor)
      {
         Motor_X_SetPulse();
         if (Motor_X_Dir == CW) { Motor_X_Pos ++; X_pos ++; MX_pos ++;}
         else { Motor_X_Pos --; X_pos --; MX_pos --;}
         
         Cm_Count = Cm_Count + Cm_Divisor;
         if (Cm_Count > Cm_Divisor)
         {
            Cm_Count = Cm_Count - 10000;
            Cs_Count = 0;
         }
         else {Cs_Count = 1;}
      }
   }
}

//////////////////////////////////////////////////////////
void e2_isr_t2b(void)   // [STM32] было ISR(TIMER2_COMPB_vect)
{
   if ( (Motor_X_Dir == CW && Motor_X_Pos > Limit_Pos) || (Motor_X_Dir == CCW && Motor_X_Pos < Limit_Pos) || (!rapid_X_flag) )
   {
      if (OCR2A < MIN_RAPID_MOTION)
      {
         Motor_X_InvertPulse();
         if (!Read_X_State)
         {
            if (Motor_X_Dir == CW) { Motor_X_Pos ++; X_pos ++; MX_pos ++;}
            else { Motor_X_Pos --; X_pos --; MX_pos --;}

            if (++Repeat_Count == REPEAt)
            {
               Repeat_Count = 0;
               OCR2A ++;
            }
         }
      }
      else  {rapid_step_X_flag = OFF;}
   }
  
   else
   {
      rapid_step_X_flag = ON;
      Motor_X_InvertPulse();
      if (!Read_X_State)
      {
         if (Motor_X_Dir == CW) { Motor_X_Pos ++; X_pos ++; MX_pos ++;}
         else { Motor_X_Pos --; X_pos --; MX_pos --;}

         if (OCR2A > MAX_RAPID_MOTION)
         {
            if (++Repeat_Count == REPEAt)
            {
               Repeat_Count = 0;
               OCR2A --;
            }
         }
      }
   }
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** Asynchron Feed ***** ////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void e2_isr_t4a(void)   // [STM32] было ISR(TIMER4_COMPA_vect)
{

   if (Joy_Z_flag == ON) {Motor_X_RemovePulse();}
   TachoRemovePulse();
   Tacho_Count = Tacho_Count + (OCR5A+1);
   if (Tacho_Count > ENC_LINE_PER_REV)
   {
      TachoSetPulse();
      Tacho_Count = Tacho_Count - ENC_LINE_PER_REV;
   }
  
   if ( (Motor_Z_Dir == CW && Motor_Z_Pos > Limit_Pos) || (Motor_Z_Dir == CCW && Motor_Z_Pos < Limit_Pos) || (!feed_Z_flag) )
   {
      if (OCR4A < max_OCR4A)
      {
         Motor_Z_InvertPulse();
         if (!Read_Z_State)
         {
            OCR4A ++;
            if (Motor_Z_Dir == CW) {Motor_Z_Pos ++; Z_pos ++;}
            else                   {Motor_Z_Pos --; Z_pos --;}
         }
      }
      else
      {             
         Step_Z_flag = OFF;
         Step_X_flag = OFF;
      }
   }
   
   else
   {
      Step_Z_flag = ON;
      Motor_Z_InvertPulse();
      if (!Read_Z_State)
      {
         if (Motor_Z_Dir == CW) {Motor_Z_Pos ++; Z_pos ++;}
         else                   {Motor_Z_Pos --; Z_pos --;}

         if      (OCR4A > aFeed_Divisor) {OCR4A --;}
         else if (OCR4A < aFeed_Divisor) {OCR4A ++;}
      }
   }
}

//////////////////////////////////////////////////////////
void e2_isr_t4b(void)   // [STM32] было ISR(TIMER4_COMPB_vect)
{
   if ( (Motor_X_Dir == CW && Motor_X_Pos > Limit_Pos) || (Motor_X_Dir == CCW && Motor_X_Pos < Limit_Pos) || (!feed_X_flag) )
   {
      if (OCR4A < max_OCR4A)
      {
         Motor_X_InvertPulse();
         if (!Read_X_State)
         {
            OCR4A ++;
            if (Motor_X_Dir == CW) {Motor_X_Pos ++; X_pos ++; MX_pos ++;}
            else                   {Motor_X_Pos --; X_pos --; MX_pos --;}
         }
      }
      else
      {             
         Step_Z_flag = OFF;
         Step_X_flag = OFF;
      }
   }
   
   else 
   {
      Step_X_flag = ON;
      Motor_X_InvertPulse();
      if (!Read_X_State)
      {
         if (Motor_X_Dir == CW) {Motor_X_Pos ++; X_pos ++; MX_pos ++;}
         else                   {Motor_X_Pos --; X_pos --; MX_pos --;}

         if      (OCR4A > aFeed_Divisor) {OCR4A --;}
         else if (OCR4A < aFeed_Divisor) {OCR4A ++;}
      }
   }
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** HandCoder ***** /////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void e2_isr_int2(void)   // [STM32] было ISR(INT2_vect)
{
   if (!Hand_Ch_A)
   {
      if (!Hand_Ch_B) {Hand_Count --;}
   }
   
   else
   {
      if (!Hand_Ch_B) {Hand_Count ++;}
   }

}

/////////////////////////////////////////////
void e2_isr_t3a(void)   // [STM32] было ISR(TIMER3_COMPA_vect)
{   
   if (Motor_Z_Dir == CW)
   {
      if (Motor_Z_Pos < Null_Z_Pos + Hand_Z_Pos)
      {
         Motor_Z_InvertPulse();
         if (!Read_Z_State) 
         {
            Motor_Z_Pos ++; Z_pos ++;
            if ((Motor_Z_Pos > Limit_Pos_HC) || (hand_Z == OFF))
            {
               if (OCR3A < max_OCR3A) OCR3A ++;
            }
            else if (Motor_Z_Pos < Limit_Pos_HC)
            {
               if (OCR3A > min_OCR3A) OCR3A --;
            }
         }
      }
      else if (Motor_Z_Pos == Hand_Z_Pos)
      {
      //
      }
   }

   else if (Motor_Z_Dir == CCW)
   {
      if (Motor_Z_Pos > Null_Z_Pos + Hand_Z_Pos)
      {
         Motor_Z_InvertPulse();
         if (!Read_Z_State) 
         {
            Motor_Z_Pos --; Z_pos --;
            if (Motor_Z_Pos < Limit_Pos_HC  || hand_Z == OFF)
            {
               if (OCR3A < max_OCR3A) OCR3A ++;
            }
            else if (Motor_Z_Pos > Limit_Pos_HC)
            {
               if (OCR3A > min_OCR3A) OCR3A --;
            }
         }
      }
      else if (Motor_Z_Pos == Hand_Z_Pos)
      {
      //
      }
   }
}

//////////////////////////////////////////////////////////
void e2_isr_t3b(void)   // [STM32] было ISR(TIMER3_COMPB_vect)
{   
   if (Motor_X_Dir == CW)
   {
      if (Motor_X_Pos < Null_X_Pos + Hand_X_Pos)
      {
         Motor_X_InvertPulse();
         if (!Read_X_State) 
         {
            Motor_X_Pos ++; 
            X_pos ++;
            MX_pos ++;
            if ((Motor_X_Pos > Limit_Pos_HC) || (hand_X == OFF))
            {
               if (OCR3A < max_OCR3A) OCR3A ++;
            }
            else if (Motor_X_Pos < Limit_Pos_HC)
            {
               if (OCR3A > min_OCR3A) OCR3A --;
            }
         }
      }
      else if (Motor_X_Pos == Hand_X_Pos)
      {             
      //
      }
   }

   else if (Motor_X_Dir == CCW)
   {
      if (Motor_X_Pos > Null_X_Pos + Hand_X_Pos)
      {
         Motor_X_InvertPulse();
         if (!Read_X_State) 
         {
            Motor_X_Pos --; 
            X_pos --;
            MX_pos --;
            if ((Motor_X_Pos < Limit_Pos_HC) || (hand_X == OFF))
            {
               if (OCR3A < max_OCR3A) OCR3A ++;
            }
            else if (Motor_X_Pos > Limit_Pos_HC)
            {
               if (OCR3A > min_OCR3A) OCR3A --;
            }
         }
      }
      else if (Motor_X_Pos == Hand_X_Pos)
      {             
      //
      }
   }
}


// ***** End ***** ///////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// [STM32] Дополнения, которых не было в 7e2
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Spindle_On.ino: «шпиндель вращается» проверялся раз в 750 проходов loop() (~1 с на Mega).
// loop() на STM32 в десятки раз быстрее, поэтому тот же интервал отмеряется по millis().
void Spindle()
{
   static uint32_t s_t = 0;
   if (millis() - s_t >= 1000)   // [STM32] было: if (++Spindle_Count > 750)
   {
      s_t = millis();
      if (Tacho_Count == Tacho_Count_Old) {spindle_flag = OFF;}
      else                                {spindle_flag = ON;}
      Tacho_Count_Old = Tacho_Count;
   }
}

// Координаты от блока DRO HS800-2 или линеек — в dro_pos_x/dro_pos_y (0.001 мм),
// дальше loop() пересчитывает их так же, как DRO_HS800.ino. Ось без линейки
// (SCALE_NONE) считается по шагам мотора, как в 7e2 без DRO.
void e2_pos_update(void)
{
#if USE_DRO_HS800
   DRV_DRO_Process();
   dro_pos_x = DRV_DRO_GetX();
   dro_pos_y = DRV_DRO_GetY();
   e2_diam_um = 2 * dro_pos_x;
#elif ELS_POS_SOURCE == POS_SOURCE_SCALES
   DRV_Scales_Process();
   if (DRV_Scales_Present(SCALE_AXIS_X)) {
      int32_t raw = DRV_Scales_GetUm(SCALE_AXIS_X);
      dro_pos_x = raw - e2_scale_off[SCALE_AXIS_X];
      e2_diam_um = 2 * raw - e2_diam_off;
   } else {
      dro_pos_x = -(int32_t)(X_pos / ((float)MOTOR_X_STEP_PER_REV / SCREW_X * McSTEP_X)) * 10;
      e2_diam_um = -(int32_t)(MX_pos / ((float)MOTOR_X_STEP_PER_REV / SCREW_X / 2 * McSTEP_X)) * 10;
   }
   if (DRV_Scales_Present(SCALE_AXIS_Z)) dro_pos_y = DRV_Scales_GetUm(SCALE_AXIS_Z) - e2_scale_off[SCALE_AXIS_Z];
   else dro_pos_y = -(int32_t)(Z_pos / ((float)MOTOR_Z_STEP_PER_REV / SCREW_Z * McSTEP_Z)) * 10;

   // Пропала связь с линейкой (протоколы с тактом) — предупредить один раз
   static uint8_t s_lost[2] = {0, 0};
   for (uint8_t i = 0; i < 2; i++) {
      ScaleAxis ax = (ScaleAxis)i;
      uint8_t lost = DRV_Scales_Present(ax) && !DRV_Scales_IsOk(ax) && millis() > 3000;
      if (lost && !s_lost[i]) {
         Serial.printf("[SCALE] ось %s (%s): нет данных от линейки\n",
                       i == SCALE_AXIS_Z ? "Z" : "X", DRV_Scales_TypeName(ax));
         Display_Send_CMD("ALERT", i == SCALE_AXIS_Z ? "НЕТ СВЯЗИ С ЛИНЕЙКОЙ Z" : "НЕТ СВЯЗИ С ЛИНЕЙКОЙ X");
      }
      s_lost[i] = lost;
   }
#endif
}

static int _Display_GetSubMode();   // e2_display.inc

// Монитор входов для проверки проводки (IO_MONITOR в els_config.h): каждое изменение
// кнопки, переключателя или упора, вращение энкодеров — строкой в отладочный порт.
#if IO_MONITOR
struct E2InPin { const char* name; GPIO_TypeDef* port; uint16_t bit; };
#define E2_IN(name, ...) { name, E2_GPIO(__VA_ARGS__), (uint16_t)E2_BIT(__VA_ARGS__) }
static const E2InPin s_inputs[] = {
   E2_IN("Режим: Синхронная подача", E2_PIN_MODE_FEED),   E2_IN("Режим: Асинхронная подача", E2_PIN_MODE_AFEED),
   E2_IN("Режим: Конус <", E2_PIN_MODE_CONE_L),           E2_IN("Режим: Конус >", E2_PIN_MODE_CONE_R),
   E2_IN("Режим: Резьба", E2_PIN_MODE_THREAD),            E2_IN("Режим: Шар", E2_PIN_MODE_SPHERE),
   E2_IN("Режим: Делилка", E2_PIN_MODE_DIVIDER),          E2_IN("Режим: Резерв", E2_PIN_MODE_RESERVE),
   E2_IN("Подрежим: Внутренний", E2_PIN_SUB_INT),         E2_IN("Подрежим: Ручной", E2_PIN_SUB_MAN),
   E2_IN("Подрежим: Наружный", E2_PIN_SUB_EXT),
   E2_IN("Кнопка Влево", E2_PIN_BTN_LEFT),  E2_IN("Кнопка Вправо", E2_PIN_BTN_RIGHT),
   E2_IN("Кнопка Вверх", E2_PIN_BTN_UP),    E2_IN("Кнопка Вниз", E2_PIN_BTN_DOWN),
   E2_IN("Кнопка Выбор", E2_PIN_BTN_SELECT),
   E2_IN("Джойстик Влево", E2_PIN_JOY_LEFT), E2_IN("Джойстик Вправо", E2_PIN_JOY_RIGHT),
   E2_IN("Джойстик Вверх", E2_PIN_JOY_UP),   E2_IN("Джойстик Вниз", E2_PIN_JOY_DOWN),
   E2_IN("Кнопка ускоренного хода", E2_PIN_JOY_RAPID),
   E2_IN("Упор Левый", E2_PIN_LIM_LEFT),  E2_IN("Упор Правый", E2_PIN_LIM_RIGHT),
   E2_IN("Упор Передний", E2_PIN_LIM_FRONT), E2_IN("Упор Задний", E2_PIN_LIM_REAR),
   E2_IN("РГИ: ось Z", E2_PIN_HAND_AXIS_Z), E2_IN("РГИ: ось X", E2_PIN_HAND_AXIS_X),
   E2_IN("РГИ: масштаб x1", E2_PIN_HAND_X1), E2_IN("РГИ: масштаб x10", E2_PIN_HAND_X10),
};
static void e2_io_monitor(void)
{
   static uint8_t s_prev[sizeof(s_inputs) / sizeof(s_inputs[0])];
   static bool s_init = false;
   for (size_t i = 0; i < sizeof(s_inputs) / sizeof(s_inputs[0]); i++) {
      uint8_t v = (s_inputs[i].port->IDR & s_inputs[i].bit) ? 1 : 0;
      if (s_init && v != s_prev[i])
         Serial.printf("[IO] %s: %s\n", s_inputs[i].name, v ? "отпущено (1)" : "замкнуто на GND (0)");
      s_prev[i] = v;
   }
   s_init = true;

   static uint32_t s_t = 0, s_edges = 0;
   static long s_hand = 0;
   if (millis() - s_t >= 1000) {
      s_t = millis();
      uint32_t e = e2_spindle_edges();
      if (e != s_edges)
         Serial.printf("[IO] Энкодер шпинделя: %lu фронтов A за 1 с (%lu об/мин), угол %ld.%03ld°, %s\n",
                       (unsigned long)(e - s_edges), (unsigned long)((e - s_edges) * 60UL / ENC_TICK),
                       (long)(Spindle_Angle / 1000), (long)(Spindle_Angle % 1000), Spindle_Dir == CW ? "по часовой" : "против часовой");
      s_edges = e;
      long h = Hand_Count;
      if (h != s_hand) Serial.printf("[IO] РГИ: %+ld щелчков (всего %ld)\n", h - s_hand, h);
      s_hand = h;
   }
}
#endif

// Сообщение, которое сейчас горит на LCD2004 (порядок как в Print.ino): 4 — джойстик в нейтраль,
// 1 — установите упоры, 2 — суппорт в исходное, 3 — операция завершена, 0 — нет.
// -1 — отправить заново (Display_Send_All, в т.ч. по READY дисплея).
int8_t e2_alert_sent = -1;
static int8_t e2_alert_now(void)
{
   if (err_0_flag)    return 4;
   if (err_1_flag)    return 1;
   if (err_2_flag)    return 2;
   if (Complete_flag) return 3;
   return 0;
}

// [STM32] Компенсация люфта (BACKLASH_*_UM в els_config.h, в 7e2 её нет). На первом шаге после смены
// направления позиция мотора сдвигается назад на люфт — ядро само делает на столько шагов больше (со
// своим разгоном, в резьбе — в такт шпинделю), и каретка приходит точно в цель. Счётчики остаются
// положением каретки. Сдвиг кратен McSTEP: loop() округляет позиции до полного шага.
#define E2_BL_STEPS(um, screw, spr, mc) \
   ((long)(((double)(um) / 10.0 / (screw) * (spr) + 0.5)) * (mc))
static long e2_bl_z = E2_BL_STEPS(BACKLASH_Z_UM, SCREW_Z, MOTOR_Z_STEP_PER_REV, McSTEP_Z);   // e2_mech_apply
static long e2_bl_x = E2_BL_STEPS(BACKLASH_X_UM, SCREW_X, MOTOR_X_STEP_PER_REV, McSTEP_X);
int8_t e2_bl_loaded_z = 0, e2_bl_loaded_x = 0;   // сторона, в которую выбран люфт (0 — неизвестно)

void e2_bl_turn_z(int8_t d)                      // из прерываний шагов (E2_BL_STEP_Z)
{
   if (e2_bl_z && e2_bl_loaded_z) { long s = d * e2_bl_z; Motor_Z_Pos -= s; Z_pos -= s; }
   e2_bl_loaded_z = d;
}

void e2_bl_turn_x(int8_t d)
{
   if (e2_bl_x && e2_bl_loaded_x) { long s = d * e2_bl_x; Motor_X_Pos -= s; X_pos -= s; MX_pos -= s; }
   e2_bl_loaded_x = d;
}

// [STM32] Механика из настроек (BACKLOG-028, e2_mech.h). Проверки — те же, что static_assert автора
// для els_config.h, плюс делители таблиц резьб и конусов: они хранятся в byte, а мотор должен успевать
// за шпинделем (делитель ≥ 1). Таблицы считаются по формулам tools/gen_e2_tables.py.
static thread_info_type e2_thread_author[TOTAL_THREADS];   // таблицы автора — для механики-отметки
static cone_info_type   e2_cone_author[TOTAL_CONE];
static bool e2_author_saved = false;

static bool e2_mech_is_stamp(const E2Mech& m)
{
   return m.enc_lines == E2_TABLES_ENC &&
          (long)m.z_steps * m.z_mc * E2_TABLES_SCREW_Z == (long)E2_TABLES_Z_STEPS * m.z_screw &&
          (long)m.x_steps * m.x_mc * E2_TABLES_SCREW_X == (long)E2_TABLES_X_STEPS * m.x_screw;
}

// Шаг резьбы, мм: дюймовые и трубные — точно 25.4 / ниток на дюйм, метрические — колонка Step.
// Расчёт в double, константы целые: при -fsingle-precision-constant дробный литерал — float.
static double e2_thread_pitch(const thread_info_type& t)
{
   const char* p = t.Thread_Print;
   while (*p == ' ') p++;
   if (strstr(p, "tpi")) return (double)254 / (10 * atoi(p));
   if (*p == 'G' || *p == 'K') {
      const char* sz = p + 1; while (*sz == ' ') sz++;
      double tpi;
      if (!strncmp(sz, "1/16", 4) || !strncmp(sz, "1/8", 3)) tpi = *p == 'G' ? 28 : 27;
      else if (!strncmp(sz, "1/4", 3) || !strncmp(sz, "3/8", 3)) tpi = *p == 'G' ? 19 : 18;
      else if (!strncmp(sz, "1/2", 3) || !strncmp(sz, "3/4", 3)) tpi = 14;
      else tpi = *p == 'G' ? 11 : (double)23 / 2;
      return (double)254 / (10 * tpi);
   }
   return (double)t.Step;
}

// Уклон образующей конуса на радиус: угол — тангенс, 1:N — на диаметр → /2, KM — конусы Морзе
static double e2_cone_slope(const cone_info_type& c)
{
   static const int morse_x1000[] = {19212, 20047, 20020, 19922, 19254, 19002, 19180};   // KM0…KM6, 1:N
   const char* p = c.Cone_Print;
   while (*p == ' ') p++;
   if (p[0] == 'K' && p[1] == 'M') return (double)1000 / (2 * morse_x1000[p[2] - '0']);
   const char* colon = strchr(p, ':');
   if (colon) return (double)atoi(p) / atoi(colon + 1) / 2;
   return tan(atoi(p) * (4 * atan((double)1)) / 180);
}

static void e2_split(double d, byte& ks, int& km)
{
   long s = (long)d;
   long m = lround((d - s) * 10000);
   if (m == 10000) { s++; m = 0; }
   ks = (byte)s; km = (int)m;
}

// Делители под механику m → в таблицы (или проверка, если out == false). nullptr — все годятся.
static const char* e2_mech_tables(const E2Mech& m, bool out)
{
   static char err[64];
   double z_spm = (double)m.z_steps * m.z_mc / ((double)m.z_screw / 100);   // шагов Z на мм
   double x_spm = (double)m.x_steps * m.x_mc / ((double)m.x_screw / 100);
   double tick = (double)2 * m.enc_lines;
   for (unsigned i = 0; i < TOTAL_THREADS; i++) {
      double p = e2_thread_pitch(Thread_Info[i]);
      double dz = tick / (p * z_spm), dx = tick / (p * x_spm);
      if (dz < 1 || dx < 1 || dz + THRD_ACCEL > 255 || dx + THRD_ACCEL > 255) {
         snprintf(err, sizeof(err), "РЕЗЬБА %.6s: %s", Thread_Info[i].Thread_Print,
                  (dz < 1 || dx < 1) ? "МОТОР НЕ УСПЕЕТ" : "ДЕЛИТЕЛЬ > 255");
         return err;
      }
      if (out) {
         e2_split(dz, Thread_Info[i].Ks_Div_Z, Thread_Info[i].Km_Div_Z);
         e2_split(dx, Thread_Info[i].Ks_Div_X, Thread_Info[i].Km_Div_X);
      }
   }
   for (unsigned i = 0; i < TOTAL_CONE; i++) {
      double d = 2 * z_spm / (x_spm * e2_cone_slope(Cone_Info[i]));
      if (d < 1 || d >= 256) {
         snprintf(err, sizeof(err), "КОНУС %.5s: ДЕЛИТЕЛЬ ВНЕ 1…255", Cone_Info[i].Cone_Print);
         return err;
      }
      if (out) e2_split(d, Cone_Info[i].Cs_Div, Cone_Info[i].Cm_Div);
   }
   return nullptr;
}

const char* e2_mech_check(const E2Mech& m)
{
   auto pow2 = [](int v) { return v >= 1 && v <= 128 && (v & (v - 1)) == 0; };
   if (m.enc_lines < 100 || m.enc_lines > 10000) return "ЭНКОДЕР: 100…10000 ЛИНИЙ";
   if (m.z_steps < 50 || m.z_steps > 1000 || m.x_steps < 50 || m.x_steps > 1000) return "ШАГОВ НА ОБОРОТ: 50…1000";
   if (!pow2(m.z_mc) || !pow2(m.x_mc)) return "МИКРОШАГ: 1, 2, 4 … 128";
   if (m.z_screw < 25 || m.z_screw > 1000 || m.x_screw < 25 || m.x_screw > 1000) return "ШАГ ВИНТА: 0.25…10 ММ";
   if (m.bl_z_um < 0 || m.bl_z_um > 2000 || m.bl_x_um < 0 || m.bl_x_um > 2000) return "ЛЮФТ: 0…2000 МКМ";
   // как static_assert автора (CHECK_MIN_FEED_* и др.) — пределы подач укладываются в таймеры
   const int ax[2][3] = {{m.z_steps, m.z_mc, m.z_screw}, {m.x_steps, m.x_mc, m.x_screw}};
   for (auto& a : ax) {
      float spr = (float)a[0] * a[1];
      if ((uint32_t)(m.enc_lines / (spr * MIN_FEED / a[2]) / 2) > 255) return "MIN_FEED: ДЕЛИТЕЛЬ > 255";
      if ((uint32_t)(m.enc_lines / (spr * MAX_FEED / a[2]) / 2) <= 1)  return "MAX_FEED: ДЕЛИТЕЛЬ ≤ 1";
      uint32_t k_min = (uint32_t)MIN_aFEED * a[0] * a[1] / ((uint32_t)60 * a[2] / 100) * 2;
      uint32_t k_max = (uint32_t)MAX_aFEED * a[0] * a[1] / ((uint32_t)60 * a[2] / 100) * 2;
      if (k_min == 0 || 250000 / k_min - 1 > 65535) return "MIN_aFEED: ДЕЛИТЕЛЬ > 65535";
      if (k_max == 0 || 250000 / k_max - 1 <= 1)    return "MAX_aFEED: ДЕЛИТЕЛЬ ≤ 1";
   }
   if (e2_mech_is_stamp(m)) return nullptr;            // таблицы автора
   return e2_mech_tables(m, false);
}

const char* e2_mech_apply(const E2Mech& m)
{
   const char* err = e2_mech_check(m);
   if (err) return err;
   if (!e2_author_saved) {
      memcpy(e2_thread_author, Thread_Info, sizeof(Thread_Info));
      memcpy(e2_cone_author, Cone_Info, sizeof(Cone_Info));
      e2_author_saved = true;
   }
   e2_mech = m;
   if (e2_mech_is_stamp(m)) {
      memcpy(Thread_Info, e2_thread_author, sizeof(Thread_Info));
      memcpy(Cone_Info, e2_cone_author, sizeof(Cone_Info));
   } else {
      e2_mech_tables(m, true);
   }
   // то же, что инициализация глобальных переменных автора
   Start_Speed = ENC_LINE_PER_REV / ((uint32_t)MOTOR_Z_STEP_PER_REV * McSTEP_Z * MIN_FEED / SCREW_Z) /FEED_ACCEL;
   max_OCR5A = ENC_LINE_PER_REV / ((uint32_t)MOTOR_Z_STEP_PER_REV * McSTEP_Z * MIN_FEED / SCREW_Z) /FEED_ACCEL;
   max_OCR4A = (250000 / ((uint32_t)MIN_aFEED * MOTOR_Z_STEP_PER_REV * McSTEP_Z / ((uint32_t)60 * SCREW_Z / 100) * 2) - 1) /FEED_ACCEL;
   e2_bl_z = E2_BL_STEPS(BACKLASH_Z_UM, SCREW_Z, MOTOR_Z_STEP_PER_REV, McSTEP_Z);
   e2_bl_x = E2_BL_STEPS(BACKLASH_X_UM, SCREW_X, MOTOR_X_STEP_PER_REV, McSTEP_X);
   return nullptr;
}

// Станок стоит: шпиндель не крутится (обороты — e2_service, раз в 250 мс), джойстик в нейтрали.
// Только тогда можно менять механику и писать флеш (стирание сектора останавливает процессор на 1–2 с).
uint32_t e2_rpm = 0;
bool e2_machine_idle(void)
{
   return e2_rpm == 0 && Joy_Z_flag == OFF && Joy_X_flag == OFF && !rapid_Z_flag && !rapid_X_flag;
}

// Механика для экрана: <CFG:энкодер,шагов Z,микрошаг Z,винт Z,шагов X,микрошаг X,винт X,люфт Z,люфт X>
void e2_mech_send(void)
{
   char b[80];
   const E2Mech& m = e2_mech;
   snprintf(b, sizeof(b), "%d,%d,%d,%d,%d,%d,%d,%d,%d", m.enc_lines, m.z_steps, m.z_mc, m.z_screw,
            m.x_steps, m.x_mc, m.x_screw, m.bl_z_um, m.bl_x_um);
   Display_Send_CMD("CFG", b);
}

// Команда экрана: применить и сохранить (CFG_SET) или вернуть els_config.h (CFG_DEFAULT).
// Ответ — <CFG_RES:OK> или причина отказа; затем текущие настройки.
void e2_mech_from_display(const char* prm)
{
   E2Mech m = E2_MECH_CONFIG;
   const char* err = nullptr;
   if (strncmp(prm, "CFG_SET:", 8) == 0 &&
       sscanf(prm + 8, "%d,%d,%d,%d,%d,%d,%d,%d,%d", &m.enc_lines, &m.z_steps, &m.z_mc, &m.z_screw,
              &m.x_steps, &m.x_mc, &m.x_screw, &m.bl_z_um, &m.bl_x_um) != 9)
      err = "НЕВЕРНАЯ КОМАНДА";
   if (!err && !e2_machine_idle()) err = "ОСТАНОВИТЕ ШПИНДЕЛЬ И ДВИЖЕНИЕ";
   if (!err) err = e2_mech_apply(m);
   if (!err) err = e2_mech_save();
   Serial.print(F("[CFG] ")); Serial.println(err ? err : "сохранено");
   Display_Send_CMD("CFG_RES", err ? err : "OK");
   if (!err) Beep(); else Beep_Error();
   e2_mech_send();
}

// Запись во флеше: метка, версия, механика, контрольная сумма
struct E2Settings {
   uint32_t magic;
   uint32_t version;
   E2Mech   mech;
   uint32_t sum;
};
#define E2_SETTINGS_MAGIC   0x45325354UL   // "E2ST"
#define E2_SETTINGS_VERSION 1

static uint32_t e2_settings_sum(const E2Settings& s)
{
   const uint8_t* b = (const uint8_t*)&s;
   uint32_t h = 2166136261UL;                  // FNV-1a по всем полям, кроме суммы
   for (size_t i = 0; i < offsetof(E2Settings, sum); i++) h = (h ^ b[i]) * 16777619UL;
   return h;
}

void e2_mech_load(void)
{
   E2Settings s;
   const char* err = "НЕТ";
   if (e2_store_read(&s, sizeof(s)) && s.magic == E2_SETTINGS_MAGIC && s.version == E2_SETTINGS_VERSION &&
       s.sum == e2_settings_sum(s)) {
      err = e2_mech_apply(s.mech);
      if (!err) { Serial.println(F("[CFG] механика — из флеша")); return; }
   }
   e2_mech_apply(E2_MECH_CONFIG);
   Serial.print(F("[CFG] механика — из els_config.h (во флеше: ")); Serial.print(err); Serial.println(')');
}

const char* e2_mech_save(void)
{
   E2Settings s;
   memset(&s, 0, sizeof(s));
   s.magic = E2_SETTINGS_MAGIC;
   s.version = E2_SETTINGS_VERSION;
   s.mech = e2_mech;
   s.sum = e2_settings_sum(s);
   return e2_store_write(&s, sizeof(s)) ? nullptr : "ОШИБКА ЗАПИСИ ВО ФЛЕШ";
}

// [STM32] Разрешение кнопок упоров (BACKLOG-022): переключатель подрежима в «Ручной», как у автора,
// или (LIMITS_BY_SELECTED_SUBMODE) выбранный подрежим текущего режима — «Ручной» (с экрана)
bool e2_limit_buttons_allowed(void)
{
   if (Submode_Read == B10100000) return true;
#if LIMITS_BY_SELECTED_SUBMODE
   switch (Mode) {
      case Mode_Thread: return Sub_Mode_Thread == Sub_Mode_Thread_Man;
      case Mode_Feed:   return Sub_Mode_Feed   == Sub_Mode_Feed_Man;
      case Mode_aFeed:  return Sub_Mode_aFeed  == Sub_Mode_aFeed_Man;
      case Mode_Cone_L:
      case Mode_Cone_R: return Sub_Mode_Cone   == Sub_Mode_Cone_Man;
      case Mode_Sphere: return Sub_Mode_Sphere == Sub_Mode_Sphere_Man;
   }
#endif
   return false;
}

// Обороты шпинделя для дисплея ESP32 и строка состояния LCD2004 для автотестов
void e2_service(void)
{
#if IO_MONITOR
   e2_io_monitor();
#endif
   static uint32_t s_rpm_t = 0, s_rpm_edges = 0, s_lcd_t = 0;
   uint32_t now = millis();
   if (now - s_rpm_t >= 250) {
      uint32_t edges = e2_spindle_edges();
      uint32_t rpm = (uint32_t)((uint64_t)(edges - s_rpm_edges) * 60000u / ((now - s_rpm_t) * (uint32_t)ENC_TICK));
      s_rpm_edges = edges;
      s_rpm_t = now;
      Display_Send_Int("RPM", rpm);
      e2_rpm = rpm;                             // [STM32] для e2_machine_idle()
   }
   // Значения, которые UartDisplay шлёт только в Display_Send_All, — по изменению (e2_display.inc)
   Display_Watch();
   // Сообщения на новом дисплее горят ровно столько же, сколько на LCD2004: ядро ставит и снимает
   // флаги ошибок в разных местах и не везде шлёт ALERT — отправляем по изменению флагов
   int8_t al = e2_alert_now();
   if (al != e2_alert_sent) {
      e2_alert_sent = al;
      Display_Send_Alert(al);
   }
   if (now - s_lcd_t >= 500) {
      s_lcd_t = now;
      // <LCD2004:mode,sub,sm,feed,afeed,ap,pass_nr,pass_total,ph,pass_fin,thr,tooth_n,tooth_c>, режим и подрежим с 0
      char buf[80];
      snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%d,%ld,%ld,%d,%d,%d,%d,%d",
               Mode - 1, _Display_GetSubMode() - 1, SelectMenu, Feed_mm, aFeed_mm, Ap,
               (long)Pass_Nr, (long)Pass_Total, Ph, PASS_FINISH + Pass_Fin, Thread_Step,
               Total_Tooth, Current_Tooth);
      Display_Send_CMD("LCD2004", buf);
   }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Остальные файлы скетча 7e2 — без изменений (порядок как у Arduino IDE).
// Не подключаются: DRO_HS800.ino (вместо него Drivers/drv_dro), Spindle_On.ino (Spindle() выше),
// UartDisplay.ino (адаптированная копия — e2_display.inc).
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#undef  A7
#define A7 E2_APIN(E2_PIN_FEED_POT)     // ADC.ino: analogRead(A7) — переменник подачи
#include "e2_display.inc"
#include "orig/ADC.ino"
#include "orig/Beeper.ino"
#include "orig/Cone.ino"
#include "orig/Feed.ino"
#include "orig/HandCoder.ino"
#include "orig/Menu.ino"
#include "orig/Print.ino"
#include "orig/SerialEmulation.ino"
#include "orig/Sphere.ino"
#include "orig/TestPresets.ino"
#include "orig/Thread.ino"
#include "orig/aFeed.ino"

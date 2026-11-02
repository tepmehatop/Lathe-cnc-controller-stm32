                                                   // ***** ПАРАМЕТРЫ ЖЕЛЕЗА ***** //

// ── DRO HS800-2 (раскомментируй чтобы включить) ──────────────────────────────
#define USE_DRO_HS800
// Подключение: HS800-2 STM32 TX → Mega pin 17 (RX2), GND → GND
// Подробнее: DRO_HS800.ino
// ─────────────────────────────────────────────────────────────────────────────
#ifdef USE_DRO_HS800
extern int32_t dro_pos_x;
extern int32_t dro_pos_y;
extern bool    dro_btn_new;
extern uint8_t dro_btn_b18;
extern uint8_t dro_btn_b19;
#endif
// ─────────────────────────────────────────────────────────────────────────────

#define ENC_LINE_PER_REV     1800     // Кол-во линий энкодера на 1 оборот шпинделя 
#define MOTOR_Z_STEP_PER_REV 200     // Кол-во шагов двигателя на оборот винта Z, продольная (200 для ШД с шагом 1,8 градуса)
#define SCREW_Z              200      // Шаг продольного винта Z в сотках, (2мм)    
#define McSTEP_Z             4        // Микрошаг, ось Z, продольная (положение переключателей на драйвере 800)
#define MOTOR_X_STEP_PER_REV 200     // Кол-во шагов двигателя на оборот винта X, поперечная (200 для ШД с шагом 1,8 градуса)
#define SCREW_X              100      // Шаг поперечного винта X в сотках, (1мм)
#define REBOUND_X            200      // Отскок резца в микрошагах, для авторезанья, должен быть больше люфта поперечки (0,25мм)
#define REBOUND_Z            200      // Отскок резца в микрошагах, для авторезанья, должен быть больше люфта продольки (0,5мм)
#define McSTEP_X             4        // Микрошаг, ось X, поперечная (положение переключателей на драйвере 800)
#define THRD_ACCEL           24       // К.деления с которого будем ускоряться на Резьбах, THRD_ACCEL + Ks должен быть < 255
#define FEED_ACCEL           3        // Жесткость разгона на подачах, бОльше значение - короче разгон
#define MIN_FEED             1        // Желаемая минимальная подача  в сотках/оборот, 0.01мм/об
#define MAX_FEED             25       // Желаемая максимальная подача в сотках/оборот, 0.25мм/об
#define MIN_aFEED            10       // Желаемая минимальная подача  в mm/минуту, 10мм/мин
#define MAX_aFEED            400      // Желаемая максимальная подача в mm/минуту, 400мм/мин
#define Excess_Lag           2        // Сколько лишних микрошагов набегает на отскоке оси Х в режиме "ШАР"
#define PASS_FINISH          2        // Количество чистовых проходов, по умолчанию, после автоцикла в режиме "Резьба"                


// Ускоренные перемещения                               
#define MAX_RAPID_MOTION     50                      // Меньше - бОльшая конечная скорость           
#define MIN_RAPID_MOTION    (MAX_RAPID_MOTION + 190) // Больше - мЕньшая начальная скорость, max 255 
#define REPEAt              (McSTEP_Z * 1)           // Кол-во повторов для постоянной скорости в пределах полного шага    
                                                     // Длительность разгона = 100/2*REPEAT(2)/Microstep(2) = 50 полных шагов цикл ускорения
// Ручной энкодер (100 линий)
#define HC_SCALE_1           1        // 1-e положение
#define HC_SCALE_10          10       // 2-e положение
#define HC_START_SPEED_1     250      // старт РГИ, 250000/(250+1)/800*60/2 = 37rpm
#define HC_MAX_SPEED_1       150      // максимум скорости РГИ, 250000/(150+1)/800*60/2 = 62rpm
#define HC_START_SPEED_10    150      // старт РГИ, 250000/(150+1)/800*60/2 = 62rpm
#define HC_MAX_SPEED_10      50       // максимум скорости РГИ, 250000/(23+1)/800*60/2 = 391rpm
#define HC_X_DIR             1        // 1-поперечка по часовой, 0-против
#define HC_Z_DIR             0        // 1-продолная по часовой, 0-против

// Настройка линейки
#define LIN_X               200       // корректировка изменений масштаба оси X на диаметр
#define BT_X                10        // масштаб 10 = 0.10mm 20 = 0.20mm и т. д., в режиме линейка кнопками влево-вправо

// Настройка мигания лимитов
#define BLINC_TIME          10
#define BLINC_ON_OFF        5

/////////////////////////////////////////////////////////////////////////////////////////////////
// ***** Cone *****
struct cone_info_type
{
  byte Cs_Div;
  int  Cm_Div;
  char Cone_Print[6];
};
const cone_info_type Cone_Info[] =
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
const thread_info_type Thread_Info[] = 
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


// ***** MY CONSTANT *****
#define CW               0
#define CCW              1
#define ON               1
#define OFF              0

#include <avr/pgmspace.h>
#include <util/delay.h>
// === LCD через I2C ===
// Библиотека: LCDI2C_Multilingual от locple (поддержка русского языка!)
// GitHub: https://github.com/locple/LCDI2C_Multilingual
// Подключение: SDA = Pin 11, SCL = Pin 12 (bit-bang I2C, пины 20/21 освобождены под энкодер шпинделя)
// Адрес I2C: 0x27 (если не работает - попробуйте 0x3F)
#include <LCDI2C_Multilingual.h>
LCDI2C_Russian lcd(0x27, 20, 4, 11, 12);  // класс для русского языка, (адрес, столбцы, строки, SDA, SCL)
// LCDI2C_Russian lcd(0x3F, 20, 4, 11, 12);  // класс для русского языка, (адрес, столбцы, строки, SDA, SCL)

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



#define Beeper_Init()          DDRH = B01100010;\
                               PORTH = B10011101    // LCD-H5,H6 Buzzer-PH1_Pin16
#define Beeper_On()            PORTH &= ~(1<<1)     // Pin16 0
#define Beeper_Off()           PORTH |= (1<<1)      // Pin16 1



// ***** Stepper Motor *****
#define Motor_Init()           DDRL = B11111011;\
                               PORTL = B0000100

#define Motor_Z_SetPulse()     PORTL &= ~(1<<0)     // Pin49 0
#define Motor_Z_RemovePulse()  PORTL |= (1<<0)      // Pin49 1
#define Motor_Z_InvertPulse()  PORTL ^= (1<<0)      // Pin49
#define Read_Z_State           (PINL & (1<<0))

#define Motor_X_SetPulse()     PORTL &= ~(1<<1)     // Pin48 0
#define Motor_X_RemovePulse()  PORTL |= (1<<1)      // Pin48 1
#define Motor_X_InvertPulse()  PORTL ^= (1<<1)      // Pin48
#define Read_X_State           (PINL & (1<<1))

#define Motor_Z_CW()           PORTL &= ~(1<<6)    // Pin43 0
#define Motor_Z_CCW()          PORTL |= (1<<6)     // Pin43 1

#define Motor_X_CW()           PORTL &= ~(1<<5)    // Pin44 0
#define Motor_X_CCW()          PORTL |= (1<<5)     // Pin44 1

#define Motor_Z_Enable()   do {PORTL |= (1<<4); _delay_ms(100);} while(0)   // Pin45 1
#define Motor_Z_Disable()     {PORTL &= ~(1<<4); _delay_ms(100);}                            // Pin45 0
#define Read_Z_Ena_State      (PINL & (1<<4))

#define Motor_X_Enable()   do {PORTL |= (1<<3); _delay_ms(100);} while(0)   // Pin46 1
#define Motor_X_Disable()     {PORTL &= ~(1<<3); _delay_ms(100);}                           // Pin46 0
#define Read_X_Ena_State       (PINL & (1<<3))


// ***** Tacho *****
#define TachoSetPulse()        PORTL |= (1<<7)      // Pin42 1
#define TachoRemovePulse()     PORTL &= ~(1<<7)     // Pin42 0


// ***** Encoder *****
#define ENC_TICK              (ENC_LINE_PER_REV * 2)    // Рабочее кол-во импульсов
#define Encoder_Init()         DDRD = B00000000;\
                               PORTD = B11111111        // подтяжка PIN_21, 20, 19, 18
#define Enc_Read              (PIND & (1<<1))
#define Enc_Ch_A              (PIND & (1<<0))
#define Enc_Ch_B              (PIND & (1<<1))
 


// ***** Hand_Coder *****            // Z/X: Input-E4,E5, подтяжка-E4,E5, X1/X10: Input-J0,J1, подтяжка-J0,J1.
#define Hand_Init()            DDRE = B00000000;\
                               PORTE = B11111111;\
                               DDRJ = B00000000;\
                               PORTJ = B11111111
                                     
#define Hand_Ch_A             (PIND & (1<<2))
#define Hand_Ch_B             (PIND & (1<<3))

#define Hand_Axis_Read        (PINE & B00110000)       // E4,E5
byte Hand_Axis_Old = 0;

#define Hand_Scale_Read        (PINJ & B00000011)      // J0,J1
byte Hand_Scale_Old = 0;

//***** Limit Buttons & LEDs *****
#define Limit_Init()           DDRA = B10101010;\
                               PORTA = B01010101    // IN-A0,A2,A4,A6, OUT-A1,A3,A5,A7, подтяжка

#define Limit_Buttons_Read    (PINA & B01010101)    // PA0 Pin22, PA2 Pin24, PA4 Pin26, PA6 Pin28.
byte Limit_Button_Old = 0;

#define Limit_Rear_LED_On()    PORTA &= ~(1<<1)     // PA1, Pin23 0
#define Limit_Rear_LED_Off()   PORTA |= (1<<1)      // PA1, Pin23 1
#define Limit_Front_LED_On()   PORTA &= ~(1<<3)     // PA3, Pin25 0
#define Limit_Front_LED_Off()  PORTA |= (1<<3)      // PA3, Pin25 1
#define Limit_Right_LED_On()   PORTA &= ~(1<<5)     // PA5, Pin27 0
#define Limit_Right_LED_Off()  PORTA |= (1<<5)      // PA5, Pin27 1
#define Limit_Left_LED_On()    PORTA &= ~(1<<7)     // PA7, Pin29 0
#define Limit_Left_LED_Off()   PORTA |= (1<<7)      // PA7, Pin29 1

#define Limit_Pos_Max          1073741824
#define Limit_Pos_Min         -1073741824


//////////////////
#define Menu_Buttons_Init()    DDRF = B00000000;\
                               PORTF = B11111111;     

#define Buttons_Read           (PINF & B00001111)    // Pin_A0 PF0, Pin_A1 PF1, Pin_A2 PF2, Pin_A3 PF3, Pin_A4 PF4.
byte Button_Old = 0;

#define Button_Sel_Read        (PINF & B00010000)    // Pin_A4 PF4
byte Button_Sel_Old = 0;
bool key_sel_flag = false;

//////////////////
#define Joy_Init()             DDRK = B00000000;\
                               PORTK = B11111111;    // подтяжка PIN_A8, A9, A10, A11, A12 // Submode Sw: A13, A14, A15

#define Joy_Read              (PINK & B00001111)     // PK0 PK1 PK2 PK3
#define Button_Rapid          (g_emul_rapid ? 0 : (PINK & B00010000))  // PK4; 0=pressed; g_emul_rapid for UART emulation
byte Joy_Old = 0;

////////////////////
#define Submode_Read          (PINK & B11100000)     // PK5 PK6 PK7
byte Submode_Old = 0;


// ***** Mode *****
#define Mode_Switch_Init()     DDRC = B00000000;\
                               PORTC = B11111111;        // подтяжка PORT_A, ОБЯЗАТЕЛЬНА! внешняя подтяжка к +5 через 1К резисторы
#define Mode_Read             (PINC & B11111111)
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
#define Timer2_Init()          TCCR2A = (1<<WGM21);\
                               TCCR2B = (1<<CS20)|(1<<CS21); // 16MHz/32 = 500kHz

//***** РГИ перемещение *****
#define Timer3_Init()          TCCR3A = 0;\
                               TCCR3B = (1<<WGM32)|(1<<CS30)|(1<<CS31); // 16MHz/32 = 250kHz

// ***** aFeed *****
#define Timer4_Init()          TCCR4A = 0;\
                               TCCR4B = (1<<WGM42)|(1<<CS40)|(1<<CS41); // 16MHz/32 = 250kHz

// ***** Feed *****
#define Timer5_Init()          TCCR5A = 0;\
                               TCCR5B = (1<<WGM52) | (1<<CS52) | (1<<CS51) | (1<<CS50)

// ***** Interrupts *****
#define INT0_Init()               EICRA |= (1<<ISC00)
#define INT2_Init()               EICRA |= (1<<ISC20)

#define Enable_INT0()             EIMSK |= (1<<INT0)
#define Disable_INT0()            EIMSK &= ~(1<<INT0)

#define Ena_INT_Hcoder()      do {EIFR = (1<<INTF2); EIMSK |= (1<<INT2);} while(0)
#define Disa_INT_Hcoder()         EIMSK &= ~(1<<INT2)

#define Enable_INT_OCR2A()    do {TCNT2 = 0; TIFR2 = (1<<OCF2A); TIMSK2 = (1<<OCIE2A);} while(0)
#define Disable_INT_OCR2A()       TIMSK2 &= ~(1<<OCIE2A)

#define Enable_INT_OCR2B()    do {TCNT2 = 0; TIFR2 = (1<<OCF2B); TIMSK2 = (1<<OCIE2B);} while(0)
#define Disable_INT_OCR2B()       TIMSK2 &= ~(1<<OCIE2B)

#define Enable_INT_OCR3A()    do {TCNT3 = 0; TIFR3 = (1<<OCF3A); TIMSK3 = (1<<OCIE3A);} while(0)
#define Disable_INT_OCR3A()       TIMSK3 &= ~(1<<OCIE3A)

#define Enable_INT_OCR3B()    do {TCNT3 = 0; TIFR3 = (1<<OCF3B); TIMSK3 = (1<<OCIE3B);} while(0)
#define Disable_INT_OCR3B()       TIMSK3 &= ~(1<<OCIE3B)

#define Enable_INT_OCR4A()    do {TCNT4 = 0; TIFR4 = (1<<OCF4A); TIMSK4 = (1<<OCIE4A);} while(0)
#define Disable_INT_OCR4A()       TIMSK4 &= ~(1<<OCIE4A)

#define Enable_INT_OCR4B()    do {TCNT4 = 0; TIFR4 = (1<<OCF4B); TIMSK4 = (1<<OCIE4B);} while(0)
#define Disable_INT_OCR4B()       TIMSK4 &= ~(1<<OCIE4B)

#define Enable_INT_OCR5A()    do {TCNT5 = 0; TIFR5 = (1<<OCF5A); TIMSK5 = (1<<OCIE5A);} while(0)
#define Disable_INT_OCR5A()       TIMSK5 &= ~(1<<OCIE5A)

#define Enable_INT_OCR5B()    do {TCNT5 = 0; TIFR5 = (1<<OCF5B); TIMSK5 = (1<<OCIE5B);} while(0)
#define Disable_INT_OCR5B()       TIMSK5 &= ~(1<<OCIE5B)

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
typedef struct
{ 
   uint8_t bit0 : 1;
   uint8_t bit1 : 1;
   uint8_t bit2 : 1;
   uint8_t bit3 : 1;
   uint8_t bit4 : 1;
   uint8_t bit5 : 1;
   uint8_t bit6 : 1;
   uint8_t bit7 : 1;
}FLAG;
#define Spindle_Dir        ((volatile FLAG*)_SFR_MEM_ADDR(GPIOR0))->bit0    // CW-0, CCW-1
#define Motor_Z_Dir        ((volatile FLAG*)_SFR_MEM_ADDR(GPIOR0))->bit1    // CW-0, CCW-1
#define Joy_Z_flag         ((volatile FLAG*)_SFR_MEM_ADDR(GPIOR0))->bit2    // On-1, Off-0
#define Step_Z_flag        ((volatile FLAG*)_SFR_MEM_ADDR(GPIOR0))->bit3    // On-1, Off-0
#define Motor_X_Dir        ((volatile FLAG*)_SFR_MEM_ADDR(GPIOR0))->bit4    // CW-0, CCW-1
#define Joy_X_flag         ((volatile FLAG*)_SFR_MEM_ADDR(GPIOR0))->bit5    // On-1, Off-0
#define Step_X_flag        ((volatile FLAG*)_SFR_MEM_ADDR(GPIOR0))->bit6    // On-1, Off-0
#define Cone_flag          ((volatile FLAG*)_SFR_MEM_ADDR(GPIOR0))->bit7    // On-1, Off-0

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

#define KEYB_TIMER_FLAG       (TIFR1 & (1<<OCF1A))
#define CLEAR_KEYB_TIMER   do {TCNT1 = 0; (TIFR1 |= (1<<OCF1A));} while(0)

uint16_t max_OCR3A = HC_START_SPEED_1;
uint16_t min_OCR3A = HC_MAX_SPEED_1;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup()
{
  // ========== SERIAL EMULATION - ИНИЦИАЛИЗАЦИЯ В ПЕРВУЮ ОЧЕРЕДЬ! ==========
  // ВАЖНО: Serial инициализируется ПЕРВЫМ, чтобы видеть отладку даже если LCD зависнет
  Serial_Emulation_Init();  // Инициализация эмуляции кнопок через Serial Monitor
  // =========================================================================

  // ========== НОВЫЙ ДИСПЛЕЙ ESP32-S3 (SoftwareSerial Pin51=TX, Pin52=RX, 57600) =========
  Display_UART_Init();
  // =========================================================================
#ifdef USE_DRO_HS800
  DRO_Init();
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


  DDRG = B11111111;

  lcd.createChar(1, strelka_vlevo);
  lcd.createChar(2, strelka_vpravo);
  lcd.createChar(3, strelka_vverh);
  lcd.createChar(4, strelka_vniz);
  lcd.createChar(5, znak_gradus);
  lcd.createChar(6, znak_diametr);



  TIMSK0 = 0;

  // ***** Timer0 ***** // ***** Тахометр *****
//  TCCR0A = (1<<COM0B0)|(1<<WGM01); // Toggle OC0B on Compare Match // CTC Mode2
//  TCCR0B = (1<<CS00);     // No Prescaler
//  OCR0A = 89; // 1800/10(выходных пульсов)/2-1 = 89
//  TIMSK0 = (1<<OCIE0B);

  Encoder_Init();
  Hand_Init();
  Motor_Init();

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

  Spindle_Dir = CW;
  Motor_Z_Dir = CW;
  Joy_Z_flag = OFF;
  Step_Z_flag = OFF;
  Motor_X_Dir = CW;
  Joy_X_flag = OFF;
  Step_X_flag = OFF;

  TCCR1A = 0;
  TCCR1B = 0
         |(0<<ICNC1)|(0<<ICES1)
         |(0<<WGM13)|(1<<WGM12)
         |(1<<CS12)|(0<<CS11)|(1<<CS10);
  OCR1A = 625;

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
  Read_ADC_Feed();
  if (KEYB_TIMER_FLAG != 0) Menu();
  if (Mode == Mode_Divider) Print(); 
 
#ifdef USE_DRO_HS800
  DRO_Process();
  {
    // Print.ino показывает минус когда переменная ПОЛОЖИТЕЛЬНАЯ (инвертированная логика).
    // Инвертируем оба знака чтобы на LCD и ESP32 отображалось то же что на HS800-2.
    int32_t new_x = -(dro_pos_x / 10L);
    int32_t new_z = -(dro_pos_y / 10L);
    if (new_x != Size_X_mm || new_z != Size_Z_mm) {
      Size_X_mm  = new_x;
      Size_Z_mm  = new_z;
      MSize_X_mm = -(dro_pos_x / 5L);
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
ISR(INT0_vect)
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



ISR(INT1_vect)
{
//
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// *****  Tacho ***** ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ISR (TIMER0_COMPB_vect)                                 // Тахометр
{
   //   
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// *****  Feed & Cone ***** //////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ISR (TIMER5_COMPA_vect)
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
ISR (TIMER5_COMPB_vect)
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
ISR (TIMER2_COMPA_vect)
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
ISR (TIMER2_COMPB_vect)
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
ISR (TIMER4_COMPA_vect)
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
ISR (TIMER4_COMPB_vect)
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
ISR(INT2_vect)
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
ISR (TIMER3_COMPA_vect)
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
ISR (TIMER3_COMPB_vect)
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

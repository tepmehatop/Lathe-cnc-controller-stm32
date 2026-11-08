#pragma once
/**
 * @file  e2_proto.h
 * @brief Прототипы функций скетча 7e2 (Arduino IDE создаёт их автоматически).
 * Сгенерировано из файлов Core/e2/orig.
 */
#include <Arduino.h>

// ADC.ino
void Read_ADC_Feed();

// Beeper.ino
void Beep();
void BeepBeep();
void BeepComplete();
void Beep_Error();

// Cone.ino
void Cone_Left(bool & a_flag, bool & b_flag);
void Cone_Right(bool & a_flag, bool & b_flag);
void Cone_Front(bool & a_flag, bool & b_flag);
void Cone_Rear(bool & a_flag, bool & b_flag);
void Rapid_Cone_Left(bool & a_flag, bool & b_flag);
void Rapid_Cone_Right(bool & a_flag, bool & b_flag);
void Cone_Ext_Left();
void Cone_Ext_Right();
void Cone_Int_Left();
void Cone_Int_Right();

// Feed.ino
void Feed_Left(bool & a_flag, bool & b_flag);
void Feed_Right(bool & a_flag, bool & b_flag);
void Feed_Front(bool & a_flag, bool & b_flag);
void Feed_Rear(bool & a_flag, bool & b_flag);
void Rapid_Feed_Left(bool & a_flag, bool & b_flag);
void Rapid_Feed_Right(bool & a_flag, bool & b_flag);
void Rapid_Feed_Front(bool & a_flag, bool & b_flag);
void Rapid_Feed_Rear(bool & a_flag, bool & b_flag);
void Feed_Ext_Left();
void Feed_Ext_Right();
void Feed_Int_Left();
void Feed_Int_Right();
void Feed_Ext_Front();
void Feed_Ext_Rear();
void Feed_Int_Front();
void Feed_Int_Rear();

// HandCoder.ino
void H_Coder();

// Menu.ino
void Menu();
void Joy_LeftPressed();
void Joy_RightPressed();
void Joy_UpPressed();
void Joy_DownPressed();
void Joy_NoPressed();
void Switch_Thread();
void Switch_Feed();
void Switch_aFeed();
void Switch_Cone_L();
void Switch_Cone_R();
void Switch_Devider();
void Switch_Sphere();
void Switch_Reserve();
void Switch_Int();
void Switch_Man();
void Switch_Ext();
void Key_Select_Pressed();
void Key_Up_Pressed();
void Key_Down_Pressed();
void Key_Left_Pressed();
void Key_Right_Pressed();
void Limit_Left_Pressed();
void Limit_Right_Pressed();
void Limit_Front_Pressed();
void Limit_Rear_Pressed();
void Switch_Hand_Axis_Z();
void Switch_Hand_Axis_X();
void Switch_Hand_Axis_No();
void Switch_Scale_x1();
void Switch_Scale_x10();
void Blinc_Limit_Buttons();

// Print.ino
void Print();

// SerialEmulation.ino
void Serial_Emulation_Init();
void Serial_Emulation_Process();
void Process_Serial_Command(String cmd);
void Print_Debug_Info();

// Sphere.ino
void Sphere();
void Sphere_Ext_Left();
void Sphere_Ext_Right();

// Spindle_On.ino
void Spindle();

// TestPresets.ino
void Set_Test_Preset(int n);

// Thread.ino
void Thread_Left(bool & c_flag, bool & d_flag);
void Thread_Right(bool & c_flag, bool & d_flag);
void Thread_Front(bool & c_flag, bool & d_flag);
void Thread_Rear(bool & c_flag, bool & d_flag);
void Thread_Ext_Left();
void Thread_Ext_Right();
void Thread_Int_Left();
void Thread_Int_Right();

// UartDisplay.ino
void Display_UART_Init();
void Display_Send_CMD(const char* cmd, const char* params);
void Display_Send_Int(const char* cmd, int32_t val);
void Display_Send_Int2(const char* cmd, int32_t a, int32_t b);
void Display_Send_All();
void Display_Send_Alert(int type);
void Display_On_Mode_Change();
void Display_On_SubMode_Change();
void Display_On_Feed_Change();
void Display_On_Thread_Change();
void Display_On_Position_Change();
void Display_On_Pass_Change();
void Display_On_Alert(int type);
void Display_On_SelectMenu_Change();
void Display_On_Limits_Change();
void Display_UART_Process();
void _Display_ParseRx(String& raw);

// aFeed.ino
void aFeed_Left(bool & a_flag, bool & b_flag);
void aFeed_Right(bool & a_flag, bool & b_flag);
void aFeed_Front(bool & a_flag, bool & b_flag);
void aFeed_Rear(bool & a_flag, bool & b_flag);
void aFeed_Ext_Left();
void aFeed_Ext_Right();
void aFeed_Int_Left();
void aFeed_Int_Right();
void aFeed_Ext_Front();
void aFeed_Ext_Rear();
void aFeed_Int_Front();
void aFeed_Int_Rear();

// [STM32] e2_core.cpp
bool e2_limit_buttons_allowed(void);   // BACKLOG-022
void e2_mech_send(void);               // <CFG:…> — механика для экрана (BACKLOG-028)
void e2_mech_from_display(const char* prm);   // CFG_SET / CFG_DEFAULT
void e2_pos_send(void);                // <POS:…> — источник координат и линейки
void e2_pos_from_display(const char* prm);    // POS_SET / POS_DEFAULT (сохранить и перезапустить)
void e2_cal_from_display(const char* prm);
void e2_gcode_line(const char* line);    // <GCODE:…> — строка G-кода (BACKLOG-001)
void e2_gcode_reset(void);
void e2_gcode_watch(void);
void e2_cal_step_from_display(const char* prm);   // CAL_STEP:<Z|X>:<фаза>:<мкм> — калибровка по индикатору
void e2_scale_reref(void);             // линейки: новая опорная точка (BACKLOG-025)
void e2_scale_track(void);
bool e2_scale_gate(void);
bool e2_scale_blocked(void);           // пропуск шагов — движение от джойстика заблокировано              // true — автоцикл в этот раз не вызывать (доводка X / пропуск шагов)    // CAL:<Z|X>:<мкм> — калибровка по линейкам
bool e2_axis_editable(void);
void e2_diam_from_keys(long mx0);      // ввод диаметра кнопками ◀/▶ при линейках напрямую           // ввод/сброс осей с экрана (нет — при блоке DRO)
void e2_axis_set(char axis, int32_t um);   // 'Z', 'X' — позиция, 'D' — диаметр; мкм, как на экране

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Режим "Конус" ********** //////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Cone_Left(bool & a_flag, bool & b_flag)  
{  
   if (b_flag == true) return;
   a_flag = false;
   b_flag = true;
   E2_MOTION_IRQ_OFF();   // [STM32] прерывание прошлого движения не должно видеть новую цель
   
   Cs_Divisor = Cone_Info[Cone_Step].Cs_Div;
   Cm_Divisor = Cone_Info[Cone_Step].Cm_Div;

   Start_Speed = ENC_LINE_PER_REV / ((uint32_t)MOTOR_Z_STEP_PER_REV * McSTEP_Z * MIN_FEED / SCREW_Z) / FEED_ACCEL /2;
   if (Motor_Z_Pos < Limit_Pos_Left - Start_Speed * 2)
   {
      Feed_Divisor = ENC_LINE_PER_REV / ((float)MOTOR_Z_STEP_PER_REV * McSTEP_Z * Feed_mm / SCREW_Z) /2 +0.5 - FEED_EXACT;   // [STM32] BACKLOG-020: таймер считает Feed_Divisor + 1 линий
      if (Feed_Divisor < Start_Speed)
      {
         max_OCR5A = Start_Speed;
         if (Step_Z_flag == OFF)
         {
            OCR5A = Start_Speed;
         }
      }
      else
      {
         max_OCR5A = Feed_Divisor;
         OCR5A = Feed_Divisor;
      }
   }
   else
   {
      Feed_Divisor = Start_Speed;
      max_OCR5A = Start_Speed;
      OCR5A = Start_Speed;
   }
   
   Brake_Compens = max_OCR5A - Feed_Divisor + 1;
   Limit_Pos = Limit_Pos_Left - Brake_Compens;

   Motor_Z_Dir = CW;
   Motor_Z_CW();
   if (Read_Z_Ena_State == false) Motor_Z_Enable();

   if      (Mode == Mode_Cone_L) {Motor_X_Dir = CW; Motor_X_CW();}
   else if (Mode == Mode_Cone_R) {Motor_X_Dir = CCW; Motor_X_CCW();}
   if (Read_X_Ena_State == false) Motor_X_Enable();
   
   feed_Z_flag = ON;
   Step_X_flag = ON;
   Joy_Z_flag = ON;
   Ena_INT_Z_Feed();
}

void Cone_Right(bool & a_flag, bool & b_flag)
{
   if (b_flag == true) return;
   a_flag = false;
   b_flag = true; 
   E2_MOTION_IRQ_OFF();   // [STM32] прерывание прошлого движения не должно видеть новую цель
   
   Cs_Divisor = Cone_Info[Cone_Step].Cs_Div;
   Cm_Divisor = Cone_Info[Cone_Step].Cm_Div;

   Start_Speed = ENC_LINE_PER_REV / ((uint32_t)MOTOR_Z_STEP_PER_REV * McSTEP_Z * MIN_FEED / SCREW_Z) / FEED_ACCEL /2;
   if (Motor_Z_Pos > Limit_Pos_Right + Start_Speed * 2)
   {
      Feed_Divisor = ENC_LINE_PER_REV / ((float)MOTOR_Z_STEP_PER_REV * McSTEP_Z * Feed_mm / SCREW_Z) /2 +0.5 - FEED_EXACT;   // [STM32] BACKLOG-020: таймер считает Feed_Divisor + 1 линий
      if (Feed_Divisor < Start_Speed)
      {
         max_OCR5A = Start_Speed;
         if (Step_Z_flag == OFF)
         {
            OCR5A = Start_Speed;
         }
      }
      else
      {
         max_OCR5A = Feed_Divisor;
         OCR5A = Feed_Divisor;
      }
   }
   else
   {
      Feed_Divisor = Start_Speed;
      max_OCR5A = Start_Speed;
      OCR5A = Start_Speed;
   }
   
   Brake_Compens = max_OCR5A - Feed_Divisor + 1;
   Limit_Pos = Limit_Pos_Right + Brake_Compens;

   Motor_Z_Dir = CCW;
   Motor_Z_CCW();
   if (Read_Z_Ena_State == false) Motor_Z_Enable();

   if      (Mode == Mode_Cone_L) {Motor_X_Dir = CCW; Motor_X_CCW();}
   else if (Mode == Mode_Cone_R) {Motor_X_Dir = CW; Motor_X_CW();}
   if (Read_X_Ena_State == false) Motor_X_Enable();
   
   feed_Z_flag = ON;
   Step_X_flag = ON;
   Joy_Z_flag = ON;
   Ena_INT_Z_Feed();
}

void Cone_Front(bool & a_flag, bool & b_flag)
{
   if (b_flag == true) return;
   a_flag = false;
   b_flag = true;
   E2_MOTION_IRQ_OFF();   // [STM32] прерывание прошлого движения не должно видеть новую цель
}

void Cone_Rear(bool & a_flag, bool & b_flag)
{
   if (b_flag == true) return;
   a_flag = false;
   b_flag = true; 
   E2_MOTION_IRQ_OFF();   // [STM32] прерывание прошлого движения не должно видеть новую цель
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Режим "Конус! Быстрая Подача" ********** ///////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Rapid_Cone_Left(bool & a_flag, bool & b_flag)
{
   if (a_flag == true) return;
   b_flag = false;
   a_flag = true;
   E2_MOTION_IRQ_OFF();   // [STM32] прерывание прошлого движения не должно видеть новую цель
   Cs_Divisor = Cone_Info[Cone_Step].Cs_Div;
   Cm_Divisor = Cone_Info[Cone_Step].Cm_Div;
   Brake_Compens = (MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt + 1;
   Limit_Pos = Limit_Pos_Left - Brake_Compens;
   
   Cs_Count = 0;
   Cm_Count = 0;            

   Motor_Z_Dir = CW;
   Motor_Z_CW();
   if (Read_Z_Ena_State == false) Motor_Z_Enable();

   if      (Mode == Mode_Cone_L) {Motor_X_Dir = CW; Motor_X_CW();}
   else if (Mode == Mode_Cone_R) {Motor_X_Dir = CCW; Motor_X_CCW();}
   if (Read_X_Ena_State == false) Motor_X_Enable();
   
   Joy_Z_flag = ON;
   rapid_Z_flag = ON;
   Step_X_flag = ON;
   Ena_INT_Z_Rapid();
}

void Rapid_Cone_Right(bool & a_flag, bool & b_flag)
{
   if (a_flag == true) return;
   b_flag = false;
   a_flag = true;
   E2_MOTION_IRQ_OFF();   // [STM32] прерывание прошлого движения не должно видеть новую цель
   Cs_Divisor = Cone_Info[Cone_Step].Cs_Div;
   Cm_Divisor = Cone_Info[Cone_Step].Cm_Div;
   Brake_Compens = (MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt + 1;
   Limit_Pos = Limit_Pos_Right + Brake_Compens;
   
   Cs_Count = 0;
   Cm_Count = 0;            

   Motor_Z_Dir = CCW;
   Motor_Z_CCW();
   if (Read_Z_Ena_State == false) Motor_Z_Enable();

   if      (Mode == Mode_Cone_L) {Motor_X_Dir = CCW; Motor_X_CCW();}
   else if (Mode == Mode_Cone_R) {Motor_X_Dir = CW; Motor_X_CW();}
   if (Read_X_Ena_State == false) Motor_X_Enable();
   
   Joy_Z_flag = ON;
   rapid_Z_flag = ON;
   Step_X_flag = ON;
   Ena_INT_Z_Rapid();  
}
//===========================================================================================
//================************многопроходный конус**************=============================
//===========================================================================================

//////////////////////////////////////////////////////////////////////////////////////////////
/////////////////наружн. для Правого конуса////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

void Cone_Ext_Left()  // это НАРУЖН для ПРАВОГО конуса >, старт справа (джойстик влево)
{
  static unsigned long Infeed_ValueX;
  static unsigned long Infeed_ValueCX;
  static unsigned long Infeed_ValueZ;
  static unsigned long Infeed_ValueCZ; 
  static long Otvod; 
  static long Limit_Pos_VL;
  static long Limit_Pos_VR;
  if (button_down_flag) { want_pause = true; Beep(); }  
  
  if ((Step == 0 && Motor_Z_Pos == Null_Z_Pos && Motor_X_Pos == Null_X_Pos && Step_Z_flag == OFF && !Complete_flag) || 
    (Step == 0 && Motor_Z_Pos == Limit_Pos_Right && Motor_X_Pos == Limit_Pos_Front && Step_Z_flag == OFF && !Complete_flag)) 
 { 
  if (cycle_flag == false && Pass_Nr <= Pass_Total) //если выполнены не все циклы(проходы)
   {
    cycle_flag = true;
    a_flag = false;
    b_flag = false;
    Cs_Divisor = (Cone_Info[Cone_Step].Cs_Div); Cm_Divisor = (Cone_Info[Cone_Step].Cm_Div);
    Otvod = ((long)MOTOR_Z_STEP_PER_REV * 100L + SCREW_Z / 2L) / SCREW_Z * McSTEP_Z;  //отвод вправо в микрошагах (где будет правый лимит)
    Infeed_ValueX = (long)(((unsigned long long)MOTOR_X_STEP_PER_REV * (unsigned long long)McSTEP_X * (unsigned long long)Ap + (unsigned long long)SCREW_X) / (2ULL * (unsigned long long)SCREW_X)); //заглубление по Х за цикл в микрошагах
    Infeed_ValueCX = (long)(((unsigned long long)MOTOR_X_STEP_PER_REV * (unsigned long long)McSTEP_X * (unsigned long long)(Ap + 20) + (unsigned long long)SCREW_X) / (2ULL * (unsigned long long)SCREW_X));  //(Ap + 20) заглубление по Х+0.2мм  микрошагах, нужно только для расчета Infeed_ValueCZ
    Infeed_ValueZ = (unsigned long)(((unsigned long long)Infeed_ValueX * Cs_Divisor + ((unsigned long long)Infeed_ValueX * Cm_Divisor + 5000ULL) / 10000ULL + 1ULL) / 2ULL); // считаем шаг по Z за цикл в микрошагах, «плюс 5000 и плюс 1 — округление при делении»
    Infeed_ValueCZ = (unsigned long)(((unsigned long long)Infeed_ValueCX * Cs_Divisor + ((unsigned long long)Infeed_ValueCX * Cm_Divisor + 5000ULL) / 10000ULL  + 1ULL) / 2ULL); //считаем шаг по Z за ПЕРВЫЙ цикл в микрошагах, «плюс 5000 и плюс 1 — округление при делении»
    if (Pass_Nr == 1) //если это первый цикл
     {
      Limit_Pos_Front = Null_X_Pos;
      Limit_Pos_Rear = Null_X_Pos - REBOUND_X;
      Limit_Pos_Right = Null_Z_Pos;
     }
    else // если это не первый цикл (проход)
     {
      Limit_Pos_Rear = Limit_Pos_Front - REBOUND_X;
     }
    Limit_Right_LED_On();
    Limit_Rear_LED_On();
    Limit_Front_LED_On();
    BeepBeep();
    Feed_Rear(a_flag, b_flag); // едем по Х вниз на REBOUND_X
    Step = 1; 
    
   }
   else // если выполнены все циклы (проходы), заканчиваем автоматический режим
   {

    Limit_Pos_Right = Limit_Pos_VR;
    Limit_Pos_Left = Limit_Pos_VL;
    Limit_Pos_Front = Limit_Pos_Max;
    Limit_Pos_Rear = Limit_Pos_Min; 
    Limit_Front_LED_Off();
    Limit_Rear_LED_Off();
    limit_Right_flag = ON;
    limit_Left_flag = ON;
    Pass_Total = 1;
    Pass_Nr = 1;
    BeepComplete(); 
   }
 } 
 
 else if (Step == 1 &&  Motor_X_Pos == Limit_Pos_Rear && Step_X_flag == OFF) //когда доехали на нижний лимит
  {
   a_flag = false;
   b_flag = false; 
      
   long Infeed_Sum = (long)Infeed_ValueCZ + (long)Infeed_ValueZ * (Pass_Nr - 1);
   Limit_Pos_Left = Null_Z_Pos + Infeed_Sum;
   Limit_Pos_VL   = Null_Z_Pos + ((Infeed_Sum + (McSTEP_Z >> 1)) / McSTEP_Z) * McSTEP_Z; //чтобы этот лимит остался в памяти на БЛИЖАЙШЕМ ЦЕЛОМ ШАГЕ (нужно только на выходе из автоматического режима)
   
   Limit_Left_LED_On();
   if ((Limit_Pos_Left - Limit_Pos_Right) > (((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2)) 
   Rapid_Cone_Left(a_flag, b_flag); 
   else Cone_Left(a_flag, b_flag); 
   Step = 2;
   }

 else if (Step == 2 && Motor_Z_Pos == Limit_Pos_Left && Step_X_flag == OFF && Step_Z_flag == OFF) //когда доехали до левого лимита 
  {
   a_flag = false;
   b_flag = false; 
   Limit_Pos_Rear = Motor_X_Pos;
   Limit_Pos_Right = Limit_Pos_Left;
   Limit_Pos_Left = Limit_Pos_Right + REBOUND_Z;
   Feed_Left(a_flag, b_flag); //1 этап выборки люфта по Z - движение влево
   Step = 3;
  }

  else if (Step == 3 && Motor_Z_Pos == Limit_Pos_Left  && Step_Z_flag == OFF) 
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Right = Limit_Pos_Left  - REBOUND_Z;
   Feed_Right(a_flag, b_flag); //2 этап выборки люфта по Z - движение вправо
   Step = 4;
  }
 else if (Step == 4 && Motor_Z_Pos == Limit_Pos_Right  && Step_Z_flag == OFF) 
  {
   a_flag = false;
   b_flag = false; 
   Limit_Pos_Left = Limit_Pos_Right;
   Limit_Pos_Front = (Limit_Pos_Rear + REBOUND_X + Infeed_ValueX); //компенсация отскока + врезание за проход
   Feed_Front(a_flag, b_flag); //врезание на глубину заглубления за проход
   Step = 5;
  }

 else if (Step == 5 && Motor_X_Pos == Limit_Pos_Front  && Step_X_flag == OFF) //когда готовы к основному движению
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Right = Null_Z_Pos - Otvod;
   Cone_Right(a_flag, b_flag); //основное движение до правого лимита (на отвод правее стартового)
   Step = 6;
   }

 else if (Step == 6 && Motor_Z_Pos == Limit_Pos_Right && Step_Z_flag == OFF && Step_X_flag == OFF) //когда доехали до правого лимита
  {
   a_flag = false;
   b_flag = false; 
   Limit_Pos_Front = Motor_X_Pos;
   Limit_Pos_VR = Limit_Pos_Right;  //чтобы этот лимит остался в памяти
   Limit_Pos_Left = Limit_Pos_Right;
   Limit_Pos_Right = Limit_Pos_Left - REBOUND_Z;
   Feed_Right(a_flag, b_flag); //1 этап выборки люфта по Z - движение вправо
   Step = 7;
  }

 else if (Step == 7 && Motor_Z_Pos == Limit_Pos_Right  && Step_Z_flag == OFF)  
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Left = Limit_Pos_Right + REBOUND_Z;
   Feed_Left(a_flag, b_flag);  //2 этап выборки люфта, возвращаемся влево
   Step = 8; 
  }

 else if (Step == 8 && Motor_Z_Pos == Limit_Pos_Left && Step_Z_flag == OFF) 
  {
   ////
   if (want_pause == true)         // ПАУЗА НА ТОЧКЕ ОТВОДА: резец сошёл с конуса,
   {                              
      isPausedAfterStep = true;
      if (button_up_flag)
      {
         want_pause = false;
         isPausedAfterStep = false;
         Beep();
      }
      else
      {
         return;                   // стоим на отводе
      }
   }
   ////
   a_flag = false;
   b_flag = false;
   Limit_Pos_Right = Limit_Pos_Left;
   Pass_Nr++; 
   if (Pass_Nr > Pass_Total) //если это последний проход, то поедем в дальнюю точку
   {
    Limit_Pos_Rear = Limit_Pos_Front - REBOUND_X;
    Feed_Rear(a_flag, b_flag); // едем по Х вниз на REBOUND_X
    Step = 9; 
   }
   else 
   {
    Step = 0;  //в начало цикла
    cycle_flag = false;
   } 
  }
//далее только для последнего прохода движение в дальнюю точку
else if (Step == 9 && Motor_X_Pos == Limit_Pos_Rear && Step_X_flag == OFF) 
{
 a_flag = false;
 b_flag = false; 
 Limit_Pos_Left = Limit_Pos_VL;
 if ((Limit_Pos_Left - Limit_Pos_Right) > (((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2)) 
   {Rapid_Cone_Left(a_flag, b_flag);} //едем влево до конечной точки Limit_Pos_VL
   else {Cone_Left(a_flag, b_flag);} 
  Step = 10;
}

else if (Step == 10 && Motor_Z_Pos == Limit_Pos_Left && Step_X_flag == OFF && Step_Z_flag == OFF) //когда доехали до левого лимита
  {
   a_flag = false;
   b_flag = false; 
   Limit_Pos_Rear = Motor_X_Pos;
   Limit_Pos_Right = Limit_Pos_Left;
   Limit_Pos_Left = Limit_Pos_Right + REBOUND_Z;
   Feed_Left(a_flag, b_flag); //1 этап выборки люфта по Z - движение влево
   Step = 11;
  }

else if (Step == 11 && Motor_Z_Pos == Limit_Pos_Left  && Step_Z_flag == OFF) //2 этап выборки люфта по Z - движение вправо
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Right = Limit_Pos_VL;
   Feed_Right(a_flag, b_flag); //2 этап выборки люфта по Z - движение вправо, возвращаемся в точку Limit_Pos_VL
   Step = 12;
  }

 else if (Step == 12 && Motor_Z_Pos == Limit_Pos_Right && Step_Z_flag == OFF) //когда доехали до точки Limit_Pos_VL
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Left = Limit_Pos_VL;
   Limit_Pos_Front = Limit_Pos_Rear + REBOUND_X; //компенсация отскока 
   Feed_Front(a_flag, b_flag); //едем на конечную точку
   Step = 13;
  }

 else if (Step == 13 && Motor_X_Pos == Limit_Pos_Front && Step_X_flag == OFF)
  {
   Step = 0;  //в начало цикла 
   cycle_flag = false; 
  }
  
} // окончание  Cone_Ext_Left(

//////////////////////////////////////////////////////////////////////////////////////////////
/////////////////наружн. для Левого конуса////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
void Cone_Ext_Right()  // это наружн . для левого конуса <, старт слева (джойстик вправо)
{
  static unsigned long Infeed_ValueX;
  static unsigned long Infeed_ValueCX;
  static unsigned long Infeed_ValueZ;
  static unsigned long Infeed_ValueCZ;
  static long Otvod; 
  static long Limit_Pos_VL;
  static long Limit_Pos_VR;

  if (button_down_flag) { want_pause = true; Beep(); }  // опрос в любой точке цикла

  if ((Step == 0 && Motor_Z_Pos == Null_Z_Pos && Motor_X_Pos == Null_X_Pos && Step_Z_flag == OFF && !Complete_flag) || 
    (Step == 0 && Motor_Z_Pos == Limit_Pos_Left && Motor_X_Pos == Limit_Pos_Front && Step_Z_flag == OFF && !Complete_flag)) 
 { 
  if (cycle_flag == false && Pass_Nr <= Pass_Total) //если выполнены не все циклы(проходы)
   {
    cycle_flag = true;
    a_flag = false;
    b_flag = false;
    Cs_Divisor = (Cone_Info[Cone_Step].Cs_Div); Cm_Divisor = (Cone_Info[Cone_Step].Cm_Div);
    Otvod = ((long)MOTOR_Z_STEP_PER_REV * 100L + SCREW_Z / 2L) / SCREW_Z * McSTEP_Z;  //отвод влево в микрошагах (где будет левый лимит)
    Infeed_ValueX = (long)(((unsigned long long)MOTOR_X_STEP_PER_REV * (unsigned long long)McSTEP_X * (unsigned long long)Ap + (unsigned long long)SCREW_X) / (2ULL * (unsigned long long)SCREW_X)); //заглубление по Х за цикл в микрошагах
    Infeed_ValueCX = (long)(((unsigned long long)MOTOR_X_STEP_PER_REV * (unsigned long long)McSTEP_X * (unsigned long long)(Ap + 20) + (unsigned long long)SCREW_X) / (2ULL * (unsigned long long)SCREW_X));  //(Ap + 20) заглубление по Х+0.2мм  микрошагах, нужно только для расчета Infeed_ValueCZ
    Infeed_ValueZ = (unsigned long)(((unsigned long long)Infeed_ValueX * Cs_Divisor + ((unsigned long long)Infeed_ValueX * Cm_Divisor + 5000ULL) / 10000ULL + 1ULL) / 2ULL); // считаем шаг по Z за цикл в микрошагах, «плюс 5000 и плюс 1 — округление при делении»
    Infeed_ValueCZ = (unsigned long)(((unsigned long long)Infeed_ValueCX * Cs_Divisor + ((unsigned long long)Infeed_ValueCX * Cm_Divisor + 5000ULL) / 10000ULL  + 1ULL) / 2ULL); //считаем шаг по Z за ПЕРВЫЙ цикл в микрошагах, «плюс 5000 и плюс 1 — округление при делении»
    if (Pass_Nr == 1) //если это первый цикл
     {
      Limit_Pos_Front = Null_X_Pos;
      Limit_Pos_Rear = Null_X_Pos - REBOUND_X;
      Limit_Pos_Left = Null_Z_Pos;
     }
    else // если это не первый цикл (проход)
     {
      Limit_Pos_Rear = Limit_Pos_Front - REBOUND_X;
     }
    Limit_Left_LED_On();
    Limit_Rear_LED_On();
    Limit_Front_LED_On();
    BeepBeep();
    Feed_Rear(a_flag, b_flag); // едем по Х вниз на REBOUND_X
    Step = 1; 
    
   }
   else // если выполнены все циклы (проходы), заканчиваем автоматический режим
   {
    Limit_Pos_Right = Limit_Pos_VR;
    Limit_Pos_Left = Limit_Pos_VL;
    Limit_Pos_Front = Limit_Pos_Max;
    Limit_Pos_Rear = Limit_Pos_Min; 
    Limit_Front_LED_Off();
    Limit_Rear_LED_Off();
    limit_Right_flag = ON;
    limit_Left_flag = ON;
    Pass_Total = 1;
    Pass_Nr = 1;
    BeepComplete(); 
   }
 } 
 
 else if (Step == 1 &&  Motor_X_Pos == Limit_Pos_Rear && Step_X_flag == OFF) //когда доехали на нижний лимит
  {
   a_flag = false;
   b_flag = false; 
   long Infeed_Sum = (long)Infeed_ValueCZ + (long)Infeed_ValueZ * (Pass_Nr - 1); 
   Limit_Pos_Right = Null_Z_Pos - Infeed_Sum;                                    
   Limit_Pos_VR    = Null_Z_Pos - ((Infeed_Sum + (McSTEP_Z >> 1)) / McSTEP_Z) * McSTEP_Z; //чтобы этот лимит остался в памяти на БЛИЖАЙШЕМ ЦЕЛОМ ШАГЕ (нужно только на выходе из автоматического режима)
   Limit_Right_LED_On();
   if ((Limit_Pos_Left - Limit_Pos_Right) > (((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2)) 
   Rapid_Cone_Right(a_flag, b_flag); 
   else Cone_Right(a_flag, b_flag); 
   Step = 2;
   }

 else if (Step == 2 && Motor_Z_Pos == Limit_Pos_Right && Step_X_flag == OFF && Step_Z_flag == OFF) //когда доехали до правого лимита 
  {
   a_flag = false;
   b_flag = false; 
   Limit_Pos_Rear = Motor_X_Pos;
   Limit_Pos_Left = Limit_Pos_Right;
   Limit_Pos_Right = Limit_Pos_Left - REBOUND_Z;
   Feed_Right(a_flag, b_flag); //1 этап выборки люфта по Z - движение вправо
   Step = 3;
  }

  else if (Step == 3 && Motor_Z_Pos == Limit_Pos_Right  && Step_Z_flag == OFF) //2 этап выборки люфта по Z - движение влево
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Left = Limit_Pos_Right  + REBOUND_Z;
   Feed_Left(a_flag, b_flag);
   Step = 4;
  }
 else if (Step == 4 && Motor_Z_Pos == Limit_Pos_Left  && Step_Z_flag == OFF) 
  {
   a_flag = false;
   b_flag = false; 
   Limit_Pos_Right = Limit_Pos_Left;
   Limit_Pos_Front = (Limit_Pos_Rear + REBOUND_X + Infeed_ValueX); //компенсация отскока + врезание за проход
   Feed_Front(a_flag, b_flag); //врезание на глубину заглубления за проход
   Step = 5;
  }

 else if (Step == 5 && Motor_X_Pos == Limit_Pos_Front  && Step_X_flag == OFF) //когда готовы к основному движению
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Left = Null_Z_Pos + Otvod;
   Limit_Pos_VL = Limit_Pos_Left;  //чтобы этот лимит остался в памяти
   Cone_Left(a_flag, b_flag); //основное движение до левого лимита (на отвод левее стартового)
   Step = 6;
   }

 else if (Step == 6 && Motor_Z_Pos == Limit_Pos_Left && Step_Z_flag == OFF && Step_X_flag == OFF) //когда доехали до левого лимита
  {
   a_flag = false;
   b_flag = false; 
   Limit_Pos_Front = Motor_X_Pos;

   Limit_Pos_Right = Limit_Pos_Left;
   Limit_Pos_Left = Limit_Pos_Right + REBOUND_Z;
   Feed_Left(a_flag, b_flag); //1 этап выборки люфта по Z - движение влево
   Step = 7;
  }

 else if (Step == 7 && Motor_Z_Pos == Limit_Pos_Left  && Step_Z_flag == OFF)  
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Right = Limit_Pos_Left - REBOUND_Z;
   Feed_Right(a_flag, b_flag);  //2 этап выборки люфта, возвращаемся вправо
   Step = 8; 
  }

 else if (Step == 8 && Motor_Z_Pos == Limit_Pos_Right && Step_Z_flag == OFF) 
  {
   ////
   if (want_pause == true)         
   {                               
      isPausedAfterStep = true;
      if (button_up_flag)
      {
         want_pause = false;
         isPausedAfterStep = false;
         Beep();
      }
      else
      {
         return;                   
      }
   }
   ////
   a_flag = false;
   b_flag = false;
   Limit_Pos_Left = Limit_Pos_Right;
   Pass_Nr++; 
   if (Pass_Nr > Pass_Total) //если это был последний проход, то поедем в дальнюю точку
   {
    Limit_Pos_Rear = Limit_Pos_Front - REBOUND_X;
    Feed_Rear(a_flag, b_flag); // только после последнего прохода едем по Х вниз на REBOUND_X
    Step = 9; 
   }
   else 
   {
    Step = 0;  //в начало цикла
    cycle_flag = false;
   } 
  }
//далее только после последнего прохода движение в дальнюю точку
else if (Step == 9 && Motor_X_Pos == Limit_Pos_Rear && Step_X_flag == OFF) 
{
 a_flag = false;
 b_flag = false; 
 Limit_Pos_Right = Limit_Pos_VR;
 if ((Limit_Pos_Left - Limit_Pos_Right) > (((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2)) 
   {Rapid_Cone_Right(a_flag, b_flag);} //едем вправо до конечной точки Limit_Pos_VR
   else {Cone_Right(a_flag, b_flag);} 
  Step = 10;
}

else if (Step == 10 && Motor_Z_Pos == Limit_Pos_Right && Step_X_flag == OFF && Step_Z_flag == OFF) //когда доехали до правого лимита
  {
   a_flag = false;
   b_flag = false; 
   Limit_Pos_Rear = Motor_X_Pos;
   Limit_Pos_Left = Limit_Pos_Right;
   Limit_Pos_Right = Limit_Pos_Left - REBOUND_Z;
   Feed_Right(a_flag, b_flag); //1 этап выборки люфта по Z - движение вправо
   Step = 11;
  }

else if (Step == 11 && Motor_Z_Pos == Limit_Pos_Right  && Step_Z_flag == OFF) //2 этап выборки люфта по Z - движение влево
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Left = Limit_Pos_VR;
   Feed_Left(a_flag, b_flag); //2 этап выборки люфта по Z - движение влево, возвращаемся в точку Limit_Pos_VR
   Step = 12;
  }

 else if (Step == 12 && Motor_Z_Pos == Limit_Pos_Left && Step_Z_flag == OFF) //когда доехали до точки Limit_Pos_VR
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Right = Limit_Pos_VR;
   Limit_Pos_Front = Limit_Pos_Rear + REBOUND_X; //компенсация отскока 
   Feed_Front(a_flag, b_flag); //едем на конечную точку
   Step = 13;
  }

 else if (Step == 13 && Motor_X_Pos == Limit_Pos_Front && Step_X_flag == OFF)
  {
   Step = 0;  //в начало цикла 
   cycle_flag = false; 
  }
  
} // окончание  Cone_Ext_Right

///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////   < внутренний конус < ////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
void Cone_Int_Left()  // внутренний левый конус <, старт справа (джойстик влево)
{
  static unsigned long Infeed_ValueX;
  static unsigned long Infeed_ValueCX;
  static unsigned long Infeed_ValueZ;
  static unsigned long Infeed_ValueCZ;
  static long Otvod_30; 
  static long Otvod; 
  static long Limit_Pos_VL;
  static long Limit_Pos_VR;
 if (button_down_flag) { want_pause = true; Beep(); }  //ДОБАВИЛ
 if ((Step == 0 && Motor_Z_Pos == Null_Z_Pos && Motor_X_Pos == Null_X_Pos && Step_Z_flag == OFF && !Complete_flag) || 
    (Step == 0 && Motor_Z_Pos == Limit_Pos_Right && Motor_X_Pos == Limit_Pos_Rear && Step_Z_flag == OFF && !Complete_flag)) 
 { 
  if (cycle_flag == false && Pass_Nr <= Pass_Total) //если выполнены не все циклы(проходы)
   {
    cycle_flag = true;
    a_flag = false;
    b_flag = false;
    Cs_Divisor = (Cone_Info[Cone_Step].Cs_Div); Cm_Divisor = (Cone_Info[Cone_Step].Cm_Div);
    Otvod = ((long)MOTOR_Z_STEP_PER_REV * 100L + SCREW_Z / 2L) / SCREW_Z * McSTEP_Z;  //отвод вправо в микрошагах (где будет правый лимит)
    Infeed_ValueX = (long)(((unsigned long long)MOTOR_X_STEP_PER_REV * (unsigned long long)McSTEP_X * (unsigned long long)Ap + (unsigned long long)SCREW_X) / (2ULL * (unsigned long long)SCREW_X)); //заглубление по Х за цикл в микрошагах
    Infeed_ValueCX = (long)(((unsigned long long)MOTOR_X_STEP_PER_REV * (unsigned long long)McSTEP_X * (unsigned long long)(Ap + 20) + (unsigned long long)SCREW_X) / (2ULL * (unsigned long long)SCREW_X));  //(Ap + 20) заглубление по Х+0.2мм  микрошагах, нужно только для расчета Infeed_ValueCZ
    Infeed_ValueZ = (unsigned long)(((unsigned long long)Infeed_ValueX * Cs_Divisor + ((unsigned long long)Infeed_ValueX * Cm_Divisor + 5000ULL) / 10000ULL + 1ULL) / 2ULL); // считаем шаг по Z за цикл в микрошагах, «плюс 5000 и плюс 1 — округление при делении»
    Infeed_ValueCZ = (unsigned long)(((unsigned long long)Infeed_ValueCX * Cs_Divisor + ((unsigned long long)Infeed_ValueCX * Cm_Divisor + 5000ULL) / 10000ULL  + 1ULL) / 2ULL); //считаем шаг по Z за ПЕРВЫЙ цикл в микрошагах, «плюс 5000 и плюс 1 — округление при делении»
    if (Pass_Nr == 1) //если это первый цикл
     {
      Limit_Pos_Rear = Null_X_Pos;
      Limit_Pos_Front = Null_X_Pos + REBOUND_X;
      Limit_Pos_Right = Null_Z_Pos;
     }
    else // если это не первый цикл (проход)
     {
      Limit_Pos_Front = Limit_Pos_Rear + REBOUND_X;
     }
    Limit_Right_LED_On();
    Limit_Rear_LED_On();
    Limit_Front_LED_On();
    BeepBeep();
    Feed_Front(a_flag, b_flag); // едем по Х вверх на REBOUND_X
    Step = 1; 
    
   }
   else // если выполнены все циклы (проходы), заканчиваем автоматический режим
   {
    Limit_Pos_Right = Limit_Pos_VR;
    Limit_Pos_Left = Limit_Pos_VL;
    Limit_Pos_Front = Limit_Pos_Max;
    Limit_Pos_Rear = Limit_Pos_Min; 
    Limit_Front_LED_Off();
    Limit_Rear_LED_Off();
    limit_Right_flag = ON;
    limit_Left_flag = ON;
    Pass_Total = 1;
    Pass_Nr = 1;
    BeepComplete(); 
   }
 }

 else if (Step == 1 &&  Motor_X_Pos == Limit_Pos_Front && Step_X_flag == OFF) //когда доехали на верхний лимит
  {
   a_flag = false;
   b_flag = false; 
   long Infeed_Sum = (long)Infeed_ValueCZ + (long)Infeed_ValueZ * (Pass_Nr - 1);
   Limit_Pos_Left = Null_Z_Pos + Infeed_Sum;
   Limit_Pos_VL   = Null_Z_Pos + ((Infeed_Sum + (McSTEP_Z >> 1)) / McSTEP_Z) * McSTEP_Z; //чтобы этот лимит остался в памяти на БЛИЖАЙШЕМ ЦЕЛОМ ШАГЕ (нужно только на выходе из автоматического режима)
   Limit_Left_LED_On();
   if ((Limit_Pos_Left - Limit_Pos_Right) > (((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2)) 
   Rapid_Cone_Left(a_flag, b_flag); 
   else Cone_Left(a_flag, b_flag); 
   Step = 2;
   }

 else if (Step == 2 && Motor_Z_Pos == Limit_Pos_Left && Step_X_flag == OFF && Step_Z_flag == OFF) //когда доехали до левого лимита
  {
   Limit_Pos_Front = Motor_X_Pos;
   Limit_Pos_Right = Limit_Pos_Left;
   a_flag = false;
   b_flag = false; 
   Limit_Pos_Left = Limit_Pos_Right + REBOUND_Z;
   Feed_Left(a_flag, b_flag); // 1 этап выборки люфта по Z - движение влево
   Step = 4;
  }

 else if (Step == 4 && Motor_Z_Pos == Limit_Pos_Left  && Step_Z_flag == OFF) 
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Right = Limit_Pos_Left  - REBOUND_Z;
   Feed_Right(a_flag, b_flag); //2 этап выборки люфта по Z - движение вправо
   Step = 5;
  }
 else if (Step == 5 && Motor_Z_Pos == Limit_Pos_Right  && Step_Z_flag == OFF) 
  {
   Limit_Pos_Left = Limit_Pos_Right;
   a_flag = false;
   b_flag = false;
   Limit_Pos_Rear = (Limit_Pos_Front - REBOUND_X - Infeed_ValueX); //компенсация отскока + врезание за проход
   Feed_Rear(a_flag, b_flag); //врезание на глубину заглубления за проход
   Step = 7;
  }

 else if (Step == 7 && Motor_X_Pos == Limit_Pos_Rear  && Step_X_flag == OFF) //когда готовы к основному движению
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Right = Null_Z_Pos - Otvod;
   Cone_Right(a_flag, b_flag); //основное движение до правого лимита
   Step = 8;
   }

 else if (Step == 8 && Motor_Z_Pos == Limit_Pos_Right && Step_Z_flag == OFF && Step_X_flag == OFF) //когда доехали до правого лимита
  {
   Limit_Pos_Rear = Motor_X_Pos;
   Limit_Pos_VR = Limit_Pos_Right;  //чтобы этот лимит остался в памяти
   Limit_Pos_Left = Limit_Pos_Right;
   a_flag = false;
   b_flag = false; 

    if (want_pause == true || Pass_Nr == Pass_Total) //если нажата нижняя кнопка меню или это последний цикл
     {
      Beep();
      Otvod_30 = ((long)MOTOR_Z_STEP_PER_REV * 3000L + SCREW_Z / 2L) / SCREW_Z * McSTEP_Z; //отвод в микрошагах, где 3000 это 30 мм в сотках, точность не нужна
      Limit_Pos_Right = Limit_Pos_Left - REBOUND_Z - Otvod_30; //на 30 мм  правее необходимого для выборки люфта
      Rapid_Feed_Right(a_flag, b_flag);  //1 этап выборки люфта по Z - движение вправо, на 30 мм правее необходимого для выборки люфта
      Step = 10;
     }
    else //продолжение цикла без паузы
     {
      Limit_Pos_Right = Limit_Pos_Left - REBOUND_Z;
      Feed_Right(a_flag, b_flag); //1 этап выборки люфта по Z - движение вправо
      Step = 13; 
     }
  }

 else if (Step == 10 && Motor_Z_Pos == Limit_Pos_Right  && Step_Z_flag == OFF) 
  {
   isPausedAfterStep = true;
   Beep();
   Step = 11;
  }

  else if (Step == 11 && isPausedAfterStep == true)
   {
    if (button_up_flag) //если нажата верхняя кнопка меню, прерываем паузу
     {
      want_pause = false;         
      isPausedAfterStep = false; 
      Step = 12;
      Beep();
     }
     else 
     {
      return; 
     }
   }

  else if (Step == 12)
   {
    a_flag = false;
    b_flag = false;
    Limit_Pos_Left = Limit_Pos_Right + REBOUND_Z + Otvod_30;
    Rapid_Feed_Left(a_flag, b_flag); // 2 этап выборки люфта и 30 мм, возвращаемся влево
    Step = 14; 
   }

  else if (Step == 13 && Motor_Z_Pos == Limit_Pos_Right  && Step_Z_flag == OFF)  
   {
    a_flag = false;
    b_flag = false;
    Limit_Pos_Left = Limit_Pos_Right + REBOUND_Z;
    Feed_Left(a_flag, b_flag);  //2 этап выборки люфта, возвращаемся влево
    Step = 14; 
   }

else if (Step == 14 && Motor_Z_Pos == Limit_Pos_Left && Step_Z_flag == OFF) 
 {
  Limit_Pos_Right = Limit_Pos_Left;
  Pass_Nr++;
  if (Pass_Nr > Pass_Total) //если это последний проход, то поедем в дальнюю точку
   {
    a_flag = false;
    b_flag = false;
    Limit_Pos_Front = Limit_Pos_Rear + REBOUND_X;
    Feed_Front(a_flag, b_flag); // едем по Х вверх на REBOUND_X
    Step = 16;   
   }
  else 
   {
   Step = 0;  //в начало цикла
   cycle_flag = false;
   }
 } 

else if (Step == 16 && Motor_X_Pos == Limit_Pos_Front && Step_X_flag == OFF) 
{
 a_flag = false;
 b_flag = false; 
 Limit_Pos_Left = Limit_Pos_VL;
 if ((Limit_Pos_Left - Limit_Pos_Right) > (((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2)) 
   Rapid_Cone_Left(a_flag, b_flag); //едем влево до конечной точки Limit_Pos_VL
   else Cone_Left(a_flag, b_flag); 
  Step = 17;
}

else if (Step == 17 && Motor_Z_Pos == Limit_Pos_Left && Step_X_flag == OFF && Step_Z_flag == OFF) //когда доехали до левого лимита
  {
   Limit_Pos_Front = Motor_X_Pos;
   Limit_Pos_Right = Limit_Pos_Left;
   a_flag = false;
   b_flag = false; 
   Limit_Pos_Left = Limit_Pos_Right + REBOUND_Z;
   Feed_Left(a_flag, b_flag); //1 этап выборки люфта по Z - движение влево
   Step = 19;
  }

else if (Step == 19 && Motor_Z_Pos == Limit_Pos_Left  && Step_Z_flag == OFF) 
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Right = Limit_Pos_VL;
   Feed_Right(a_flag, b_flag); //2 этап выборки люфта по Z - движение вправо, возвращаемся в точку Limit_Pos_VL
   Step = 20;
  }

 else if (Step == 20 && Motor_Z_Pos == Limit_Pos_Right && Step_Z_flag == OFF) //когда доехали до точки Limit_Pos_VL
  {
   a_flag = false;
   b_flag = false;
   Limit_Pos_Rear = (Limit_Pos_Front - REBOUND_X ); //компенсация отскока 
   Feed_Rear(a_flag, b_flag); //едем на конечную точку
   Step = 21;
  }

 else if (Step == 21 && Motor_X_Pos == Limit_Pos_Rear && Step_X_flag == OFF)
  {
   Step = 0;  //в начало цикла
   cycle_flag = false; 
  }

} //окончание Cone_Int_Left() 

/////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////
void Cone_Int_Right()
{
   
}

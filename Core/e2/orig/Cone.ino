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
   
   Cs_Divisor = Cone_Info[Cone_Step].Cs_Div;
   Cm_Divisor = Cone_Info[Cone_Step].Cm_Div;

   Start_Speed = ENC_LINE_PER_REV / ((uint32_t)MOTOR_Z_STEP_PER_REV * McSTEP_Z * MIN_FEED / SCREW_Z) / FEED_ACCEL /2;
   if (Motor_Z_Pos < Limit_Pos_Left - Start_Speed * 2)
   {
      Feed_Divisor = ENC_LINE_PER_REV / ((uint32_t)MOTOR_Z_STEP_PER_REV * McSTEP_Z * Feed_mm / SCREW_Z) /2;
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
   
   Cs_Divisor = Cone_Info[Cone_Step].Cs_Div;
   Cm_Divisor = Cone_Info[Cone_Step].Cm_Div;

   Start_Speed = ENC_LINE_PER_REV / ((uint32_t)MOTOR_Z_STEP_PER_REV * McSTEP_Z * MIN_FEED / SCREW_Z) / FEED_ACCEL /2;
   if (Motor_Z_Pos > Limit_Pos_Right + Start_Speed * 2)
   {
      Feed_Divisor = ENC_LINE_PER_REV / ((uint32_t)MOTOR_Z_STEP_PER_REV * McSTEP_Z * Feed_mm / SCREW_Z) /2;
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
}

void Cone_Rear(bool & a_flag, bool & b_flag)
{
   if (b_flag == true) return;
   a_flag = false;
   b_flag = true; 
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


void Cone_Ext_Left()
{
   if (Motor_Z_Pos == Limit_Pos_Right && Motor_X_Pos == Null_X_Pos && Step_Z_flag == OFF)
   {  Step = 0;
      if (cycle_flag == false && Pass_Nr <= Pass_Total)
      {
         cycle_flag = true;
         a_flag = false;
         b_flag = false;

         Infeed_Value = (int)((float)MOTOR_X_STEP_PER_REV * (Ap / 2) / SCREW_X +0.5) * McSTEP_X;
         if (Infeed_Value == 0)
         {
            if (Pass_Nr == 1) Limit_Pos_Front = (Null_X_Pos + 1);
            else              Limit_Pos_Front = (Limit_Pos_Rear + REBOUND_X);
         }
         else
         {
            if (Pass_Nr == 1) Limit_Pos_Front = (Null_X_Pos + Infeed_Value);
            else              Limit_Pos_Front = (Limit_Pos_Rear + REBOUND_X + Infeed_Value);
         }
         Limit_Front_LED_On();
         BeepBeep();
         Feed_Front(a_flag, b_flag);
      }
   }

   else if (Motor_Z_Pos == Limit_Pos_Right && Motor_X_Pos == Limit_Pos_Front && Step_X_flag == OFF && Step != 6)
   {
      if (Pass_Nr <= Pass_Total)
      {
         a_flag = false;
         b_flag = false;

         Cone_Left(a_flag, b_flag);
         Step = 1;
      }
   }

   else if (Motor_Z_Pos == Limit_Pos_Left && Step == 1)
   {
    Motor_Z_Pos = Limit_Pos_Left;
    Motor_X_Pos = Limit_Pos_Front;
    Step = 2;
   }
      else if (Motor_Z_Pos == Limit_Pos_Left && Motor_X_Pos == Limit_Pos_Front && Step == 2)
   {
      cycle_flag = false;
      a_flag = false;
      b_flag = false;
      
      Limit_Pos_Rear = (Limit_Pos_Front - REBOUND_X);
      Limit_Rear_LED_On();
      Feed_Rear(a_flag, b_flag);
      Step = 3;
   }
      else if (Motor_Z_Pos == Limit_Pos_Left && Step == 3 && Motor_X_Pos == Limit_Pos_Rear && Step_X_flag == OFF)
   {
      a_flag = false;
      b_flag = false;
      Rapid_Cone_Right(a_flag, b_flag);
      Step = 4;
      Pass_Nr++;
   }
      else if (Motor_Z_Pos == Limit_Pos_Right && Step == 4 && Step_Z_flag == OFF)
      {
       Motor_Z_Pos = Limit_Pos_Right;
       Motor_X_Pos = Limit_Pos_Rear;
       Step = 5;       
        }
      else if (Motor_Z_Pos == Limit_Pos_Right && Step == 5 && Motor_X_Pos == Limit_Pos_Rear && Step_Z_flag == OFF)
   {
      if (Pass_Total-Pass_Nr+1 > 0)
     { cycle_flag = false;
      a_flag = false;
      b_flag = false;
      Infeed_Value = (int)((float)MOTOR_X_STEP_PER_REV * (Ap / 2) / SCREW_X +0.5) * McSTEP_X;
      Limit_Pos_Front = (Limit_Pos_Rear + REBOUND_X) + Infeed_Value;
      Limit_Front_LED_On();
      Feed_Front(a_flag, b_flag);
      BeepBeep();
      Print();
     }
     else     {
      Step = 6;
      a_flag = false;
      b_flag = false;
      Limit_Pos_Front = Limit_Pos_Rear + REBOUND_X;
      Limit_Front_LED_Off();
      Limit_Rear_LED_Off();
      Pass_Total = 1;
      Pass_Nr = 1;      
      Feed_Front(a_flag, b_flag);
      BeepComplete(); 
      Print();
      }
      if (Step == 6)
   { Limit_Pos_Front = Limit_Pos_Max;
     Limit_Pos_Rear = Limit_Pos_Min; 
   }
}
}

////////////////////////////////////////////////////////////////////////////////////////////////
void Cone_Ext_Right()
{
   if (Motor_Z_Pos == Limit_Pos_Left && Motor_X_Pos == Null_X_Pos && Step_Z_flag == OFF)
   {  Step = 0;
      if (cycle_flag == false && Pass_Nr <= Pass_Total)
      {
         cycle_flag = true;
         a_flag = false;
         b_flag = false;

         Infeed_Value = (int)((float)MOTOR_X_STEP_PER_REV * (Ap / 2) / SCREW_X +0.5) * McSTEP_X;
         if (Infeed_Value == 0)
         {
            if (Pass_Nr == 1) Limit_Pos_Front = (Null_X_Pos + 1);
            else              Limit_Pos_Front = (Limit_Pos_Rear + REBOUND_X);
         }
         else
         {
            if (Pass_Nr == 1) Limit_Pos_Front = (Null_X_Pos + Infeed_Value);
            else              Limit_Pos_Front = (Limit_Pos_Rear + REBOUND_X + Infeed_Value);
         }
         Limit_Front_LED_On();
         BeepBeep();
         Feed_Front(a_flag, b_flag);
      }
   }

   else if (Motor_Z_Pos == Limit_Pos_Left && Motor_X_Pos == Limit_Pos_Front && Step_X_flag == OFF && Step != 6)
   {
      if (Pass_Nr <= Pass_Total)
      {
         a_flag = false;
         b_flag = false;

         Cone_Right(a_flag, b_flag);
         Step = 1;
      }
   }

   else if (Motor_Z_Pos == Limit_Pos_Right && Step == 1)
   {
    Motor_Z_Pos = Limit_Pos_Right;
    Motor_X_Pos = Limit_Pos_Front;
    Step = 2;
   }
      else if (Motor_Z_Pos == Limit_Pos_Right && Motor_X_Pos == Limit_Pos_Front && Step == 2)
   {
      cycle_flag = false;
      a_flag = false;
      b_flag = false;
      
      Limit_Pos_Rear = (Limit_Pos_Front - REBOUND_X);
      Limit_Rear_LED_On();
      Feed_Rear(a_flag, b_flag);
      Step = 3;
   }
      else if (Motor_Z_Pos == Limit_Pos_Right && Step == 3 && Motor_X_Pos == Limit_Pos_Rear && Step_X_flag == OFF)
   {
      a_flag = false;
      b_flag = false;
      Rapid_Cone_Left(a_flag, b_flag);
      Step = 4;
      Pass_Nr++;
   }
      else if (Motor_Z_Pos == Limit_Pos_Left && Step == 4 && Step_Z_flag == OFF)
      {
       Motor_Z_Pos = Limit_Pos_Left;
       Motor_X_Pos = Limit_Pos_Rear;
       Step = 5;       
        }
      else if (Motor_Z_Pos == Limit_Pos_Left && Step == 5 && Motor_X_Pos == Limit_Pos_Rear && Step_Z_flag == OFF)
   {
      if (Pass_Total-Pass_Nr+1 > 0 )
     { cycle_flag = false;
      a_flag = false;
      b_flag = false;
      Infeed_Value = (int)((float)MOTOR_X_STEP_PER_REV * (Ap / 2) / SCREW_X +0.5) * McSTEP_X;
      Limit_Pos_Front = (Limit_Pos_Rear + REBOUND_X) + Infeed_Value;
      Limit_Front_LED_On();
      BeepBeep();
      Feed_Front(a_flag, b_flag);
      Print();
     }
     else {
      Step = 6;
      a_flag = false;
      b_flag = false;
      Limit_Pos_Front = Limit_Pos_Rear + REBOUND_X;
      Limit_Front_LED_Off();
      Limit_Rear_LED_Off();
      Pass_Total = 1;
      Pass_Nr = 1;
      Feed_Front(a_flag, b_flag);
      BeepComplete(); 
      Print(); 
   }
      if (Step == 6)
   { Limit_Pos_Front = Limit_Pos_Max;
     Limit_Pos_Rear = Limit_Pos_Min; 
   }
      
 }
}
///////////////////////////////////////////////////////////////////////////////////////////////
void Cone_Int_Left()
{
   if (Motor_Z_Pos == Limit_Pos_Right && Motor_X_Pos == Null_X_Pos && Step_Z_flag == OFF)
   {  Step = 0;
      if (cycle_flag == false && Pass_Nr <= Pass_Total)
      {
         cycle_flag = true;
         a_flag = false;
         b_flag = false;

         Infeed_Value = (int)((float)MOTOR_X_STEP_PER_REV * (Ap / 2) / SCREW_X +0.5) * McSTEP_X;
         if (Infeed_Value == 0)
         {
            if (Pass_Nr == 1) Limit_Pos_Rear = (Null_X_Pos + 1);
            else              Limit_Pos_Rear = (Limit_Pos_Rear + REBOUND_X);
         }
         else
         {
            if (Pass_Nr == 1) Limit_Pos_Rear = (Null_X_Pos - Infeed_Value);
            else              Limit_Pos_Rear = (Limit_Pos_Front - REBOUND_X - Infeed_Value);
         }
         Limit_Rear_LED_On();
         BeepBeep();
         Feed_Rear(a_flag, b_flag);
      }
   }

   else if (Motor_Z_Pos == Limit_Pos_Right && Motor_X_Pos == Limit_Pos_Rear && Step_X_flag == OFF && Step != 6)
   {
      if (Pass_Nr <= Pass_Total)
      {
         a_flag = false;
         b_flag = false;

         Cone_Left(a_flag, b_flag);
         Step = 1;
      }
   }

   else if (Motor_Z_Pos == Limit_Pos_Left && Step == 1)
   {
    Motor_Z_Pos = Limit_Pos_Left;
    Motor_X_Pos = Limit_Pos_Rear;
    Step = 2;
   }
      else if (Motor_Z_Pos == Limit_Pos_Left && Motor_X_Pos == Limit_Pos_Rear && Step == 2)
   {
      cycle_flag = false;
      a_flag = false;
      b_flag = false;
      
      Limit_Pos_Front = (Limit_Pos_Rear + REBOUND_X);
      Limit_Front_LED_On();
      Feed_Front(a_flag, b_flag);
      Step = 3;
   }
      else if (Motor_Z_Pos == Limit_Pos_Left && Step == 3 && Motor_X_Pos == Limit_Pos_Front && Step_X_flag == OFF)
   {
      a_flag = false;
      b_flag = false;
      Rapid_Cone_Right(a_flag, b_flag);
      Step = 4;
      Pass_Nr++;
   }
      else if (Motor_Z_Pos == Limit_Pos_Right && Step == 4 && Step_Z_flag == OFF)
      {
       Motor_Z_Pos = Limit_Pos_Right;
       Motor_X_Pos = Limit_Pos_Front;
       Step = 5;       
        }
      else if (Motor_Z_Pos == Limit_Pos_Right && Step == 5 && Motor_X_Pos == Limit_Pos_Front && Step_Z_flag == OFF)
   {
      if (Pass_Total-Pass_Nr+1 > 0 )
     { cycle_flag = false;
      a_flag = false;
      b_flag = false;
      Infeed_Value = (int)((float)MOTOR_X_STEP_PER_REV * (Ap / 2) / SCREW_X +0.5) * McSTEP_X;
      Limit_Pos_Rear = (Limit_Pos_Front - REBOUND_X) - Infeed_Value;
      Limit_Rear_LED_On();
      Feed_Rear(a_flag, b_flag);
      BeepBeep();
      Print();
     }
     else {
      Step = 6;
      a_flag = false;
      b_flag = false;
      Limit_Pos_Rear = Limit_Pos_Front - REBOUND_X;
      Limit_Front_LED_Off();
      Limit_Rear_LED_Off();
      Pass_Total = 1;
      Pass_Nr = 1;
      Feed_Rear(a_flag, b_flag);
      BeepComplete(); 
      Print();
   }
      if (Step == 6)
   { Limit_Pos_Front = Limit_Pos_Max;
     Limit_Pos_Rear = Limit_Pos_Min; 
   }
}
}

/////////////////////////////////////////////////////////////////////////////////////////////////
void Cone_Int_Right()
{
   if (Motor_Z_Pos == Limit_Pos_Left && Motor_X_Pos == Null_X_Pos && Step_Z_flag == OFF)
   {  Step = 0;
      if (cycle_flag == false && Pass_Nr <= Pass_Total)
      {
         cycle_flag = true;
         a_flag = false;
         b_flag = false;

         Infeed_Value = (int)((float)MOTOR_X_STEP_PER_REV * (Ap / 2) / SCREW_X +0.5) * McSTEP_X;
         if (Infeed_Value == 0)
         {
            if (Pass_Nr == 1) Limit_Pos_Rear = (Null_X_Pos + 1);
            else              Limit_Pos_Rear = (Limit_Pos_Rear + REBOUND_X);
         }
         else
         {
            if (Pass_Nr == 1) Limit_Pos_Rear = (Null_X_Pos - Infeed_Value);
            else              Limit_Pos_Rear = (Limit_Pos_Front - REBOUND_X - Infeed_Value);
         }
         Limit_Rear_LED_On();
         BeepBeep();
         Feed_Rear(a_flag, b_flag);
      }
   }

   else if (Motor_Z_Pos == Limit_Pos_Left && Motor_X_Pos == Limit_Pos_Rear && Step_X_flag == OFF && Step != 6)
   {
      if (Pass_Nr <= Pass_Total)
      {
         a_flag = false;
         b_flag = false;

         Cone_Right(a_flag, b_flag);
         Step = 1;
      }
   }

   else if (Motor_Z_Pos == Limit_Pos_Right && Step == 1)
   {
    Motor_Z_Pos = Limit_Pos_Right;
    Motor_X_Pos = Limit_Pos_Rear;
    Step = 2;
   }
      else if (Motor_Z_Pos == Limit_Pos_Right && Motor_X_Pos == Limit_Pos_Rear && Step == 2)
   {
      cycle_flag = false;
      a_flag = false;
      b_flag = false;
      
      Limit_Pos_Front = (Limit_Pos_Rear + REBOUND_X);
      Limit_Front_LED_On();
      Feed_Front(a_flag, b_flag);
      Step = 3;
   }
      else if (Motor_Z_Pos == Limit_Pos_Right && Step == 3 && Motor_X_Pos == Limit_Pos_Front && Step_X_flag == OFF)
   {
      a_flag = false;
      b_flag = false;
      Rapid_Cone_Left(a_flag, b_flag);
      Step = 4;
      Pass_Nr++;
   }
      else if (Motor_Z_Pos == Limit_Pos_Left && Step == 4 && Step_Z_flag == OFF)
      {
       Motor_Z_Pos = Limit_Pos_Left;
       Motor_X_Pos = Limit_Pos_Front;
       Step = 5;       
        }
      else if (Motor_Z_Pos == Limit_Pos_Left && Step == 5 && Motor_X_Pos == Limit_Pos_Front && Step_Z_flag == OFF)
   {
      if (Pass_Total-Pass_Nr+1 > 0 )
     { cycle_flag = false;
      a_flag = false;
      b_flag = false;
      Infeed_Value = (int)((float)MOTOR_X_STEP_PER_REV * (Ap / 2) / SCREW_X +0.5) * McSTEP_X;
      Limit_Pos_Rear = (Limit_Pos_Front - REBOUND_X) - Infeed_Value;
      Limit_Rear_LED_On();
      Feed_Rear(a_flag, b_flag);
      BeepBeep();      
      Print();
     }
     else {
      Step = 6;
      a_flag = false;
      b_flag = false;
      Limit_Pos_Rear = Limit_Pos_Front - REBOUND_X;
      Limit_Front_LED_Off();
      Limit_Rear_LED_Off();
      Pass_Total = 1;
      Pass_Nr = 1;
      Feed_Rear(a_flag, b_flag);
      BeepComplete(); 
      Print(); 
   }
      if (Step == 6)
   { Limit_Pos_Front = Limit_Pos_Max;
     Limit_Pos_Rear = Limit_Pos_Min; 
   }
}


}

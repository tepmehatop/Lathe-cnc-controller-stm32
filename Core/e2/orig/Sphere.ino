void Sphere() 
{       
   if (Raz == 1)  //режим шара
  {
            Pass_Total_Sphr = Sph_R_mm * 2 / Cutting_Width; 
            Sph_R = (float)MOTOR_X_STEP_PER_REV * Sph_R_mm / SCREW_X * McSTEP_X;
            Bar_R = (float)MOTOR_X_STEP_PER_REV * Bar_R_mm / SCREW_X * McSTEP_X; 
            SphBar = Sph_R - Bar_R; 
  }
else if (Raz == 2) // режим отрезания
  {
            Raz_R = Pass_Total * Ap / 100;  
  }
}


void Sphere_Ext_Left()
{ 
  static unsigned long  Infeed_ValueZ;  
  static unsigned long  Infeed_ValueX; 

  if (button_down_flag) { want_pause = true; Beep(); }   // опрос в любой точке цикла

  ////////////////////////////////////////////////////////////////////////
  /////////// ПАУЗА НА БЛИЖНЕМ ЛИМИТЕ: X отведён от детали ///////////////
  ////////////////////////////////////////////////////////////////////////
  if (want_pause == true && Motor_X_Pos == Limit_Pos_Rear && Step_X_flag == OFF && Step_Z_flag == OFF && !Complete_flag)
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

  /////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////РЕЖИМ ОТРЕЗАНИЯ///////////////////////////////////////
  if (Raz == 2) 
  {
   
  if (Step == 0 && Motor_X_Pos == Limit_Pos_Rear && Motor_Z_Pos == Limit_Pos_Right && Step_Z_flag == OFF && !Complete_flag)  
   { 
    if (cycle_flag == false && Pass_Nr <= Pass_Total)   
     {
      cycle_flag = true; 
      a_flag = false; 
      b_flag = false; 
      Infeed_ValueZ = (long)((float)MOTOR_Z_STEP_PER_REV * Staggered_Width / 2 / SCREW_Z + 0.5) * McSTEP_Z;   // [STM32] округление до шага, потом × микрошаг (как автор в V3.4 для aFeed)
      Infeed_ValueX = (long)((float)MOTOR_X_STEP_PER_REV * 0.5 * Ap / SCREW_X + 0.5) * McSTEP_X;   // [STM32] было +2 микрошага на врезание
      if (Pass_Nr == 1) Limit_Pos_Front = (Null_X_Pos + Infeed_ValueX);
      else              Limit_Pos_Front = (Limit_Pos_Rear + REBOUND_X + Infeed_ValueX * 2);
      Limit_Front_LED_On();
      Feed_Front(a_flag, b_flag);   //движение вверх на правой канавке
      Step = 1;
     }
    else //когда все циклы выполнены, то выход из автоматического режима
     {
        Limit_Front_LED_Off();
        Limit_Left_LED_Off();
        Limit_Pos_Front = Limit_Pos_Max;
        Limit_Pos_Left  = Limit_Pos_Max;
        Print();
        BeepComplete();              // ставит Complete_flag = true, снимается джойстиком в нейтрали
      }
   }
   
  else if (Step == 1 && Motor_X_Pos == Limit_Pos_Front && Step_Z_flag == OFF) 
   {
    a_flag = false; 
    b_flag = false; 
    Limit_Pos_Rear = (Limit_Pos_Front - Infeed_ValueX - REBOUND_X);
    Feed_Rear(a_flag, b_flag); //отскок вниз на правой канавке
    Step = 2;
    Pass_Nr++;
   }
   
  else if (Step == 2 && Motor_X_Pos == Limit_Pos_Rear && Step_X_flag == OFF) 
   {
     a_flag = false;
     b_flag = false;
     Limit_Pos_Left = (Null_Z_Pos + Infeed_ValueZ + REBOUND_Z);
     Limit_Left_LED_On();
     Feed_Left(a_flag, b_flag); //движение влево на левую канавку
     Step = 3; 
    }
    
  else if (Step == 3 && Motor_Z_Pos == Limit_Pos_Left && Step_Z_flag == OFF) 
  {
    if (Pass_Nr <= Pass_Total)   
    {
    a_flag = false; 
    b_flag = false; 
    Limit_Pos_Front = (Limit_Pos_Rear + REBOUND_X + Infeed_ValueX * 2);
    Feed_Front(a_flag, b_flag);      //движение вверх на левой канавке
    Step = 4; 
    }
    else if (!Complete_flag) //когда все циклы выполнены, то выход из автоматического режима, выполним завершение один раз
     {
        Limit_Front_LED_Off();
        Limit_Left_LED_Off();
        Limit_Pos_Front = Limit_Pos_Max;
        Limit_Pos_Left  = Limit_Pos_Max;   
        Print();
        BeepComplete();              // внутри ставит Complete_flag = true
      }
  } 
  
  else if (Step == 4 && Motor_X_Pos == Limit_Pos_Front && Step_Z_flag == OFF) 
   {
    a_flag = false; 
    b_flag = false; 
    Limit_Pos_Rear = (Limit_Pos_Front - Infeed_ValueX - REBOUND_X);
    Feed_Rear(a_flag, b_flag); //отскок вниз на левой канавке
    Step = 5;
    Pass_Nr++;
   } 
   
   else if (Step == 5 && Motor_X_Pos == Limit_Pos_Rear && Step_X_flag == OFF) 
   {
     a_flag = false;
     b_flag = false;
     Limit_Pos_Right = Null_Z_Pos;
     Feed_Right(a_flag, b_flag); //движение вправо на правую канавку
     Step = 0; 
     cycle_flag = false;
    }
  } // окончания условия if (Raz == 2)

  else if (Raz == 1)  
  {
    if (Motor_X_Pos == Limit_Pos_Rear && Motor_Z_Pos == Limit_Pos_Right && Step_X_flag == OFF && Step_Z_flag == OFF)
   {             
      if (cycle_flag == false && Pass_Nr <= Pass_Total_Sphr + 1)
      {
         McStep_Lag = McStep_Lag + Excess_Lag;
         cycle_flag = true;
         a_flag = false;
         b_flag = false;
         if (Pass_Nr <= Pass_Total_Sphr / 2) Infeed_ValueZ = ((float)MOTOR_Z_STEP_PER_REV * (Cutting_Width * Pass_Nr) / SCREW_Z +0.5) * McSTEP_Z;
         else                                Infeed_ValueZ = ((float)MOTOR_Z_STEP_PER_REV * ((Cutting_Width * (Pass_Nr - 1) + Cutter_Width)) / SCREW_Z +0.5) * McSTEP_Z;
         Limit_Pos_Left = (Null_Z_Pos + Infeed_ValueZ);
         Limit_Left_LED_On();
         BeepBeep();
         Feed_Left(a_flag, b_flag);    
      }
   }

   else if (Motor_X_Pos == Limit_Pos_Rear && Motor_Z_Pos == Limit_Pos_Left && Step_Z_flag == OFF && Step_X_flag == OFF)
   {
      if (Pass_Nr <= Pass_Total_Sphr + 1)
      {
         a_flag = false;
         b_flag = false;
         
         if (Pass_Nr <= Pass_Total_Sphr / 2)                          // до экватора
         {   
            Infeed_ValueX = (float)(Sph_R_mm - (sqrt((Sph_R_mm * Sph_R_mm) - (Sph_R_mm - Cutting_Width * Pass_Nr)*(Sph_R_mm - Cutting_Width * Pass_Nr)))) * MOTOR_X_STEP_PER_REV / SCREW_X * McSTEP_X;
         } 
         
         else if (Pass_Nr > Pass_Total_Sphr / 2 )                     // после экватора
         {  
            Infeed_ValueX = (float)(Sph_R_mm - (sqrt((Sph_R_mm * Sph_R_mm) - (Cutting_Width * (Pass_Nr - (Pass_Total_Sphr / 2 + 1)))*(Cutting_Width * (Pass_Nr - (Pass_Total_Sphr / 2 + 1)))))) * MOTOR_X_STEP_PER_REV / SCREW_X * McSTEP_X;
            if (Infeed_ValueX >= SphBar) Infeed_ValueX = SphBar;      // оставить ножку  
         }
         X_pos = X_pos - Excess_Lag;   
         Limit_Pos_Front = (Null_X_Pos + Infeed_ValueX + McStep_Lag - Excess_Lag);
         Limit_Pos_Right = Limit_Pos_Left;
         Limit_Front_LED_On();
       
         Feed_Front(a_flag, b_flag);
      }
   }

   else if (Motor_X_Pos == Limit_Pos_Front && Motor_Z_Pos == Limit_Pos_Left && Step_Z_flag == OFF && Step_X_flag == OFF)
   {
      cycle_flag = false;
      a_flag = false;
      b_flag = false;
      
      Limit_Pos_Rear = (Null_X_Pos - REBOUND_X + McStep_Lag - Excess_Lag);
      Pass_Nr++;
      if (Motor_X_Pos >= (Limit_Pos_Rear + ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) *2)) 
          {
            Rapid_Feed_Rear(a_flag, b_flag);
          }
      else                                                                                      
          {
            Feed_Rear(a_flag, b_flag);
          }
   }
  if (Pass_Nr > Pass_Total_Sphr + 1 && Complete_flag == false && Motor_X_Pos == Limit_Pos_Rear)
  {
         Limit_Front_LED_Off();
         Limit_Left_LED_Off();
         Limit_Pos_Front = Limit_Pos_Max;
         Limit_Pos_Left = Limit_Pos_Max;
         McStep_Lag = 0;          
         BeepComplete();
  }
  
  }
}

void Sphere_Ext_Right()
{
//
}

  
  
   
  
 
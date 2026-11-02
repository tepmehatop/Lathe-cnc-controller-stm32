void Sphere() 
{
            Pass_Total_Sphr = Sph_R_mm * 2 / Cutting_Width; 
            Sph_R = (float)MOTOR_X_STEP_PER_REV * Sph_R_mm / SCREW_X * McSTEP_X;
            Bar_R = (float)MOTOR_X_STEP_PER_REV * Bar_R_mm / SCREW_X * McSTEP_X;
            SphBar = Sph_R - Bar_R; 
}

void Sphere_Ext_Left()
{ 
  if (Motor_X_Pos == Limit_Pos_Rear && Motor_Z_Pos == Limit_Pos_Right && Step_X_flag == OFF)
   {        
    McStep_Lag = McStep_Lag + Excess_Lag; 
            
      if (cycle_flag == false && Pass_Nr <= Pass_Total_Sphr + 1)
      {
         cycle_flag = true;
         a_flag = false;
         b_flag = false;
         
         long Infeed_Value;
         {
            if (Pass_Nr <= Pass_Total_Sphr / 2) Infeed_ValueZ = ((float)MOTOR_Z_STEP_PER_REV * (Cutting_Width * Pass_Nr) / SCREW_Z +0.5) * McSTEP_Z;
            else                                Infeed_ValueZ = ((float)MOTOR_Z_STEP_PER_REV * ((Cutting_Width * (Pass_Nr - 1) + Cutter_Width)) / SCREW_Z +0.5) * McSTEP_Z;
         }
         
         Limit_Pos_Left = (Null_Z_Pos + Infeed_ValueZ);
         Limit_Left_LED_On();
         BeepBeep();
         Feed_Left(a_flag, b_flag);    
   
      }
   }

   else if (Motor_X_Pos == Limit_Pos_Rear && Motor_Z_Pos == Limit_Pos_Left && Step_Z_flag == OFF)
   {
      if (Pass_Nr <= Pass_Total_Sphr + 1)
      {
         a_flag = false;
         b_flag = false;
         
         long Infeed_Value;
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

   else if (Motor_X_Pos == Limit_Pos_Front && Motor_Z_Pos == Limit_Pos_Left && Step_Z_flag == OFF)
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
  if (Pass_Nr > Pass_Total_Sphr + 1 && Complete_flag == false)
  {
         Limit_Front_LED_Off();
         Limit_Left_LED_Off();
         Limit_Pos_Front = Limit_Pos_Max;
         Limit_Pos_Left = Limit_Pos_Max;
         McStep_Lag = 0;
         BeepComplete();
  }
}

void Sphere_Ext_Right()
{
//
}

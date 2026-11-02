void Menu() 
{
/////// Джойстик ///////////////////////////////////////////////
  byte Joy_New = Joy_Read;
        if ((Joy_New == B00001111) && (err_0_flag)) { 
             Joy_NoPressed();
             err_0_flag = false;  
             Joy_Old = Joy_New;
  } else if (!err_0_flag) {
         if (Joy_New == B00001110) {
         if (Joy_Old == B00001101) { Joy_NoPressed(); }
             Joy_LeftPressed();
  } else if (Joy_New == B00001101) {
         if (Joy_Old == B00001110) { Joy_NoPressed(); }
             Joy_RightPressed();
  } else if (Joy_New == B00001011) {
         if (Joy_Old == B00000111) { Joy_NoPressed(); }
             Joy_UpPressed();
  } else if (Joy_New == B00000111) {
         if (Joy_Old == B00001011) { Joy_NoPressed(); }
             Joy_DownPressed();
  } else if (Joy_New == B00001111) {
             Joy_NoPressed();
    }
              Joy_Old = Joy_New;
  } else {
              Beep_Error();
  }
    
   /////// Переключатель Режима ////////////////////////////////
   if (!Joy_Z_flag && !Joy_X_flag)
   {
      byte Mode_New = Mode_Read;
      if (Mode_New != Mode_Old)
      {
         if      (Mode_New == B01111111) {Switch_Feed(); Beep();}
         else if (Mode_New == B10111111) {Switch_aFeed(); Beep();}
         else if (Mode_New == B11011111) {Switch_Cone_L(); Beep();}
         else if (Mode_New == B11101111) {Switch_Cone_R(); Beep();}
         else if (Mode_New == B11110111) {Switch_Thread(); Beep();}
         else if (Mode_New == B11111011) {Switch_Sphere(); Beep();}
         else if (Mode_New == B11111101) {Switch_Devider(); Beep();}
         else if (Mode_New == B11111110) {Switch_Reserve(); Beep();} 
         Mode_Old = Mode_New;
      }
   }


   /////// Переключатель Подрежима //////////////////////////////
   if (!Joy_Z_flag && !Joy_X_flag)
   {
      byte Submode_New = Submode_Read;
      if (Submode_New != Submode_Old)
      {
         if      (Submode_New == B11000000) {Switch_Int(); Beep();}
         else if (Submode_New == B10100000) {Switch_Man(); Beep();}
         else if (Submode_New == B01100000) {Switch_Ext(); Beep();}
         Submode_Old = Submode_New;
      }
   }

    
   /////////// Кнопки Меню //////////////////////////////////////   
   byte Button_Sel_New = Button_Sel_Read;
   if (Button_Sel_New == Button_Sel_Old)
   {
      if (!Button_Sel_Read) {Key_Select_Pressed();}
      else                  {key_sel_flag = false; Print();}
   }
   Button_Sel_Old = Button_Sel_New;

   byte Button_New = Buttons_Read;
   if (Button_New != Button_Old)                                
   {
      if      (Button_New == B00000111) Key_Down_Pressed();
      else if (Button_New == B00001011) Key_Up_Pressed();
      else if (Button_New == B00001101) Key_Right_Pressed();
      else if (Button_New == B00001110) Key_Left_Pressed();
      else  {
            button_up_flag = false;
            button_down_flag = false;
            button_left_flag = false;
            button_right_flag = false;
            }
      KeyCycle = 0;                                         
   } else {
      KeyCycle++;                                     
      if(KeyCycle > DELAY_ENTER_KEYCYCLE)     
      {
        if (Button_New == B00000111)
        {
          button_down_flag = false;                                    
          Key_Down_Pressed();
        }
        else if (Button_New == B00001011)
        {
          button_up_flag = false;                                 
          Key_Up_Pressed();
        }
        else if (Button_New == B00001101)
        {
          button_right_flag = false;                                  
          Key_Right_Pressed();
        }
        else if (Button_New == B00001110)
        {
          button_left_flag = false;                                   
          Key_Left_Pressed();
        }
        KeyCycle = (DELAY_ENTER_KEYCYCLE - DELAY_INTO_KEYCYCLE);
      }
   }
   Button_Old = Button_New;
//      if (Thread_Info[Thread_Step].Ks_Div_Z / Ph == 0)
//   {
//    Beep_Error();
//   }  

   /////// Кнопки Лимитов ///////////////////////////////////////
   byte Limit_Button_New = Limit_Buttons_Read;
   if (Limit_Button_New == Limit_Button_Old)
   {
      if      (Limit_Button_New == B00010101) Limit_Left_Pressed();
      else if (Limit_Button_New == B01000101) Limit_Right_Pressed();
      else if (Limit_Button_New == B01010001) Limit_Front_Pressed();
      else if (Limit_Button_New == B01010100) Limit_Rear_Pressed();
      else     limit_button_flag = false;
   }
   Limit_Button_Old = Limit_Button_New;
   Blinc_Limit_Buttons();

   /////// Переключатель Оси для РГИ ////////////////////////////
   byte Hand_Axis_New = Hand_Axis_Read;
   if (Hand_Axis_New != Hand_Axis_Old)
   {
      if      (Hand_Axis_New == B00100000) {Switch_Hand_Axis_Z();}
      else if (Hand_Axis_New == B00010000) {Switch_Hand_Axis_X();}
      else if (Hand_Axis_New == B00110000) {Switch_Hand_Axis_No();}
      Hand_Axis_Old = Hand_Axis_New;
   }


   /////// Переключатель Масштаба для РГИ //////////////////////
   byte Hand_Scale_New = Hand_Scale_Read;
   if (Hand_Scale_New != Hand_Scale_Old)
   {
      if      (Hand_Scale_New == B00000001) {Switch_Scale_x1();}
      else if (Hand_Scale_New == B00000010) {Switch_Scale_x10();}
      Hand_Scale_Old = Hand_Scale_New;
      
   }
   CLEAR_KEYB_TIMER;
}




//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Обработка Джойстика ********** ///////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Обработчик Джойстик Влево **********
void Joy_LeftPressed()
{  
   flag_j = ON;
   Disa_INT_Hcoder();
   Disable_INT_OCR3A();
   Disable_INT_OCR3B();
   hand_Z = OFF;
   hand_X = OFF;
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;

   ///////////////////////
   if (Mode == Mode_Thread)
   {
      if (Sub_Mode_Thread == Sub_Mode_Thread_Man && err_1_flag == false && err_2_flag == false)
      {
         if (Spindle_Dir == CW) {Thread_Left(c_flag, d_flag);}    
         else
         {
            if (!Button_Rapid) {Limit_Pos = Limit_Pos_Min;}
            else               {Limit_Pos = Limit_Pos_Right + Brake_Compens;}
            Thread_Right(c_flag, d_flag);
         }
      }
      else if (Sub_Mode_Thread == Sub_Mode_Thread_Ext)
      {
         if (Spindle_Dir == CW) {Thread_Ext_Left();}    
         else                   {Thread_Ext_Right();}
      }
      else if (Sub_Mode_Thread == Sub_Mode_Thread_Int)
      {
         if (Spindle_Dir == CW) {Thread_Int_Left();}    
         else                   {Thread_Int_Right();}
      }
   }

   //////////////////////////
   else if (Mode == Mode_Feed)
   {
      if (Sub_Mode_Feed == Sub_Mode_Feed_Man && err_1_flag == false && err_2_flag == false)
      {
         if (!Button_Rapid)
         {  
            if (Motor_Z_Pos < (Limit_Pos_Left - ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
            {
               feed_Z_flag = OFF;
               if (!Step_Z_flag) {Rapid_Feed_Left(a_flag, b_flag);}
            }
         }
         else
         {
            if (OCR5A == max_OCR5A)
            {
               rapid_Z_flag = OFF;
               if (!rapid_step_Z_flag)
               {
                  if (Read_Z_Ena_State == false) Motor_Z_Enable();
                  feed_Z_flag = ON;
                  Feed_Left(a_flag, b_flag);
               }
            }
         }
      }
      else if (Sub_Mode_Feed == Sub_Mode_Feed_Ext)
      {
         Feed_Ext_Left();
      }
      else if (Sub_Mode_Feed == Sub_Mode_Feed_Int)
      {
         Feed_Int_Left();
      }  
   }

   ///////////////////////////
   else if (Mode == Mode_aFeed)
   {
       
      if (Sub_Mode_aFeed == Sub_Mode_aFeed_Man && err_1_flag == false && err_2_flag == false)
      {
         if (!Button_Rapid)
         {  
            if (Motor_Z_Pos < (Limit_Pos_Left - ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
            {
               feed_Z_flag = OFF;
               if (!Step_Z_flag) {Rapid_Feed_Left(a_flag, b_flag);}
            }
         }
         else
         {
            if (OCR4A == max_OCR4A)
            { 
               rapid_Z_flag = OFF;
               if (!rapid_step_Z_flag)
               {
                  if (Read_Z_Ena_State == false) Motor_Z_Enable();
                  feed_Z_flag = ON;
                  aFeed_Left(a_flag, b_flag);
               }
            }
         }
      }
      else if (Sub_Mode_aFeed == Sub_Mode_aFeed_Ext)
      {
         aFeed_Ext_Left();
      }
      else if (Sub_Mode_aFeed == Sub_Mode_aFeed_Int)
      {
         aFeed_Int_Left();
      }  
   }

   ///////////////////////////////////////////////////////////////////////////////////////////////////
   else if ((Mode == Mode_Cone_L || Mode == Mode_Cone_R) && err_1_flag == false && err_2_flag == false)
   {
      if (Sub_Mode_Cone == Sub_Mode_Cone_Man && err_1_flag == false && err_2_flag == false)
      {
    
      if (!Button_Rapid)
      {  
         if (Motor_Z_Pos < (Limit_Pos_Left - ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
         {
            feed_Z_flag = OFF;
            if (!Step_Z_flag) {Rapid_Cone_Left(a_flag, b_flag);}
         }
      }
      else
      {
         if (OCR5A == max_OCR5A)
         {
            rapid_Z_flag = OFF;
            if (!rapid_step_Z_flag)
            {
               Cone_Left(a_flag, b_flag);
            }
         }

      }
      
   }
       else if (Sub_Mode_Cone == Sub_Mode_Cone_Ext)
      {
         Cone_Ext_Left();
      }
      else if (Sub_Mode_Cone == Sub_Mode_Cone_Int)
      {
         Cone_Int_Left();
      }
   }

   //////////////////////////////////////////////////////////////////////////
   else if (Mode == Mode_Sphere && err_1_flag == false && err_2_flag == false)
    {
     if (Sub_Mode_Sphere == Sub_Mode_Sphere_Man && err_1_flag == false && err_2_flag == false)
      {
         if (!Button_Rapid)
         {  
            if (Motor_Z_Pos < (Limit_Pos_Left - ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
            {
               feed_Z_flag = OFF;
               if (!Step_Z_flag) {Rapid_Feed_Left(a_flag, b_flag);}
            }
         }
         else
         {
            if (OCR5A == max_OCR5A)
            {
               rapid_Z_flag = OFF;
               if (!rapid_step_Z_flag)
               {
                  if (Read_Z_Ena_State == false) Motor_Z_Enable();
                  feed_Z_flag = ON;
                  Feed_Left(a_flag, b_flag);
               }
            }
         }
      }
       else if (Sub_Mode_Sphere == Sub_Mode_Sphere_Ext) Sphere_Ext_Left();
       else if (Sub_Mode_Sphere == Sub_Mode_Sphere_Int)
      {
       //
      }
         
    }
   
}  


// ********** Обработчик Джойстик Вправо **********  
void Joy_RightPressed()
{
   flag_j = ON;
   Disa_INT_Hcoder();
   Disable_INT_OCR3A();
   Disable_INT_OCR3B();
   hand_Z = OFF;
   hand_X = OFF;
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;

   ////////////////////////
   if (Mode == Mode_Thread)
{
      if (Sub_Mode_Thread == Sub_Mode_Thread_Man && err_1_flag == false && err_2_flag == false)
     {
         if (Spindle_Dir == CW)
         {
            if (!Button_Rapid) {Limit_Pos = Limit_Pos_Min;}
            else               {Limit_Pos = Limit_Pos_Right + Brake_Compens;}
            Thread_Right(c_flag, d_flag);
         }
         else {Thread_Left(c_flag, d_flag);}
      }
      else if (Sub_Mode_Thread == Sub_Mode_Thread_Ext)
      {
         if (Spindle_Dir == CW) {Thread_Ext_Right();}
         else                   {Thread_Ext_Left();}
      }
      else if (Sub_Mode_Thread == Sub_Mode_Thread_Int)
      {
         if (Spindle_Dir == CW) {Thread_Int_Right();}
         else                   {Thread_Int_Left();}
      }
   }  

   ///////////////////////////
   else if (Mode == Mode_Feed)
   {
      if (Sub_Mode_Feed == Sub_Mode_Feed_Man && err_1_flag == false && err_2_flag == false)
      {
         if (!Button_Rapid)
         {  
            if (Motor_Z_Pos > (Limit_Pos_Right + ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
            {
               feed_Z_flag = OFF;
               if (!Step_Z_flag) {Rapid_Feed_Right(a_flag, b_flag);}
            }
         }
         else
         {
            if (OCR5A == max_OCR5A)
            {
               rapid_Z_flag = OFF;
               if (!rapid_step_Z_flag)
               {
                  if (Read_Z_Ena_State == false) Motor_Z_Enable();
                  feed_Z_flag = ON;
                  Feed_Right(a_flag, b_flag);
               }
            }
         }
      }
      else if (Sub_Mode_Feed == Sub_Mode_Feed_Ext)
      {
         Feed_Ext_Right();
      }
      else if (Sub_Mode_Feed == Sub_Mode_Feed_Int)
      {
         Feed_Int_Right();
      }  
   }

   ////////////////////////////
   else if (Mode == Mode_aFeed)
   {
     //if (Mode == Mode_aFeed){ Enable_INT0(); }
     
      if (Sub_Mode_aFeed == Sub_Mode_aFeed_Man && err_1_flag == false && err_2_flag == false)
      {
         if (!Button_Rapid)
         {  
            if (Motor_Z_Pos > (Limit_Pos_Right + ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
            {
               feed_Z_flag = OFF;
               if (!Step_Z_flag) {Rapid_Feed_Right(a_flag, b_flag);}
            }
         }
         else
         {
            if (OCR4A == max_OCR4A)
            {
               rapid_Z_flag = OFF;
               if (!rapid_step_Z_flag)
               {
                  if (Read_Z_Ena_State == false) Motor_Z_Enable();
                  feed_Z_flag = ON;
                  aFeed_Right(a_flag, b_flag);
               }
            }
         }
      }
      else if (Sub_Mode_aFeed == Sub_Mode_aFeed_Ext)
      {
         aFeed_Ext_Right();
      }
      else if (Sub_Mode_aFeed == Sub_Mode_aFeed_Int)
      {
         aFeed_Int_Right();
      }  
   }

   ////////////////////////////////////////////////////////////////////////////////////////////////////
   else if ((Mode == Mode_Cone_L || Mode == Mode_Cone_R) && err_1_flag == false && err_2_flag == false)
   {
     if (Sub_Mode_Cone == Sub_Mode_Cone_Man && err_1_flag == false && err_2_flag == false)
    {
      if (!Button_Rapid)
      {
         if (Motor_Z_Pos > (Limit_Pos_Right + ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
         {
            feed_Z_flag = OFF;
            if (!Step_Z_flag) {Rapid_Cone_Right(a_flag, b_flag);}
         }
      }
      else
      {
         if (OCR5A == max_OCR5A)
         {
            rapid_Z_flag = OFF;
            if (!rapid_step_Z_flag)
            {
               Cone_Right(a_flag, b_flag);
            }
         }
      }
   }   
      else if (Sub_Mode_Cone == Sub_Mode_Cone_Ext)
      {
         Cone_Ext_Right();
      }
      else if (Sub_Mode_Cone == Sub_Mode_Cone_Int)
      {
         Cone_Int_Right();
      }
   }
//////////////////////////////////////////////////////////////////////////
else if (Mode == Mode_Sphere && err_1_flag == false && err_2_flag == false)
    {
     if (Sub_Mode_Sphere == Sub_Mode_Sphere_Man && err_1_flag == false && err_2_flag == false)
      {
         if (!Button_Rapid)
         {  
            if (Motor_Z_Pos > (Limit_Pos_Right + ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
            {
               feed_Z_flag = OFF;
               if (!Step_Z_flag) {Rapid_Feed_Right(a_flag, b_flag);}
            }
         }
         else
         {
            if (OCR5A == max_OCR5A)
            {
               rapid_Z_flag = OFF;
               if (!rapid_step_Z_flag)
               {
                  if (Read_Z_Ena_State == false) Motor_Z_Enable();
                  feed_Z_flag = ON;
                  Feed_Right(a_flag, b_flag);
               }
            }
         }
      }
      else if (Sub_Mode_Sphere == Sub_Mode_Sphere_Ext)
      {
         Sphere_Ext_Right();
      }
      else if (Sub_Mode_Sphere == Sub_Mode_Sphere_Int)
      {
       //
      }
         
    }
}

// ********** Обработчик Джойстик Вверх **********  
void Joy_UpPressed()
{
   flag_j = ON;
   Disa_INT_Hcoder();
   Disable_INT_OCR3A();
   Disable_INT_OCR3B();
   hand_X = OFF;
   hand_Z = OFF;
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;

   ///////////////////////
   if (Mode == Mode_Thread)
   {  
      if (Sub_Mode_Thread == Sub_Mode_Thread_Man && err_1_flag == false && err_2_flag == false)
      {
         if (Spindle_Dir == CW) {Thread_Front(c_flag, d_flag);}
         else                   {Thread_Rear(c_flag, d_flag);}
      }
   }

   //////////////////////////
   else if (Mode == Mode_Feed || Mode == Mode_Sphere)
   {
      if (Sub_Mode_Feed == Sub_Mode_Feed_Man && err_1_flag == false && err_2_flag == false)
      {
         if (!Button_Rapid)
         {
            if (Motor_X_Pos < (Limit_Pos_Front - ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
            {  
               feed_X_flag = OFF;
               if (!Step_X_flag) {Rapid_Feed_Front(a_flag, b_flag);}
            }
         }
         else
         {
            if (OCR5A == max_OCR5A)
            {
               rapid_X_flag = OFF;
               if (!rapid_step_X_flag)
               {
                  if (Read_X_Ena_State == false) Motor_X_Enable();
                  feed_X_flag = ON;
                  Feed_Front(a_flag, b_flag);
               }
            }
         }
      }
      else if (Sub_Mode_Feed == Sub_Mode_Feed_Ext)
      {
         Feed_Ext_Front();
      }
      else if (Sub_Mode_Feed == Sub_Mode_Feed_Int)
      {
         Feed_Int_Front();
      } 
   }

   //////////////////////////
   else if (Mode == Mode_aFeed)
   {
    // if (Mode == Mode_aFeed){ Enable_INT0(); }
    
      if (Sub_Mode_aFeed == Sub_Mode_aFeed_Man && err_1_flag == false && err_2_flag == false)
      {
         if (!Button_Rapid)
         {
            if (Motor_X_Pos < (Limit_Pos_Front - ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
            {  
               feed_X_flag = OFF;
               if (!Step_X_flag) {Rapid_Feed_Front(a_flag, b_flag);}
            }
         }
         else
         {
            if (OCR4A == max_OCR4A)
            {
               rapid_X_flag = OFF;
               if (!rapid_step_X_flag)
               {
                  if (Read_X_Ena_State == false) Motor_X_Enable();
                  feed_X_flag = ON;
                  aFeed_Front(a_flag, b_flag);
               }
            }
         }
      }
      else if (Sub_Mode_aFeed == Sub_Mode_aFeed_Ext)
      {
         aFeed_Ext_Front();
      }
      else if (Sub_Mode_aFeed == Sub_Mode_aFeed_Int)
      {
         aFeed_Int_Front();
      } 
   }

   //////////////////////////////////////////////////////////////////////////
   else if (Mode == Mode_Cone_L && err_1_flag == false && err_2_flag == false)
   {
      if (!Button_Rapid)
      {
         if (Motor_X_Pos < (Limit_Pos_Front - ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
         {  
            feed_X_flag = OFF;
            if (!Step_X_flag) {Rapid_Feed_Front(a_flag, b_flag);}
         }
      }
      else
      {
         if (OCR5A == max_OCR5A)
         {
            rapid_X_flag = OFF;
            if (!rapid_step_X_flag)
            {
               if (Read_X_Ena_State == false) Motor_X_Enable();
               feed_X_flag = ON;
               Feed_Front(a_flag, b_flag);
            }
         }
      }
   }

   //////////////////////////////////////////////////////////////////////////
   else if (Mode == Mode_Cone_R && err_1_flag == false && err_2_flag == false)
   {
      if (!Button_Rapid)
      {
         if (Motor_X_Pos < (Limit_Pos_Front - ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
         {  
            feed_X_flag = OFF;
            if (!Step_X_flag) {Rapid_Feed_Front(a_flag, b_flag);}
         }
      }
      else
      {
         if (OCR5A == max_OCR5A)
         {
            rapid_X_flag = OFF;
            if (!rapid_step_X_flag)
            {
               if (Read_X_Ena_State == false) Motor_X_Enable();
               feed_X_flag = ON;
               Feed_Front(a_flag, b_flag);
            }
         }
      }
   }
}


// ********** Обработчик Джойстик Вниз **********  
void Joy_DownPressed()
{
   flag_j = ON;
   Disa_INT_Hcoder();
   Disable_INT_OCR3A();
   Disable_INT_OCR3B();
   hand_X = OFF;
   hand_Z = OFF;
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;

   ///////////////////////
   if (Mode == Mode_Thread)
   {
      if (Sub_Mode_Thread == Sub_Mode_Thread_Man && err_1_flag == false && err_2_flag == false)
      {
         if (Spindle_Dir == CW) {Thread_Rear(c_flag, d_flag);}
         else                   {Thread_Front(c_flag, d_flag);}
      }
   }

   ///////////////////////////
   else if (Mode == Mode_Feed || Mode == Mode_Sphere)
   {
      if (Sub_Mode_Feed == Sub_Mode_Feed_Man && err_1_flag == false && err_2_flag == false)
      {
         if (!Button_Rapid)
         {
            if (Motor_X_Pos > (Limit_Pos_Rear + ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
            {  
               feed_X_flag = OFF;
               if (!Step_X_flag) {Rapid_Feed_Rear(a_flag, b_flag);}
            }
         }
         else
         {
            if (OCR5A == max_OCR5A)
            {
               rapid_X_flag = OFF;
               if (!rapid_step_X_flag)
               {
                  if (Read_X_Ena_State == false) Motor_X_Enable();
                  feed_X_flag = ON;
                  Feed_Rear(a_flag, b_flag);
               }
            }
         }
      }
      else if (Sub_Mode_Feed == Sub_Mode_Feed_Ext)
      {
         Feed_Ext_Rear();
      }
      else if (Sub_Mode_Feed == Sub_Mode_Feed_Int)
      {
         Feed_Int_Rear();
      }
   }


   ///////////////////////////
   else if (Mode == Mode_aFeed)
   {
   //  if (Mode == Mode_aFeed){ Enable_INT0(); }
   
      if (Sub_Mode_aFeed == Sub_Mode_aFeed_Man && err_1_flag == false && err_2_flag == false)
      {
         if (!Button_Rapid)
         {
            if (Motor_X_Pos > (Limit_Pos_Rear + ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
            {  
               feed_X_flag = OFF;
               if (!Step_X_flag) {Rapid_Feed_Rear(a_flag, b_flag);}
            }
         }
         else
         {
            if (OCR4A == max_OCR4A)
            {
               rapid_X_flag = OFF;
               if (!rapid_step_X_flag)
               {
                  if (Read_X_Ena_State == false) Motor_X_Enable();
                  feed_X_flag = ON;
                  aFeed_Rear(a_flag, b_flag);
               }
            }
         }
      }
      else if (Sub_Mode_aFeed == Sub_Mode_aFeed_Ext)
      {
         //
      }
      else if (Sub_Mode_aFeed == Sub_Mode_aFeed_Int)
      {
         //
      }
   }

   //////////////////////////////////////////////////////////////////////////
   else if (Mode == Mode_Cone_L && err_1_flag == false && err_2_flag == false)
   {
      if (!Button_Rapid)
      {
         if (Motor_X_Pos > (Limit_Pos_Rear + ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
         {  
            feed_X_flag = OFF;
            if (!Step_X_flag) {Rapid_Feed_Rear(a_flag, b_flag);}
         }
      }
      else
      {
         if (OCR5A == max_OCR5A)
         {
            rapid_X_flag = OFF;
            if (!rapid_step_X_flag)
            {
               if (Read_X_Ena_State == false) Motor_X_Enable();
               feed_X_flag = ON;
               Feed_Rear(a_flag, b_flag);
            }
         }
      }
   }

   //////////////////////////////////////////////////////////////////////////
   else if (Mode == Mode_Cone_R && err_1_flag == false && err_2_flag == false)
   {
      if (!Button_Rapid)
      {
         if (Motor_X_Pos > (Limit_Pos_Rear + ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
         {  
            feed_X_flag = OFF;
            if (!Step_X_flag) {Rapid_Feed_Rear(a_flag, b_flag);}
         }
      }
      else
      {
         if (OCR5A == max_OCR5A)
         {
            rapid_X_flag = OFF;
            if (!rapid_step_X_flag)
            {
               if (Read_X_Ena_State == false) Motor_X_Enable();
               feed_X_flag = ON;
               Feed_Rear(a_flag, b_flag);
            }
         }
      }
   }
}


// ********** Обработчик Джойстик в нейтрали **********  
void Joy_NoPressed() 
{  
  Complete_flag = false;
  McStep_Lag = 0;
  Step = 0;

  
   if (Mode == Mode_aFeed){ Enable_INT0(); }
   if (flag_j == ON)
   {
      if (!Step_Z_flag && !rapid_step_Z_flag && !Step_X_flag && !rapid_step_X_flag)
      {
         flag_j = OFF;
         
         Motor_Z_Pos = ((Motor_Z_Pos + McSTEP_Z / 2) & ~(McSTEP_Z - 1));
         Motor_X_Pos = ((Motor_X_Pos + McSTEP_X / 2) & ~(McSTEP_X - 1));
         Null_Z_Pos = Motor_Z_Pos;
         Null_X_Pos = Motor_X_Pos;

         Motor_Z_RemovePulse();
         Motor_X_RemovePulse();
         Ks_Count = 0;
         Km_Count = 0;
         Cs_Count = 0;
         Cm_Count = 0;
         Repeat_Count = 0;
         a_flag = false;
         c_flag = false;
         d_flag = false;
         cycle_flag = false;
         Pass_Nr = 1;
         OCR5A = max_OCR5A;
         OCR4A = max_OCR4A;
         OCR2A = MIN_RAPID_MOTION;
         if (!flag_hand_Z) {Motor_Z_Disable();}
         if (!flag_hand_X) {Motor_X_Disable();}
      }
   }
   
   Joy_Z_flag = OFF;
   Joy_X_flag = OFF;
   feed_Z_flag = OFF;
   feed_X_flag = OFF;
   rapid_Z_flag = OFF;
   rapid_X_flag = OFF;
   b_flag = false;

   
   if (!Step_Z_flag && !rapid_step_Z_flag)
   {
      if (hand_Z == OFF)
      {         
         if (flag_hand_Z == ON)
         {
            hand_Z = ON;
         }
      }
      H_Coder();
   }

   if (!Step_X_flag && !rapid_step_X_flag)
   {
      if (hand_X == OFF)
      {
         if (flag_hand_X == ON)
         {
            hand_X = ON;
         }
      }
      H_Coder();
   }


   if (Mode == Mode_Thread)
   {
      //
      if (Sub_Mode_Thread != Sub_Mode_Thread_Man)
      {
         //
      }
   }
   
   if (Mode == Mode_Feed || Mode == Mode_aFeed)
   {
      //
      if (Sub_Mode_Feed != Sub_Mode_Feed_Man)
      {
         //
      }
   }

   if (Mode == Mode_Cone_L || Mode == Mode_Cone_R)
   {
      Pass_Nr = 1;
      if (!Step_Z_flag && !rapid_step_Z_flag)
      {
         rapid_step_X_flag = OFF;
         Step_X_flag = OFF;
      }
   }

   if (Mode == Mode_Sphere)
   {
    Sphere();
   }

   

   if ((Mode == Mode_Thread && Sub_Mode_Thread == Sub_Mode_Thread_Man)||
       (Mode == Mode_Feed && Sub_Mode_Feed == Sub_Mode_Feed_Man)||
       (Mode == Mode_aFeed && Sub_Mode_aFeed == Sub_Mode_aFeed_Man) ||
       (Mode == Mode_Cone_L && Sub_Mode_Cone == Sub_Mode_Cone_Man) ||
       (Mode == Mode_Cone_R && Sub_Mode_Cone == Sub_Mode_Cone_Man) ||
       (Mode == Mode_Sphere && Sub_Mode_Sphere == Sub_Mode_Sphere_Man) ||
       (Mode == Mode_Reserve))
    {
     // 
    }
    else
    {
       Disa_INT_Hcoder();
    }  
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Обработка Переключателя Режима ********** //////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Switch_Thread()
{
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;
   Null_Z_Pos = Motor_Z_Pos;
   Null_X_Pos = Motor_X_Pos;
   
   Ena_INT_Thrd();
   Mode = Mode_Thread;
   Step_Z_flag = OFF;
   Step_X_flag = OFF;
   rapid_step_Z_flag = OFF;
   rapid_step_X_flag = OFF;
   Ks_Count = 0;
   Km_Count = 0;
   Repeat_Count = 0;
   SelectMenu = 1;
   Thr_Pass_Summ = 0;
   Pass_Fin = 0;
   Print();
   Display_Send_All();
}

void Switch_Feed()
{
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;
   Null_Z_Pos = Motor_Z_Pos;
   Null_X_Pos = Motor_X_Pos;
   
   Ena_INT_Z_Feed();
   Mode = Mode_Feed;
   Step_Z_flag = OFF;
   Step_X_flag = OFF;
   rapid_step_Z_flag = OFF;
   rapid_step_X_flag = OFF;
   Ks_Count = 0;
   Km_Count = 0;
   Repeat_Count = 0;
   Pass_Total = 1;
   Ap = 0;
   SelectMenu = 1;
   Print();
   Display_Send_All();
}

void Switch_aFeed()
{
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;
   Null_Z_Pos = Motor_Z_Pos;
   Null_X_Pos = Motor_X_Pos;

   Ena_INT_X_aFeed();
   Mode = Mode_aFeed;
   Step_Z_flag = OFF;
   Step_X_flag = OFF;
   rapid_step_Z_flag = OFF;
   rapid_step_X_flag = OFF;
   Pass_Total = 1;
   Enc_Pos = 0;
   Ap = 0;
   SelectMenu = 1;
   Print();
   Display_Send_All();
}

void Switch_Cone_L()
{
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;
   Null_Z_Pos = Motor_Z_Pos;
   Null_X_Pos = Motor_X_Pos;
   
   Ena_INT_Z_Feed();
   Mode = Mode_Cone_L;
   Step_Z_flag = OFF;
   Step_X_flag = OFF;
   rapid_step_Z_flag = OFF;
   rapid_step_X_flag = OFF;
   Ks_Count = 0;
   Km_Count = 0;
   Repeat_Count = 0;
   Pass_Total = 1;
   Step = 0;
   Ap = 0;
   SelectMenu = 1;
   Print();
   Display_Send_All();
}

void Switch_Cone_R()
{
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;
   Null_Z_Pos = Motor_Z_Pos;
   Null_X_Pos = Motor_X_Pos;
   
   Ena_INT_Z_Feed();
   Mode = Mode_Cone_R;
   Step_Z_flag = OFF;
   Step_X_flag = OFF;
   rapid_step_Z_flag = OFF;
   rapid_step_X_flag = OFF;
   Ks_Count = 0;
   Km_Count = 0;
   Repeat_Count = 0;
   Pass_Total = 1;
   Step = 0;
   Ap = 0;
   SelectMenu = 1;
   Print();
   Display_Send_All();
}

void Switch_Devider()
{
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;
   Null_Z_Pos = Motor_Z_Pos;
   Null_X_Pos = Motor_X_Pos;
   
   Ena_INT_Thrd();
   Mode = Mode_Divider;
   Step_Z_flag = OFF;
   Step_X_flag = OFF;
   rapid_step_Z_flag = OFF;
   rapid_step_X_flag = OFF;
   Ks_Count = 0;
   Km_Count = 0;
   Repeat_Count = 0;
   Display_Send_All();
}

void Switch_Sphere()
{
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;
   Null_Z_Pos = Motor_Z_Pos;
   Null_X_Pos = Motor_X_Pos;
   
   Ena_INT_Z_Feed();
   Mode = Mode_Sphere;
   Step_Z_flag = OFF;
   Step_X_flag = OFF;
   rapid_step_Z_flag = OFF;
   rapid_step_X_flag = OFF;
   Ks_Count = 0;
   Km_Count = 0;
   Repeat_Count = 0;
   Pass_Total_Sphr = 1;
   Sphere();  // пересчитать Pass_Total_Sphr до отправки на дисплей
   Ap = 0;
   SelectMenu = 1;
   McStep_Lag= 0;
   Print();
   Display_Send_All();
}

void Switch_Reserve()
{
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;
   Null_Z_Pos = Motor_Z_Pos;
   Null_X_Pos = Motor_X_Pos;
   
   Mode = Mode_Reserve;
   Print();
   Display_Send_All();
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Обработка Переключателя Подрежима ********** ///////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Switch_Int()
{
   switch (Mode) 
   {
      case Mode_Thread:
      case Mode_Feed:
      case Mode_aFeed:
      case Mode_Cone_L:
      case Mode_Cone_R:
      if ((limit_Left_flag == ON && limit_Right_flag == ON) || (limit_Front_flag == ON && limit_Rear_flag == ON))
      {
         Sub_Mode_Thread = Sub_Mode_Thread_Int;
         Sub_Mode_Feed = Sub_Mode_Feed_Int;
         Sub_Mode_aFeed = Sub_Mode_aFeed_Int;
         Sub_Mode_Cone = Sub_Mode_Cone_Int;
         err_1_flag = false;
         Step_Z_flag = OFF;
         Step_X_flag = OFF;
         rapid_step_Z_flag = OFF;
         rapid_step_X_flag = OFF;
         Ks_Count = 0;
         Km_Count = 0;
         Repeat_Count = 0;

         if (Motor_Z_Pos == Limit_Pos_Right || Motor_Z_Pos == Limit_Pos_Left || Motor_X_Pos == Limit_Pos_Rear || Motor_X_Pos == Limit_Pos_Front)
         {
            err_2_flag = false;
         }
         else
         {
            Sub_Mode_Thread = Sub_Mode_Thread_Man;
            Sub_Mode_Feed = Sub_Mode_Feed_Man;
            Sub_Mode_aFeed = Sub_Mode_aFeed_Man;
            Sub_Mode_Cone = Sub_Mode_Cone_Man;
            err_2_flag = true;
            Beep_Error();
            Display_Send_Alert(2);  // Установите суппорт в исходное положение
         }
      }
      else
      {
         Sub_Mode_Thread = Sub_Mode_Thread_Man;
         Sub_Mode_Feed = Sub_Mode_Feed_Man;
         Sub_Mode_aFeed = Sub_Mode_aFeed_Man;
         Sub_Mode_Cone = Sub_Mode_Cone_Man;
         err_1_flag = true;
         Beep_Error();
         Display_Send_Alert(1);  // Установите упоры
      }
      Print();
      Display_Send_All();
      break;

      case Mode_Sphere:  /////////////////////////////////////////////////////////////////////////////////////
      if ((limit_Right_flag == ON && limit_Rear_flag == ON) || 
          (limit_Left_flag == ON && limit_Rear_flag == ON))
      {
         Sub_Mode_Sphere = Sub_Mode_Sphere_Int;
         err_1_flag = false;
         Step_Z_flag = OFF;
         Step_X_flag = OFF;
         Ks_Count = 0;
         Km_Count = 0;
         Repeat_Count = 0;
   
         if ((Motor_Z_Pos == Limit_Pos_Right && Motor_X_Pos == Limit_Pos_Rear) || 
             (Motor_Z_Pos == Limit_Pos_Left && Motor_X_Pos == Limit_Pos_Rear))
         {
            err_2_flag = false;
         }
         else
         {
            Sub_Mode_Sphere = Sub_Mode_Sphere_Man;
            err_2_flag = true;
            Beep_Error();
            Display_Send_Alert(2);  // Установите суппорт в исходное положение
         }
      }
      else
      {
         Sub_Mode_Sphere = Sub_Mode_Sphere_Man;
         err_1_flag = true;
         Beep_Error();
         Display_Send_Alert(1);  // Установите упоры
      }
      Print();
      Display_Send_All();
      break;
   }
}

void Switch_Man()
{  
      switch (Mode) 
      {
         case Mode_Thread:
         case Mode_Feed:
         case Mode_aFeed:
         case Mode_Cone_L:
         case Mode_Cone_R:
         case Mode_Sphere:
         Sub_Mode_Thread = Sub_Mode_Thread_Man;
         Sub_Mode_Feed = Sub_Mode_Feed_Man;
         Sub_Mode_aFeed = Sub_Mode_aFeed_Man;
         Sub_Mode_Cone = Sub_Mode_Cone_Man;
         Sub_Mode_Sphere = Sub_Mode_Sphere_Man;
         err_1_flag = false;
         err_2_flag = false;
         Display_Send_Alert(0);  // Закрыть алерт на новом дисплее
         Step_Z_flag = OFF;
         Step_X_flag = OFF;
         rapid_step_Z_flag = OFF;
         rapid_step_X_flag = OFF;
         Ks_Count = 0;
         Km_Count = 0;
         Repeat_Count = 0;
         Print();
         Display_Send_All();
         break;
      }
}

void Switch_Ext()
{
   switch (Mode) //////////////////////////////////////////////////////////////////////////////////////////////
   {
      case Mode_Thread:
      case Mode_Feed:
      case Mode_aFeed:
      case Mode_Cone_L:
      case Mode_Cone_R:
      if ((limit_Left_flag == ON && limit_Right_flag == ON) || (limit_Front_flag == ON && limit_Rear_flag == ON))
      {
         Sub_Mode_Thread = Sub_Mode_Thread_Ext;
         Sub_Mode_Feed = Sub_Mode_Feed_Ext;
         Sub_Mode_aFeed = Sub_Mode_aFeed_Ext;
         Sub_Mode_Cone = Sub_Mode_Cone_Ext;
         err_1_flag = false;
         Step_Z_flag = OFF;
         Step_X_flag = OFF;
         rapid_step_Z_flag = OFF;
         rapid_step_X_flag = OFF;
         Ks_Count = 0;
         Km_Count = 0;
         Repeat_Count = 0;

         if (Motor_Z_Pos == Limit_Pos_Right || Motor_Z_Pos == Limit_Pos_Left || Motor_X_Pos == Limit_Pos_Rear || Motor_X_Pos == Limit_Pos_Front)
         {
            err_2_flag = false;
         }
         else
         {
            Sub_Mode_Thread = Sub_Mode_Thread_Man;
            Sub_Mode_Feed = Sub_Mode_Feed_Man;
            Sub_Mode_aFeed = Sub_Mode_aFeed_Man;
            Sub_Mode_Cone = Sub_Mode_Cone_Man;
            err_2_flag = true;
            Beep_Error();
            Display_Send_Alert(2);  // Установите суппорт в исходное положение
         }
      }
      else
      {
         Sub_Mode_Thread = Sub_Mode_Thread_Man;
         Sub_Mode_Feed = Sub_Mode_Feed_Man;
         Sub_Mode_aFeed = Sub_Mode_aFeed_Man;
         Sub_Mode_Cone = Sub_Mode_Cone_Man;
         err_1_flag = true;
         Beep_Error();
         Display_Send_Alert(1);  // Установите упоры
      }
      Print();
      Display_Send_All();
      break;

      case Mode_Sphere:  /////////////////////////////////////////////////////////////////////////////////////
      if ((limit_Right_flag == ON && limit_Rear_flag == ON) || (limit_Left_flag == ON && limit_Rear_flag == ON))
      {
         Sub_Mode_Sphere = Sub_Mode_Sphere_Ext;
         err_1_flag = false;
         Step_Z_flag = OFF;
         Step_X_flag = OFF;
         Ks_Count = 0;
         Km_Count = 0;
         Repeat_Count = 0;
   
         if ((Motor_Z_Pos == Limit_Pos_Right && Motor_X_Pos == Limit_Pos_Rear) || (Motor_Z_Pos == Limit_Pos_Left && Motor_X_Pos == Limit_Pos_Rear))
         {
            err_2_flag = false;
         }
         else
         {
            Sub_Mode_Sphere = Sub_Mode_Sphere_Man;
            err_2_flag = true;
            Beep_Error();
            Display_Send_Alert(2);  // Установите суппорт в исходное положение
         }
      }
      else
      {
         Sub_Mode_Sphere = Sub_Mode_Sphere_Man;
         err_1_flag = true;
         Beep_Error();
         Display_Send_Alert(1);  // Установите упоры
      }
      Print();
      Display_Send_All();
      break;
   }
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Обработка Кнопок Меню ********** ///////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Обработчик нажатия кнопки Select **********
void Key_Select_Pressed()
{
   if (!key_sel_flag)
   {  
      switch (Mode) 
      {
         case Mode_Feed:
            if (Sub_Mode_Feed == Sub_Mode_Feed_Ext && limit_Front_flag == ON && limit_Rear_flag == ON){
            if      (SelectMenu == 1) SelectMenu = 2;
            else if (SelectMenu == 2) SelectMenu = 3;
            else if (SelectMenu == 3) SelectMenu = 1;
            Beep();
            Display_On_SelectMenu_Change();
            }
            else{
            if      (SelectMenu == 1) SelectMenu = 2;
            else if (SelectMenu == 2) SelectMenu = 1;
            if      (SelectMenu == 3) SelectMenu = 1;
            Beep();
            Display_On_SelectMenu_Change();
            }
         break;
         
         case Mode_aFeed:
            if      (SelectMenu == 1) SelectMenu = 2;
            else if (SelectMenu == 2) SelectMenu = 3;
            else if (SelectMenu == 3) SelectMenu = 1;
            Beep();
            Display_On_SelectMenu_Change();
         break;
        
         case Mode_Cone_L:
            if      (SelectMenu == 1) SelectMenu = 2;
            else if (SelectMenu == 2) SelectMenu = 3;
            else if (SelectMenu == 3) SelectMenu = 1;
            Beep();
            Display_On_SelectMenu_Change();
         break;

         case Mode_Cone_R:
            if      (SelectMenu == 1) SelectMenu = 2;
            else if (SelectMenu == 2) SelectMenu = 3;
            else if (SelectMenu == 3) SelectMenu = 1;
            Beep();
            Display_On_SelectMenu_Change();
         break;

         case Mode_Thread:
            if      (SelectMenu == 1) SelectMenu = 2;
            else if (SelectMenu == 2) SelectMenu = 3;
            else if (SelectMenu == 3) SelectMenu = 1;
            Beep();
            Display_On_SelectMenu_Change();
         break;         

         case Mode_Sphere:
            if      (SelectMenu == 1) SelectMenu = 2;
            else if (SelectMenu == 2) SelectMenu = 3;
            else if (SelectMenu == 3) SelectMenu = 1;
            Beep();
            Display_On_SelectMenu_Change();
         break;
         
         case Mode_Reserve:
            //
         break;
         
         case Mode_Divider:
            Enc_Pos = 0;
            Beep();
         break;
      }
      key_sel_flag = true;
      Print();
   }
}

// ********** Обработчик нажатия кнопки Up **********
void Key_Up_Pressed()
{
  if (!button_up_flag)
  {
     switch (Mode)
     {
        case Mode_Feed:
        if (SelectMenu == 1 && !Joy_Z_flag && !Joy_X_flag)
        {
         if (!Button_Rapid){
         if         (Ap < 9900)  {Ap = Ap + 2;    Beep();}}
         else { if  (Ap < 9900)  {Ap = Ap + 10;   Beep();}}
         }
        else if (SelectMenu == 2)
        {
         X_pos = 0;

         Beep();
        }
        else if (SelectMenu == 3)
         {
          if (TENSION_Z < 100) {TENSION_Z = TENSION_Z + TENS;   Beep();}
         }
        Print();
        if (SelectMenu == 1) Display_Send_Int("AP", Ap);
        break;

        case Mode_aFeed:
        if (SelectMenu == 1 && !Joy_Z_flag && !Joy_X_flag)
           {
         if (!Button_Rapid){
         if         (Ap < 9900)  {Ap = Ap + 1;    Beep();}}
         else { if  (Ap < 9900)  {Ap = Ap + 5;    Beep();}}
           }
         else if (SelectMenu == 2 && Total_Tooth < 255 && !Joy_Z_flag && !Joy_X_flag)
           {
           Total_Tooth++;
           Current_Tooth = 1;
           Beep();
           }
           else if (SelectMenu == 3)
           {
            X_pos = 0;
            Beep();
           }
           Print();
           if (SelectMenu == 1) Display_Send_Int("AP", Ap);
           break;

      
        case Mode_Thread:
        if (Thread_Step < TOTAL_THREADS - 1 && SelectMenu == 1)
        {
           if (!Joy_Z_flag && !Joy_X_flag)
           {
              Thread_Step++;
              if (Thread_Info[Thread_Step].Ks_Div_Z / Ph == 0)
              {Thread_Step--;}
              Ks_Count = 0;
              Km_Count = 0;
              Repeat_Count = 0;
              Step_Z_flag = OFF;
              Step_X_flag = OFF;
              rapid_step_Z_flag = OFF;
              rapid_step_X_flag = OFF;
              Thr_Pass_Summ = 0;
              Pass_Fin = 0;
              Beep();
           }
        }
        else if (SelectMenu == 2)
        {
         if (CThr_flag == false){ 
         if  (Ph < 90)  {Ph = Ph + 1; }
         if (Thread_Info[Thread_Step].Ks_Div_Z / Ph == 0)
        {
         if  (Ph > 0)  {Ph = Ph - 1;  }  
        }
        Beep();
        }
        }
        else if (SelectMenu == 3) 
        {
          X_pos = 0; 
          Beep();
        }        
        Print();
        break;        

        case Mode_Sphere:
           if (SelectMenu == 1 && !Joy_Z_flag && !Joy_X_flag)
           {
              if      (Sph_R_mm < 1250) {Sph_R_mm = Sph_R_mm + 25;  Beep();}
              else if (Sph_R_mm < 2500) {Sph_R_mm = Sph_R_mm + 50;  Beep();}
              else if (Sph_R_mm < 4750) {Sph_R_mm = Sph_R_mm + 250; Beep();}
           }
           else if (SelectMenu == 2 && !Joy_Z_flag && !Joy_X_flag)
           {
              if (Cutter_Step < TOTAL_CUTTER_WIDTH-1)
              {
                 Cutter_Step++;
                 Cutter_Width = Cutter_Width_array[Cutter_Step];
                 Beep();
              }
           }
           else if (SelectMenu == 3)
           {
            X_pos = 0;
            Beep();
           }
        Print();
        if (SelectMenu == 1) Display_Send_Int("SPHERE", Sph_R_mm);
        break;
        
        case Mode_Reserve:
           //
        Print();
        break;

        case Mode_Cone_L:
        case Mode_Cone_R:
        if (SelectMenu == 1 && !Joy_Z_flag && !Joy_X_flag)
        {
         if (!Button_Rapid){
         if         (Ap < 9900)  {Ap = Ap + 2;    Beep();}}
         else { if  (Ap < 9900)  {Ap = Ap + 10;   Beep();}}
        }
        else if (SelectMenu == 2 && !Joy_Z_flag && !Joy_X_flag)
        {
          if (Mode == Mode_Cone_R && Ph == 1) {ConR_Thr_flag = true; ConL_Thr_flag = false; CThr_flag = true; Beep();}
     else if (Mode == Mode_Cone_L && Ph == 1) {ConL_Thr_flag = true; ConR_Thr_flag = false; CThr_flag = true; Beep();}
        }
        else if (SelectMenu == 3)
        {
         X_pos = 0;
         Beep();
        }
        Print();
        if (SelectMenu == 1) Display_Send_Int("AP", Ap);
        break;
      }
     button_up_flag = true;
  }
}

// ********** Обработчик нажатия кнопки Down **********
void Key_Down_Pressed()
{
   if (!button_down_flag)
   {
      switch (Mode)
      {
         case Mode_Feed:
         if (SelectMenu == 1 && !Joy_Z_flag && !Joy_X_flag)
         {
         if (!Button_Rapid){
                if  (Ap > 0)    {Ap = Ap - 2;    Beep();}}
         else { if  (Ap > 10)   {Ap = Ap - 10;   Beep();}
           else if  (Ap > 0)    {Ap = Ap - 2;    Beep();}}
         }
         else if (SelectMenu == 2)
         {
          Z_pos = 0;

          Beep();
         }
         else if (SelectMenu == 3)
         {
          if (TENSION_Z > 0) {TENSION_Z = TENSION_Z - TENS;   Beep();}
         }
         Print();
         if (SelectMenu == 1) Display_Send_Int("AP", Ap);
         break;

         case Mode_aFeed:
         if (SelectMenu == 1 && !Joy_Z_flag && !Joy_X_flag)
            {
         if (!Button_Rapid){
                if  (Ap > 0)    {Ap = Ap - 1;    Beep();}}
         else { if  (Ap > 5)    {Ap = Ap - 5;    Beep();}
           else if  (Ap > 0)    {Ap = Ap - 1;    Beep();}}
            }
            else if (SelectMenu == 2 && Total_Tooth > 1 && !Joy_Z_flag && !Joy_X_flag)
            {
            Total_Tooth--;
            Current_Tooth = 1;
            Beep();
            }
         else if (SelectMenu == 3)
         {
          Z_pos = 0;
          Beep();
         }
         Print();
         if (SelectMenu == 1) Display_Send_Int("AP", Ap);
         break;
         
         case Mode_Thread:
         if (Thread_Step > 0 && SelectMenu == 1)
         {
            if (!Joy_Z_flag && !Joy_X_flag)
            {
               Thread_Step--;
               Ks_Count = 0;
               Km_Count = 0;
               Repeat_Count = 0;
               Step_Z_flag = OFF;
               Step_X_flag = OFF;
               rapid_step_Z_flag = OFF;
               rapid_step_X_flag = OFF;
               Thr_Pass_Summ = 0;
               Pass_Fin = 0;
               Beep();
            }           
         }
                 else if (SelectMenu == 2)
        {
         if  (Ph > 1)  {Ph = Ph - 1;    Beep();} 
        }
         else if (SelectMenu == 3) 
         {
          Z_pos = 0; 
          Beep();
         }          
         Print();
         break;        

         case Mode_Sphere:
            if (SelectMenu == 1 && !Joy_Z_flag && !Joy_X_flag)
            {
               if      (Sph_R_mm > 2500) {Sph_R_mm = Sph_R_mm - 250; Beep();}
               else if (Sph_R_mm > 1250) {Sph_R_mm = Sph_R_mm - 50;  Beep();}
               else if (Sph_R_mm > 50)   {Sph_R_mm = Sph_R_mm - 25;  Beep();}
               if (Sph_R_mm < Bar_R_mm) Bar_R_mm = Sph_R_mm;
            }
            else if (SelectMenu == 2 && !Joy_Z_flag && !Joy_X_flag)
            {
               if (Cutter_Step > 0)
               {
                  Cutter_Step--;
                  Cutter_Width = Cutter_Width_array[Cutter_Step];
                  Beep();
               }
            }
         else if (SelectMenu == 3)
         {
          Z_pos = 0;
          Beep();
         }
         Print();
         if (SelectMenu == 1) Display_Send_Int("SPHERE", Sph_R_mm);
         break;
        
         case Mode_Reserve:
          //
         Print();
         break;
         
         case Mode_Cone_L:
         case Mode_Cone_R:
         if (SelectMenu == 1 && !Joy_Z_flag && !Joy_X_flag)
         {
         if (!Button_Rapid){
                if  (Ap > 0)    {Ap = Ap - 2;    Beep();}}
         else { if  (Ap > 10)   {Ap = Ap - 10;   Beep();}
           else if  (Ap > 0)    {Ap = Ap - 2;    Beep();}}
         }
         else if (SelectMenu == 2 && !Joy_Z_flag && !Joy_X_flag)
         {
          if (Mode == Mode_Cone_R) {ConR_Thr_flag = false; CThr_flag = false; Beep();}
     else if (Mode == Mode_Cone_L) {ConL_Thr_flag = false; CThr_flag = false; Beep();}
         }
         else if (SelectMenu == 3)
         {
          Z_pos = 0;
          Beep();
         }
         Print();
         if (SelectMenu == 1) Display_Send_Int("AP", Ap);
         break;
      }
      button_down_flag = true;
   }
}

// ********** Обработчик нажатия кнопки Left **********
void Key_Left_Pressed()
{
   if (!button_left_flag)
   {
      switch (Mode)
      {
         case Mode_Feed:
         if (SelectMenu == 1 && Pass_Total > 1 && !Joy_Z_flag && !Joy_X_flag)
            {
               Pass_Total--;
               Beep();
            }
         else if (SelectMenu == 2)
         {
         if (flag_Scale_x1 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA * 100 ; i ++)  { MX_pos ++ ;}}
         else { for (int i = 0; i < LINEIKA ; i ++)        { MX_pos ++ ;}}}
         else if (flag_Scale_x10 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA *100 ; i ++)   { MX_pos ++ ;}}
         else { for (int i = 0; i < LINEIKA*BT_X; i ++)    { MX_pos ++ ;}}}
         Beep();
         }
         else if (SelectMenu == 3)
         {
        if (OTSKOK_Z == REBOUND_Z) {OTSKOK_Z = 1;   Beep();}
         }
         Print();
         Display_On_Pass_Change();
         break;

         case Mode_aFeed:
         if (SelectMenu == 1 && Pass_Total > 1 && !Joy_Z_flag && !Joy_X_flag)
            {
             Pass_Total--;
             Beep();
            }

         else if (SelectMenu == 2 && !Joy_Z_flag && !Joy_X_flag)
         {
         if (Current_Tooth > 1)
         {
            Current_Tooth--;
            Beep();
         }
         else if (Current_Tooth == 1)
         {
            Current_Tooth = Total_Tooth;
            Beep();
         }
         }
         else if (SelectMenu == 3)
         {
         if (flag_Scale_x1 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA * 100 ; i ++)  { MX_pos ++ ;}}
         else { for (int i = 0; i < LINEIKA ; i ++)        { MX_pos ++ ;}}}
         else if (flag_Scale_x10 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA *100 ; i ++)   { MX_pos ++ ;}}
         else { for (int i = 0; i < LINEIKA*BT_X; i ++)    { MX_pos ++ ;}}}
         Beep();
         }
         Print();
         Display_On_Pass_Change();
         break;

         case Mode_Cone_L:
         case Mode_Cone_R:
         if (SelectMenu == 1 && Pass_Total > 1 && !Joy_Z_flag && !Joy_X_flag)
         {
          Pass_Total--;
          Beep();
         }
         else if (SelectMenu == 2 && Cone_Step > 0 && !Joy_Z_flag && !Joy_X_flag)
         {
               Cone_Step--;
               Ks_Count = 0;
               Km_Count = 0;
               Repeat_Count = 0;
               Step_Z_flag = OFF;
               Step_X_flag = OFF;
               rapid_step_Z_flag = OFF;
               rapid_step_X_flag = OFF;
               Beep();
         }
         else if (SelectMenu == 3)
         {
         if (flag_Scale_x1 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA * 100 ; i ++)  { MX_pos ++ ;}}
         else { for (int i = 0; i < LINEIKA ; i ++)        { MX_pos ++ ;}}}
         else if (flag_Scale_x10 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA *100 ; i ++)   { MX_pos ++ ;}}
         else { for (int i = 0; i < LINEIKA*BT_X; i ++)    { MX_pos ++ ;}}}
         Beep();
         }
         Print();
         Display_On_Pass_Change();
         break;

         case Mode_Sphere:
            if (SelectMenu == 1 && !Joy_Z_flag && !Joy_X_flag)
            {
               if (Bar_R_mm > 0)
               {
                  Bar_R_mm = Bar_R_mm - 25;
                  Beep();
               }
            }
            else if (SelectMenu == 2 && !Joy_Z_flag && !Joy_X_flag)
            {
               if (Cutting_Step > 0)
               {
                  Cutting_Step--;
                  Cutting_Width = Cutting_Width_array[Cutting_Step];
                  Beep();
               }
            }
            else if (SelectMenu == 3)
            {
         if (flag_Scale_x1 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA * 100 ; i ++)  { MX_pos ++ ;}}
         else { for (int i = 0; i < LINEIKA ; i ++)        { MX_pos ++ ;}}}
         else if (flag_Scale_x10 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA *100 ; i ++)   { MX_pos ++ ;}}
         else { for (int i = 0; i < LINEIKA*BT_X; i ++)    { MX_pos ++ ;}}}
         Beep();
            }
            Print();
            if (SelectMenu == 1) Display_Send_Int("BAR", Bar_R_mm);
            break;
     
         case Mode_Reserve:
          //
         Print();
         break;  

         case Mode_Thread:
         if (Pass_Fin > 0 && SelectMenu == 2 && !Joy_Z_flag && !Joy_X_flag)
            {
               Pass_Fin --;
               Beep();
            }
         else if (SelectMenu == 1 && (Thread_Info[Thread_Step].Pass + Thr_Pass_Summ) > 2 && !Joy_Z_flag && !Joy_X_flag)
            {
               Thr_Pass_Summ --;
               Beep();
            }
         else if (SelectMenu == 3)
            {
         if (flag_Scale_x1 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA * 100 ; i ++)  { MX_pos ++ ;}}
         else { for (int i = 0; i < LINEIKA ; i ++)        { MX_pos ++ ;}}}
         else if (flag_Scale_x10 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA *100 ; i ++)   { MX_pos ++ ;}}
         else { for (int i = 0; i < LINEIKA*BT_X; i ++)    { MX_pos ++ ;}}}
         Beep();
            }
         Print();
         Display_On_Pass_Change();
         break;

      }
      button_left_flag = true;
   }
}

// ********** Обработчик нажатия кнопки Right **********
void Key_Right_Pressed()
{
   if (!button_right_flag)
   {
      switch (Mode)
      {
         case Mode_Feed:
         if (SelectMenu == 1 && Pass_Total < 99 && !Joy_Z_flag && !Joy_X_flag)
            {
               Pass_Total++;
               Beep();
            }
         else if (SelectMenu == 2)
         {
         if (flag_Scale_x1 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA * 100 ; i ++)  { MX_pos -- ;}}
         else { for (int i = 0; i < LINEIKA ; i ++)        { MX_pos -- ;}}}
         else if (flag_Scale_x10 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA *100 ; i ++)   { MX_pos -- ;}}
         else { for (int i = 0; i < LINEIKA*BT_X; i ++)    { MX_pos -- ;}}}
         Beep();
         }
         else if (SelectMenu == 3)
         {
          if (OTSKOK_Z < REBOUND_Z) {OTSKOK_Z = REBOUND_Z;   Beep();}
         }
         Print();
         Display_On_Pass_Change();
         break;

         case Mode_aFeed:
         if (Pass_Total < 99 && SelectMenu == 1 && !Joy_Z_flag && !Joy_X_flag)
         {
               Pass_Total++;
               Beep();
         }
         else if (SelectMenu == 2 && !Joy_Z_flag && !Joy_X_flag)
         {
         if (Current_Tooth < Total_Tooth)
         {
            Current_Tooth++;
            Beep();
         }
         else if (Current_Tooth == Total_Tooth)
         {
            Current_Tooth = 1;
            Beep();
         }
         }
         else if (SelectMenu == 3)
         {
         if (flag_Scale_x1 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA * 100 ; i ++)  { MX_pos -- ;}}
         else { for (int i = 0; i < LINEIKA ; i ++)        { MX_pos -- ;}}}
         else if (flag_Scale_x10 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA *100 ; i ++)   { MX_pos -- ;}}
         else { for (int i = 0; i < LINEIKA*BT_X; i ++)    { MX_pos -- ;}}}
         Beep();
         }
         Print();
         Display_On_Pass_Change();
         break;

         case Mode_Cone_L:
         case Mode_Cone_R:
         if (SelectMenu == 1 && Pass_Total < 99 && !Joy_Z_flag && !Joy_X_flag)
         {
          Pass_Total++;
          Beep();
         }
         else if (SelectMenu == 2 && Cone_Step < TOTAL_CONE - 1 && !Joy_Z_flag && !Joy_X_flag)
         {
          Cone_Step++;
          Ks_Count = 0;
          Km_Count = 0;
          Repeat_Count = 0;
          Step_Z_flag = OFF;
          Step_X_flag = OFF;
          rapid_step_Z_flag = OFF;
          rapid_step_X_flag = OFF;
          Beep();
         }
         else if (SelectMenu == 3)
         {
         if (flag_Scale_x1 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA * 100 ; i ++)  { MX_pos -- ;}}
         else { for (int i = 0; i < LINEIKA ; i ++)        { MX_pos -- ;}}}
         else if (flag_Scale_x10 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA *100 ; i ++)   { MX_pos -- ;}}
         else { for (int i = 0; i < LINEIKA*BT_X; i ++)    { MX_pos -- ;}}}
         Beep();
         }
         Print();
         Display_On_Pass_Change();
         break;

         case Mode_Sphere:
         if (SelectMenu == 1 && !Joy_Z_flag && !Joy_X_flag)
            {
               if (Bar_R_mm < Sph_R_mm)
               {
                  Bar_R_mm = Bar_R_mm + 25;
                  Beep();
               }
            }
            else if (SelectMenu == 2 && !Joy_Z_flag && !Joy_X_flag)
            {
               if (Cutting_Step < TOTAL_CUTTING_STEP-1)
               {
                  Cutting_Step++;
                  Cutting_Width = Cutting_Width_array[Cutting_Step];
                  Beep();
               }
            }
         else if (SelectMenu == 3)
            {
         if (flag_Scale_x1 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA * 100 ; i ++)  { MX_pos -- ;}}
         else { for (int i = 0; i < LINEIKA ; i ++)        { MX_pos -- ;}}}
         else if (flag_Scale_x10 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA *100 ; i ++)   { MX_pos -- ;}}
         else { for (int i = 0; i < LINEIKA*BT_X; i ++)    { MX_pos -- ;}}}
         Beep();
            }
         Print();
         if (SelectMenu == 1) Display_Send_Int("BAR", Bar_R_mm);
         break;
     
         case Mode_Reserve:
          //
         Print();
         break;

         case Mode_Thread:
         if (Pass_Fin < 9 && SelectMenu == 2 && !Joy_Z_flag && !Joy_X_flag)
            {
               Pass_Fin ++;
               Beep();
            }
         else if (SelectMenu == 1 && Thread_Info[Thread_Step].Pass + PASS_FINISH + Thr_Pass_Summ < 90 && !Joy_Z_flag && !Joy_X_flag)
            {
               Thr_Pass_Summ ++;
               Beep();
            }
         else if (SelectMenu == 3)
            {
         if (flag_Scale_x1 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA * 100 ; i ++)  { MX_pos -- ;}}
         else { for (int i = 0; i < LINEIKA ; i ++)        { MX_pos -- ;}}}
         else if (flag_Scale_x10 == ON){
         if (!Button_Rapid){
         for        (int i = 0; i < LINEIKA *100 ; i ++)   { MX_pos -- ;}}
         else { for (int i = 0; i < LINEIKA*BT_X; i ++)    { MX_pos -- ;}}}
         Beep();
            }
         Print();
         Display_On_Pass_Change();
         break;
                   
      }  
      button_right_flag = true;
   }
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Обработка кнопок лимита ********** /////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Обработчик нажатия кнопки Limit_Left **********
void Limit_Left_Pressed()
{
   if (!limit_button_flag)
   {
      limit_button_flag = true;
      switch (Mode)
      {
         case Mode_Thread:
         case Mode_Feed:
         case Mode_aFeed:
         case Mode_Cone_L:
         case Mode_Cone_R:
         case Mode_Sphere:
         if (!Joy_Z_flag && Submode_Read == B10100000)
         { 
            if (limit_Left_flag == OFF)
            {
               if (Motor_Z_Pos > (Limit_Pos_Right + ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
               {
                  limit_Left_flag = ON;
                  Limit_Pos_Left = ((Motor_Z_Pos + McSTEP_Z / 2) & ~(McSTEP_Z - 1));
                  Limit_Left_LED_On();
                  Beep();
               }
            }
            else
            {
               limit_Left_flag = OFF;
               Limit_Pos_Left = Limit_Pos_Max;
               Limit_Left_LED_Off();
               Beep();
            }
         }
      }
      Display_On_Limits_Change();
   }
}

// ********** Обработчик нажатия кнопки Limit_Right **********
void Limit_Right_Pressed()
{
   if (!limit_button_flag)
   {
      limit_button_flag = true;
      switch (Mode)
      {
         case Mode_Thread:
         case Mode_Feed:
         case Mode_aFeed:
         case Mode_Cone_L:
         case Mode_Cone_R:
         case Mode_Sphere:
         if (!Joy_Z_flag && Submode_Read == B10100000)
         { 
            if (limit_Right_flag == OFF)
            {
               if (Motor_Z_Pos < (Limit_Pos_Left - ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
               {
                  limit_Right_flag = ON;
                  Limit_Pos_Right = ((Motor_Z_Pos + McSTEP_Z / 2) & ~(McSTEP_Z - 1));
                  Limit_Right_LED_On();
                  Beep();
               }
            }
            else
            {
               limit_Right_flag = OFF;
               Limit_Pos_Right = Limit_Pos_Min;
               Limit_Right_LED_Off();
               Beep();
            }
         }
      }
      Display_On_Limits_Change();
   }
}

// ********** Обработчик нажатия кнопки Limit_Front **********
void Limit_Front_Pressed()
{
   if (!limit_button_flag)
   {
      limit_button_flag = true;
      switch (Mode)
      { 
         case Mode_Thread:
         case Mode_Feed:
         case Mode_aFeed:
         case Mode_Cone_L:
         case Mode_Cone_R:
         case Mode_Sphere:
         if (!Joy_X_flag && Submode_Read == B10100000)
         { 
            if (limit_Front_flag == OFF)
            {
               if (Motor_X_Pos > (Limit_Pos_Rear + ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
               {
                  limit_Front_flag = ON;
                  Limit_Pos_Front = ((Motor_X_Pos + McSTEP_X / 2) & ~(McSTEP_X - 1));
                  Limit_Front_LED_On();
                  Beep();
               }
            }
            else
            {
               limit_Front_flag = OFF;
               Limit_Pos_Front = Limit_Pos_Max;
               Limit_Front_LED_Off();
               Beep();
            }
         }
      }
      Display_On_Limits_Change();
   }
}

// ********** Обработчик нажатия кнопки Limit_Rear **********   
void Limit_Rear_Pressed()
{
   if (!limit_button_flag)
   {
      limit_button_flag = true;
      switch (Mode)
      { 
         case Mode_Thread:
         case Mode_Feed:
         case Mode_aFeed:
         case Mode_Cone_L:
         case Mode_Cone_R:
         case Mode_Sphere:
         if (!Joy_X_flag && Submode_Read == B10100000)
         { 
            if (limit_Rear_flag == OFF)
            {
               if (Motor_X_Pos < (Limit_Pos_Front - ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2))
               {
                  limit_Rear_flag = ON;
                  Limit_Pos_Rear = ((Motor_X_Pos + McSTEP_X / 2) & ~(McSTEP_X - 1));
                  Limit_Rear_LED_On();
                  Beep();
               }
            }
            else
            {
               limit_Rear_flag = OFF;
               Limit_Pos_Rear = Limit_Pos_Min;
               Limit_Rear_LED_Off();
               Beep();
            }
         }
      }
      Display_On_Limits_Change();
   }
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Обработка переключателя оси РГИ ********** /////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Switch_Hand_Axis_Z()
{
   Motor_X_Disable();
   Motor_Z_Enable();
   
   flag_hand_X = OFF;
   hand_X = OFF;
   flag_hand_Z = ON;
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Null_Z_Pos = Motor_Z_Pos;
   
   Disable_INT_OCR3B();
   Enable_INT_OCR3A();
   Ena_INT_Hcoder();
}

void Switch_Hand_Axis_X()
{
   Motor_Z_Disable();
   Motor_X_Enable();
   
   flag_hand_Z = OFF;
   hand_Z = OFF;
   flag_hand_X = ON;

   
   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_X_Pos = 0;
   Null_X_Pos = Motor_X_Pos;
   
   Disable_INT_OCR3A();
   Enable_INT_OCR3B();
   Ena_INT_Hcoder();
}

void Switch_Hand_Axis_No()
{
   Motor_X_Disable();
   Motor_Z_Disable();
   
   flag_hand_Z = OFF;
   hand_Z = OFF;
   flag_hand_X = OFF;
   hand_X = OFF;


   Hand_Count = 0;
   Hand_Count_New = 0;
   Hand_Count_Old = 0;
   Hand_Z_Pos = 0;
   Hand_X_Pos = 0;

   Null_Z_Pos = Motor_Z_Pos;
   Null_X_Pos = Motor_X_Pos;

   Disa_INT_Hcoder();
   Disable_INT_OCR3A();
   Disable_INT_OCR3B();

}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Обработка Масштаба РГИ ********** /////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Switch_Scale_x1()
{
   flag_Scale_x10 = OFF;
   flag_Scale_x1 = ON;
   hand_Z = OFF;
   hand_X = OFF;
}

void Switch_Scale_x10()
{
   flag_Scale_x1 = OFF;
   flag_Scale_x10 = ON;
   hand_Z = OFF;
   hand_X = OFF;
   
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ********** Обработка мигания упоров ********** /////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Blinc_Limit_Buttons()

{ 
      Blinc_Count++;
      if (Blinc_Count > BLINC_TIME) { Blinc_Count = 0; }
      //
      if (limit_Left_flag == ON) {
      if (Motor_Z_Pos == Limit_Pos_Left) {
      Limit_Left_LED_On();
      if (Blinc_Count > BLINC_ON_OFF) { Limit_Left_LED_Off(); }
    } else {
      Limit_Left_LED_On();
    }
  }
  //
      if (limit_Right_flag == ON) {
      if (Motor_Z_Pos == Limit_Pos_Right) {
      Limit_Right_LED_On();
      if (Blinc_Count > BLINC_ON_OFF) { Limit_Right_LED_Off(); }
    } else {
      Limit_Right_LED_On();
    }
  }
  //
      if (limit_Front_flag == ON) {
      if (Motor_X_Pos == Limit_Pos_Front) {
      Limit_Front_LED_On();
      if (Blinc_Count > BLINC_ON_OFF) { Limit_Front_LED_Off(); }
    } else {
      Limit_Front_LED_On();
    }
  }
  //
      if (limit_Rear_flag == ON) {
      if (Motor_X_Pos == Limit_Pos_Rear) {
      Limit_Rear_LED_On();
      if (Blinc_Count > BLINC_ON_OFF) { Limit_Rear_LED_Off(); }
    } else {
      Limit_Rear_LED_On();
    }
  }
}

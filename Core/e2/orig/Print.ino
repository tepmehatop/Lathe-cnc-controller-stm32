//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** Print ***** /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 
void Print()
{   
     if      (err_0_flag == true)
   {
      lcd.setCursor(0, 0);
      lcd.print("      BHИMAHИE      ");
      lcd.setCursor(0, 1);
      lcd.print("Установите джойстик ");
      lcd.setCursor(0, 2);
      lcd.print("   в нейтральное    "); 
      lcd.setCursor(0, 3);
      lcd.print("     положение      ");
      return;   
   } 
  
   if      (err_1_flag == true)
   {
      lcd.setCursor(0, 0);
      lcd.print(" BHИMAHИE:          ");
      lcd.setCursor(0, 1);
      lcd.print("                    ");
      lcd.setCursor(0, 2);
      lcd.print("   УСТАНОВИТЕ УПОРЫ!"); 
      lcd.setCursor(0, 3);
      lcd.print("                    ");
      return;   
   }
   else if (err_2_flag == true)
   {
      lcd.setCursor(0, 0);
      lcd.print(" BHИMAHИE:          ");
      lcd.setCursor(0, 1);     
      lcd.print("                    ");
      lcd.setCursor(0, 2);
      lcd.print("УСТАНОВИТЕ СУППОРТ  ");
      lcd.setCursor(0, 3);
      lcd.print(" В ИСХОДНУЮ ПОЗИЦИЮ!");      
      return;   
   }
   else if (Complete_flag == true)
   {
      lcd.setCursor(0, 0);
      lcd.print("                    ");
      lcd.setCursor(0, 1);     
      lcd.print("OПEPAЦИЯ 3ABEPШEHA! ");
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      lcd.setCursor(0, 3);
      lcd.print("                    ");       
      return;   
   }   
     
     if ((Mode == Mode_Feed    && SelectMenu == 2) || 
         (Mode == Mode_Cone_L  && SelectMenu == 3) ||
         (Mode == Mode_Cone_R  && SelectMenu == 3) || 
         (Mode == Mode_aFeed   && SelectMenu == 3) || 
         (Mode == Mode_Sphere  && SelectMenu == 3) ||
         (Mode == Mode_Thread  && SelectMenu == 3))
   {
         lcd.setCursor(0, 0);
         lcd.print(" Ввод \6  Сброс_осей ");
         lcd.setCursor(0, 1);
         if (MSize_X_mm <= 0)
         lcd.print("Диаметр \1\2  ");
         else 
         lcd.print("Диаметр \1\2 -"); 
         snprintf(LCD_Row_7, 7, "%3ld.%02d", abs(MSize_X_mm/100), abs(MSize_X_mm%100));
         lcd.print(LCD_Row_7);
         lcd.print("мм");  
         lcd.setCursor(0, 2);
         if (Size_X_mm <= 0)
         lcd.print("Ocь X:  \3   ");
         else
         lcd.print("Ocь X:  \3  -");
         snprintf(LCD_Row_1, 7, "%3ld.%02d", abs(Size_X_mm/100), abs(Size_X_mm%100));
         lcd.print(LCD_Row_1);
         lcd.print("мм");         
         lcd.setCursor(0, 3);
         if (Size_Z_mm <= 0)
         lcd.print("Ocь Y:  \4   ");
         else
         lcd.print("Ocь Y:  \4  -");
         snprintf(LCD_Row_2, 7, "%3ld.%02d", abs(Size_Z_mm/100), abs(Size_Z_mm%100));
         lcd.print(LCD_Row_2); 
         lcd.print("мм"); 
     }
     if ((Mode == Mode_Feed      && SelectMenu == 1) || 
         (Mode == Mode_Cone_L    && SelectMenu == 1) ||
         (Mode == Mode_Cone_R    && SelectMenu == 1) || 
         (Mode == Mode_aFeed     && SelectMenu == 1) || 
         (Mode == Mode_Sphere    && SelectMenu == 1) ||
         (Mode == Mode_Thread    && SelectMenu == 1))
       {
                   ///////         Линейка     ////////
         
         lcd.setCursor(0, 1);
         if (MSize_X_mm <= 0)
         snprintf(LCD_Row_7, 20, "\6 %2ld.%02d", abs(MSize_X_mm/100), abs(MSize_X_mm%100));
         else 
         snprintf(LCD_Row_7, 20, "\6-%2ld.%02d", abs(MSize_X_mm/100), abs(MSize_X_mm%100));
         lcd.print(LCD_Row_7);
         lcd.print(" ");

         lcd.setCursor(0, 2);
         if (Size_X_mm <= 0)
         snprintf(LCD_Row_6, 20, "X %2ld.%02d", abs(Size_X_mm/100), abs(Size_X_mm%100));
         else 
         snprintf(LCD_Row_6, 20, "X-%2ld.%02d", abs(Size_X_mm/100), abs(Size_X_mm%100));
         lcd.print(LCD_Row_6);
         lcd.print(" ");
                  
         lcd.setCursor(0, 3);
         if (Size_Z_mm <= 0)
         snprintf(LCD_Row_5, 20, "Y %2ld.%02d", abs(Size_Z_mm/100), abs(Size_Z_mm%100));
         else
         snprintf(LCD_Row_5, 20, "Y-%2ld.%02d", abs(Size_Z_mm/100), abs(Size_Z_mm%100));
         lcd.print(LCD_Row_5);
         lcd.print(" "); 
   }
//================================    РЕЗЬБА   ===========================================================================================================================================

   if (Mode == Mode_Thread)
   { 
    if (SelectMenu == 1)
     {      
         if ((Ph > 1) && (ConL_Thr_flag == false || ConR_Thr_flag == false))
            {
            lcd.setCursor(8, 0);
            lcd.print("\2  Pезьба");
            snprintf(LCD_Row_1, 18, " %2d", Gewinde_flag);
            lcd.print(LCD_Row_1);
             }
      else if (ConL_Thr_flag == true && Ph == 1)
             {
            lcd.setCursor(8, 0);
            snprintf(LCD_Row_1, 8, "<%s ", Cone_Info[Cone_Step].Cone_Print);
            lcd.print(LCD_Row_1);
            lcd.print("Pезьба");
             }
      else if (ConR_Thr_flag == true && Ph == 1)
            {
           lcd.setCursor(8, 0);
            snprintf(LCD_Row_1, 8, ">%s ", Cone_Info[Cone_Step].Cone_Print);
            lcd.print(LCD_Row_1);
            lcd.print("Pезьба");  
            }
            else 
             {
            lcd.setCursor(8, 0);
            lcd.print("\2  Pезьба   ");
             }
           
            lcd.setCursor(8, 2);
            lcd.print(" Циклов:  ");                                             
            lcd.setCursor(8, 1);
            lcd.print(" Шаг: ");        
            snprintf(LCD_Row_2, 7, "%s", Thread_Info[Thread_Step].Thread_Print);  
            lcd.print(LCD_Row_2);
     
      switch(Sub_Mode_Thread)
      {
        case Sub_Mode_Thread_Int:
             lcd.setCursor(0, 0);
             lcd.print(" Bнyтp  "); 
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_4, 3, "%2d", (Thread_Info[Thread_Step].Pass - Pass_Nr+1 + PASS_FINISH + Pass_Fin) + Thr_Pass_Summ );
             lcd.print(LCD_Row_4);
             break;
        case Sub_Mode_Thread_Man:
             lcd.setCursor(0, 0);
             lcd.print(" Pyчная ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_4, 3, "%2d", (Thread_Info[Thread_Step].Pass + PASS_FINISH + Pass_Fin) + Thr_Pass_Summ );
             lcd.print(LCD_Row_4); 
             break;
        case Sub_Mode_Thread_Ext:
             lcd.setCursor(0, 0);
             lcd.print(" Hapyжн ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_4, 3, "%2d", (Thread_Info[Thread_Step].Pass - Pass_Nr+1 + PASS_FINISH + Pass_Fin) + Thr_Pass_Summ );
             lcd.print(LCD_Row_4);
             break;
             }
            if (Ph >1)
            {
             int Thstep_mm = Thread_Info[Thread_Step].Step * 100 * Ph;  
             lcd.setCursor(8, 3); 
             lcd.print(" Ход: "); 
             snprintf(LCD_Row_3, 5, "%1d.%02d", Thstep_mm/100, Thstep_mm%100);
             lcd.print(LCD_Row_3);
             lcd.print("мм");  
            }
            else 
            { 
             lcd.setCursor(8, 3);
             lcd.print(" Об/мин: "); 
             snprintf(LCD_Row_3, 4, "%3d", Thread_Info[Thread_Step].Limit_Print); 
             lcd.print(LCD_Row_3);
            }
         } 
         else if (SelectMenu == 2) 
         {
             int Thstep_mm = Thread_Info[Thread_Step].Step * 100 * Ph;  
             lcd.setCursor(0, 0);                            
             lcd.print("Чист проходов: \1\2 "); 
             snprintf(LCD_Row_3, 3, "%2d", PASS_FINISH + Pass_Fin);
             lcd.print(LCD_Row_3);
             lcd.setCursor(0, 1);
             lcd.print("Колич заходов: \3\4 ");
             snprintf(LCD_Row_4, 3, "%2d", Ph);
             lcd.print(LCD_Row_4);
             lcd.setCursor(0, 2); 
             lcd.print("Ход резьбы:   "); 
             snprintf(LCD_Row_5, 5, "%1d.%02d", Thstep_mm/100, Thstep_mm%100);
             lcd.print(LCD_Row_5);
             lcd.print("мм"); 
             lcd.setCursor(0, 3);
             lcd.print("Шаг резьбы:   ");        
             snprintf(LCD_Row_6, 7, "%s", Thread_Info[Thread_Step].Thread_Print);  
             lcd.print(LCD_Row_6);
    
          }
       }
   
//================================    ПОДАЧА СИНХРОННАЯ   ===========================================================================================================================================
  
   else if (Mode == Mode_Feed)
   {  
          if (SelectMenu == 1)
           {  
       if  (OTSKOK_Z < REBOUND_Z || TENSION_Z > 0){
            lcd.setCursor(8, 0);
            lcd.print("\2  Синхрон *");}
            else{
            lcd.setCursor(8, 0);
            lcd.print("\2  Синхрон  ");          
            }
            lcd.setCursor(8, 1);      
            lcd.print(" Подача:");
            snprintf(LCD_Row_2, 5, "%1d.%02d", Feed_mm/100, Feed_mm%100);
            lcd.print(LCD_Row_2);
            lcd.setCursor(8, 2);
            lcd.print(" Циклов:  ");
            lcd.setCursor(8, 3); 
            lcd.print(" Съём \6:");
            snprintf(LCD_Row_4, 5, "%1d.%02d", Ap/100, Ap%100);
            lcd.print(LCD_Row_4); 
                  
      switch(Sub_Mode_Feed)
      {
        case Sub_Mode_Feed_Int:
             lcd.setCursor(0, 0);      
             lcd.print(" Внутре ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_3, 3, "%2d", Pass_Total-Pass_Nr+1);
             lcd.print(LCD_Row_3);     
             break;
        case Sub_Mode_Feed_Man:             
             lcd.setCursor(0, 0);      
             lcd.print(" Ручной ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_3, 3, "%2d", Pass_Total);
             lcd.print(LCD_Row_3);            
             break;
        case Sub_Mode_Feed_Ext:
             lcd.setCursor(0, 0);      
             lcd.print(" Наружн ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_3, 3, "%2d", Pass_Total-Pass_Nr+1);
             lcd.print(LCD_Row_3);           
             break;
            }                    
         }
              else if (SelectMenu == 3){
              lcd.setCursor(0, 0);
              lcd.print("Отскок Y: \1\2");
              snprintf(LCD_Row_5, 16, " %2ld.%02dmm", abs(OTSKOK_Z_mm/100), abs(OTSKOK_Z_mm%100));
              lcd.print(LCD_Row_5);  
              lcd.setCursor(0, 1);
              lcd.print("                    ");
              lcd.setCursor(0, 2);
              lcd.print("Ослабление \3        ");
              lcd.setCursor(0, 3);
              lcd.print("натяга Y:  \4");
              snprintf(LCD_Row_6, 16, " %2ld.%02dmm", abs(TENSION_Z_mm/100), abs(TENSION_Z_mm%100));
              lcd.print(LCD_Row_6);
     }
   }
   
//================================    ПОДАЧА АСИНХРОННАЯ    ===========================================================================================================================================

   else if (Mode == Mode_aFeed)
   {   
          if (SelectMenu == 1) 
           {
            lcd.setCursor(8, 0);
            lcd.print("\2  Асинхрон ");
            lcd.setCursor(8, 1);      
            lcd.print(" Подача: ");
            snprintf(LCD_Row_1, 4, "%3d", aFeed_mm);
            lcd.print(LCD_Row_1);
            lcd.setCursor(8, 2);
            lcd.print(" Циклов:  ");
            lcd.setCursor(8, 3); 
            lcd.print(" Съём R:");
            snprintf(LCD_Row_4, 5, "%1d.%02d", Ap/100, Ap%100);
            lcd.print(LCD_Row_4);

      switch(Sub_Mode_aFeed)
      {
        case Sub_Mode_aFeed_Int:
             lcd.setCursor(0, 0);      
             lcd.print(" Внутре ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_3, 3, "%2d", Pass_Total-Pass_Nr+1);
             lcd.print(LCD_Row_3);            
             break;
        case Sub_Mode_aFeed_Man:
             lcd.setCursor(0, 0);      
             lcd.print(" Ручной ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_3, 3, "%2d", Pass_Total);
             lcd.print(LCD_Row_3);           
             break;
        case Sub_Mode_aFeed_Ext:
             lcd.setCursor(0, 0);      
             lcd.print(" Наружн ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_3, 3, "%2d", Pass_Total-Pass_Nr+1);
             lcd.print(LCD_Row_3);               
             break;
            }                               
    }
        else if (SelectMenu == 2)
            {
             lcd.setCursor(0, 0);
             lcd.print("Текущий  угол:");
             snprintf(LCD_Row_2, 7, "%3ld.%01ld", Spindle_Angle/1000, Spindle_Angle%1000/100);
             lcd.print(LCD_Row_2);
             lcd.print("\5");                           
             lcd.setCursor(0, 1);
             lcd.print("Делим круг на:");
             snprintf(LCD_Row_1, 5,"%3d " , Total_Tooth);
             lcd.print(LCD_Row_1);
             lcd.print("\3\4");                           
             lcd.setCursor(0, 2);
             lcd.print("Выбор отметки:");
             snprintf(LCD_Row_3, 5, "%3d ", Current_Tooth);
             lcd.print(LCD_Row_3);
             lcd.print("\1\2");
             lcd.setCursor(0, 3);
             lcd.print("Угол  сектора:");
             snprintf(LCD_Row_4, 7, "%3ld.%01ld", Required_Angle/1000, Required_Angle%1000/100);
             lcd.print(LCD_Row_4);
             lcd.print("\5");
            }   
   }
   
//================================    КОНУС ЛЕВЫЙ <    ===========================================================================================================================================

   else if (Mode == Mode_Cone_L)
   {
          if (SelectMenu == 1)
           {
            lcd.setCursor(8, 0);
            snprintf(LCD_Row_1, 8, "<%s ", Cone_Info[Cone_Step].Cone_Print);
            lcd.print(LCD_Row_1);
            lcd.setCursor(13, 0);
            lcd.print(" Kонус ");                                   
            lcd.setCursor(8, 1);      
            lcd.print(" Подача:");
            snprintf(LCD_Row_2, 5, "%1d.%02d", Feed_mm/100, Feed_mm%100);
            lcd.print(LCD_Row_2);
            lcd.setCursor(8, 2);
            lcd.print(" Циклов:  ");
            lcd.setCursor(8, 3); 
            lcd.print(" Съём \6:");
            snprintf(LCD_Row_4, 5, "%1d.%02d", Ap/100, Ap%100);
            lcd.print(LCD_Row_4); 
      
      switch(Sub_Mode_Cone)
      {
        case Sub_Mode_Cone_Int:
             lcd.setCursor(0, 0);      
             lcd.print(" Внутре ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_3, 3, "%2d", Pass_Total-Pass_Nr+1);
             lcd.print(LCD_Row_3);         
             break;
        case Sub_Mode_Cone_Man:
             lcd.setCursor(0, 0);      
             lcd.print(" Ручной ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_3, 3, "%2d", Pass_Total);
             lcd.print(LCD_Row_3);           
             break;
        case Sub_Mode_Cone_Ext:
             lcd.setCursor(0, 0);      
             lcd.print(" Наружн ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_3, 3, "%2d", Pass_Total-Pass_Nr+1);
             lcd.print(LCD_Row_3);          
             break;
            }                    
           }
           else if (SelectMenu == 2)
           {
            lcd.setCursor(0, 0);
            lcd.print("Конус  < ");
            snprintf(LCD_Row_1, 5, "%s", Cone_Info[Cone_Step].Cone_Print);
            lcd.print(LCD_Row_1);
            lcd.setCursor(13, 0);
            lcd.print("    \1 \2");
            lcd.setCursor(0, 2);
            lcd.print("Коническая        \3 ");
            if (ConL_Thr_flag == true){
            lcd.setCursor(0, 1);
            lcd.print("  В режиме резьба   ");  
            lcd.setCursor(0, 3);
            lcd.print("резьба:    Вкл    \4 ");}
            else{
            lcd.setCursor(0, 1);
            lcd.print("                    ");    
            lcd.setCursor(0, 3);
            lcd.print("резьба:    Выкл   \4 ");}
           } 
      }
   
//================================    КОНУС ПРАВЫЙ >   ===========================================================================================================================================

   else if (Mode == Mode_Cone_R)
   {
          if (SelectMenu == 1)
           {
            lcd.setCursor(8, 0);
            snprintf(LCD_Row_1, 8, ">%s ", Cone_Info[Cone_Step].Cone_Print);
            lcd.print(LCD_Row_1);
            lcd.setCursor(13, 0);
            lcd.print(" Kонус ");                                   
            lcd.setCursor(8, 1);      
            lcd.print(" Подача:");
            snprintf(LCD_Row_2, 5, "%1d.%02d", Feed_mm/100, Feed_mm%100);
            lcd.print(LCD_Row_2);
            lcd.setCursor(8, 2);
            lcd.print(" Циклов:  ");
            lcd.setCursor(8, 3); 
            lcd.print(" Съём \6:");
            snprintf(LCD_Row_4, 5, "%1d.%02d", Ap/100, Ap%100);
            lcd.print(LCD_Row_4);  
            
      switch(Sub_Mode_Cone)
      {
        case Sub_Mode_Cone_Int:
             lcd.setCursor(0, 0);      
             lcd.print(" Внутре ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_3, 3, "%2d", Pass_Total-Pass_Nr+1);
             lcd.print(LCD_Row_3);        
             break;
        case Sub_Mode_Cone_Man:
             lcd.setCursor(0, 0);      
             lcd.print(" Ручной ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_3, 3, "%2d", Pass_Total);
             lcd.print(LCD_Row_3);            
             break;
        case Sub_Mode_Cone_Ext:
             lcd.setCursor(0, 0);      
             lcd.print(" Наружн ");
             lcd.setCursor(18, 2);
             snprintf(LCD_Row_3, 3, "%2d", Pass_Total-Pass_Nr+1);
             lcd.print(LCD_Row_3);        
             break;
            }               
           }
           else if (SelectMenu == 2)
           {
            lcd.setCursor(0, 0);
            lcd.print("Kонус  > ");
            snprintf(LCD_Row_1, 5, "%s", Cone_Info[Cone_Step].Cone_Print);
            lcd.print(LCD_Row_1);
            lcd.setCursor(13, 0);
            lcd.print("    \1 \2");
            lcd.setCursor(0, 2);
            lcd.print("Коническая        \3 ");
            if (ConR_Thr_flag == true){
            lcd.setCursor(0, 1);
            lcd.print("  В режиме резьба   "); 
            lcd.setCursor(0, 3);
            lcd.print("резьба:    Вкл    \4 ");}
            else{
            lcd.setCursor(0, 1);
            lcd.print("                    ");    
            lcd.setCursor(0, 3);
            lcd.print("резьба:    Выкл   \4 ");}
           }  
         }
   
//================================    РЕЖИМ ШАРА (Шароточка)   ===========================================================================================================================================

   else if (Mode == Mode_Sphere)
   { 
         
     if (SelectMenu == 1 && Sub_Mode_Sphere != Sub_Mode_Sphere_Int)
      {  
         lcd.setCursor(0, 0);
         lcd.print("ШAP \6");
         snprintf(LCD_Row_1, 5, "%2ld.%01ld", Sph_R_mm * 2 / 100, Sph_R_mm * 2 / 10 %10);
         lcd.print(LCD_Row_1);
         lcd.print("мм");
         lcd.setCursor(8, 1);      
         lcd.print(" Подача:");
         snprintf(LCD_Row_2, 5, "%1d.%02d", Feed_mm/100, Feed_mm%100);
         lcd.print(LCD_Row_2);
         
          if (Sub_Mode_Sphere == Sub_Mode_Sphere_Man)
          {  
             lcd.setCursor(11, 0);
             lcd.print(" Oтключен");
          }
          else if (Sub_Mode_Sphere == Sub_Mode_Sphere_Ext)
          {  
             lcd.setCursor(11, 0);
             lcd.print("  Включен");
          }
          lcd.setCursor(8, 2);
          lcd.print(" Ножка:\6"); 
          snprintf(LCD_Row_3, 5, "%ld.%02ld", Bar_R_mm*2/100, Bar_R_mm*2%100);
          lcd.print(LCD_Row_3);
          lcd.setCursor(8, 3);
          lcd.print(" Заходов:");
          snprintf(LCD_Row_4, 4, "%3ld", Pass_Total_Sphr+2-Pass_Nr); 
          lcd.print(LCD_Row_4); 
          
   
      }
      else if (SelectMenu == 2)
      {  
         lcd.setCursor(0, 0);
         lcd.print("Шиpина peзцa: ");
         snprintf(LCD_Row_1, 5, "%1d.%02d", Cutter_Width/100, Cutter_Width%100);
         lcd.print(LCD_Row_1);
         lcd.print("мм");
         lcd.setCursor(0, 1);
         lcd.print("Шaг по ocи Y: ");
         snprintf(LCD_Row_2, 5, "%1d.%02d", Cutting_Width/100, Cutting_Width%100);
         lcd.print(LCD_Row_2);
         lcd.print("мм"); 
         lcd.setCursor(0, 2);
         lcd.print("                    ");
         lcd.setCursor(0, 3);
         lcd.print("                    ");
      } 
       if (Sub_Mode_Sphere == Sub_Mode_Sphere_Int)
         {
             lcd.setCursor(0, 0);
             lcd.print("                    ");
             lcd.setCursor(0, 1);     
             lcd.print(" Режим невозможен!  ");
             lcd.setCursor(0, 2);
             lcd.print("                    ");
             lcd.setCursor(0, 3);
             lcd.print("                    "); 
         }            
   }
//================================    Делилка  ===========================================================================================================================================
   else if (Mode == Mode_Divider)
{
                       

                           lcd.setCursor(0, 0);
                           lcd.print("    Текущий угол    ");
                           lcd.setCursor(0, 1);
                           lcd.print("       ");
                           snprintf(LCD_Row_2, 9, "%3ld.%01ld", Spindle_Angle/1000, Spindle_Angle%1000/100);
                           lcd.print(LCD_Row_2);
                           lcd.print("\5       "); 
                           lcd.setCursor(0, 2);   
                           lcd.print("                    "); 
                           lcd.setCursor(0, 3);
                           lcd.print("Сброс кнопкой Селект");                        
                  
                         
                          } 


//================================    Зарезервированный режим  ===========================================================================================================================================
   
   else if (Mode == Mode_Reserve)  //////////////////////////////////////////////////////////////
   {
    lcd.setCursor(0, 0);
    lcd.print("                    ");
    lcd.setCursor(0, 1);
    lcd.print("                    ");
    lcd.setCursor(0, 2);
    lcd.print("                    ");
    lcd.setCursor(0, 3);
    lcd.print("              PE3EPB");
   }
      
 
}

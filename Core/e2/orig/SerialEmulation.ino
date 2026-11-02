//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** ЭМУЛЯЦИЯ КНОПОК ЧЕРЕЗ SERIAL MONITOR ***** //////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Этот файл добавляет возможность управления через Serial Monitor для тестирования на столе
// Команды отправляются в Serial Monitor с переводом строки (Newline)

// Флаг для включения/отключения эмуляции
#define SERIAL_EMULATION_ENABLED true

// Буфер для команд
String serialCommand = "";

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** ИНИЦИАЛИЗАЦИЯ SERIAL ЭМУЛЯЦИИ ***** /////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Serial_Emulation_Init()
{
  Serial.begin(115200);
  Serial.println(F("==========================================="));
  Serial.println(F("ELS v1.3 - Serial Emulation Mode"));
  Serial.println(F("==========================================="));
  Serial.println(F("Доступные команды:"));
  Serial.println(F(""));
  Serial.println(F("=== ДЖОЙСТИК ==="));
  Serial.println(F("JL  - Джойстик Влево"));
  Serial.println(F("JR  - Джойстик Вправо"));
  Serial.println(F("JU  - Джойстик Вверх"));
  Serial.println(F("JD  - Джойстик Вниз"));
  Serial.println(F("JN  - Джойстик в Нейтраль"));
  Serial.println(F(""));
  Serial.println(F("=== КНОПКИ МЕНЮ ==="));
  Serial.println(F("BU  - Button Up"));
  Serial.println(F("BD  - Button Down"));
  Serial.println(F("BL  - Button Left"));
  Serial.println(F("BR  - Button Right"));
  Serial.println(F("BS  - Button Select"));
  Serial.println(F(""));
  Serial.println(F("=== КНОПКА RAPID ==="));
  Serial.println(F("RAP - Rapid Button (удержание)"));
  Serial.println(F("RAN - Rapid Button отпущена"));
  Serial.println(F(""));
  Serial.println(F("=== ПЕРЕКЛЮЧАТЕЛЬ РЕЖИМА ==="));
  Serial.println(F("M1  - Mode Feed (Синхронная)"));
  Serial.println(F("M2  - Mode aFeed (Асинхронная)"));
  Serial.println(F("M3  - Mode Thread (Резьба)"));
  Serial.println(F("M4  - Mode Cone_L (Конус <)"));
  Serial.println(F("M5  - Mode Cone_R (Конус >)"));
  Serial.println(F("M6  - Mode Sphere (Шар)"));
  Serial.println(F("M7  - Mode Divider (Делитель)"));
  Serial.println(F("M8  - Mode Reserve"));
  Serial.println(F(""));
  Serial.println(F("=== ПЕРЕКЛЮЧАТЕЛЬ ПОДРЕЖИМА ==="));
  Serial.println(F("S1  - SubMode Internal (Внутренний)"));
  Serial.println(F("S2  - SubMode Manual (Ручной)"));
  Serial.println(F("S3  - SubMode External (Наружный)"));
  Serial.println(F(""));
  Serial.println(F("=== КНОПКИ ЛИМИТОВ ==="));
  Serial.println(F("LL  - Limit Left"));
  Serial.println(F("LR  - Limit Right"));
  Serial.println(F("LF  - Limit Front"));
  Serial.println(F("LB  - Limit Rear (Back)"));
  Serial.println(F(""));
  Serial.println(F("=== ПЕРЕКЛЮЧАТЕЛЬ ОСИ РГИ ==="));
  Serial.println(F("HZ  - Hand Coder Axis Z"));
  Serial.println(F("HX  - Hand Coder Axis X"));
  Serial.println(F("HN  - Hand Coder Axis None"));
  Serial.println(F(""));
  Serial.println(F("=== ПЕРЕКЛЮЧАТЕЛЬ МАСШТАБА РГИ ==="));
  Serial.println(F("SC1 - Scale x1"));
  Serial.println(F("SC10- Scale x10"));
  Serial.println(F(""));
  Serial.println(F("=== ОТЛАДКА ==="));
  Serial.println(F("INFO  - Показать текущее состояние"));
  Serial.println(F("HELP  - Показать эту справку"));
  Serial.println(F(""));
  Serial.println(F("=== НОВЫЙ ДИСПЛЕЙ (ESP32-S3) ==="));
  Serial.println(F("SEND  - Отправить всё состояние на новый дисплей"));
  Serial.println(F("DINIT - Инициализировать UART к дисплею"));
  Serial.println(F("DPING - PING к дисплею"));
  Serial.println(F("AL1   - Алерт: Установите упоры"));
  Serial.println(F("AL2   - Алерт: Установите суппорт"));
  Serial.println(F("AL3   - Алерт: Операция завершена"));
  Serial.println(F("AL4   - Алерт: Нейтраль джойстика"));
  Serial.println(F(""));
  Serial.println(F("=== ТЕСТОВЫЕ ПРЕСЕТЫ (Arduino → Дисплей) ==="));
  Serial.println(F("PT:N  - Установить пресет N (1..35) и отправить"));
  Serial.println(F("        Смотри TEST_SCENARIOS.md для таблицы"));
  Serial.println(F("==========================================="));
  Serial.println(F("Готов к работе!"));
  Serial.println(F(""));
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** ОБРАБОТКА КОМАНД ИЗ SERIAL ***** ////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Serial_Emulation_Process()
{
  if (!SERIAL_EMULATION_ENABLED) return;

  // Читаем команды из Serial
  while (Serial.available() > 0)
  {
    char inChar = (char)Serial.read();

    if (inChar == '\n' || inChar == '\r')
    {
      if (serialCommand.length() > 0)
      {
        serialCommand.toUpperCase();
        serialCommand.trim();

        Serial.print(F(">> Команда: "));
        Serial.println(serialCommand);

        // Обработка команд
        Process_Serial_Command(serialCommand);

        serialCommand = "";
      }
    }
    else
    {
      serialCommand += inChar;
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** ОБРАБОТЧИК КОМАНД ***** /////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Process_Serial_Command(String cmd)
{
  // === ДЖОЙСТИК ===
  if (cmd == "JL")
  {
    Serial.println(F("Эмуляция: Джойстик Влево"));
    Joy_LeftPressed();
  }
  else if (cmd == "JR")
  {
    Serial.println(F("Эмуляция: Джойстик Вправо"));
    Joy_RightPressed();
  }
  else if (cmd == "JU")
  {
    Serial.println(F("Эмуляция: Джойстик Вверх"));
    Joy_UpPressed();
  }
  else if (cmd == "JD")
  {
    Serial.println(F("Эмуляция: Джойстик Вниз"));
    Joy_DownPressed();
  }
  else if (cmd == "JN")
  {
    Serial.println(F("Эмуляция: Джойстик в Нейтраль"));
    Joy_NoPressed();
  }

  // === КНОПКИ МЕНЮ ===
  else if (cmd == "BU")
  {
    Serial.println(F("Эмуляция: Button Up"));
    button_up_flag = false;
    Key_Up_Pressed();
  }
  else if (cmd == "BD")
  {
    Serial.println(F("Эмуляция: Button Down"));
    button_down_flag = false;
    Key_Down_Pressed();
  }
  else if (cmd == "BL")
  {
    Serial.println(F("Эмуляция: Button Left"));
    button_left_flag = false;
    Key_Left_Pressed();
  }
  else if (cmd == "BR")
  {
    Serial.println(F("Эмуляция: Button Right"));
    button_right_flag = false;
    Key_Right_Pressed();
  }
  else if (cmd == "BS")
  {
    Serial.println(F("Эмуляция: Button Select"));
    key_sel_flag = false;
    Key_Select_Pressed();
  }

  // === RAPID BUTTON ===
  else if (cmd == "RAP")
  {
    Serial.println(F("Эмуляция: Rapid Button нажата"));
    // Эмулируем нажатие rapid button (активный LOW)
    // В к��де проверяется как !Button_Rapid
  }
  else if (cmd == "RAN")
  {
    Serial.println(F("Эмуляция: Rapid Button отпущена"));
  }

  // === ПЕРЕКЛЮЧАТЕЛЬ РЕЖИМА ===
  else if (cmd == "M1")
  {
    Serial.println(F("Эмуляция: Mode Feed"));
    Switch_Feed();
    Beep();
  }
  else if (cmd == "M2")
  {
    Serial.println(F("Эмуляция: Mode aFeed"));
    Switch_aFeed();
    Beep();
  }
  else if (cmd == "M3")
  {
    Serial.println(F("Эмуляция: Mode Thread"));
    Switch_Thread();
    Beep();
  }
  else if (cmd == "M4")
  {
    Serial.println(F("Эмуляция: Mode Cone Left"));
    Switch_Cone_L();
    Beep();
  }
  else if (cmd == "M5")
  {
    Serial.println(F("Эмуляция: Mode Cone Right"));
    Switch_Cone_R();
    Beep();
  }
  else if (cmd == "M6")
  {
    Serial.println(F("Эмуляция: Mode Sphere"));
    Switch_Sphere();
    Beep();
  }
  else if (cmd == "M7")
  {
    Serial.println(F("Эмуляция: Mode Divider"));
    Switch_Devider();
    Beep();
  }
  else if (cmd == "M8")
  {
    Serial.println(F("Эмуляция: Mode Reserve"));
    Switch_Reserve();
    Beep();
  }

  // === ПЕРЕКЛЮЧАТЕЛЬ ПОДРЕЖИМА ===
  else if (cmd == "S1")
  {
    Serial.println(F("Эмуляция: SubMode Internal"));
    Switch_Int();
    Beep();
  }
  else if (cmd == "S2")
  {
    Serial.println(F("Эмуляция: SubMode Manual"));
    Switch_Man();
    Beep();
  }
  else if (cmd == "S3")
  {
    Serial.println(F("Эмуляция: SubMode External"));
    Switch_Ext();
    Beep();
  }

  // === КНОПКИ ЛИМИТОВ (прямое переключение без проверки Submode_Read) ===
  else if (cmd == "LL")
  {
    if (limit_Left_flag == OFF) {
      limit_Left_flag = ON;
      Limit_Pos_Left = ((Motor_Z_Pos + McSTEP_Z / 2) & ~(McSTEP_Z - 1));
      Limit_Left_LED_On();
    } else {
      limit_Left_flag = OFF;
      Limit_Pos_Left = Limit_Pos_Max;
      Limit_Left_LED_Off();
    }
    Display_On_Limits_Change();
    Beep();
    Serial.print(F("Лимит Лево: ")); Serial.println(limit_Left_flag ? F("ON") : F("OFF"));
  }
  else if (cmd == "LR")
  {
    if (limit_Right_flag == OFF) {
      limit_Right_flag = ON;
      Limit_Pos_Right = ((Motor_Z_Pos + McSTEP_Z / 2) & ~(McSTEP_Z - 1));
      Limit_Right_LED_On();
    } else {
      limit_Right_flag = OFF;
      Limit_Pos_Right = Limit_Pos_Min;
      Limit_Right_LED_Off();
    }
    Display_On_Limits_Change();
    Beep();
    Serial.print(F("Лимит Право: ")); Serial.println(limit_Right_flag ? F("ON") : F("OFF"));
  }
  else if (cmd == "LF")
  {
    if (limit_Front_flag == OFF) {
      limit_Front_flag = ON;
      Limit_Pos_Front = ((Motor_X_Pos + McSTEP_X / 2) & ~(McSTEP_X - 1));
      Limit_Front_LED_On();
    } else {
      limit_Front_flag = OFF;
      Limit_Pos_Front = Limit_Pos_Max;
      Limit_Front_LED_Off();
    }
    Display_On_Limits_Change();
    Beep();
    Serial.print(F("Лимит Перед: ")); Serial.println(limit_Front_flag ? F("ON") : F("OFF"));
  }
  else if (cmd == "LB")
  {
    if (limit_Rear_flag == OFF) {
      limit_Rear_flag = ON;
      Limit_Pos_Rear = ((Motor_X_Pos + McSTEP_X / 2) & ~(McSTEP_X - 1));
      Limit_Rear_LED_On();
    } else {
      limit_Rear_flag = OFF;
      Limit_Pos_Rear = Limit_Pos_Min;
      Limit_Rear_LED_Off();
    }
    Display_On_Limits_Change();
    Beep();
    Serial.print(F("Лимит Зад: ")); Serial.println(limit_Rear_flag ? F("ON") : F("OFF"));
  }

  // === ПЕРЕКЛЮЧАТЕЛЬ ОСИ РГИ ===
  else if (cmd == "HZ")
  {
    Serial.println(F("Эмуляция: Hand Coder Axis Z"));
    Switch_Hand_Axis_Z();
  }
  else if (cmd == "HX")
  {
    Serial.println(F("Эмуляция: Hand Coder Axis X"));
    Switch_Hand_Axis_X();
  }
  else if (cmd == "HN")
  {
    Serial.println(F("Эмуляция: Hand Coder Axis None"));
    Switch_Hand_Axis_No();
  }

  // === ПЕРЕКЛЮЧАТЕЛЬ МАСШТАБА ===
  else if (cmd == "SC1")
  {
    Serial.println(F("Эмуляция: Scale x1"));
    Switch_Scale_x1();
  }
  else if (cmd == "SC10")
  {
    Serial.println(F("Эмуляция: Scale x10"));
    Switch_Scale_x10();
  }

  // === ОТЛАДКА ===
  else if (cmd == "INFO")
  {
    Print_Debug_Info();
  }
  else if (cmd == "HELP")
  {
    Serial_Emulation_Init(); // Показываем справку снова
  }

  // === UART К НОВОМУ ДИСПЛЕЮ ===
  else if (cmd == "SEND")
  {
    // Отправить полное текущее состояние на новый дисплей (ESP32-S3)
    Serial.println(F("Эмуляция: Отправка состояния на новый дисплей..."));
    Display_Send_All();
    Serial.println(F("Готово."));
  }
  else if (cmd == "DINIT")
  {
    // Переинициализировать UART к дисплею + отправить PING
    Serial.println(F("Эмуляция: Инициализация UART дисплея..."));
    Display_UART_Init();
  }
  else if (cmd == "DPING")
  {
    // Послать PING дисплею
    Serial.println(F("Эмуляция: PING → дисплей"));
    Display_Send_CMD("PING", nullptr);
  }
  // === ТЕСТОВЫЕ ПРЕСЕТЫ (Arduino → Новый Дисплей) ===
  // PT:1..PT:35 — установить пресет и отправить на новый дисплей
  else if (cmd.startsWith("PT:"))
  {
    int preset_n = cmd.substring(3).toInt();
    Set_Test_Preset(preset_n);
  }
  // === ТЕСТОВЫЕ АЛЕРТЫ ===
  else if (cmd == "AL1") { Display_Send_Alert(1); Serial.println(F("Алерт: УСТАНОВИТЕ УПОРЫ")); }
  else if (cmd == "AL2") { Display_Send_Alert(2); Serial.println(F("Алерт: УСТАНОВИТЕ СУППОРТ")); }
  else if (cmd == "AL3") { Display_Send_Alert(3); Serial.println(F("Алерт: ОПЕРАЦИЯ ЗАВЕРШЕНА")); }
  else if (cmd == "AL4") { Display_Send_Alert(4); Serial.println(F("Алерт: НЕЙТРАЛЬ ДЖОЙСТИКА")); }

  else
  {
    Serial.println(F("Неизвестная команда! Наберите HELP для справки."));
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** ВЫВОД ОТЛАДОЧНОЙ ИНФОРМАЦИИ ***** ///////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Print_Debug_Info()
{
  Serial.println(F(""));
  Serial.println(F("=== ТЕКУЩЕЕ СОСТОЯНИЕ ==="));

  // Режим
  Serial.print(F("Режим: "));
  switch(Mode)
  {
    case Mode_Feed:    Serial.println(F("Feed (Синхронная подача)")); break;
    case Mode_aFeed:   Serial.println(F("aFeed (Асинхронная подача)")); break;
    case Mode_Thread:  Serial.println(F("Thread (Резьба)")); break;
    case Mode_Cone_L:  Serial.println(F("Cone_L (Конус <)")); break;
    case Mode_Cone_R:  Serial.println(F("Cone_R (Конус >)")); break;
    case Mode_Sphere:  Serial.println(F("Sphere (Шар)")); break;
    case Mode_Divider: Serial.println(F("Divider (Делитель)")); break;
    case Mode_Reserve: Serial.println(F("Reserve")); break;
    default: Serial.println(F("Неизвестный")); break;
  }

  // Подрежим
  Serial.print(F("Подрежим: "));
  if (Mode == Mode_Thread)
  {
    switch(Sub_Mode_Thread)
    {
      case Sub_Mode_Thread_Int: Serial.println(F("Внутренняя")); break;
      case Sub_Mode_Thread_Man: Serial.println(F("Ручная")); break;
      case Sub_Mode_Thread_Ext: Serial.println(F("Наружная")); break;
    }
  }
  else if (Mode == Mode_Feed)
  {
    switch(Sub_Mode_Feed)
    {
      case Sub_Mode_Feed_Int: Serial.println(F("Внутренняя")); break;
      case Sub_Mode_Feed_Man: Serial.println(F("Ручная")); break;
      case Sub_Mode_Feed_Ext: Serial.println(F("Наружная")); break;
    }
  }
  else
  {
    Serial.println(F("N/A"));
  }

  // Позиции
  Serial.print(F("Позиция Z: "));
  Serial.print(Size_Z_mm / 100.0, 2);
  Serial.println(F(" мм"));

  Serial.print(F("Позиция X: "));
  Serial.print(Size_X_mm / 100.0, 2);
  Serial.println(F(" мм"));

  Serial.print(F("Диаметр: "));
  Serial.print(MSize_X_mm / 100.0, 2);
  Serial.println(F(" мм"));

  // Меню
  Serial.print(F("Выбранное меню: "));
  Serial.println(SelectMenu);

  // Шпиндель
  Serial.print(F("Угол шпинделя: "));
  Serial.print(Spindle_Angle / 1000.0, 1);
  Serial.println(F(" градусов"));

  // Резьба (если в режиме резьбы)
  if (Mode == Mode_Thread)
  {
    Serial.print(F("Шаг резьбы: "));
    Serial.println(Thread_Info[Thread_Step].Thread_Print);
    Serial.print(F("Количество заходов: "));
    Serial.println(Ph);
  }

  // Подача (если в режиме подачи)
  if (Mode == Mode_Feed)
  {
    Serial.print(F("Подача: "));
    Serial.print(Feed_mm / 100.0, 2);
    Serial.println(F(" мм/об"));
    Serial.print(F("Проход: "));
    Serial.print(Pass_Nr);
    Serial.print(F(" из "));
    Serial.println(Pass_Total);
  }

  Serial.println(F("========================="));
  Serial.println(F(""));
}

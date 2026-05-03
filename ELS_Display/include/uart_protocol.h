/**
 * @file uart_protocol.h
 * @brief UART протокол для связи между Arduino Mega и ESP32
 *
 * Формат команд: <CMD:PARAM1,PARAM2,...>\n
 *
 * Arduino → ESP32: Отправка данных для отображения
 * ESP32 → Arduino: Нажатия на экранные кнопки
 */

#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <Arduino.h>

// ============================================================================
// ТИПЫ ДАННЫХ
// ============================================================================

// Режимы работы станка
typedef enum {
    MODE_FEED = 1,      // M1 - Синхронная подача
    MODE_AFEED,         // M2 - Асинхронная подача
    MODE_THREAD,        // M3 - Резьба
    MODE_CONE_L,        // M4 - Конус влево
    MODE_CONE_R,        // M5 - Конус вправо
    MODE_SPHERE,        // M6 - Шар
    MODE_DIVIDER,       // M7 - Делитель
    MODE_RESERVE        // M8 - Резерв
} LatheMode;

// Подрежимы
typedef enum {
    SUBMODE_INTERNAL = 1,  // S1 - Внутренний
    SUBMODE_MANUAL,        // S2 - Ручной
    SUBMODE_EXTERNAL       // S3 - Наружный
} LatheSubMode;

// Статус лимитов (битовая маска)
typedef struct {
    uint8_t left  : 1;
    uint8_t right : 1;
    uint8_t front : 1;
    uint8_t rear  : 1;
} LimitStatus;

// Все данные станка
typedef struct {
    LatheMode mode;          // Текущий режим
    LatheSubMode submode;    // Текущий подрежим

    int16_t feed_mm;         // Подача (мм/об * 100, т.е. 25 = 0.25 мм/об)
    int16_t afeed_mm;        // Асинхронная подача (мм/об * 100)
    int16_t thread_mm;       // Резьба (мм * 100, т.е. 150 = 1.50 мм)
    int16_t cone_angle;      // Угол конуса (градусы * 10)
    int16_t sphere_radius;   // Радиус шара (мм * 100)

    int32_t pos_z;           // Позиция Z (мкм, т.е. 123450 = 123.45 мм)
    int32_t pos_x;           // Позиция X (мкм)

    uint16_t rpm;            // Обороты шпинделя (RPM)

    uint16_t pass_nr;        // Текущий проход (1-based, 0 = не начат)
    uint16_t pass_total;     // Всего проходов
    int16_t  ap;             // Съём за проход (мм * 100)
    uint8_t  ph;             // Количество заходов / starts (резьба, 1..8)
    int16_t  spindle_angle;  // Угол шпинделя (градусы * 10, для делилки)
    uint8_t  cone_idx;       // Индекс конуса (для режима конус)

    uint16_t total_tooth;    // Делилка: на сколько частей делим круг
    uint16_t current_tooth;  // Делилка: текущая выбранная метка (1-based)
    int16_t  bar_r;          // Шар: радиус ножки/остатка (мм * 100)
    uint16_t pass_total_sphr;// Шар: всего заходов (Pass_Total_Sphr, отдельная переменная)

    uint8_t  select_menu;    // Arduino SelectMenu (1=main, 2=params, 3=extra)

    int32_t  otskok_z;       // M1 SM=3: отскок Z (мм * 100 * 10, как pos_z)
    int32_t  tension_z;      // M1 SM=3: ослабление натяга Z
    int32_t  diam_x;         // M1 SM=2: диаметр заготовки (MSize_X_mm * 10)
    uint8_t  pass_fin;       // M3 SM=2: чистовых проходов (PASS_FINISH + Pass_Fin)
    bool     cone_thr;       // M4/M5 SM=2: коническая резьба включена
    int16_t  cutter_w;      // M6 SM=2: ширина резца (мм × 100)
    int16_t  cutting_w;     // M6 SM=2: шаг по оси Z (мм × 100)

    char     thread_name[8]; // Резьба: строка из Thread_Print (напр. "1.50mm", " 8tpi ", "G  1/8")
    int16_t  rpm_limit;      // Резьба: макс. обороты для текущего шага (Об/мин)
    int16_t  thread_cycles;  // Резьба: Циклов (как на старом LCD, не pass_total)
    int16_t  thread_travel;  // Резьба: Ход = Шаг × Заходы (мм × 100)

    LimitStatus limits;      // Статус концевиков

    bool motor_z_enabled;    // Двигатель Z включен
    bool motor_x_enabled;    // Двигатель X включен
    bool is_running;         // Станок в работе (STATE:run)
} LatheData;

// ============================================================================
// КОМАНДЫ ARDUINO → ESP32
// ============================================================================

/*
 * Примеры команд:
 *
 * <MODE:3>              - Режим M3 (Thread)
 * <SUBMODE:2>           - Подрежим S2 (Manual)
 * <FEED:25>             - Подача 0.25 мм/об
 * <AFEED:50>            - Асинхронная подача 0.50 мм/об
 * <THREAD:150>          - Резьба 1.50 мм
 * <POS_Z:123450>        - Позиция Z = 123.450 мм
 * <POS_X:-67890>        - Позиция X = -67.890 мм
 * <RPM:1500>            - Обороты 1500 RPM
 * <LIMITS:1,0,0,1>      - Лимиты: Left=ON, Right=OFF, Front=OFF, Rear=ON
 * <ALERT:1>             - Уведомление: 1=Установите упоры, 2=В исходную позицию, 3=Завершено, 4=Нейтраль
 * <SCREEN:1>            - Переключить на экран #1
 * <UPDATE>              - Полное обновление (отправить все данные)
 * <PING>                - Проверка связи
 */

// ============================================================================
// КОМАНДЫ ESP32 → ARDUINO
// ============================================================================

/*
 * Примеры ответов:
 *
 * <TOUCH:BTN_UP>        - Нажата кнопка UP на экране
 * <TOUCH:BTN_DOWN>      - Нажата кнопка DOWN
 * <TOUCH:BTN_SELECT>    - Нажата кнопка SELECT
 * <TOUCH:BTN_LEFT>      - Нажата кнопка LEFT
 * <TOUCH:BTN_RIGHT>     - Нажата кнопка RIGHT
 * <TOUCH:MODE:3>        - Нажат режим M3 на экране
 * <TOUCH:SUBMODE:2>     - Нажат подрежим S2 на экране
 * <READY>               - ESP32 готов к работе
 * <PONG>                - Ответ на PING
 * <ERROR:msg>           - Ошибка
 */

// ============================================================================
// ФУНКЦИИ
// ============================================================================

class UartProtocol {
public:
    UartProtocol();

    // Инициализация UART
    void begin(HardwareSerial& serial, uint32_t baud_rate = 115200);

    // Обработка входящих команд (вызывать в loop())
    void process();

    // Получить текущие данные станка
    const LatheData& getData() const { return data_; }

    // Оптимистично обновить режим (при нажатии кнопки, до подтверждения от STM32)
    void setModeOptimistic(LatheMode m) { data_.mode = m; }

    // Оптимистично обновить индекс конуса
    void setConeOptimistic(uint8_t ci) { data_.cone_idx = ci; }

    // Оптимистичные обновления параметров до подтверждения от STM32
    void setFeedOptimistic(int16_t v) { data_.feed_mm = v; }
    void setAfeedOptimistic(int16_t v) { data_.afeed_mm = v; }
    void setApOptimistic(int16_t v) { data_.ap = v; }
    void setBarOptimistic(int16_t v) { data_.bar_r = v; }
    void setSphereOptimistic(int16_t v) { data_.sphere_radius = v; }
    void setTotalToothOptimistic(uint16_t v) { data_.total_tooth = v; }
    void setCurrentToothOptimistic(uint16_t v) { data_.current_tooth = v; }
    void setThreadOptimistic(int16_t mm_x100, const char* name) {
        data_.thread_mm = mm_x100;
        strncpy(data_.thread_name, name, sizeof(data_.thread_name) - 1);
        data_.thread_name[sizeof(data_.thread_name) - 1] = '\0';
    }

    // Отправить команду Arduino (нажатие кнопки)
    void sendButtonPress(const char* button_name);

    // Отправить готовность
    void sendReady();

    // Отправить ошибку
    void sendError(const char* message);

    // Отправить PONG (ответ на PING)
    void sendPong();

    // Отправить строку GCode в STM32: <GCODE:line>\n
    void sendGCodeLine(const char* line);

    // Callback функции (устанавливаются пользователем)
    typedef void (*DataUpdateCallback)(const LatheData& data);
    typedef void (*ScreenChangeCallback)(uint8_t screen_num);
    typedef void (*AlertCallback)(const char* message);
    typedef void (*GCodeResponseCallback)(bool ok, const char* err);

    void setDataUpdateCallback(DataUpdateCallback callback) {
        data_update_callback_ = callback;
    }

    void setScreenChangeCallback(ScreenChangeCallback callback) {
        screen_change_callback_ = callback;
    }

    void setAlertCallback(AlertCallback callback) {
        alert_callback_ = callback;
    }

    typedef void (*AlertDismissCallback)();
    void setAlertDismissCallback(AlertDismissCallback callback) {
        alert_dismiss_callback_ = callback;
    }

    void setGCodeResponseCallback(GCodeResponseCallback callback) {
        gcode_response_callback_ = callback;
    }

private:
    HardwareSerial* serial_;
    LatheData data_;

    char rx_buffer_[256];
    uint16_t rx_index_;

    DataUpdateCallback data_update_callback_;
    ScreenChangeCallback screen_change_callback_;
    AlertCallback alert_callback_;
    AlertDismissCallback alert_dismiss_callback_;
    GCodeResponseCallback gcode_response_callback_;

    bool data_dirty_;  // true when data_ changed but callback not yet fired

    // Парсинг команд
    void parseCommand(const char* cmd);
    void parseMode(const char* params);
    void parseSubMode(const char* params);
    void parseFeed(const char* params);
    void parseAFeed(const char* params);
    void parseThread(const char* params);
    void parsePosZ(const char* params);
    void parsePosX(const char* params);
    void parseRPM(const char* params);
    void parseLimits(const char* params);
    void parseScreen(const char* params);
    void parsePass(const char* params);    // <PASS:nr,total>
    void parseAp(const char* params);     // <AP:50>  → 0.50мм
    void parsePh(const char* params);     // <PH:2>   → 2 захода
    void parseAngle(const char* params);  // <ANGLE:1234> → 123.4°
    void parseCone(const char* params);      // <CONE:3>       → индекс конуса
    void parseConeAngle(const char* params); // <CONE_ANGLE:30> → угол × 10
    void parseDivN(const char* params);   // <DIVN:24> → total_tooth=24
    void parseDivM(const char* params);   // <DIVM:5>  → current_tooth=5
    void parseBar(const char* params);          // <BAR:250>         → bar_r=2.50мм
    void parsePassSphr(const char* params);     // <PASS_SPHR:N>     → pass_total_sphr=N
    void parseSphere(const char* params);       // <SPHERE:2000>     → sphere_radius=20.00мм
    void parseState(const char* params);        // <STATE:run/stop>  → статус работы
    void parseAlert(const char* params);        // <ALERT:N|S>       → показать уведомление
    void parseThreadName(const char* params);   // <THREAD_NAME:...> → thread_name строка
    void parseRpmLim(const char* params);       // <RPM_LIM:300>     → rpm_limit=300
    void parseThreadCycl(const char* params);   // <THREAD_CYCL:16>  → thread_cycles=16
    void parseThreadTravel(const char* params); // <THREAD_TRAVEL:300> → thread_travel=300
    void parseSelectMenu(const char* params);   // <SELECTMENU:2>     → select_menu=2
    void parseOtskokZ(const char* params);      // <OTSKOK_Z:N>       → otskok_z=N
    void parseTensionZ(const char* params);     // <TENSION_Z:N>      → tension_z=N
    void parseDiamX(const char* params);        // <DIAM_X:N>         → diam_x=N
    void parsePassFin(const char* params);      // <PASS_FIN:N>       → pass_fin=N
    void parseConeThr(const char* params);      // <CONE_THR:0|1>     → cone_thr
    void parseCutterW(const char* params);      // <CUTTER_W:N>       → cutter_w
    void parseCuttingW(const char* params);     // <CUTTING_W:N>      → cutting_w

    // Отправка команды
    void sendCommand(const char* cmd, const char* params = nullptr);
};

// ============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================================

// Преобразование значения в строку для отображения
class DisplayFormatter {
public:
    // Подача (мм/об * 100 → строка "0.25")
    static void formatFeed(char* buffer, int16_t feed_mm_x100);

    // Резьба (мм * 100 → строка "1.50")
    static void formatThread(char* buffer, int16_t thread_mm_x100);

    // Позиция (мкм → строка "123.45")
    static void formatPosition(char* buffer, int32_t pos_um);

    // Обороты (RPM → строка "1500")
    static void formatRPM(char* buffer, uint16_t rpm);

    // Название режима
    static const char* getModeName(LatheMode mode);
    static const char* getModeShortName(LatheMode mode);  // M1, M2, ...

    // Название подрежима
    static const char* getSubModeName(LatheSubMode submode);
    static const char* getSubModeShortName(LatheSubMode submode);  // S1, S2, ...
};

#endif // UART_PROTOCOL_H

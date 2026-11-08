#pragma once
// Циклы токарной обработки Fanuc G71 (черновая обдирка по контуру) и G70 (чистовой проход по контуру).
// STM32 получает строки по одной и контур заранее не видит — поэтому циклы разворачиваются здесь, на дисплее,
// в обычные G0/G1/G2/G3. Чистый C++ (без Arduino): проверяется на компьютере (tests/gcode_cycles).
//
// Вход — строки программы без комментариев, в верхнем регистре, пустые выброшены; номер строки n = индекс + 1
// (так же считают исполнитель и веб-страница). Выход — строки для STM32 с номером исходной строки.
#include <string>
#include <vector>

struct GcLine {
    int         n;   // номер строки программы (для экрана и веб-страницы)
    std::string g;   // строка для STM32
};

// true — в программе есть G70/G71 (нужен разворот)
bool GCodeCycles_Needed(const std::vector<std::string>& lines);

// Развернуть программу. false — ошибка: err (по-русски, для экрана), err_n — номер строки
bool GCodeCycles_Expand(const std::vector<std::string>& lines, std::vector<GcLine>& out,
                        std::string& err, int& err_n);

// Время каждой строки, с — по модели исполнителя STM32 (Core/e2/e2_gcode.inc: ускоренный ход 600 мм/мин,
// не быстрее 8000 шагов/с, разгон выше 1000 шагов/с за 400 шагов, дуги — отрезками по 0.1 мм) + обмен по UART.
// spz / spx — шагов на мм по Z и X; rpm — обороты для подачи на оборот (G95/G99). sec.size() == lines.size()
void GCodeTime_Lines(const std::vector<std::string>& lines, double spz, double spx, double rpm,
                     std::vector<float>& sec);

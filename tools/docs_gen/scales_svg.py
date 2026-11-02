#!/usr/bin/env python3
"""Схемы сопряжения линеек → docs/scales/*.svg. Запуск: python3 tools/docs_gen/scales_svg.py"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from svg import S
OUT = os.path.join(os.path.dirname(__file__), '..', '..', 'docs', 'scales')

# Канал «вход линейки → STM32» с подтяжкой, RC и буфером (для квадратуры TTL)
def ttl_channel(s, y, name, pin):
    s.text(170, y - 6, name, size=13, bold=True)
    s.line(160, y, 260, y); s.dot(230, y)
    s.line(230, y, 230, y - 30); s.res(230, y - 70, '2.2k', vertical=True); s.line(230, y - 70, 230, y - 80)
    s.text(238, y - 82, '+5V', size=12)
    s.res(260, y, '1k'); s.line(300, y, 360, y); s.dot(330, y); s.cap(330, y, '1n')
    s.line(360, y, 420, y)
    s.e.append(f'<polygon points="420,{y-18} 420,{y+18} 455,{y}" fill="#fff" stroke="#222" stroke-width="2"/>')
    s.e.append(f'<circle cx="460" cy="{y}" r="5" fill="#fff" stroke="#222" stroke-width="2"/>')
    s.line(465, y, 600, y); s.text(612, y + 5, pin, size=13, bold=True)

# 1. Квадратура TTL 5 В — стеклянные и магнитные линейки
s = S(820, 470, 'Квадратура TTL 5 В: стеклянные и магнитные линейки → STM32', OUT)
s.box(20, 50, 140, 250, 'Линейка', 'DB9 / 5-pin\n+5V, GND, A, B\n(распиновка —\nпо паспорту)', fill='#fdf3e6')
s.box(600, 50, 200, 250, 'STM32F407', 'ось Z: A→PA15, B→PB3\nось X: A→PB4, B→PB5', fill='#eaf6ea')
ttl_channel(s, 130, 'A', 'A (PA15 / PB4)')
ttl_channel(s, 230, 'B', 'B (PB3 / PB5)')
s.text(437, 180, '74LVC14', size=12, anchor='middle', bold=True)
s.text(437, 196, '(питание 3.3 В)', size=11, anchor='middle')
s.note(330, ['• 74LVC14 — триггер Шмитта: убирает дребезг и помехи ЧП/моторов, входы терпят 5 В.',
             '  Инвертируются оба канала — направление счёта не меняется.',
             '• 2.2k на +5 В нужны для линеек с открытым коллектором; с двухтактным выходом не мешают.',
             '• Минимальный вариант: только резистор 1k от линейки к пину STM32 (пины толерантны к 5 В).',
             '• Питание линейки +5 В (до ~100 мА) через самовосстанавливающийся предохранитель.',
             '• Экран кабеля — на GND контроллера, только с одной стороны.',
             '• Конфиг: SCALE_*_TYPE = SCALE_QUADRATURE, SCALE_*_NM_PER_COUNT = 5000 (5 мкм) / 1000 (1 мкм)'])
s.save('quadrature_ttl.svg')

# 2. RS-422 (дифференциальный выход)
s = S(820, 420, 'Квадратура RS-422 (A, /A, B, /B) → приёмник → STM32', OUT)
s.box(20, 50, 160, 220, 'Линейка RS-422', '+5V, GND\nA, /A, B, /B\n(иногда Z, /Z)', fill='#fdf3e6')
s.box(330, 50, 170, 220, 'AM26LV32', '4 приёмника RS-422\nпитание 3.3 В\nвход ±7 В', fill='#eef4fb')
s.box(620, 50, 180, 220, 'STM32F407', 'A → PA15 / PB4\nB → PB3 / PB5', fill='#eaf6ea')
for y, n in ((110, 'A'), (140, '/A'), (190, 'B'), (220, '/B')):
    s.line(180, y, 330, y); s.text(190, y - 4, n, size=12, bold=True)
s.res(250, 125, '120Ω', vertical=False); s.res(250, 205, '120Ω', vertical=False)
s.line(500, 125, 620, 125); s.text(560, 120, '1Y → A', size=12, anchor='middle')
s.line(500, 205, 620, 205); s.text(560, 200, '2Y → B', size=12, anchor='middle')
s.note(300, ['• Резистор 120 Ω между A и /A (и B и /B) — согласование линии, ставится у приёмника.',
             '• Выход приёмника — сразу на пин STM32 (уровни 3.3 В), инверсии нет.',
             '• Дифференциальный выход лучше всего держит помехи — для длинных кабелей и ЧП.',
             '• Конфиг: как у TTL — SCALE_QUADRATURE + разрешение линейки в нм.'])
s.save('quadrature_rs422.svg')

# 3. Ёмкостные 1.5 В (Sylvac 48 / BIN6) через компаратор
s = S(820, 520, 'Ёмкостные линейки 1.5 В (Sylvac 48 бит, BIN6) → компаратор → STM32', OUT)
s.box(20, 60, 150, 220, 'Линейка 1.5 В', '4-контактный разъём\n+1.5V, GND\nCLK, DATA', fill='#fdf3e6')
s.box(330, 60, 170, 220, 'LM393', 'компаратор\nпитание 3.3 В\nвыход — откр. коллектор', fill='#eef4fb')
s.box(640, 60, 160, 220, 'STM32F407', 'CLK → PA15 / PB4\nDATA → PB3 / PB5', fill='#eaf6ea')
for y, n in ((130, 'CLK → IN+'), (210, 'DATA → IN+')):
    s.line(170, y, 330, y); s.text(180, y - 6, n, size=12, bold=True)
s.line(250, 165, 330, 165); s.text(180, 160, 'Uоп 0.75 В → IN−', size=12)
s.text(180, 182, '(делитель 10k/10k от 1.5 В)', size=11, color='#555')
for y in (130, 210):
    s.line(500, y, 640, y); s.dot(580, y); s.line(580, y, 580, y - 25)
    s.res(580, y - 65, '4.7k', vertical=True); s.text(588, y - 70, '+3.3V', size=12)
s.box(20, 300, 280, 60, 'Питание линейки 1.5 В', 'TLV70215 (или LM317 на 1.52 В)', fill='#fff6f6')
s.note(385, ['⚠ У большинства китайских линеек 1.5 В корпус соединён с «+» питания. Если корпус',
             '  касается заземлённой станины, а «−» линейки — на GND контроллера, источник 1.5 В замкнётся.',
             '  Корпус линейки изолировать от станины ИЛИ питать линейку от развязанного источника.',
             '• Компаратор не инвертирует → SCALE_*_SIGNAL_INVERT = 0.',
             '  Вариант на одном NPN (BC547, база через 10k) инвертирует → SIGNAL_INVERT = 1.',
             '• Конфиг: SCALE_SYLVAC48 (2×24 бит) или SCALE_BIN6 (24 бит). ⚠ Схема требует проверки сообществом.'])
s.save('capacitive_1v5.svg')

# 4. iGaging 21 бит, 3 В
s = S(820, 400, 'iGaging EZ-View / AccuRemote / DigiMag (21 бит, 3 В) → STM32', OUT)
s.box(20, 60, 170, 200, 'Линейка iGaging', 'разъём mini/micro-USB\nVCC (≈3 В), GND\nCLK (D−), DATA (D+)', fill='#fdf3e6')
s.box(620, 60, 180, 200, 'STM32F407', 'CLK ← PA15 / PB4 (выход)\nDATA → PB3 / PB5', fill='#eaf6ea')
s.line(190, 120, 620, 120); s.res(380, 120, '470Ω'); s.text(250, 112, 'CLK (такт от STM32)', size=12, bold=True)
s.line(190, 200, 620, 200); s.res(380, 200, '470Ω'); s.text(250, 192, 'DATA', size=12, bold=True)
s.note(290, ['• Уровни 3 В совместимы с STM32 напрямую; резисторы 470 Ω защищают пины.',
             '• Питание линейки — 3.3 В контроллера (у штатного дисплея ≈3 В). Корпус — на GND.',
             '• Такт выдаёт STM32 (50 опросов/с) → SCALE_*_TYPE = SCALE_IGAGING21, SIGNAL_INVERT = 0.',
             '• Цвета проводов не стандартны — сверяйте по контактам платы линейки.'])
s.save('igaging_21bit.svg')

# 5. Mitutoyo Digimatic
s = S(820, 430, 'Mitutoyo Digimatic (SPC): REQ / CLK / DATA → STM32', OUT)
s.box(20, 60, 170, 230, 'Прибор Mitutoyo', 'кабель SPC\nGND, DATA, CLK,\nREQ, READY', fill='#fdf3e6')
s.box(620, 60, 180, 230, 'STM32F407', 'CLK → PA15 / PB4\nDATA → PB3 / PB5\nREQ ← PC6 / PC7', fill='#eaf6ea')
for y, n in ((110, 'CLK'), (170, 'DATA')):
    s.line(190, y, 620, y); s.text(200, y - 6, n, size=12, bold=True); s.dot(520, y)
    s.line(520, y, 520, y - 15); s.res(520, y - 55, '10k', vertical=True); s.text(528, y - 60, '+3.3V', size=12)
s.text(200, 234, 'REQ', size=12, bold=True); s.line(190, 240, 400, 240)
s.box(400, 215, 120, 55, 'NPN BC547', 'К → REQ, Э → GND', fill='#fff')
s.line(520, 240, 620, 240); s.res(560, 240, '4.7k'); s.text(570, 262, 'база ← PC6/PC7', size=11)
s.note(320, ['• DATA и CLK прибора — открытый коллектор: подтяжка 10k к 3.3 В, инверсии нет (SIGNAL_INVERT = 0).',
             '• REQ: STM32 выдаёт «1» → транзистор прижимает REQ к земле → прибор отправляет 52 бита.',
             '• Опрос 20 раз в секунду. Конфиг: SCALE_*_TYPE = SCALE_DIGIMATIC.',
             '• Распиновка разъёма — в руководстве прибора (Mitutoyo 99MAH059).'])
s.save('digimatic.svg')

# 6. Общая схема: два способа подключения
s = S(820, 330, 'Два способа подключения линеек к контроллеру ELS', OUT)
s.box(20, 60, 150, 90, 'Линейки', 'любые, которые\nпонимает блок DRO', fill='#fdf3e6')
s.box(250, 60, 170, 90, 'Блок DRO', 'HS800-2\n(свой экран)', fill='#eef4fb')
s.box(520, 60, 280, 90, 'STM32 ELS', 'ELS_POS_SOURCE =\nPOS_SOURCE_DRO_UART', fill='#eaf6ea')
s.line(170, 105, 250, 105); s.line(420, 105, 520, 105); s.text(470, 98, 'UART', size=12, anchor='middle')
s.box(20, 190, 150, 90, 'Линейки', 'квадратура или\nёмкостные', fill='#fdf3e6')
s.box(250, 190, 170, 90, 'Схема', 'сопряжения\n(см. ниже)', fill='#fff6f6')
s.box(520, 190, 280, 90, 'STM32 ELS', 'ELS_POS_SOURCE =\nPOS_SOURCE_SCALES', fill='#eaf6ea')
s.line(170, 235, 250, 235); s.line(420, 235, 520, 235)
s.text(20, 315, 'Способ 1 — сверху: блок DRO остаётся. Способ 2 — снизу: без блока DRO, линейки напрямую.', size=13, color='#333')
s.save('connection_options.svg')
print('ok')

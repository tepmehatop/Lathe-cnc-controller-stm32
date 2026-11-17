#!/usr/bin/env python3
"""
Касание «проваливается» под закрывшееся окно? (баг 2026-11-15: ВВОД клавиатуры нажимал ЗАКРЫТЬ / ИЗ КОНФИГА под ней)

Для каждого окна — кнопка, которую держат 1 с (медленное нажатие пальцем). После отпускания:
  - закрылось только это окно (или открылось то, что кнопка и должна открыть);
  - под ним ничего не открылось (меню режимов, клавиатура, барабан, правка, сообщение, настройки);
  - режим, подэкран, механика и источник координат STM32 не изменились (не сработали «ИЗ КОНФИГА», «ОК» и т. п.).

Дисплей по USB, STM32 по ST-Link.  ~/.platformio/penv/bin/python tools/ui_fallthrough_test.py
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd

OPEN = ('axis_kb', 'roller', 'edit', 'sub_edit', 'alert', 'info', 'mode_menu', 'thr_type_menu', 'settings',
        'joystick', 'cal')
GEAR, SET_MECH, SET_CAL, HUB_CLOSE = (465, 19), (126, 66), (354, 66), (409, 18)
MECH_CELL, ENTER, CANCEL = (264, 72), (340, 236), (210, 236)
MECH_CLOSE, POPUP_OK = (394, 236), (390, 241)
Z_TILE, D_TILE, MAIN, AP_TILE = (71, 153), (347, 153), (150, 78), (71, 230)
MODE_LBL, JOY = (54, 20), (438, 19)


def main():
    stm, e = cd.Stm32(), cd.Esp('/dev/cu.usbmodem14801')
    time.sleep(1)
    res = []

    def chk(ok, m):
        res.append((ok, m)); print(('    OK   ' if ok else '    FAIL ') + m)

    def tap(xy, ms=120):
        e.send(f'<TAP:{xy[0]},{xy[1]},{ms}>'); time.sleep(0.5 + ms / 1000)

    def hold(xy):
        tap(xy, 1000); time.sleep(0.6)

    def opened():
        u = e.uidump()
        return {k for k in OPEN if u.get(k) == '1'}

    def core():
        mech = stm.read(stm.sym['e2_mech'][0], 9, 32)
        pos = stm.read(stm.sym['e2_pos'][0], 12, 32)
        return (stm.var('Mode'), stm.var('SelectMenu'), tuple(mech), tuple(pos))

    def reset(mode='M1'):
        e.send('<UIRESET>'); time.sleep(0.4); e.btn('ALERT_OK'); e.btn(mode); time.sleep(0.8)
        for _ in range(3):
            if stm.var('SelectMenu') == 1:
                break
            e.btn('PARAM_OK'); time.sleep(0.5)

    def case(name, setup, button, want_open, mode='M1', mode_changes=False):
        print(f'[{name}]')
        reset(mode)
        for s in setup:
            if isinstance(s, str):
                e.btn(s); time.sleep(0.6)
            else:
                tap(s)
        c0 = core()
        hold(button)
        o = opened(); c1 = core()
        if mode_changes:                                # кнопка и должна сменить режим
            c0, c1 = c0[2:], c1[2:]
        chk(o == set(want_open), f'открыто после кнопки: {sorted(o) or "ничего"} (ожидалось {sorted(want_open) or "ничего"})')
        chk(c0 == c1, 'режим, подэкран, механика, линейки — без изменений' if c0 == c1 else f'изменилось: {c0} -> {c1}')

    # клавиатура над экраном «МЕХАНИКА»: под ВВОД — ЗАКРЫТЬ / ИЗ КОНФИГА, под ОТМЕНА — ИЗ КОНФИГА
    case('механика: ВВОД', [GEAR, SET_MECH, MECH_CELL], ENTER, {'settings'})
    case('механика: ОТМЕНА', [GEAR, SET_MECH, MECH_CELL], CANCEL, {'settings'})
    case('механика: ЗАКРЫТЬ', [GEAR, SET_MECH], MECH_CLOSE, set())
    case('настройки: МЕХАНИКА', [GEAR], SET_MECH, {'settings'})
    case('настройки: ЗАКРЫТЬ', [GEAR], HUB_CLOSE, set())
    # клавиатура над главным экраном: под ВВОД / ОТМЕНА — плитки нижнего ряда
    case('позиция Z: ВВОД', [Z_TILE], ENTER, set())
    case('позиция Z: ОТМЕНА', [Z_TILE], CANCEL, set())
    case('подача: ОТМЕНА', [MAIN], CANCEL, set())
    case('съём: ОТМЕНА', [AP_TILE], CANCEL, set())
    case('диаметр: ОТМЕНА', [D_TILE], CANCEL, set())
    # окно «НЕ ПРИНЯТО» — ПОНЯТНО над плиткой справа внизу
    case('НЕ ПРИНЯТО: ПОНЯТНО', [MAIN, (354, 189), (125, 143), (354, 189), ENTER], POPUP_OK, set())
    # меню режимов: кнопка режима над плитками
    case('меню режимов: M3', [MODE_LBL], (121, 110), set(), mode_changes=True)
    case('меню режимов: M2', [MODE_LBL], (356, 63), set(), mode_changes=True)
    # меню типа резьбы (M3): пункт над главным значением и плитками
    case('тип резьбы: дюймовая', [(347, 230)], (240, 116), {'roller', 'edit'}, mode='M3')
    # джойстик: ✕ над шапкой
    case('джойстик: ✕', [JOY], (435, 58), set())

    e.send('<UIRESET>'); e.btn('M1')
    stm.close()
    fails = [m for ok, m in res if not ok]
    print('\nИТОГ:', 'OK' if not fails else f'{len(fails)} FAIL')
    for f in fails:
        print('  -', f)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())

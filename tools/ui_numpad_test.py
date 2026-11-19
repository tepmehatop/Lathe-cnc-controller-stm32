#!/usr/bin/env python3
"""
Цифровая клавиатура во всех местах ввода: кнопки ▲▼ сбоку и окно «НЕ ПРИНЯТО».

Для каждого места ввода:
  - ▲/▼ меняют значение на 1 младшего разряда (смещение отрезания — на соседнее значение ядра);
  - на пределе значение не меняется, вверху клавиатуры — «МАКС …» / «МИН …» (окна нет);
  - ▲ вне пределов (набрано 0.40 при пределе 0.25) — к ближайшему пределу;
  - удержание ▲ — повтор;
  - ▲▼ клавиатуры не проваливаются в ▲▼ главного экрана (значение в ядре не меняется до ВВОДА,
    режим и подэкран — те же);
  - ВВОД — значение в ядре (STM32) равно показанному;
  - неверное число → «НЕ ПРИНЯТО»; ПОНЯТНО закрывает только окно, клавиатура остаётся с набранным числом;
    дальше можно исправить (▼ — к пределу) и ВВОД, или ОТМЕНА — значение в ядре прежнее;
  - ПОНЯТНО над кнопкой ВВОД клавиатуры — касание не проваливается в ВВОД (окно не открывается снова).
Места: подача (M1), асинхронная подача (M2), съём, ДО УПОРА (подэкран 4), отрезание (ШАГ ⌀, ПРОРЕЗ ⌀,
СМЕЩЕНИЕ), механика в настройках (настройки остаются открытыми), ХОД калибровки (минимум 5 мм).

Дисплей по USB, STM32 по ST-Link.  ~/.platformio/penv/bin/python tools/ui_numpad_test.py [--only feed,afeed,...]
"""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd

K = {'7': (89, 97), '8': (165, 97), '9': (242, 97), '4': (89, 143), '5': (165, 143), '6': (242, 143),
     '1': (89, 189), '2': (165, 189), '3': (242, 189), '0': (318, 189), '.': (81, 235), '<': (318, 97)}
ENTER, CANCEL, POPUP_OK = (304, 236), (174, 236), (390, 241)
UP, DN = (394, 117), (394, 211)        # колонка ▲▼ внутри окна клавиатуры
MAIN, AP_TILE, ROW1, ROW3, ROW4, THR = (150, 78), (71, 230), (72, 154), (72, 230), (209, 230), (346, 230)
GEAR, SET_MECH, SET_CAL, MECH_CELL, MECH_CLOSE = (465, 19), (126, 66), (354, 66), (264, 72), (394, 236)
CAL_LEN_Z, CAL_CLOSE = (245, 120), (394, 236)

res = []


def chk(ok, m):
    res.append((ok, m)); print(('    OK   ' if ok else '    FAIL ') + m, flush=True)


class T:
    def __init__(self):
        self.stm, self.e = cd.Stm32(), cd.Esp('/dev/cu.usbmodem14801')
        time.sleep(1)

    def tap(self, xy, ms=120, w=0.5):
        self.e.send(f'<TAP:{xy[0]},{xy[1]},{ms}>'); time.sleep(w + ms / 1000)

    def typ(self, s):
        """Стереть набранное (после ▲▼ цифры дописываются) и набрать s."""
        for _ in range(len(self.val()) + 1):
            self.tap(K['<'], w=0.3)
        for c in s:
            self.tap(K[c], w=0.35)

    def ui(self):
        return self.e.uidump() or {}

    def v(self, n):
        return self.stm.var(n)

    def val(self):
        return (self.ui().get('np_val') or '').split(' ')[0]

    def b(self, c, w=0.5):
        self.e.btn(c); time.sleep(w)

    def reset(self, mode='M1'):
        self.e.send('<UIRESET>'); time.sleep(0.4); self.b('ALERT_OK', 0.3); self.b(mode, 0.8); self.b('S2', 0.5)
        self.b('ALERT_OK', 0.3)
        for _ in range(4):
            if self.v('SelectMenu') == 1:
                break
            self.b('PARAM_OK', 0.5)

    def core(self):
        return self.v('Mode'), self.v('SelectMenu')

    # ── общий сценарий для одного места ввода ───────────────────────────────
    def site(self, name, open_kb, var, start, up1, top, top_hint, bad, scale=1, low=None, low_hint=None,
             accept=None, sm=1):
        """open_kb() — открыть клавиатуру; var — переменная ядра (в единицах экрана × scale);
        start — значение ядра перед проверкой (текстом команды <BTN:…>), up1 — текст после ▲;
        top — текст на верхнем пределе; bad — неверное число (строка клавиш)."""
        print(f'[{name}]')
        start()
        c0, v0 = self.core(), self.v(var)
        open_kb()
        u = self.ui()
        init = self.val()
        chk(u.get('axis_kb') == '1', f'клавиатура открыта ({u.get("np_val")!r})')
        self.tap(UP)
        chk(float(self.val()) == float(up1), f'▲ → {self.val()!r} (ожидалось {up1!r})')
        chk(self.v(var) == v0 and self.core() == c0, f'▲ клавиатуры не дошла до ядра/главного экрана '
                                                     f'({var} {v0} → {self.v(var)}, режим/подэкран {c0} → {self.core()})')
        self.tap(DN); self.tap(DN)
        self.tap(UP)
        chk(float(self.val()) == float(init) and self.v(var) == v0, f'▼▼▲ — снова {self.val()!r} (исходное {init!r}), ядро не менялось')
        # к верхнему пределу — набором и ▲
        self.typ(top)
        self.tap(UP)
        u = self.ui()
        chk(self.val() == top and top_hint in (u.get('np_hint') or '') and u.get('info') == '0',
            f'на пределе ▲: {self.val()!r}, подсказка {u.get("np_hint")!r} (ожидалось {top!r}, {top_hint!r}, без окна)')
        if low is not None:
            self.typ(low); self.tap(DN)
            u = self.ui()
            chk(self.val() == low and low_hint in (u.get('np_hint') or ''),
                f'на пределе ▼: {self.val()!r}, подсказка {u.get("np_hint")!r} (ожидалось {low!r}, {low_hint!r})')
            self.tap(UP)
            chk(not (self.ui().get('np_hint') or ''), 'после шага от предела подсказка исчезла')
        # ВВОД значения, полученного стрелкой
        self.typ(top); self.tap(DN)
        want = accept(top, -1) if accept else None
        shown = self.val()
        self.tap(ENTER, w=0.8)
        u = self.ui()
        chk(u.get('axis_kb') == '0', f'ВВОД закрыл клавиатуру (значение {shown})')
        if want is not None:
            chk(self.v(var) == want, f'ядро {var} = {self.v(var)} (ожидалось {want} — то, что показано: {shown})')
        chk(self.core() == c0, f'режим/подэкран без изменений ({c0} → {self.core()})')
        # неверное число: окно, клавиатура остаётся
        if bad:
            v1 = self.v(var)
            open_kb(); self.typ(bad); self.tap(ENTER, w=0.8)
            u = self.ui()
            chk(u.get('info') == '1' and u.get('axis_kb') == '1', f'{bad} — окно «НЕ ПРИНЯТО» поверх клавиатуры '
                                                                  f'(info={u.get("info")}, клавиатура={u.get("axis_kb")})')
            self.tap(POPUP_OK, ms=600, w=0.8)                     # медленный палец — над ВВОДОМ клавиатуры
            u = self.ui()
            chk(u.get('info') == '0' and u.get('axis_kb') == '1' and self.val() == bad,
                f'ПОНЯТНО: окно закрыто, клавиатура открыта, набрано {self.val()!r} (info={u.get("info")}, '
                f'клавиатура={u.get("axis_kb")})')
            chk(self.v(var) == v1, f'ядро не изменилось ({var} {v1} → {self.v(var)})')
            self.tap(DN)
            chk(self.val() == top, f'▼ от неверного {bad} — к пределу {self.val()!r} (ожидалось {top!r})')
            self.tap(CANCEL, w=0.8)
            u = self.ui()
            chk(u.get('axis_kb') == '0' and self.v(var) == v1 and self.core() == c0,
                f'ОТМЕНА: клавиатура закрыта, ядро {var}={self.v(var)} (было {v1}), режим/подэкран {self.core()}')
        # удержание ▲ — повтор
        open_kb()
        a = self.val()
        self.tap(DN, ms=1500, w=0.6)
        b = self.val()
        chk(a != b, f'удержание ▼ 1.5 с: {a} → {b}')
        self.tap(CANCEL)
        self.tap(CANCEL) if self.ui().get('axis_kb') == '1' else None


def num(s, dec):
    return round(float(s) * 10 ** dec)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only')
    a = ap.parse_args()
    t = T()
    allt = ['feed', 'afeed', 'ap', 'dist', 'cut', 'mech', 'cal']
    only = a.only.split(',') if a.only else allt

    if 'feed' in only:
        t.site('подача M1', lambda: t.tap(MAIN), 'Feed_mm', lambda: (t.reset('M1'), t.b('FEED:13')),
               '0.14', '0.25', 'МАКС 0.25', '0.40', low='0.01', low_hint='МИН 0.01',
               accept=lambda s, d: num(s, 2) + d)
    if 'afeed' in only:
        t.site('асинхронная подача M2', lambda: t.tap(MAIN), 'aFeed_mm', lambda: (t.reset('M2'), t.b('AFEED:200')),
               '201', '400', 'МАКС 400', '450', low='10', low_hint='МИН 10', accept=lambda s, d: int(s) + d)
        t.b('AFEED:200')
    if 'ap' in only:
        t.site('съём', lambda: t.tap(AP_TILE), 'Ap', lambda: (t.reset('M1'), t.b('AP:150')),
               '1.51', '99.00', 'МАКС 99.00', '99.5', low='0.00', low_hint='МИН 0.00',
               accept=lambda s, d: num(s, 2) + d)
        t.b('AP:0')
    if 'dist' in only:
        def st():
            t.reset('M1')
            for _ in range(5):
                if t.v('SelectMenu') == 4: break
                t.b('PARAM_OK', 0.5)
            t.b('DIST:12500')
        # Distance_mm — в 0.0005 мм: ×2 от мкм
        t.site('ДО УПОРА (подэкран 4)', lambda: t.tap(ROW1), 'Distance_mm', st, '12.501', '999.999', 'МАКС 999.999',
               None, low='0.000', low_hint='МИН 0.000', accept=lambda s, d: (num(s, 3) + d) * 2)
        t.b('DIST:0'); t.reset('M1')
    if 'cut' in only:
        def st():
            t.reset('M6'); t.b('RAZ:2', 0.6); t.b('AP:200'); t.b('CUT_D:900'); t.b('STAG:4')
        t.site('отрезание: ШАГ ⌀', lambda: t.tap(ROW3), 'Ap', st, '3', '20', 'МАКС 20', '25', low='1', low_hint='МИН 1',
               accept=lambda s, d: (int(s) + d) * 100)
        # СМЕЩЕНИЕ — по значениям ядра 0.10 0.25 0.50 0.75 1.00 1.25 1.50 1.75 2.00
        print('[отрезание: СМЕЩЕНИЕ]')
        st(); c0 = t.core()
        t.tap(ROW4)
        seq = []
        for k in (UP, UP, DN, DN, DN):
            t.tap(k); seq.append(t.val())
        chk(seq == ['1.25', '1.50', '1.25', '1.00', '0.75'], f'▲▲▼▼▼ от 1.00: {seq}')
        t.typ('2.00'); t.tap(UP)
        chk(t.val() == '2.00' and 'МАКС 2.00' in (t.ui().get('np_hint') or ''), f'на 2.00 ▲ — {t.val()!r}, {t.ui().get("np_hint")!r}')
        t.typ('0.10'); t.tap(DN)
        chk(t.val() == '0.10' and 'МИН 0.10' in (t.ui().get('np_hint') or ''), f'на 0.10 ▼ — {t.val()!r}, {t.ui().get("np_hint")!r}')
        t.typ('0.60'); t.tap(UP)
        chk(t.val() == '0.75', f'набрано 0.60 (нет в списке) ▲ → {t.val()!r} (ожидалось 0.75)')
        t.tap(ENTER, w=0.8)
        chk(t.v('Staggered_Width') == 75 and t.core() == c0, f'ВВОД: ядро смещение {t.v("Staggered_Width")} (0.75 = 75)')
        st()
        t.site('отрезание: ПРОРЕЗ ⌀', lambda: t.tap(THR), 'Pass_Total', st, '10.01', '999.99', 'МАКС 999.99', None,
               low='0.01', low_hint='МИН 0.01', accept=lambda s, d: -(-(num(s, 2) + d) // 200))
        t.b('RAZ:1', 0.6)
    if 'mech' in only:
        print('[механика в настройках]')
        t.reset('M1')
        mech0 = t.stm.read(t.stm.sym['e2_mech'][0], 9, 32)
        t.tap(GEAR); t.tap(SET_MECH); t.tap(MECH_CELL)
        u = t.ui(); v0 = t.val()
        chk(u.get('axis_kb') == '1' and u.get('settings') == '1', f'ШАГ ВИНТА Z — клавиатура над настройками ({v0})')
        t.tap(UP)
        want = f'{num(v0, 2) + 1:d}'
        want = f'{int(want) // 100}.{int(want) % 100:02d}'
        chk(t.val() == want, f'▲ {v0} → {t.val()} (ожидалось {want})')
        t.typ('0.01'); t.tap(DN)
        chk(t.val() == '0.01' and 'МИН 0.01' in (t.ui().get('np_hint') or ''), f'шаг винта: на 0.01 ▼ — {t.val()!r}, {t.ui().get("np_hint")!r}')
        t.typ('99.99'); t.tap(UP)
        chk('МАКС 99.99' in (t.ui().get('np_hint') or ''), f'шаг винта: МАКС 99.99 ({t.ui().get("np_hint")!r})')
        t.tap(CANCEL, ms=600, w=0.8)
        u = t.ui()
        chk(u.get('axis_kb') == '0' and u.get('settings') == '1', 'ОТМЕНА — клавиатура закрыта, настройки открыты')
        t.tap(MECH_CELL); t.tap(UP); t.tap(ENTER, ms=600, w=0.8)
        u = t.ui()
        chk(u.get('axis_kb') == '0' and u.get('settings') == '1', 'ВВОД (медленный палец) — настройки остались открытыми')
        chk(t.stm.read(t.stm.sym['e2_mech'][0], 9, 32) == mech0, 'механика в ядре не изменилась (без СОХРАНИТЬ)')
        t.e.send('<UIRESET>'); time.sleep(0.6)
    if 'cal' in only:
        print('[калибровка: ХОД]')
        t.reset('M1')
        t.tap(GEAR); t.tap(SET_CAL); t.tap(CAL_LEN_Z)
        u = t.ui()
        chk(u.get('axis_kb') == '1' and u.get('cal') == '1', f'ХОД Z — клавиатура над калибровкой ({t.val()!r})')
        t.typ('5'); t.tap(DN)
        chk(t.val() == '5' and 'МИН 5' in (t.ui().get('np_hint') or ''), f'ХОД: на 5 ▼ — {t.val()!r}, {t.ui().get("np_hint")!r}')
        t.typ('999'); t.tap(UP)
        chk('МАКС 999' in (t.ui().get('np_hint') or ''), f'ХОД: МАКС 999 ({t.ui().get("np_hint")!r})')
        t.tap(DN); t.tap(ENTER, ms=600, w=0.8)
        u = t.ui()
        chk(u.get('axis_kb') == '0' and u.get('cal') == '1', 'ВВОД — клавиатура закрыта, окно калибровки открыто')
        t.e.send('<UIRESET>'); time.sleep(0.6)

    t.e.send('<UIRESET>'); t.b('M1')
    t.stm.close()
    fails = [m for ok, m in res if not ok]
    print('\nИТОГ:', f'{len(res)} проверок,', 'OK' if not fails else f'{len(fails)} FAIL')
    for f in fails:
        print('  -', f)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())

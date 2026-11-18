#!/usr/bin/env python3
"""
Экран дисплея для функций мода 7e2 Лях V3.4–V3.8 (BACKLOG-043) на стенде: касания (<TAP:x,y> в сетке 480×272),
состояние экрана (<UIDUMP>) и ядра (OpenOCD). Ядро и экран должны совпадать, откуда бы ни пришло изменение —
с экрана или с кнопок пульта.

  sm      — подэкраны V3.6: «Выбор» 1 → 3 (ВВОД ДИАМЕТРА) → 4 (ДИСТАНЦ. ЛИМИТЫ) → 1; M1 «Наружный» с упорами X — 2 = отскок/натяг;
  dist    — подэкран 4: тап «ДО УПОРА» — клавиатура; расстояние с экрана и с пульта; кнопки упоров на экране ставят/снимают
            упор на расстоянии, кнопки подсвечиваются; негатив — в «Наружном» упоры не ставятся;
  prof    — плитка ТИП/ПРОФИЛЬ: окно с двумя рядами, профиль BSW → ядро Kzr=2, плитка; с пульта (уск+Выбор) — плитка следом;
  cut     — M6: переключатель ШАР/ОТРЕЗАНИЕ (экран и пульт), плитки ШАГ ⌀ / СМЕЩЕНИЕ / ПРОРЕЗ ⌀, клавиатура, ⌀ = врезаний × шаг;
  cone    — подэкран 2: ДЛ. КОНУСА = формуле V3.8; конус > «Внутренний» — «Режим невозможен!»;
  pause   — в автоцикле ▲/▼ — ДАЛЕЕ/ПАУЗА; ПАУЗА в резьбе → окно «ПАУЗА» с кнопкой ДАЛЕЕ → цикл до конца; в «Ручном» — стрелки.

  ~/.platformio/penv/bin/python tools/hil_v38_ui.py [--only sm,dist,...]
"""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hil_v38 as H

# центры плиток (create_k_content, база 480×272)
ROW1, ROW2, DIAM = (72, 154), (209, 154), (346, 154)
ROW3, ROW4, THR = (72, 230), (209, 230), (346, 230)
DIST_B = [(55, 230), (158, 230), (261, 230), (364, 230)]
UP, OK, DN = (446, 74), (446, 154), (446, 234)
RAZ = (249, 78)
MENU_CAT = [(81, 95), (185, 95), (289, 95), (393, 95)]
MENU_PROF = [(81, 163), (185, 163), (289, 163), (393, 163)]
ALERT_OK = (392, 172)
res = H.res
chk = H.chk


class UI(H.Bench):
    def tap(self, xy, w=0.6, ms=80):
        self.e.send(f'<TAP:{xy[0]},{xy[1]},{ms}>'); time.sleep(w + ms / 1000)

    def ui(self):
        return self.e.uidump() or {}

    def to_sm(self, n):
        for _ in range(6):
            if self.v('SelectMenu') == n:
                return True
            self.b('PARAM_OK', 0.4)
        return self.v('SelectMenu') == n


def norm(t):
    return (t or '').replace('\\n', '\n')


def t_sm(U):
    print('[sm]')
    U.b('M1'); U.manual(); U.clear_limits(); U.to_sm(1)
    seq = []
    for _ in range(3):
        U.b('PARAM_OK', 0.6); seq.append((U.v('SelectMenu'), norm(U.ui().get('sm_row6'))))
    chk([s for s, _ in seq] == [3, 4, 1], f'M1 «Ручной»: «Выбор» 1 → {seq[0][0]} → {seq[1][0]} → {seq[2][0]} (ожидалось 3 → 4 → 1)')
    chk('ВВОД' in seq[0][1] and 'ДИАМЕТРА' in seq[0][1], f'подэкран 3 подписан «ВВОД ДИАМЕТРА» ({seq[0][1]!r})')
    chk('ДИСТАНЦ' in seq[1][1], f'подэкран 4 подписан «ДИСТАНЦ. ЛИМИТЫ» ({seq[1][1]!r})')
    # M1 «Наружный» с упорами X: подэкран 2 — ОТСКОК / НАТЯГ
    U.to_sm(4); U.b('DIST:3000'); U.b('EMUL:STOP_B'); U.to_sm(1); U.b('EMUL:STOP_F')   # задний — на 3 мм, передний — у резца (старт)
    U.b('S3', 0.8)
    U.b('PARAM_OK', 0.6)
    u = U.ui()
    chk(U.v('SelectMenu') == 2 and 'ОТСКОК' in (u.get('row1_title') or ''), f'M1 «Наружный» + упоры X: подэкран 2 — ОТСКОК ({u.get("row1_title")!r})')
    U.to_sm(1); U.manual(); U.clear_limits(); U.b('DIST:0')


def t_dist(U):
    print('[dist]')
    U.b('M1'); U.manual(); U.clear_limits(); U.to_sm(4)
    u = U.ui()
    chk(u.get('dist_panel') == '1', 'подэкран 4: кнопки упоров видны')
    chk('ДО УПОРА' in (u.get('row1_title') or ''), f'плитка «ДО УПОРА» ({u.get("row1_title")!r})')
    U.tap(ROW1)
    chk(U.ui().get('axis_kb') == '1', 'тап «ДО УПОРА» — клавиатура')
    U.e.send('<UIRESET>'); time.sleep(0.5); U.to_sm(4)
    U.b('DIST:12500'); time.sleep(0.3)
    u = U.ui()
    chk(u.get('row1_val') == '12.500' and u.get('row2_val') == '25.000', f'расстояние 12.500, на ⌀ 25.000 ({u.get("row1_val")}, {u.get("row2_val")})')
    U.keys('BTN_UP'); time.sleep(0.3)                 # с пульта: ▲ +0.1 мм — экран следом
    chk(U.ui().get('row1_val') == '12.600', f'▲ пульта → на экране 12.600 ({U.ui().get("row1_val")})')
    z, x = U.v('Motor_Z_Pos'), U.v('Motor_X_Pos')
    for i, (flag, pos, exp) in enumerate((('limit_Left_flag', 'Limit_Pos_Left', z + 5040), ('limit_Right_flag', 'Limit_Pos_Right', z - 5040),
                                          ('limit_Front_flag', 'Limit_Pos_Front', x + 10080), ('limit_Rear_flag', 'Limit_Pos_Rear', x - 10080))):
        U.tap(DIST_B[i])
        ok = U.v(flag) == 1 and abs(U.v(pos) - exp) <= 4
        col = U.ui().get(f'dist_b{i}')
        chk(ok and (col or '').startswith('00FF8'), f'кнопка {i + 1}: упор на 12.6 мм ({U.v(pos) - (z if i < 2 else x):+d} шагов), подсветка {col}')
    for i in range(4):
        U.tap(DIST_B[i])
    chk(not any(U.v(f) for f in ('limit_Left_flag', 'limit_Right_flag', 'limit_Front_flag', 'limit_Rear_flag')), 'повторный тап снимает упоры')
    U.b('DIST:0'); U.to_sm(1)
    chk(U.ui().get('dist_panel') == '0', 'на подэкране 1 кнопок упоров нет')


def t_prof(U):
    print('[prof]')
    U.b('M3'); U.manual(); U.to_sm(1); U.b('KZR:1'); time.sleep(0.3)
    u = U.ui()
    chk(u.get('thr_type_title') == 'ТИП/ПРОФИЛЬ' and 'МЕТРИЧ 60' in (u.get('thr_sub') or ''),
        f'плитка ТИП/ПРОФИЛЬ: {u.get("thr_type_val")} / {u.get("thr_sub")}')
    U.tap(THR)
    chk(U.ui().get('thr_type_menu') == '1', 'тап по плитке — окно ТИП/ПРОФИЛЬ')
    U.tap(MENU_PROF[1])
    u = U.ui()
    chk(U.v('Kzr') == 2 and 'BSW' in (u.get('thr_sub') or '') and u.get('thr_type_menu') == '0',
        f'профиль BSW 55°: ядро Kzr={U.v("Kzr")}, плитка {u.get("thr_sub")!r}, окно закрыто')
    U.tap(THR); U.tap(MENU_CAT[1]); time.sleep(0.5)    # раздел «Дюймовая» — шаги из таблицы
    U.tap(OK, 1.0)
    chk('ДЮЙМ' in (U.ui().get('thr_type_val') or ''), f'раздел «Дюймовая»: плитка {U.ui().get("thr_type_val")!r}')
    U.b('THREAD_STEP:13')
    U.stm.press('JOY_RAPID'); U.keys('BTN_SELECT'); U.stm.release('JOY_RAPID'); time.sleep(0.4)
    chk(U.v('Kzr') == 3 and 'UNF' in (U.ui().get('thr_sub') or ''), f'с пульта уск+«Выбор» → UNF: ядро {U.v("Kzr")}, плитка {U.ui().get("thr_sub")!r}')
    U.tap(THR)
    u = U.ui()
    chk(int(u.get('prof_b2', 0)) > int(u.get('prof_b0', 0)), f'в окне выделен текущий профиль UNF (рамки {u.get("prof_b0")}/{u.get("prof_b2")})')
    U.e.send('<UIRESET>'); time.sleep(0.5); U.b('KZR:1')


def t_cut(U):
    print('[cut]')
    U.b('M6'); U.manual(); U.to_sm(1); U.b('RAZ:1', 0.5)
    u = U.ui()
    chk(u.get('raz_btn') == '1' and U.v('Raz') == 1, 'M6: переключатель ШАР/ОТРЕЗАНИЕ виден, режим — шар')
    U.tap(RAZ)
    u = U.ui()
    chk(U.v('Raz') == 2 and u.get('mode_lbl') == 'ОТРЕЗАНИЕ', f'тап — отрезание: ядро Raz={U.v("Raz")}, шапка {u.get("mode_lbl")!r}')
    chk(u.get('row3_title') == 'ШАГ ⌀' and u.get('row4_title') == 'СМЕЩЕНИЕ' and u.get('thr_type_title') == 'ПРОРЕЗ ⌀',
        f'плитки отрезания: {u.get("row3_title")!r} / {u.get("row4_title")!r} / {u.get("thr_type_title")!r}')
    U.b('AP:200'); U.b('CUT_D:900'); U.b('STAG:4'); time.sleep(0.3)
    u = U.ui()
    chk(u.get('row3_val') == '2.00' and u.get('thr_type_val') == '10' and u.get('row4_val') == '1.00',
        f'шаг 2.00, прорез ⌀10 (5 × 2), смещение 1.00: {u.get("row3_val")}, {u.get("thr_type_val")}, {u.get("row4_val")}')
    chk('5/5' in (u.get('thr_sub') or ''), f'врезаний 5/5 ({u.get("thr_sub")!r})')
    for xy, n in ((ROW3, 'ШАГ'), (ROW4, 'СМЕЩЕНИЕ'), (THR, 'ПРОРЕЗ')):
        U.tap(xy)
        chk(U.ui().get('axis_kb') == '1', f'тап «{n}» — клавиатура')
        U.e.send('<UIRESET>'); time.sleep(0.5)
    U.stm.press('JOY_RAPID'); U.keys('BTN_SELECT'); U.stm.release('JOY_RAPID'); time.sleep(0.4)
    u = U.ui()
    chk(U.v('Raz') == 1 and u.get('mode_lbl') != 'ОТРЕЗАНИЕ', f'с пульта уск+«Выбор» → шар: ядро {U.v("Raz")}, шапка {u.get("mode_lbl")!r}')
    U.b('M1')
    chk(U.ui().get('raz_btn') == '0', 'вне M6 переключателя нет')


def t_cone(U):
    print('[cone]')
    U.b('M5'); U.manual(); U.b('CONE:57'); U.b('AP:50')
    for _ in range(20):
        if U.v('Pass_Total') <= 1: break
        U.b('KEY:LEFT', 0.15)
    U.b('KEY:RIGHT', 0.15); U.b('KEY:RIGHT', 0.15)        # 3 прохода
    U.to_sm(2); time.sleep(0.3)
    u = U.ui()
    # 1:10, съём 0.50, 3 прохода: длина = 3 × 0.25 / 0.05 = 15.00 мм
    chk(u.get('row3_title') == 'ДЛ. КОНУСА' and u.get('row3_val') == '15.00', f'ДЛ. КОНУСА {u.get("row3_val")!r} (ожидалось 15.00)')
    U.to_sm(1); U.b('S1', 0.8)
    chk(U.ui().get('submode_lbl') == 'Режим невозможен!', f'конус > «Внутренний»: {U.ui().get("submode_lbl")!r}')
    U.manual()


def t_pause(U):
    print('[pause]')
    U.b('M3'); U.manual(); U.to_sm(1)
    u = U.ui()
    chk(u.get('dn_lbl') != 'ПАУЗА' and u.get('up_lbl') != 'ДАЛЕЕ', 'в «Ручном» ▲/▼ — стрелки')
    st = {'p': False, 'lbl': None, 'alert': None, 'ok': None, 'resumed': False}

    def tick(t):
        if st['lbl'] is None and t > 2:
            u = U.ui(); st['lbl'] = (u.get('up_lbl'), u.get('dn_lbl'))
        if not st['p'] and U.v('Pass_Nr') >= 2:
            st['p'] = True; U.tap(DN, 0.2)
        if st['p'] and st['alert'] is None and U.v('isPausedAfterStep'):
            time.sleep(0.6); u = U.ui(); st['alert'] = norm(u.get('alert_msg')); st['ok'] = u.get('alert_ok')
            time.sleep(1.0); U.tap(ALERT_OK, 0.3); st['resumed'] = True

    done, *_ = H.thread_cycle(U, 13, 'S3', 1, rpm=300, tick=tick)
    chk(st['lbl'] == ('ДАЛЕЕ', 'ПАУЗА'), f'в автоцикле ▲/▼ — ДАЛЕЕ/ПАУЗА ({st["lbl"]})')
    chk(st['alert'] is not None and 'ПАУЗА' in st['alert'], f'тап ПАУЗА → окно «ПАУЗА» ({st["alert"]!r})')
    chk(st['ok'] == 'ДАЛЕЕ', f'кнопка окна — ДАЛЕЕ ({st["ok"]!r})')
    chk(done, 'ДАЛЕЕ — цикл продолжился и завершился')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only')
    a = ap.parse_args()
    allt = ['sm', 'dist', 'prof', 'cut', 'cone', 'pause']
    only = a.only.split(',') if a.only else allt
    U = UI()
    U.e.send('<UIRESET>'); time.sleep(0.5)
    for n in allt:
        if n in only:
            try:
                globals()['t_' + n](U)
            except Exception as ex:
                chk(False, f'{n}: исключение {ex!r}')
                for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_RAPID'):
                    U.stm.release(p)
                U.sp.off()
            U.e.send('<UIRESET>'); time.sleep(0.4)
    U.stm.close()
    fails = [m for ok, m in res if not ok]
    print('\nИТОГ:', f'{len(res)} проверок,', 'OK' if not fails else f'{len(fails)} FAIL')
    for f in fails:
        print('  -', f)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())

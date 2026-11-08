#!/usr/bin/env python3
"""
Снимки экрана для руководства пользователя (docs/manual) — с настоящего дисплея 10.1" на стенде.

Стенд: Discovery по ST-Link (SWD: переменные, нажатия кнопок), дисплей по USB (команды, касания, снимок),
Mega — эмулятор блока DRO. Режим и подэкран ставятся командами дисплея и проверяются по переменным STM32.
Сообщения (ALERT) подаются тем же протоколом, что шлёт STM32: дисплей показывает их как настоящие.

  ~/.platformio/penv/bin/python tools/docs_gen/manual_shots.py            — все сцены
  ~/.platformio/penv/bin/python tools/docs_gen/manual_shots.py main_feed  — одна
Результат — docs/manual/raw/<сцена>.jpg (1280×800); стрелки — tools/docs_gen/annotate.py.
"""
import os, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import compare_displays as cd
import hil_cycles as hc

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
RAW = os.path.join(ROOT, 'docs', 'manual', 'raw')
ESP_PORT = '/dev/cu.usbmodem14801'
MEGA_PORT = '/dev/cu.usbserial-1410'

# Касания — в координатах базы 480×272 (снимок 1280×800: x·0.375, y·0.34)
TAP = {'mode': (54, 20), 'joy': (438, 19), 'gear': (465, 19), 'ok': (445, 152), 'up': (445, 73), 'dn': (445, 231),
       'cancel': (240, 248), 'set_close': (409, 18), 'set_mech': (126, 66), 'set_cal': (354, 66),
       'set_wifi': (126, 129), 'set_scales': (354, 129), 'pos_z': (71, 153), 'pos_x': (210, 153), 'diam': (347, 153), 'alert_ok': (382, 170), 'type': (347, 230)}


class Bench:
    def __init__(self):
        self.stm = cd.Stm32()
        self.esp = cd.Esp(ESP_PORT)
        try:
            from follow_hil import Mega
            self.mega = Mega(MEGA_PORT)
        except Exception:
            self.mega = None
        self.sp = hc.Spindle(self.stm)

    def tap(self, key, ms=120):
        x, y = TAP[key] if isinstance(key, str) else key
        self.esp.send(f'<TAP:{x},{y},{ms}>'); time.sleep(0.6 + ms / 1000)

    def btn(self, name, wait=0.5):
        self.esp.btn(name); time.sleep(wait)

    def inject(self, cmd, wait=0.6):
        self.esp.send(f'<{cmd}>'); time.sleep(wait)

    def mode(self, m, sm=1):
        """Режим M1…M8 и подэкран SM1…SM3 (кнопка «OK» на экране — следующий подэкран)."""
        self.btn(f'M{m}', 0.8)
        for _ in range(4):
            if self.stm.var('SelectMenu') == sm:
                break
            self.btn('PARAM_OK', 0.6)                  # «Выбор» — следующий подэкран
        time.sleep(0.5)

    def clear(self):
        self.inject('ALERT:0', 0.4)

    def reset(self):
        """Закрыть всё, что открыто поверх главного экрана (по флагам UIDUMP)."""
        for _ in range(4):
            ui = self.esp.uidump()
            if ui.get('alert') == '1':
                self.tap('alert_ok'); continue
            if ui.get('mode_menu') == '1':
                self.tap('cancel'); continue
            if ui.get('settings') == '1':            # настройки и подэкраны — один флаг: перебрать кнопки «ЗАКРЫТЬ»
                for key in SET_CLOSE:
                    self.tap(key)
                    if self.esp.uidump().get('settings') != '1':
                        break
                continue
            for flag, key in CLOSE.items():
                if ui.get(flag) == '1':
                    self.tap(key); break
            else:
                return

    def shot(self, name):
        time.sleep(1.2)                              # дисплей дорисовывает после полного обновления
        os.makedirs(RAW, exist_ok=True)
        tmp = os.path.join(RAW, name + '_raw.jpg')
        hc.esp_shot(self.esp, tmp)
        out = os.path.join(RAW, name + '.jpg')
        subprocess.run(['sips', '-r', '90', tmp, '--out', out], capture_output=True)
        os.unlink(tmp)
        print('снимок', name)

    def close(self):
        try:
            self.sp.off()
        except Exception:
            pass
        self.stm.close()


# Закрыть окно: флаг UIDUMP → касание (кнопка «ЗАКРЫТЬ»/«ОТМЕНА» окна)
CLOSE = {'cal': (401, 243), 'thr_type_menu': (240, 224), 'joystick': (445, 54)}   # калибровка «ЗАКРЫТЬ», тип резьбы «ОТМЕНА»
# «ЗАКРЫТЬ» подэкранов настроек: механика (внизу справа), затем экран «НАСТРОЙКИ» (вверху справа)
SET_CLOSE = [(394, 236), 'set_close']

# ── Сцены ────────────────────────────────────────────────────────────────────────────────────────────────
def s_main_feed(b):
    b.mode(1, 1); b.shot('main_feed')

def s_feed_sm2(b):
    b.mode(1, 2); b.shot('feed_sm2')

def s_feed_sm3(b):
    b.mode(1, 3); b.shot('feed_sm3')

def s_mode_menu(b):
    b.mode(1, 1); b.tap('mode'); b.shot('mode_menu'); b.tap('cancel')

def s_afeed(b):
    b.mode(2, 1); b.shot('afeed')

def s_thread(b):
    b.mode(3, 1); b.btn('THREAD_STEP:13'); b.shot('thread')

def s_thread_sm2(b):
    b.mode(3, 2); b.shot('thread_sm2')

def s_cone(b):
    b.mode(4, 1); b.shot('cone')

def s_cone_sm2(b):
    b.mode(4, 2); b.shot('cone_sm2')

def s_sphere(b):
    b.mode(6, 1); b.shot('sphere')

def s_sphere_sm2(b):
    b.mode(6, 2); b.shot('sphere_sm2')

def s_divider(b):
    b.mode(7, 1); b.shot('divider')

def s_thread_type(b):
    b.mode(3, 1); b.btn('THREAD_STEP:13'); b.tap('type'); b.shot('thread_type')

def s_limits(b):
    """Упоры правый и передний поставлены в текущей точке (кнопки упоров), подрежим «Ручной»."""
    b.mode(1, 1)
    for pin in ('LIM_RIGHT', 'LIM_FRONT'):
        b.stm.press(pin); time.sleep(0.2); b.stm.release(pin); time.sleep(0.4)
    b.shot('limits')
    for pin in ('LIM_RIGHT', 'LIM_FRONT'):                       # снять
        b.stm.press(pin); time.sleep(0.2); b.stm.release(pin); time.sleep(0.4)

def s_joystick(b):
    b.mode(1, 1); b.tap('joy'); b.shot('joystick')

def s_numpad(b):
    """Цифровая клавиатура — на поле «ШАГ ВИНТА Z» экрана механики (тот же ввод, что для осей и диаметра)."""
    b.mode(1, 1); b.tap('gear'); b.tap('set_mech'); b.tap((264, 72)); b.shot('numpad')
    b.tap((210, 234)); b.tap(MECH_CLOSE); b.tap(HUB_CLOSE)                  # «ОТМЕНА» — без ввода

# Настройки: каждая сцена закрывает свои окна сама (экран «ЛИНЕЙКИ» в UIDUMP не отмечается)
HUB_CLOSE, MECH_CLOSE, CAL_CLOSE, SCALES_CLOSE = (409, 18), (394, 236), (401, 243), (393, 237)

def s_settings(b):
    b.mode(1, 1); b.tap('gear'); b.shot('settings'); b.tap(HUB_CLOSE)

def s_set_mech(b):
    b.mode(1, 1); b.tap('gear'); b.tap('set_mech'); b.shot('set_mech'); b.tap(MECH_CLOSE); b.tap(HUB_CLOSE)

def s_set_cal(b):
    b.mode(1, 1); b.tap('gear'); b.tap('set_cal'); b.tap((262, 23)); b.shot('set_cal')     # вкладка «ПО ЛИНЕЙКАМ»
    b.tap(CAL_CLOSE); b.tap(HUB_CLOSE)

def s_set_cal_ind(b):
    b.mode(1, 1); b.tap('gear'); b.tap('set_cal'); b.tap((402, 23)); b.shot('set_cal_ind')   # «ПО ИНДИКАТОРУ»
    b.tap((262, 23)); b.tap(CAL_CLOSE); b.tap(HUB_CLOSE)

def s_set_wifi(b):
    b.mode(1, 1); b.tap('gear'); b.tap('set_wifi'); b.shot('set_wifi'); b.tap(HUB_CLOSE); b.tap(HUB_CLOSE)

def s_set_scales(b):
    b.mode(1, 1); b.tap('gear'); b.tap('set_scales'); b.shot('set_scales'); b.tap(SCALES_CLOSE); b.tap(HUB_CLOSE)

ALERTS = {
    'alert_limits': 'ALERT:1', 'alert_carriage': 'ALERT:2', 'alert_done': 'ALERT:3', 'alert_joystick': 'ALERT:4',
    # тексты — как их шлёт STM32 (Core/e2: e2_scalefix.inc, e2_core.cpp, e2_gcode.inc)
    'alert_scale_dir': 'ALERT:ПРОВЕДИТЕ ОСИ ДЖОЙСТИКОМ НА 2 ММ (НАПРАВЛЕНИЕ ЛИНЕЕК)',
    'alert_steploss': 'ALERT:ПРОПУСК ШАГОВ',
    'alert_scale_lost': 'ALERT:НЕТ СВЯЗИ С ЛИНЕЙКОЙ Z',
    'alert_scale_mech': 'ALERT:ЛИНЕЙКА Z НЕ СОВПАДАЕТ С МЕХАНИКОЙ',
    'alert_cfg': 'ALERT:ДИСПЛЕЙ НЕ СОВПАДАЕТ С КОНФИГОМ STM32',
    'alert_fix_x': 'ALERT:ДОВОДКА X НЕ ГОТОВА: ПРОВЕДИТЕ X НА 2 ММ',
}


def s_cal_error(b):
    """Калибровка при вращающемся шпинделе — отказ «ОСТАНОВИТЕ ШПИНДЕЛЬ И ДВИЖЕНИЕ» (ось не двигается)."""
    if not b.stm.var('e2_rpm'):
        print('шпиндель не вращается — «ПУСК» не нажимаю (ось поехала бы)'); return
    b.mode(1, 1); b.tap('gear'); b.tap('set_cal'); b.tap((262, 23)); b.tap((400, 129)); b.shot('cal_error')
    b.tap(CAL_CLOSE); b.tap(HUB_CLOSE)

def cal_prepare(b):
    """Калибровка: шпиндель стоит, Mega следует за моторами как блок DRO (люфт Z 80 мкм — для наглядного итога)."""
    b.sp.off(); time.sleep(1.0)
    if b.mega:
        for c in ('w0', 'kz2500', 'kx1250', 'lz80', 'lx0', 'f1', 'o'):
            b.mega.cmd(c)
    b.mode(1, 1); b.tap('gear'); b.tap('set_cal')

CAL_GO_Z, CAL_NEXT, NUMPAD_CANCEL = (389, 120), (86, 237), (210, 234)

def s_cal_run(b):
    """По линейкам: ПУСК по Z — снимок хода и итога («ПРИМЕНИТЬ» не нажимается: настройки стенда не меняются)."""
    cal_prepare(b); b.tap((262, 23)); b.tap(CAL_GO_Z); time.sleep(2.5); b.shot('cal_run')
    time.sleep(55.0)                              # 3 хода туда-обратно по 21 мм — около 40 с
    b.shot('cal_result'); b.tap(CAL_CLOSE); b.tap(HUB_CLOSE); b.sp.on(500)

def s_cal_ind(b):
    """По индикатору: выбран люфт → «ДАЛЕЕ» → ход → окно ввода показания (отмена — без расчёта)."""
    cal_prepare(b); b.tap((402, 23)); b.tap(CAL_GO_Z); time.sleep(4.0); b.shot('cal_ind_zero')
    b.tap(CAL_NEXT); time.sleep(12.0); b.shot('cal_ind_input')
    b.tap(NUMPAD_CANCEL); b.tap(CAL_CLOSE); b.tap(HUB_CLOSE); b.sp.on(500)

def make_alert(name, cmd):
    def f(b):
        b.mode(1, 1); b.inject(cmd, 1.0); b.shot(name); b.tap('alert_ok'); b.clear()
    return f

SCENES = {k[2:]: v for k, v in list(globals().items()) if k.startswith('s_')}
SCENES.update({n: make_alert(n, c) for n, c in ALERTS.items()})


def main():
    names = sys.argv[1:] or list(SCENES)
    b = Bench()
    try:
        b.btn('S2'); b.clear()
        b.sp.on(500); time.sleep(1.0)
        for n in names:
            b.reset()
            SCENES[n](b)
    finally:
        b.close()


if __name__ == '__main__':
    main()

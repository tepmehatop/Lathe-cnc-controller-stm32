#!/usr/bin/env python3
"""
Снимки экрана для docs/manual/mechanics.md: правка и сохранение механики, окна «НЕ СОХРАНЕНО», экран «ЛИНЕЙКИ».

Стенд как у manual_shots.py (дисплей 10.1" по USB, STM32 подключена). Механика в конце возвращается к
els_config.h («ИЗ КОНФИГА», 2.5 с). Снимки — прямо в docs/manual/img/ (960×600), стрелки не нужны.

  ~/.platformio/penv/bin/python tools/docs_gen/mech_shots.py
"""
import os, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import compare_displays as cd
import hil_cycles as hc

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
IMG = os.path.join(ROOT, 'docs', 'manual', 'img')
ESP_PORT = '/dev/cu.usbmodem14801'

# касания в координатах 480×272
GEAR, SET_MECH, SET_SCALES, HUB_CLOSE = (465, 19), (126, 66), (354, 129), (409, 18)
SAVE, DEFAULT, CLOSE, ENTER, OK_POPUP = (86, 237), (240, 237), (394, 236), (304, 236), (390, 241)
CELL = {'screwZ': (264, 72), 'stepsZ': (264, 104), 'mcZ': (264, 136), 'blZ': (264, 168), 'enc': (264, 200)}
KEY = {'7': (89, 97), '8': (165, 97), '9': (242, 97), '4': (89, 143), '5': (165, 143), '6': (242, 143),
       '1': (89, 189), '2': (165, 189), '3': (242, 189), '0': (318, 189), '.': (81, 235)}



class Shots:
    def __init__(self):
        self.esp = cd.Esp(ESP_PORT)
        time.sleep(1)

    def tap(self, xy, ms=120):
        self.esp.send(f'<TAP:{xy[0]},{xy[1]},{ms}>'); time.sleep(0.7 + ms / 1000)

    def shot(self, name):
        time.sleep(1.0)
        tmp = os.path.join(IMG, name + '_raw.jpg')
        hc.esp_shot(self.esp, tmp)
        out = os.path.join(IMG, name + '.jpg')
        subprocess.run(['sips', '-r', '90', tmp, '--out', out], capture_output=True)
        subprocess.run(['sips', '-z', '600', '960', out], capture_output=True)
        os.unlink(tmp)
        print('  снимок', name)

    def enter(self, cell, digits):
        self.tap(CELL[cell])
        for d in digits:
            self.tap(KEY[d])
        self.tap(ENTER)

    def mech(self):
        self.esp.send('<UIRESET>'); time.sleep(0.4)
        self.tap(GEAR); self.tap(SET_MECH)

    def close_all(self):
        self.tap(CLOSE); self.tap(HUB_CLOSE)

    def error(self, name, cell, digits):
        self.mech()
        self.enter(cell, digits)
        self.tap(SAVE); time.sleep(2.5)
        self.shot(name)
        self.tap(OK_POPUP)
        self.close_all()


def main():
    s = Shots()
    try:
        # правка и сохранение: люфт Z 50 мкм
        s.mech()
        s.enter('blZ', '50')
        s.shot('mech_edit')
        s.tap(SAVE); time.sleep(2.5)
        s.shot('mech_saved')
        s.tap(DEFAULT, 2500); time.sleep(3)              # вернуть els_config.h
        s.close_all()
        # окна «НЕ СОХРАНЕНО»
        s.error('mech_err_min_feed', 'enc', '3600')
        s.error('mech_err_cone', 'stepsZ', '188')
        s.error('mech_err_thread', 'enc', '1000')
        s.error('mech_err_mc', 'mcZ', '3')
        # «ЛИНЕЙКИ»: сохранение без изменений — STM32 перезапускается сам (окно «СОХРАНЕНО»). Поля не трогаем:
        # при источнике «ЛИНЕЙКИ» тап по «ТИП» меняет тип линейки и сохранение его записало бы.
        s.esp.send('<UIRESET>'); time.sleep(0.4)
        s.tap(GEAR); s.tap(SET_SCALES)
        s.tap(SAVE); time.sleep(3)
        s.shot('set_scales_saved')
        s.tap(OK_POPUP)
        s.close_all()
    finally:
        s.esp.send('<UIRESET>')


if __name__ == '__main__':
    main()

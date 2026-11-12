#!/usr/bin/env python3
"""
Картинки для docs/windows/README.md (прошивка и сборка под Windows).

* Окна Windows, VS Code, STM32CubeProgrammer и GitHub — СХЕМЫ окон (SVG), нарисованные по их настоящему виду:
  снять их на macOS нельзя. Номера в красных кружках совпадают с шагами в тексте. Строки терминала — из
  настоящих сборки и прошивки (stm32f407_dro, jc8012p4a1c), пути убраны.
* Код — настоящие строки из Core/els_config.h и ELS_Display/include/wifi_config.example.h (читаются при запуске),
  в виде редактора VS Code; меняемая строка подсвечена, рядом — новое значение из примера.
* esptool-js — настоящие снимки страницы https://espressif.github.io/esptool-js/ (headless Chrome); состояние
  «после Connect» воспроизведено на копии страницы. Снимки — в docs/manual/raw/ (не в git), сюда — с номерами.

  /usr/bin/python3 tools/docs_gen/windows_shots.py [--esptool]      (нужен Pillow)
"""
import html, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'docs', 'windows', 'img')
RAW = os.path.join(ROOT, 'docs', 'manual', 'raw')
CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
FONT = "Segoe UI, Tahoma, Arial, sans-serif"
MONO = "Consolas, Menlo, monospace"
RED = '#e0242b'


def esc(t):
    return html.escape(str(t), quote=True)


class Svg:
    def __init__(self, w, h, title, note=True):
        self.w, self.h, self.o = w, h, []
        self.note = note
        self.rect(0, 0, w, h, '#ffffff', '#9aa0a6', 1)
        # заголовок окна в стиле Windows 11
        self.rect(0, 0, w, 32, '#f3f3f3')
        self.text(14, 21, title, 13, '#202020')
        for i, g in enumerate(('—', '☐', '✕')):
            self.text(w - 120 + i * 40, 21, g, 13, '#404040', anchor='middle')

    def rect(self, x, y, w, h, fill='none', stroke='none', sw=1, r=0):
        self.o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}" '
                      f'stroke-width="{sw}"/>')

    def line(self, x1, y1, x2, y2, c='#c8c8c8', sw=1):
        self.o.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{c}" stroke-width="{sw}"/>')

    def text(self, x, y, t, size=13, color='#202020', bold=False, anchor='start', mono=False, italic=False):
        extra = (' font-weight="bold"' if bold else '') + (' font-style="italic"' if italic else '')
        self.o.append(f'<text x="{x}" y="{y}" font-family="{MONO if mono else FONT}" font-size="{size}" '
                      f'fill="{color}" text-anchor="{anchor}"{extra} xml:space="preserve">{esc(t)}</text>')

    def btn(self, x, y, w, h, label, fill='#0067c0', color='#ffffff', size=13, r=4, stroke='none'):
        self.rect(x, y, w, h, fill, stroke, 1, r)
        self.text(x + w / 2, y + h / 2 + size * 0.36, label, size, color, anchor='middle')

    def box(self, x, y, w, h, sw=3):
        """Красная рамка вокруг того, куда нажимать."""
        self.rect(x - 3, y - 3, w + 6, h + 6, 'none', RED, sw, 4)

    def mark(self, x, y, n):
        self.o.append(f'<circle cx="{x}" cy="{y}" r="13" fill="{RED}" stroke="#ffffff" stroke-width="2"/>')
        self.text(x, y + 5, n, 14, '#ffffff', bold=True, anchor='middle')

    def save(self, name):
        if self.note:
            self.text(self.w - 8, self.h - 6, 'схема окна (рисунок), не снимок экрана', 10, '#9aa0a6', anchor='end')
        body = '\n'.join(self.o)
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
               f'viewBox="0 0 {self.w} {self.h}">\n{body}\n</svg>\n')
        with open(os.path.join(OUT, name), 'w', encoding='utf-8') as f:
            f.write(svg)
        print('  ', name)


# ── Windows ──────────────────────────────────────────────────────────────────

def device_manager():
    s = Svg(760, 470, 'Диспетчер устройств')
    s.text(14, 54, 'Файл    Действие    Вид    Справка', 13, '#303030')
    s.line(0, 66, 760, 66)
    rows = [
        (0, '▸  Аудиовходы и аудиовыходы', None),
        (0, '▸  Видеоадаптеры', None),
        (0, '▸  Дисковые устройства', None),
        (0, '▾  Порты (COM и LPT)', None),
        (1, 'Устройство с последовательным интерфейсом USB (COM5)', '1'),
        (0, '▸  Процессоры', None),
        (0, '▾  Устройства USB', None),
        (1, 'STM32 STLink', '2'),
        (0, '▸  Устройства HID (Human Interface Devices)', None),
    ]
    y = 92
    for lvl, t, m in rows:
        x = 24 + lvl * 34
        if lvl:
            s.rect(x, y - 13, 16, 16, '#dfe8f5', '#7c93b8', 1, 2)
        s.text(x + (24 if lvl else 0), y, t, 14, '#202020')
        if m:
            w = 9 * len(t)
            s.box(x - 2, y - 17, w + 30, 24)
            s.mark(x + w + 46, y - 5, m)
        y += 30
    s.rect(24, y + 6, 712, 66, '#fff8e1', '#e0b000', 1, 4)
    s.text(36, y + 30, '⚠  «Другие устройства» → «STM32 STLink» с жёлтым треугольником — драйвер ST-Link не установлен', 13)
    s.text(36, y + 54, '    (шаг 1.2). Порт дисплея без треугольника и с номером COM — драйвер не нужен.', 13)
    s.save('dm_ports.svg')


def env_var():
    s = Svg(560, 250, 'Новая пользовательская переменная')
    s.text(24, 76, 'Имя переменной:', 14)
    s.rect(170, 58, 360, 28, '#ffffff', '#8a8a8a')
    s.text(178, 77, 'PLATFORMIO_CORE_DIR', 14, mono=True)
    s.mark(510, 72, '1')
    s.text(24, 126, 'Значение переменной:', 14)
    s.rect(170, 108, 360, 28, '#ffffff', '#8a8a8a')
    s.text(178, 127, 'C:\\.platformio', 14, mono=True)
    s.mark(510, 122, '2')
    s.btn(330, 190, 96, 30, 'ОК')
    s.box(330, 190, 96, 30)
    s.mark(316, 205, '3')
    s.btn(436, 190, 96, 30, 'Отмена', '#fbfbfb', '#202020', stroke='#c0c0c0')
    s.save('win_envvar.svg')


# ── GitHub ───────────────────────────────────────────────────────────────────

def browser(title, url, h):
    s = Svg(900, h, title)
    s.rect(0, 32, 900, 40, '#f8f8f8')
    s.rect(110, 40, 680, 26, '#ffffff', '#d0d0d0', 1, 13)
    s.text(126, 58, url, 13, '#505050')
    s.text(20, 58, '←   →   ⟳', 14, '#606060')
    s.line(0, 72, 900, 72)
    return s


def gh_release():
    s = browser('Releases — Google Chrome', 'github.com/<автор>/Lathe-cnc-controller-stm32/releases/latest', 640)
    s.text(40, 112, '<автор> / Lathe-cnc-controller-stm32', 16, '#0969da')
    s.text(40, 150, 'v0.3.0', 26, '#1f2328', bold=True)
    s.btn(150, 128, 56, 22, 'Latest', '#ffffff', '#1a7f37', 12, 11, '#1a7f37')
    s.text(40, 178, 'Что нового … Файлы … Как прошить (то же, что docs/RELEASE.md)', 13, '#59636e')
    s.rect(40, 196, 820, 400, '#ffffff', '#d1d9e0', 1, 6)
    s.text(56, 224, '▾ Assets  9', 15, '#1f2328', bold=True)
    files = [('els-display-jc1060p470.bin', '2.08 MB', None), ('els-display-jc1060p470_v2.bin', '2.08 MB', None),
             ('els-display-jc1060p470_v3.bin', '2.08 MB', None), ('els-display-jc4827w543.bin', '1.61 MB', None),
             ('els-display-jc8012p4a1c.bin', '2.51 MB', '2'), ('els-stm32-dro.bin', '153 KB', '1'),
             ('SHA256SUMS.txt', '0.5 KB', None), ('Source code (zip)', '', None), ('Source code (tar.gz)', '', None)]
    y = 258
    for name, size, m in files:
        s.line(40, y - 22, 860, y - 22, '#d1d9e0')
        s.text(60, y, '◫  ' + name, 14, '#0969da')
        s.text(840, y, size, 13, '#59636e', anchor='end')
        if m:
            s.box(56, y - 18, 9 * len(name) + 30, 26)
            s.mark(9 * len(name) + 110, y - 5, m)
        y += 38
    s.save('gh_release.svg')


def gh_zip():
    s = browser('Lathe-cnc-controller-stm32 — Google Chrome', 'github.com/<автор>/Lathe-cnc-controller-stm32', 420)
    s.text(40, 112, '<автор> / Lathe-cnc-controller-stm32', 16, '#0969da')
    s.text(40, 150, '⑂ main ▾      Go to file', 13, '#1f2328')
    s.btn(640, 132, 90, 30, '<> Code ▾', '#1f883d', '#ffffff', 13, 6)
    s.box(640, 132, 90, 30)
    s.mark(626, 147, '1')
    s.rect(470, 172, 400, 210, '#ffffff', '#d1d9e0', 1, 8)
    s.text(490, 200, 'Local      Codespaces', 13, '#1f2328', bold=True)
    s.text(490, 232, 'Clone  HTTPS', 13, '#1f2328')
    s.rect(490, 244, 360, 26, '#f6f8fa', '#d1d9e0', 1, 4)
    s.text(498, 262, 'https://github.com/<автор>/Lathe-cnc-controller-stm32.git', 11, '#59636e', mono=True)
    s.line(470, 300, 870, 300, '#d1d9e0')
    s.text(490, 344, '⇩   Download ZIP', 14, '#1f2328')
    s.box(486, 324, 150, 28)
    s.mark(660, 338, '2')
    s.rect(40, 180, 400, 200, '#f6f8fa', '#d1d9e0', 1, 6)
    for i, t in enumerate(('Core', 'Drivers', 'ELS_Display', 'ELS_Display_P4', 'ELS_Display_P7', 'docs', 'platformio.ini')):
        s.text(60, 210 + i * 24, ('📁 ' if '.' not in t else '📄 ') + t, 13, '#1f2328')
    s.save('gh_zip.svg')


# ── STM32CubeProgrammer ──────────────────────────────────────────────────────

def cubeprog():
    s = Svg(980, 560, 'STM32CubeProgrammer')
    s.rect(0, 32, 54, 528, '#03234b')
    icons = ['▤', '⇩', 'OB', 'CPU', 'SWV', '⚙']
    for i, ic in enumerate(icons):
        y = 70 + i * 56
        if i == 1:
            s.rect(4, y - 24, 46, 46, '#3cb4e6', 'none', 1, 4)
            s.box(4, y - 24, 46, 46)
            s.mark(70, y - 2, '3')
        s.text(27, y + 5, ic, 15 if len(ic) == 1 else 11, '#ffffff', bold=True, anchor='middle')
    # правая панель подключения
    s.rect(740, 32, 240, 528, '#f2f5f8')
    s.btn(760, 52, 120, 30, 'ST-LINK ▾', '#ffffff', '#03234b', 13, 4, '#8aa0b8')
    s.box(760, 52, 120, 30)
    s.mark(900, 67, '1')
    s.btn(760, 96, 200, 34, 'Connect', '#3cb46e', '#ffffff', 14, 4)
    s.box(760, 96, 200, 34)
    s.mark(746, 113, '2')
    s.text(760, 158, 'ST-LINK configuration', 12, '#03234b', bold=True)
    for i, (k, v) in enumerate((('Port', 'SWD'), ('Frequency (kHz)', '4000'), ('Mode', 'Normal'), ('Reset mode', 'Software reset'))):
        s.text(760, 186 + i * 28, k, 12, '#404040')
        s.text(960, 186 + i * 28, v, 12, '#202020', anchor='end')
    s.text(760, 320, '● Connected', 13, '#3cb46e', bold=True)
    s.text(760, 344, 'Target: STM32F405xx/F407xx', 12, '#404040')
    # центральная часть: Erasing & programming
    s.text(80, 66, 'Erasing & programming', 18, '#03234b', bold=True)
    s.text(80, 104, 'Download', 14, '#03234b', bold=True)
    s.text(80, 136, 'File path', 13)
    s.rect(180, 120, 400, 26, '#ffffff', '#8aa0b8')
    s.text(188, 138, 'C:\\els\\els-stm32-dro.bin', 13, mono=True)
    s.btn(590, 120, 90, 26, 'Browse', '#ffffff', '#03234b', 13, 3, '#8aa0b8')
    s.box(180, 120, 500, 26)
    s.mark(700, 133, '4')
    s.text(80, 178, 'Start address', 13)
    s.rect(180, 162, 200, 26, '#ffffff', '#8aa0b8')
    s.text(188, 180, '0x08000000', 13, mono=True)
    s.box(180, 162, 200, 26)
    s.mark(400, 175, '5')
    s.text(80, 216, '☑ Skip flash erase before programming', 13, '#909090')
    s.text(80, 242, '☑ Verify programming', 13)
    s.text(80, 268, '☑ Run after programming', 13)
    s.box(76, 226, 230, 48)
    s.mark(326, 250, '6')
    s.btn(80, 292, 180, 34, 'Start Programming', '#3cb4e6', '#ffffff', 14, 4)
    s.box(80, 292, 180, 34)
    s.mark(280, 309, '7')
    # журнал
    s.rect(70, 360, 660, 170, '#ffffff', '#c8d2dc')
    log = ['Memory Programming ...', 'Opening and parsing file: els-stm32-dro.bin',
           '  File          : els-stm32-dro.bin', '  Address       : 0x08000000',
           'Erasing memory corresponding to segment 0:', 'Download in Progress:',
           'File download complete', 'Download verified successfully']
    for i, t in enumerate(log):
        s.text(80, 382 + i * 19, t, 12, '#3cb46e' if 'complete' in t or 'success' in t else '#404040', mono=True)
    s.box(76, 495, 280, 22)
    s.mark(376, 506, '8')
    s.save('cubeprog.svg')


# ── VS Code ──────────────────────────────────────────────────────────────────

ACT = ['⧉', '⌕', '⑂', '▷', '⊞', '👽']   # проводник, поиск, git, запуск, расширения, PlatformIO


def vscode(title, w=1000, h=560, act=None, pio=True):
    s = Svg(w, h, title)
    s.rect(0, 32, w, h - 32, '#1f1f1f')
    s.rect(0, 32, 48, h - 54, '#181818')
    icons = ACT if pio else ACT[:-1]
    for i, ic in enumerate(icons):
        y = 66 + i * 48
        if act == i:
            s.rect(0, y - 22, 3, 40, '#0078d4')
        s.text(24, y + 5, ic, 20, '#ffffff' if act == i else '#858585', anchor='middle')
    s.rect(0, h - 22, w, 22, '#181818')
    return s


def vsc_ext():
    s = vscode('Extensions — Visual Studio Code', act=4, pio=False)
    s.mark(70, 258, '1')
    s.box(4, 236, 40, 40)
    s.rect(48, 32, 330, 506, '#181818')
    s.text(64, 58, 'РАСШИРЕНИЯ: МАГАЗИН', 11, '#cccccc')
    s.rect(64, 72, 298, 26, '#313131', '#0078d4')
    s.text(72, 90, 'platformio', 13, '#ffffff')
    s.box(64, 72, 298, 26)
    s.mark(392, 85, '2')
    s.rect(56, 112, 314, 70, '#04395e')
    s.rect(66, 122, 46, 46, '#f5821f', 'none', 1, 6)
    s.text(89, 153, '👽', 24, '#ffffff', anchor='middle')
    s.text(122, 138, 'PlatformIO IDE', 14, '#ffffff', bold=True)
    s.text(122, 157, 'Professional development environment…', 11, '#cccccc')
    s.text(122, 175, 'PlatformIO   ✓', 11, '#3794ff')
    for i, (t, d) in enumerate((('C/C++', 'C/C++ IntelliSense, debugging…'), ('Python', 'Python language support…'))):
        s.text(122, 214 + i * 64, t, 14, '#cccccc', bold=True)
        s.text(122, 233 + i * 64, d, 11, '#9d9d9d')
    s.text(400, 80, 'PlatformIO IDE', 26, '#ffffff', bold=True)
    s.text(400, 106, 'PlatformIO  |  ⇩ 4 000 000+  |  ★★★★★', 13, '#9d9d9d')
    s.text(400, 132, 'Your Gateway to Embedded Software Development Excellence', 13, '#cccccc')
    s.btn(400, 150, 90, 28, 'Install', '#0078d4', '#ffffff', 13, 2)
    s.box(400, 150, 90, 28)
    s.mark(512, 164, '3')
    s.text(400, 220, 'Установка — 1–2 минуты. Затем PlatformIO сам скачает свой Python и инструменты', 13, '#9d9d9d')
    s.text(400, 240, '(справа внизу — «Installing PlatformIO IDE Core…»), это ещё 5–10 минут.', 13, '#9d9d9d')
    s.save('vsc_ext.svg')


def vsc_reload():
    s = vscode('Visual Studio Code', act=5)
    s.box(4, 284, 40, 40)
    s.mark(70, 304, '1')
    s.text(300, 200, 'После установки слева появляется значок PlatformIO (голова пришельца)', 14, '#cccccc')
    s.rect(560, 420, 420, 100, '#252526', '#454545', 1, 4)
    s.text(576, 448, 'ⓘ  PlatformIO: Please restart VSCode to apply the changes.', 13, '#cccccc')
    s.btn(820, 476, 140, 28, 'Reload Now', '#0078d4', '#ffffff', 13, 2)
    s.box(820, 476, 140, 28)
    s.mark(800, 490, '2')
    s.save('vsc_reload.svg')


def vsc_open():
    s = vscode('Visual Studio Code', act=0)
    s.rect(48, 32, 952, 24, '#1f1f1f')
    s.text(60, 49, 'Файл    Правка    Выделение    Вид    …', 12, '#cccccc')
    s.rect(54, 56, 330, 190, '#252526', '#454545')
    items = [('Новый текстовый файл', 'Ctrl+N'), ('Новое окно', 'Ctrl+Shift+N'), ('—', ''),
             ('Открыть файл…', 'Ctrl+O'), ('Открыть папку…', 'Ctrl+K Ctrl+O'), ('Открыть последние', '▸')]
    for i, (t, k) in enumerate(items):
        y = 82 + i * 27
        if t == '—':
            s.line(64, y - 6, 374, y - 6, '#454545')
            continue
        s.text(70, y, t, 13, '#cccccc')
        s.text(372, y, k, 12, '#9d9d9d', anchor='end')
        if t.startswith('Открыть папку'):
            s.box(60, y - 17, 318, 24)
            s.mark(398, y - 5, '1')
    s.rect(48, 270, 300, 268, '#181818')
    s.text(60, 292, 'ПРОВОДНИК', 11, '#cccccc')
    s.text(60, 316, '▾ ELS', 12, '#cccccc', bold=True)
    tree = ['▸ Core', '▸ Drivers', '▸ ELS_Display', '▸ ELS_Display_P4', '▸ ELS_Display_P7', '▸ docs', '▸ tools',
            '  platformio.ini', '  README.md']
    for i, t in enumerate(tree):
        s.text(72, 340 + i * 21, t, 12, '#cccccc')
    s.box(66, 322, 270, 196)
    s.mark(360, 420, '2')
    s.text(420, 340, 'Папка C:\\els — та, где лежит platformio.ini.', 14, '#cccccc')
    s.text(420, 364, 'При первом открытии PlatformIO скачивает компиляторы', 14, '#cccccc')
    s.text(420, 388, 'для STM32 и ESP32 (около 2 ГБ, 10–30 минут) — внизу', 14, '#cccccc')
    s.text(420, 412, 'бежит строка «Configuring project…». Дождитесь конца.', 14, '#cccccc')
    s.save('vsc_open.svg')


def vsc_env():
    s = vscode('ELS — Visual Studio Code', act=0)
    s.rect(250, 40, 520, 330, '#252526', '#454545', 1, 6)
    s.text(266, 64, 'Select Project Environment', 13, '#cccccc')
    envs = [('Default (stm32f407_discovery)', ''), ('env:stm32f407_discovery', 'STM32, без линеек'),
            ('env:stm32f407_dro', 'STM32, блок DRO'), ('env:stm32f407_scales', 'STM32, линейки напрямую'),
            ('env:jc4827w543', 'дисплей 4.3"'), ('env:jc8012p4a1c', 'дисплей 10.1"'),
            ('env:jc1060p470', 'дисплей 7" (без «V2»)'), ('env:jc1060p470_v2', 'дисплей 7" «V2»'),
            ('env:jc1060p470_v3', 'дисплей 7" «V2», ESP32-P4 rev 3'), ('env:dro_simulator', 'стенд, не нужен')]
    for i, (e, d) in enumerate(envs):
        y = 92 + i * 27
        if e == 'env:stm32f407_dro':
            s.rect(252, y - 18, 516, 26, '#04395e')
            s.mark(790, y - 5, '2')
        s.text(266, y, e, 13, '#ffffff', mono=True)
        s.text(756, y, d, 12, '#9d9d9d', anchor='end')
    s.rect(48, 538, 952, 22, '#0078d4')
    s.text(60, 553, '⌂   ✓   →   🗑   ⚗   🔌   ▭', 13, '#ffffff')
    s.text(300, 553, '⚙ env:stm32f407_discovery (ELS)', 12, '#ffffff')
    s.box(296, 538, 250, 20, 2)
    s.mark(566, 548, '1')
    s.save('vsc_env.svg')


def vsc_statusbar():
    s = Svg(900, 200, 'Строка состояния VS Code (внизу окна)')
    s.rect(0, 32, 900, 168, '#1f1f1f')
    s.rect(0, 110, 900, 34, '#0078d4')
    items = [('⌂', 'Home'), ('✓', 'Build'), ('→', 'Upload'), ('🗑', 'Clean'), ('⚗', 'Test'), ('🔌', 'Monitor'),
             ('▭', 'Terminal')]
    for i, (g, n) in enumerate(items):
        x = 30 + i * 52
        s.text(x, 133, g, 18, '#ffffff', anchor='middle')
        s.text(x, 166, n, 11, '#cccccc', anchor='middle')
    s.box(66, 114, 28, 26, 2)
    s.mark(80, 92, '1')
    s.box(118, 114, 28, 26, 2)
    s.mark(132, 92, '2')
    s.text(400, 133, '⚙ env:stm32f407_dro (ELS)', 14, '#ffffff')
    s.box(394, 114, 220, 26, 2)
    s.mark(630, 127, '3')
    s.text(20, 190, '1 — собрать, 2 — собрать и прошить, 3 — что собирать (env). Monitor — сообщения платы.', 12, '#cccccc')
    s.save('vsc_statusbar.svg')


def terminal(name, title, lines, marks):
    s = vscode(title, h=120 + 21 * len(lines), act=5)
    s.rect(48, 52, 952, 20 + 21 * len(lines), '#181818')
    s.text(60, 70, 'ТЕРМИНАЛ', 11, '#e7e7e7')
    for i, t in enumerate(lines):
        y = 96 + i * 21
        c = '#cccccc'
        if 'SUCCESS' in t or 'Verified' in t or 'verified' in t:
            c = '#23d18b'
        s.text(64, y, t, 12, c, mono=True)
        if i in marks:
            s.box(60, y - 15, 8 * len(t) + 8, 20, 2)
            s.mark(8 * len(t) + 96, y - 5, marks[i])
    s.save(name)


def terminals():
    terminal('vsc_upload_stm.svg', 'Сборка и прошивка STM32 — Visual Studio Code', [
        ' *  Executing task: platformio run --target upload --environment stm32f407_dro',
        'Processing stm32f407_dro (platform: ststm32@19.5.0; board: disco_f407vg; framework: arduino)',
        'RAM:   [=         ]  10.0% (used 13052 bytes from 131072 bytes)',
        'Flash: [=         ]  14.9% (used 155900 bytes from 1048576 bytes)',
        'Configuring upload protocol...',
        'Uploading .pio\\build\\stm32f407_dro\\firmware.elf',
        '** Programming Started **',
        '** Programming Finished **',
        '** Verified OK **',
        '========================= [SUCCESS] Took 13.00 seconds =========================',
    ], {8: '1', 9: '2'})
    terminal('vsc_upload_disp.svg', 'Сборка и прошивка дисплея — Visual Studio Code', [
        ' *  Executing task: platformio run --target upload --environment jc8012p4a1c',
        'Processing jc8012p4a1c (… board: esp32-p4; framework: arduino)',
        'RAM:   [==        ]  20.8% (used 68236 bytes from 327680 bytes)',
        'Flash: [========  ]  80.3% (used 2526022 bytes from 3145728 bytes)',
        'Configuring upload protocol...',
        'Auto-detected: COM5',
        'Connecting...',
        'Chip type:          ESP32-P4 (revision v1.3)',
        'Wrote 2567024 bytes (1019189 compressed) at 0x00010000 in 13.1 seconds (1571.4 kbit/s).',
        'Hash of data verified.',
        'Hard resetting via RTS pin...',
        '========================= [SUCCESS] Took 28.57 seconds =========================',
    ], {5: '1', 7: '2', 11: '3'})


# ── Код: настоящие строки из файлов проекта ─────────────────────────────────

def code(name, path, first, last, change, caption, shown=None):
    """change: {номер строки: (новый текст строки, пояснение)}."""
    src = open(os.path.join(ROOT, path), encoding='utf-8').read().split('\n')
    lines = list(range(first, last + 1))
    for n in change:
        assert first <= n <= last, (name, n)
    maxlen = max(len(src[n - 1]) for n in lines)
    notes = max(len(src[n - 1].split('//')[0]) + len(change[n][0].split('//')[0]) + len(change[n][1]) for n in change)
    w = max(760, min(1180, 70 + 7.3 * maxlen), min(1180, 120 + 7.6 * notes))
    shown = shown or path
    h = 106 + 21 * len(lines) + 8 + 21 * len(change) + 56
    s = vscode(os.path.basename(shown) + ' — Visual Studio Code', w=int(w), h=h, act=0)
    s.rect(48, 32, w - 48, 30, '#181818')
    s.rect(48, 32, 12 + 7.5 * len(os.path.basename(shown)) + 20, 30, '#1f1f1f')
    s.text(60, 52, os.path.basename(shown) + ' ●', 12, '#ffffff')
    s.text(60, 80, shown.replace('/', ' › '), 11, '#9d9d9d')
    for i, n in enumerate(lines):
        y = 106 + i * 21
        t = src[n - 1]
        if n in change:
            t = change[n][0]
            s.rect(48, y - 15, w - 48, 21, '#264f78')
        s.text(92, y, str(n), 12, '#6e7681', anchor='end', mono=True)
        col = '#6a9955' if t.lstrip().startswith(('//', '/*', '*')) else (
            '#c586c0' if t.lstrip().startswith('#') else '#cccccc')
        s.text(104, y, t.expandtabs(4), 12.5, col, mono=True)
        if n in change:
            s.mark(w - 24, y - 5, '✎')
    y = 106 + len(lines) * 21 + 8
    for n in sorted(change):
        old = re.sub(r'\s+', ' ', src[n - 1].split('//')[0]).strip()
        new = re.sub(r'\s+', ' ', change[n][0].split('//')[0]).strip()
        s.text(60, y, f'✎ строка {n}: {old}  →  {new}   — {change[n][1]}', 12.5, '#ffcc66', mono=True)
        y += 21
    s.text(60, y + 8, caption, 13, '#ffcc66', bold=True)
    s.note = False
    s.text(w - 8, h - 28, 'вид в редакторе; строки — из файла проекта', 10, '#6e7681', anchor='end')
    s.save(name)


def find(path, pattern):
    for i, l in enumerate(open(os.path.join(ROOT, path), encoding='utf-8').read().split('\n'), 1):
        if re.search(pattern, l):
            return i
    raise SystemExit(f'{path}: нет строки {pattern}')


def sub(path, n, old, new):
    t = open(os.path.join(ROOT, path), encoding='utf-8').read().split('\n')[n - 1]
    assert old in t, (path, n, old, t)
    return t.replace(old, new)


def codes():
    C = 'Core/els_config.h'
    n = find(C, r'^#define ELS_POS_SOURCE\s')
    code('code_pos_source.svg', C, n - 10, n, {n: (sub(C, n, 'POS_SOURCE_STEPS', 'POS_SOURCE_DRO_UART'),
                                                     'блок DRO')}, 'Пример 1: координаты от блока DRO — или env stm32f407_dro')
    n = find(C, r'^#define SCREW_Z\s')
    nx = find(C, r'^#define SCREW_X\s')
    code('code_mech.svg', C, n - 2, nx + 1, {n: (sub(C, n, '200      //', '300      //'), 'винт Z 3 мм'),
                                            nx: (sub(C, nx, '100      //', '125      //'), 'винт X 1.25 мм')},
         'Пример 2: винты 3 мм и 1.25 мм (то же — на экране: НАСТРОЙКИ → МЕХАНИКА)')
    n = find(C, r'^#define DRIVER_SIGNALS_DIRECT\s')
    code('code_driver.svg', C, n - 7, n + 1, {n: (sub(C, n, '0', '1'), 'напрямую от STM32')},
         'Пример 3: драйверы подключены напрямую, без транзисторов (только на экране не меняется)')
    n = find(C, r'^#define BEEPER_ON_LEVEL\s')
    code('code_beeper.svg', C, n - 3, n + 1, {n: (sub(C, n, '1', '0'), 'зуммер как у 7e2')},
         'Пример 4: зуммер включён как у автора 7e2 — между +3.3 В и выводом')
    n = find(C, r'^#define HC_X_DIR\s')
    code('code_hc.svg', C, n, n + 1, {n: (sub(C, n, '1        //', '0        //'), 'РГИ X в другую сторону')},
         'Пример 5: ручной энкодер (РГИ) крутит поперечку не в ту сторону')
    n = find(C, r'^#define MIN_FEED\s')
    np_ = find(C, r'^#define PASS_FINISH\s')
    code('code_feed.svg', C, n, np_, {n + 1: (sub(C, n + 1, '25 ', '30 '), 'макс. подача 0.30'),
                                     np_: (sub(C, np_, '2 ', '3 '), '3 чистовых прохода')},
         'Пример 6: подача до 0.30 мм/об и 3 чистовых прохода резьбы')
    n = find(C, r'^#define USE_FEED_POT\s')
    code('code_pot.svg', C, n - 5, n, {n: (sub(C, n, '0', '1'), 'ручка подачи на пульте')},
         'Пример 7: переменный резистор подачи на пульте, как у 7e2 (вход PC1)')
    n = find(C, r'^#define ELS_DISPLAY_TYPE\s')
    code('code_display.svg', C, n - 7, n, {n: (sub(C, n, 'DISPLAY_TYPE_10_1', 'DISPLAY_TYPE_4_3'), 'дисплей 4.3"')},
         'Пример 8: какой дисплей подключён (влияет только на проверку при включении)')
    W = 'ELS_Display/include/wifi_config.example.h'
    a = find(W, r'^#define WIFI_STA_SSID')
    code('code_wifi.svg', W, a - 2, a + 1, {a: (sub(W, a, '""', '"MyHomeWiFi"'), 'сеть роутера'),
                                           a + 1: (sub(W, a + 1, '""', '"пароль_роутера"'), 'пароль')},
         'Пример 9: Wi-Fi в прошивке дисплея — копия шаблона под именем wifi_config.h (проще — на экране)',
         shown='ELS_Display/include/wifi_config.h')


# ── esptool-js: настоящие снимки ─────────────────────────────────────────────

def esptool_shots():
    """Снимки страницы и копии «после Connect» → docs/manual/raw (не в git)."""
    import shutil, tempfile, urllib.request
    os.makedirs(RAW, exist_ok=True)
    url = 'https://espressif.github.io/esptool-js/'
    subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--window-size=1200,560',
                    '--virtual-time-budget=9000', f'--screenshot={RAW}/esptool_connect.png', url], capture_output=True)
    s = urllib.request.urlopen(url, timeout=30).read().decode('utf-8')
    s = s.replace('<head>', f'<head><base href="{url}">', 1)
    s = re.sub(r'<script src="typescript[^>]*></script>', '', s)
    s = s.replace('<label for="baudrates" id="lblBaudrate">Baudrate:</label>', '')
    s = s.replace('<label style="display: none" id="lblConnTo">Connected to device: </label>',
                  '<label id="lblConnTo">Connected to device: ESP32-P4 (revision v1.3)</label>')
    s = re.sub(r'<select name="baudrates" id="baudrates">.*?</select>', '', s, flags=re.S)
    s = s.replace('<input class="btn btn-info btn-sm" type="button" id="connectButton" value="Connect">', '')
    s = s.replace('<input class="btn btn-info btn-sm" type="button" id="copyTraceButton" value="Copy Trace">', '')
    s = s.replace('<tbody id="tableBody">', '<tbody id="tableBody"><tr><td><input type="text" value="0x0" '
                  'style="width:110px"></td><td><span style="border:1px solid #aaa;padding:2px 6px;background:#eee;'
                  'border-radius:3px">Выберите файл</span> els-display-jc8012p4a1c.bin</td><td><input class="btn '
                  'btn-danger btn-sm" type="button" value="Remove"></td></tr>')
    i, j = s.index('<div id="console">'), s.index('</body>')
    s = s[:i] + '</div></div>' + s[j:]
    d = tempfile.mkdtemp()
    p = os.path.join(d, 'after.html')
    open(p, 'w', encoding='utf-8').write(s)
    subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--window-size=1200,800',
                    '--virtual-time-budget=6000', f'--screenshot={RAW}/esptool_program.png', 'file://' + p],
                   capture_output=True)
    shutil.rmtree(d)


def annotate_png(src, dst, marks, crop=None):
    from PIL import Image, ImageDraw, ImageFont
    im = Image.open(src).convert('RGB')
    if crop:
        im = im.crop(crop)
    d = ImageDraw.Draw(im)
    try:
        f = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial Bold.ttf', 20)
    except OSError:
        f = ImageFont.load_default()
    for n, (x0, y0, x1, y1), (cx, cy) in marks:
        d.rounded_rectangle([x0, y0, x1, y1], 5, outline=RED, width=4)
        d.ellipse([cx - 16, cy - 16, cx + 16, cy + 16], fill=RED, outline='white', width=2)
        d.text((cx, cy), n, fill='white', font=f, anchor='mm')
    im.save(os.path.join(OUT, dst), optimize=True)
    print('  ', dst)


def esptool_png():
    annotate_png(os.path.join(RAW, 'esptool_connect.png'), 'esptool_connect.png',
                 [('1', (26, 310, 172, 336), (195, 323)), ('2', (26, 353, 102, 391), (125, 372))], crop=(0, 0, 1200, 420))
    annotate_png(os.path.join(RAW, 'esptool_program.png'), 'esptool_program.png',
                 [('3', (26, 310, 345, 334), (370, 322)), ('4', (34, 429, 152, 463), (175, 446)),
                  ('5', (308, 427, 618, 459), (640, 443)), ('6', (26, 718, 102, 756), (125, 737))],
                 crop=(0, 260, 1200, 780) if False else None)


def main():
    os.makedirs(OUT, exist_ok=True)
    if '--esptool' in sys.argv:
        esptool_shots()
    device_manager(); env_var(); gh_release(); gh_zip(); cubeprog()
    vsc_ext(); vsc_reload(); vsc_open(); vsc_env(); vsc_statusbar(); terminals(); codes()
    if os.path.exists(os.path.join(RAW, 'esptool_program.png')):
        esptool_png()


if __name__ == '__main__':
    main()

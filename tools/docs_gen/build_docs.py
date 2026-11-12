#!/usr/bin/env python3
"""
Сборка документации: схемы (SVG), таблица выводов и docs/index.html.
Запуск после любой смены пинов или схем: python3 tools/docs_gen/build_docs.py
"""
import html, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

for script in ('scales_svg.py', 'wiring_svg.py', 'shield_docs.py', 'wiring_html.py'):
    subprocess.run([sys.executable, os.path.join(HERE, script)], check=True)

# Таблица выводов: markdown → HTML
md = open(os.path.join(ROOT, 'docs', 'wiring', 'pins.md'), encoding='utf-8').read()
rows = [l for l in md.splitlines() if l.startswith('|') and not l.startswith('|---')]
out = ['<table>']
for i, l in enumerate(rows):
    cells = [c.strip() for c in l.strip('|').split('|')]
    tag = 'th' if i == 0 else 'td'
    cells = [re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', html.escape(c)) for c in cells]
    out.append('<tr>' + ''.join(f'<{tag}>{c}</{tag}>' for c in cells) + '</tr>')
out.append('</table>')

tpl = open(os.path.join(HERE, 'index_template.html'), encoding='utf-8').read()
open(os.path.join(ROOT, 'docs', 'index.html'), 'w', encoding='utf-8').write(tpl.replace('{{PINS_TABLE}}', '\n'.join(out)))
print('docs/index.html готов')

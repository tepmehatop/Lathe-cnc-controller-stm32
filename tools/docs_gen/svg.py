"""Мини-библиотека рисования схем в SVG для документации (docs/)."""
import os

class S:
    def __init__(self, w, h, title, out):
        self.w, self.h, self.e, self.out = w, h, [], out
        self.text(w / 2, 26, title, size=17, bold=True, anchor='middle')
    def line(self, x1, y1, x2, y2, dash=False, color='#222'):
        d = ' stroke-dasharray="5,4"' if dash else ''
        self.e.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="2"{d}/>')
    def poly(self, pts):
        p = ' '.join(f'{x},{y}' for x, y in pts)
        self.e.append(f'<polyline points="{p}" fill="none" stroke="#222" stroke-width="2"/>')
    def box(self, x, y, w, h, label, sub='', fill='#eef4fb'):
        self.e.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="{fill}" stroke="#335" stroke-width="2"/>')
        self.text(x + w / 2, y + 22, label, bold=True, anchor='middle')
        for i, t in enumerate(sub.split('\n') if sub else []):
            self.text(x + w / 2, y + 42 + i * 17, t, size=12, anchor='middle', color='#333')
    def text(self, x, y, t, size=13, bold=False, anchor='start', color='#111'):
        t = t.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
        b = ' font-weight="bold"' if bold else ''
        self.e.append(f'<text x="{x}" y="{y}" font-family="Arial, sans-serif" font-size="{size}"{b} '
                      f'text-anchor="{anchor}" fill="{color}">{t}</text>')
    def res(self, x, y, label, vertical=False):           # резистор 40×14
        if vertical:
            self.e.append(f'<rect x="{x-7}" y="{y}" width="14" height="40" fill="#fff" stroke="#222" stroke-width="2"/>')
            self.text(x + 12, y + 25, label, size=12)
        else:
            self.e.append(f'<rect x="{x}" y="{y-7}" width="40" height="14" fill="#fff" stroke="#222" stroke-width="2"/>')
            self.text(x + 20, y - 12, label, size=12, anchor='middle')
    def cap(self, x, y, label):                           # конденсатор на землю (вертикально)
        self.line(x, y, x, y + 14); self.line(x - 10, y + 14, x + 10, y + 14); self.line(x - 10, y + 20, x + 10, y + 20)
        self.line(x, y + 20, x, y + 30); self.gnd(x, y + 30); self.text(x + 14, y + 20, label, size=12)
    def gnd(self, x, y):
        self.line(x - 10, y, x + 10, y); self.line(x - 6, y + 4, x + 6, y + 4); self.line(x - 2, y + 8, x + 2, y + 8)
    def dot(self, x, y):
        self.e.append(f'<circle cx="{x}" cy="{y}" r="4" fill="#222"/>')
    def note(self, y, lines):
        for i, t in enumerate(lines):
            self.text(20, y + i * 18, t, size=13, color='#333')
    def save(self, name):
        os.makedirs(self.out, exist_ok=True)
        with open(os.path.join(self.out, name), 'w', encoding='utf-8') as f:
            f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                    f'viewBox="0 0 {self.w} {self.h}"><rect width="100%" height="100%" fill="#fff"/>'
                    + ''.join(self.e) + '</svg>')


"""Минимальный разбор и запись S-выражений KiCad."""
import re

TOKEN = re.compile(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+')


class Q(str):
    """Строка, которую надо писать в кавычках."""


def parse(text):
    stack, cur = [], []
    for m in TOKEN.finditer(text):
        t = m.group(0)
        if t == '(':
            stack.append(cur); cur = []
        elif t == ')':
            done = cur; cur = stack.pop(); cur.append(done)
        elif t.startswith('"'):
            cur.append(Q(t[1:-1].replace('\\"', '"').replace('\\\\', '\\')))
        else:
            cur.append(t)
    return cur[0]


def dump(x, indent=0):
    if isinstance(x, list):
        if all(not isinstance(e, list) for e in x):
            return '(' + ' '.join(dump(e) for e in x) + ')'
        pad = '  ' * (indent + 1)
        head = []
        for e in x:
            if isinstance(e, list):
                break
            head.append(dump(e))
        rest = x[len(head):]
        return '(' + ' '.join(head) + ''.join('\n' + pad + dump(e, indent + 1) for e in rest) + ')'
    if isinstance(x, Q):
        return '"' + x.replace('\\', '\\\\').replace('"', '\\"') + '"'
    if isinstance(x, float):
        s = f'{x:.4f}'.rstrip('0').rstrip('.')
        return s if s not in ('-0', '') else '0'
    return str(x)


def find(x, key):
    return [e for e in x if isinstance(e, list) and e and e[0] == key]


def walk(x, key):
    if isinstance(x, list):
        if x and x[0] == key:
            yield x
        for e in x:
            yield from walk(e, key)

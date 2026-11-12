#!/usr/bin/env python3
"""
Добавить в плату только НОВЫЕ дорожки и переходы из результата Freerouting (.ses), не пересоздавая старые:
SES загружается в копию платы, из неё берётся всё, чего нет в оригинале (по геометрии, допуск 1 мкм),
и это добавляется в оригинал. Старые элементы оригинала не трогаются.

  $KPY merge_ses.py build/res.ses
"""
import os, shutil, sys
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, 'els_shield.kicad_pcb')
TMP = os.path.join(HERE, 'build', 'ses_copy.kicad_pcb')
TOL = 1000                                        # нм


def key(t):
    r = lambda v: (round(v.x / TOL), round(v.y / TOL))
    if t.GetClass() == 'PCB_VIA':
        return ('V', r(t.GetPosition()), t.GetWidth(), t.GetNetname())
    a, b = sorted([r(t.GetStart()), r(t.GetEnd())])
    return ('T', a, b, t.GetWidth(), t.GetLayer(), t.GetNetname())


shutil.copy(PCB, TMP)
copy = pcbnew.LoadBoard(TMP)
if not pcbnew.ImportSpecctraSES(copy, sys.argv[1]):
    raise SystemExit('ошибка загрузки SES')

orig = pcbnew.LoadBoard(PCB)
have = {key(t) for t in orig.GetTracks()}
lost = have - {key(t) for t in copy.GetTracks()}
added = 0
SKIP = {'GND'}                                    # земля новых разъёмов — полигоном; сшивающие переходы трассировщик
                                                  # соединял бы дорожками (без полигонов они для него «острова»)
for t in copy.GetTracks():
    if key(t) in have or t.GetNetname() in SKIP:
        continue
    net = orig.FindNet(t.GetNetname())
    if t.GetClass() == 'PCB_VIA':
        n = pcbnew.PCB_VIA(orig)
        n.SetPosition(t.GetPosition()); n.SetWidth(t.GetWidth()); n.SetDrill(t.GetDrillValue())
        n.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    else:
        n = pcbnew.PCB_TRACK(orig)
        n.SetStart(t.GetStart()); n.SetEnd(t.GetEnd()); n.SetWidth(t.GetWidth()); n.SetLayer(t.GetLayer())
    n.SetNet(net)
    orig.Add(n)
    added += 1
    print('  +', t.GetClass(), t.GetNetname(), pcbnew.ToMM(t.GetStart().x), pcbnew.ToMM(t.GetStart().y),
          '→', pcbnew.ToMM(t.GetEnd().x), pcbnew.ToMM(t.GetEnd().y), pcbnew.ToMM(t.GetWidth()))
orig.Save(PCB)
print(f'добавлено {added}; старых элементов в результате трассировщика не нашлось (изменены им): {len(lost)}')

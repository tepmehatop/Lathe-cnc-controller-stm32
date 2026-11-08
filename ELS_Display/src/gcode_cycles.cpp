/**
 * gcode_cycles.cpp — разворот циклов Fanuc G71 / G70 в G0/G1/G2/G3 (см. gcode_cycles.h)
 *
 * G71 U<глубина на сторону> R<отскок>
 * G71 P<N начала контура> Q<N конца> U<припуск по X, на диаметр> W<припуск по Z> F<подача>
 *   (или в одну строку: G71 P Q U W D<глубина> F)
 * Черновая обдирка наружного контура от точки старта (положение резца перед циклом): проходы вдоль Z
 * слоями по глубине, затем получистовой проход по контуру с припуском, возврат в точку старта.
 * Контур от торца к патрону: Z только убывает; X может уходить вниз (шейка, «карман» — тип II) —
 * такие места резец проходит отдельно, врезаясь сверху. Кадры P…Q сами по себе не исполняются.
 * G70 P Q [F] — чистовой проход по контуру (F строки G70 — вместо F контура), возврат в точку старта.
 */

#include "gcode_cycles.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace {

bool word(const std::string& s, char w, double* v)
{
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] == w && (i == 0 || !isalpha((unsigned char)s[i - 1]))) { *v = atof(s.c_str() + i + 1); return true; }
    return false;
}

bool has_g(const std::string& s, int g)
{
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] == 'G' && (i == 0 || !isalpha((unsigned char)s[i - 1])) && atoi(s.c_str() + i + 1) == g) return true;
    return false;
}

std::string num(double v)
{
    char b[24];
    snprintf(b, sizeof(b), "%.4f", fabs(v) < 5e-5 ? 0.0 : v);
    char* e = b + strlen(b) - 1;
    while (*e == '0') *e-- = '\0';
    if (*e == '.') *e = '\0';
    return b;
}

struct Modal {
    bool   abs = true, diam = true;
    int    motion = 0;
    double z = 0, r = 0;            // положение программы (r — радиус)
    bool   kz = false, kx = false;  // положение известно
    double f = 0;                   // последняя F
};

// модальные G и положение после строки, исполненной STM32 как есть
void track(Modal& m, const std::string& s)
{
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != 'G' || (i && isalpha((unsigned char)s[i - 1]))) continue;
        int g = atoi(s.c_str() + i + 1);
        if (g <= 3) m.motion = g;
        else if (g == 7) m.diam = true;  else if (g == 8) m.diam = false;
        else if (g == 90) m.abs = true;  else if (g == 91) m.abs = false;
        else if (g == 28) {
            double t;
            if (!word(s, 'U', &t) && !word(s, 'W', &t)) { m.z = m.r = 0; m.kz = m.kx = true; }  // G28 U0 W0 — на месте
            return;
        }
    }
    double v;
    if (word(s, 'F', &v)) m.f = v;
    if (word(s, 'Z', &v)) { m.z = m.abs ? v : m.z + v; m.kz = true; }
    if (word(s, 'W', &v)) m.z += v;
    if (word(s, 'X', &v)) { v /= m.diam ? 2 : 1; m.r = m.abs ? v : m.r + v; m.kx = true; }
    if (word(s, 'U', &v)) m.r += v / (m.diam ? 2 : 1);
}

struct Move {
    int    n, type;          // 0 — G0, 1 — G1, 2/3 — дуга по/против часовой
    double z0, r0, z, r;     // начало и конец (r — радиус)
    double cz, cr;           // центр дуги
    double f;                // F строки (0 — нет)
};

class Expander {
public:
    Expander(const std::vector<std::string>& in, std::vector<GcLine>& out) : in_(in), out_(out) {}
    bool run(std::string& err, int& err_n);

private:
    const std::vector<std::string>& in_;
    std::vector<GcLine>& out_;
    Modal m_;
    double d_ = 1.0, e_ = 0.5;   // G71 U (глубина) и R (отскок) из первой строки цикла
    std::string err_;
    int err_n_ = 0;

    bool fail(int n, const std::string& e) { err_ = e; err_n_ = n; return false; }
    int  find_n(int nn) const;
    bool contour(int n, int p, int q, std::vector<Move>& mv);
    void emit(int n, const std::string& g) { out_.push_back({n, g}); }
    std::string xw(double r) const { return "X" + num(m_.diam ? 2 * r : r); }
    void move_line(int n, const Move& a, double dz, double dr, double f, bool use_f);
    bool g71(int n, const std::string& s);
    bool g70(int n, const std::string& s);
    void back_to(int n, double za, double ra, double z, double r);
};

int Expander::find_n(int nn) const
{
    for (size_t i = 0; i < in_.size(); i++) {
        const std::string& s = in_[i];
        if (s.size() > 1 && s[0] == 'N' && atoi(s.c_str() + 1) == nn) return (int)i;
    }
    return -1;
}

// Кадры P…Q → перемещения от текущего положения (точки старта)
bool Expander::contour(int n, int p, int q, std::vector<Move>& mv)
{
    int ip = find_n(p), iq = find_n(q);
    if (ip < 0) return fail(n, "НЕТ КАДРА N" + std::to_string(p) + " (P)");
    if (iq < 0) return fail(n, "НЕТ КАДРА N" + std::to_string(q) + " (Q)");
    if (iq < ip) return fail(n, "КАДР Q РАНЬШЕ КАДРА P");
    int motion = m_.motion;
    double z = m_.z, r = m_.r, pend_f = 0;   // F в кадре без перемещения — для следующего
    for (int i = ip; i <= iq; i++) {
        const std::string& s = in_[i];
        int ln = i + 1;
        for (size_t k = 0; k < s.size(); k++) {
            if (s[k] != 'G' || (k && isalpha((unsigned char)s[k - 1]))) continue;
            int g = atoi(s.c_str() + k + 1);
            if (g <= 3) motion = g;
            else if (g == 91) return fail(ln, "КОНТУР ЦИКЛА — ТОЛЬКО В АБСОЛЮТНЫХ (G90)");
            else if (g == 41 || g == 42) return fail(ln, "КОМПЕНСАЦИЯ РАДИУСА РЕЗЦА G41/G42 НЕ ПОДДЕРЖИВАЕТСЯ");
            else if (g == 70 || g == 71) return fail(ln, "ЦИКЛ ВНУТРИ КОНТУРА ЦИКЛА");
        }
        double v, tz = z, tr = r, f = 0;
        bool has = false;
        if (word(s, 'Z', &v)) { tz = v; has = true; }
        if (word(s, 'W', &v)) { tz = z + v; has = true; }
        if (word(s, 'X', &v)) { tr = v / (m_.diam ? 2 : 1); has = true; }
        if (word(s, 'U', &v)) { tr = r + v / (m_.diam ? 2 : 1); has = true; }
        if (!word(s, 'F', &f)) f = 0;
        if (!has) { if (f > 0) pend_f = f; continue; }
        if (f <= 0) f = pend_f;
        pend_f = 0;
        Move a{ln, motion, z, r, tz, tr, 0, 0, f};
        if (motion >= 2) {
            bool cw = motion == 2;
            double ci, ck, R;
            bool hi = word(s, 'I', &ci), hk = word(s, 'K', &ck);
            if (hi || hk) { a.cz = z + (hk ? ck : 0); a.cr = r + (hi ? ci : 0); }
            else if (word(s, 'R', &R)) {
                double mz = (z + tz) / 2, mr = (r + tr) / 2, dz = tz - z, dr = tr - r, d = hypot(dz, dr);
                if (d < 1e-9 || fabs(R) < d / 2 - 1e-4) return fail(ln, "ДУГА: R МЕНЬШЕ ПОЛОВИНЫ ХОРДЫ");
                double h = sqrt(fmax(R * R - d * d / 4, 0)), sg = (cw ? -1 : 1) * (R > 0 ? 1 : -1);
                a.cz = mz - sg * h * dr / d; a.cr = mr + sg * h * dz / d;
            } else return fail(ln, "ДУГА БЕЗ I/K ИЛИ R");
        }
        mv.push_back(a);
        z = tz; r = tr;
    }
    if (mv.empty()) return fail(n, "В КОНТУРЕ НЕТ ПЕРЕМЕЩЕНИЙ");
    return true;
}

// Перемещение контура со сдвигом (dz, dr) — припуск
void Expander::move_line(int n, const Move& a, double dz, double dr, double f, bool use_f)
{
    std::string g;
    if (a.type == 0) g = "G0 ";
    else g = a.type == 1 ? "G1 " : (a.type == 2 ? "G2 " : "G3 ");
    g += xw(a.r + dr) + " Z" + num(a.z + dz);
    if (a.type >= 2) g += " I" + num(a.cr - a.r0) + " K" + num(a.cz - a.z0);
    if (a.type && use_f && f > 0) g += " F" + num(f);
    emit(n, g);
}

// После цикла — в точку старта (za, ra): сначала вверх до её X (если ниже), потом по Z
void Expander::back_to(int n, double za, double ra, double z, double r)
{
    if (r < ra - 1e-9) { emit(n, "G0 " + xw(ra)); r = ra; }
    if (fabs(z - za) > 1e-9) emit(n, "G0 Z" + num(za));
    if (fabs(r - ra) > 1e-9) emit(n, "G0 " + xw(ra));
}

bool Expander::g71(int n, const std::string& s)
{
    double p, q, u = 0, w = 0, f = m_.f, v;
    if (!word(s, 'P', &p) || !word(s, 'Q', &q)) {             // первая строка: глубина и отскок
        if (word(s, 'U', &v)) { if (v <= 0) return fail(n, "G71: ГЛУБИНА U <= 0"); d_ = v; }
        if (word(s, 'R', &v)) e_ = fabs(v);
        return true;
    }
    word(s, 'U', &u); word(s, 'W', &w);
    if (word(s, 'D', &v)) { if (v <= 0) return fail(n, "G71: ГЛУБИНА D <= 0"); d_ = v; }
    if (word(s, 'F', &v)) f = v;
    if (f <= 0) return fail(n, "G71: НЕТ ПОДАЧИ F");
    if (!m_.abs) return fail(n, "G71 — ТОЛЬКО В АБСОЛЮТНЫХ (G90)");
    if (!m_.kz || !m_.kx) return fail(n, "G71: ПЕРЕД ЦИКЛОМ ПОДВЕДИТЕ РЕЗЕЦ В ТОЧКУ СТАРТА (G0 X Z)");
    std::vector<Move> mv;
    if (!contour(n, (int)p, (int)q, mv)) return false;
    const double za = m_.z, ra = m_.r, du = u / (m_.diam ? 2 : 1), dw = w;
    if (du < 0) return fail(n, "G71: ПРИПУСК U < 0 (РАСТОЧКА НЕ ПОДДЕРЖИВАЕТСЯ)");

    // профиль детали с припуском — ломаная (дуги — хорды до 0.05), от точки старта до выхода на X старта
    std::vector<double> pz{za + dw}, pr{ra};
    for (const Move& a : mv) {
        if (a.type >= 2) {
            double R = hypot(a.z0 - a.cz, a.r0 - a.cr);
            double a0 = atan2(a.r0 - a.cr, a.z0 - a.cz), a1 = atan2(a.r - a.cr, a.z - a.cz), da = a1 - a0;
            if (a.type == 2) { while (da >= 0) da -= 2 * M_PI; } else { while (da <= 0) da += 2 * M_PI; }
            int k = (int)ceil(fabs(da) * R / 0.05); if (k < 1) k = 1;
            for (int i = 1; i <= k; i++) {
                double t = a0 + da * i / k;
                pz.push_back((i == k ? a.z : a.cz + R * cos(t)) + dw);
                pr.push_back((i == k ? a.r : a.cr + R * sin(t)) + du);
            }
        } else { pz.push_back(a.z + dw); pr.push_back(a.r + du); }
    }
    size_t np = pz.size();
    for (size_t i = 1; i < pz.size(); i++) {
        if (pr[i] >= ra - 1e-9 && i > 1) { np = i + 1; break; }        // вышли на X старта — дальше воздух
        if (pz[i] > pz[i - 1] + 1e-6)
            return fail(mv.front().n, "G71: КОНТУР ДОЛЖЕН ИДТИ ОТ ТОРЦА К ПАТРОНУ (Z ТОЛЬКО УБЫВАЕТ)");
    }
    pz.resize(np); pr.resize(np);
    double rmin = 1e30;
    for (size_t i = 1; i < np; i++) rmin = fmin(rmin, pr[i]);
    if (rmin >= ra - 1e-9) return fail(n, "G71: КОНТУР НЕ НИЖЕ ТОЧКИ СТАРТА ПО X — НЕЧЕГО СНИМАТЬ");

    // слои: X старта − d, − 2d … пока выше профиля; в каждом — участки Z, где профиль ниже слоя
    double z = za, r = ra;
    for (int k = 1;; k++) {
        double L = ra - d_ * k;
        if (L <= rmin + 1e-6) break;
        std::vector<std::pair<double, double>> iv;               // (z входа, z выхода)
        bool in = false; double zin = 0;
        for (size_t i = 1; i < np; i++) {
            bool a = pr[i - 1] < L, b = pr[i] < L;
            double zc = pz[i - 1];
            if (a != b) zc = pz[i - 1] + (L - pr[i - 1]) / (pr[i] - pr[i - 1]) * (pz[i] - pz[i - 1]);
            if (!a && b) { in = true; zin = zc; }
            if (a && !b && in) { iv.push_back({zin, zc}); in = false; }
        }
        if (in) iv.push_back({zin, pz[np - 1]});
        bool single = iv.size() == 1 && iv[0].first >= za - 1e-6;
        for (size_t j = 0; j < iv.size(); j++) {
            double zi = fmin(iv[j].first, za), zo = iv[j].second;
            if (zi - zo < 1e-3) continue;
            if (j == 0 && zi >= za - 1e-6 && fabs(z - za) < 1e-9) {
                emit(n, "G0 " + xw(L));
            } else {                                              // карман: сверху, врезание подачей
                if (r < ra - 1e-9) emit(n, "G0 " + xw(ra));
                emit(n, "G0 Z" + num(zi));
                double over = fmin(ra, L + d_);
                if (over > L + 1e-9 && over < ra - 1e-9) emit(n, "G0 " + xw(over));
                emit(n, "G1 " + xw(L) + " F" + num(f));
            }
            emit(n, "G1 Z" + num(zo) + " F" + num(f));
            double rz = fmin(zo + e_, zi), rr = fmin(L + e_, ra);   // отскок под 45°
            emit(n, "G0 " + xw(rr) + " Z" + num(rz));
            z = rz; r = rr;
        }
        if (iv.empty()) continue;
        if (!single) { emit(n, "G0 " + xw(ra)); r = ra; }
        emit(n, "G0 Z" + num(za)); z = za;
    }
    // получистовой проход по контуру с припуском, возврат в точку старта
    if (r < ra - 1e-9 && fabs(z - za) > 1e-9) { emit(n, "G0 " + xw(ra)); r = ra; }
    if (fabs(z - za) > 1e-9) emit(n, "G0 Z" + num(za));
    for (const Move& a : mv) move_line(n, a, dw, du, f, true);
    back_to(n, za, ra, mv.back().z + dw, mv.back().r + du);
    m_.z = za; m_.r = ra; m_.motion = 0; m_.f = f;
    return true;
}

bool Expander::g70(int n, const std::string& s)
{
    double p, q, f = 0;
    if (!word(s, 'P', &p) || !word(s, 'Q', &q)) return fail(n, "G70: НУЖНЫ P И Q");
    bool ff = word(s, 'F', &f) && f > 0;
    if (!m_.abs) return fail(n, "G70 — ТОЛЬКО В АБСОЛЮТНЫХ (G90)");
    if (!m_.kz || !m_.kx) return fail(n, "G70: ПЕРЕД ЦИКЛОМ ПОДВЕДИТЕ РЕЗЕЦ В ТОЧКУ СТАРТА (G0 X Z)");
    std::vector<Move> mv;
    if (!contour(n, (int)p, (int)q, mv)) return false;
    double cf = ff ? f : m_.f;
    for (const Move& a : mv) {
        if (!ff && a.f > 0) cf = a.f;
        if (a.type && cf <= 0) return fail(a.n, "G70: НЕТ ПОДАЧИ F");
        move_line(a.n, a, 0, 0, cf, true);
    }
    back_to(n, m_.z, m_.r, mv.back().z, mv.back().r);
    m_.motion = 0; m_.f = cf;
    return true;
}

bool Expander::run(std::string& err, int& err_n)
{
    // кадры P…Q всех циклов сами по себе не исполняются (как на Fanuc — цикл идёт дальше после Q)
    std::vector<bool> skip(in_.size(), false);
    for (size_t i = 0; i < in_.size(); i++) {
        const std::string& s = in_[i];
        double p, q;
        if ((has_g(s, 71) || has_g(s, 70)) && word(s, 'P', &p) && word(s, 'Q', &q)) {
            int ip = find_n((int)p), iq = find_n((int)q);
            for (int k = ip; ip >= 0 && iq >= ip && k <= iq; k++) skip[k] = true;
        }
    }
    for (size_t i = 0; i < in_.size(); i++) {
        const std::string& s = in_[i];
        int n = (int)i + 1;
        bool ok = true;
        if (has_g(s, 71)) ok = g71(n, s);
        else if (has_g(s, 70)) ok = g70(n, s);
        else if (skip[i]) continue;
        else { emit(n, s); track(m_, s); }
        if (!ok) { err = err_; err_n = err_n_; return false; }
    }
    return true;
}

}  // namespace

bool GCodeCycles_Needed(const std::vector<std::string>& lines)
{
    for (const std::string& s : lines) if (has_g(s, 70) || has_g(s, 71)) return true;
    return false;
}

bool GCodeCycles_Expand(const std::vector<std::string>& lines, std::vector<GcLine>& out,
                        std::string& err, int& err_n)
{
    out.clear();
    err.clear(); err_n = 0;
    return Expander(lines, out).run(err, err_n);
}

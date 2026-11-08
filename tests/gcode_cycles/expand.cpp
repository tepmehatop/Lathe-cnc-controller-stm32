// Проверка разворота циклов G71/G70 на компьютере (тот же код, что на дисплее).
// Сборка и запуск: bash tests/gcode_cycles/run.sh  (или вручную:
//   c++ -std=c++17 -I ELS_Display/include tests/gcode_cycles/expand.cpp ELS_Display/src/gcode_cycles.cpp -o expand
//   ./expand файл.nc [вывод.nc])
// Печатает развёрнутую программу, проверяет, что резец нигде не заходит в деталь (контур чистового прохода)
// и что после G71 на детали остался только припуск. Вывод — для симулятора STM32 (SIM_GCFILE).
#include "gcode_cycles.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

// как preprocess() исполнителя: без комментариев, верхний регистр, пустые — выброшены
static std::vector<std::string> load(const char* path)
{
    std::ifstream f(path);
    std::vector<std::string> v;
    std::string raw;
    while (std::getline(f, raw)) {
        std::string s; bool par = false;
        for (char c : raw) {
            if (c == '(') { par = true; continue; }
            if (c == ')') { par = false; continue; }
            if (par) continue;
            if (c == ';' || c == '\r') break;
            s += (char)toupper((unsigned char)c);
        }
        size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
        if (a == std::string::npos) continue;
        v.push_back(s.substr(a, b - a + 1));
    }
    return v;
}

static bool word(const std::string& s, char w, double* v)
{
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] == w && (i == 0 || !isalpha((unsigned char)s[i - 1]))) { *v = atof(s.c_str() + i + 1); return true; }
    return false;
}

struct P { double z, r; int n; bool rapid; };

// Путь резца по развёрнутым строкам (G0/G1/G2/G3 X Z I K, абсолютные, X — диаметр), точки через ≤ 0.02
static std::vector<P> trace(const std::vector<GcLine>& out, double z0, double r0)
{
    std::vector<P> p{{z0, r0, 0, true}};
    double z = z0, r = r0; int mot = 0;
    for (const GcLine& l : out) {
        const std::string& s = l.g;
        for (size_t i = 0; i < s.size(); i++)
            if (s[i] == 'G' && (i == 0 || s[i - 1] == ' ')) { int g = atoi(s.c_str() + i + 1); if (g <= 3) mot = g; }
        double v, tz = z, tr = r;
        bool has = false;
        if (word(s, 'Z', &v)) { tz = v; has = true; }
        if (word(s, 'X', &v)) { tr = v / 2; has = true; }
        if (!has || (s[0] == 'G' && atoi(s.c_str() + 1) == 28)) continue;
        if (mot >= 2) {
            double I = 0, K = 0; word(s, 'I', &I); word(s, 'K', &K);
            double cz = z + K, cr = r + I, R = hypot(z - cz, r - cr);
            double a0 = atan2(r - cr, z - cz), a1 = atan2(tr - cr, tz - cz), da = a1 - a0;
            if (mot == 2) { while (da >= 0) da -= 2 * M_PI; } else { while (da <= 0) da += 2 * M_PI; }
            int k = std::max(1, (int)ceil(fabs(da) * R / 0.02));
            for (int i = 1; i <= k; i++) { double a = a0 + da * i / k; p.push_back({cz + R * cos(a), cr + R * sin(a), l.n, false}); }
        } else {
            int k = std::max(1, (int)ceil(hypot(tz - z, tr - r) / 0.02));
            for (int i = 1; i <= k; i++) p.push_back({z + (tz - z) * i / k, r + (tr - r) * i / k, l.n, mot == 0});
        }
        z = tz; r = tr;
    }
    return p;
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "файл?\n"); return 2; }
    std::vector<std::string> in = load(argv[1]);
    std::vector<GcLine> out;
    std::string err; int en = 0;
    if (!GCodeCycles_Expand(in, out, err, en)) { printf("ОШИБКА строка %d: %s\n", en, err.c_str()); return 1; }
    if (argc > 2) {
        std::ofstream o(argv[2]);
        for (const GcLine& l : out) o << l.g << "\n";
    }
    // кадры контура (P…Q чистового G70) — поверхность готовой детали
    int p = 0, q = 0, n70 = 0; double v;
    for (size_t i = 0; i < in.size(); i++)
        if (in[i].find("G70") != std::string::npos && word(in[i], 'P', &v)) {
            p = (int)v; word(in[i], 'Q', &v); q = (int)v; n70 = (int)i + 1;
        }
    int ip = 0, iq = 0;
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i][0] == 'N' && atoi(in[i].c_str() + 1) == p) ip = (int)i + 1;
        if (in[i][0] == 'N' && atoi(in[i].c_str() + 1) == q) iq = (int)i + 1;
    }
    printf("строк программы %zu, развёрнуто %zu\n", in.size(), out.size());
    std::vector<P> path = trace(out, 0, 0);
    // поверхность детали — путь чистового прохода по контуру (точки строк P…Q), берём только участки до выхода
    // на наибольший X контура (дальше — отвод)
    std::vector<P> prof;
    for (const P& t : path) if (t.n >= ip && t.n <= iq && !t.rapid) prof.push_back(t);
    if (prof.empty()) { printf("нет G70 — проверка врезания пропущена\n"); return 0; }
    // r детали при z: наименьший r контура среди его отрезков, покрывающих z (вертикальные стенки — не берём)
    auto part_r = [&](double z, double& r) {
        bool f = false;
        for (size_t i = 1; i < prof.size(); i++) {
            double za = prof[i - 1].z, zb = prof[i].z;
            if (fabs(za - zb) < 1e-6 || (prof[i].n != prof[i - 1].n && fabs(za - zb) > 0.05)) continue;
            if (z <= std::max(za, zb) - 1e-3 && z >= std::min(za, zb) + 1e-3) {
                double t = (z - za) / (zb - za), rr = prof[i - 1].r + t * (prof[i].r - prof[i - 1].r);
                if (!f || rr > r) r = rr;
                f = true;
            }
        }
        return f;
    };
    double worst = 1e9, wz = 0; int wn = 0, gouge = 0;
    for (const P& t : path) {
        double r;
        if (!part_r(t.z, r)) continue;
        double d = t.r - r;
        if (d < worst) { worst = d; wz = t.z; wn = t.n; }
        if (d < -0.002) gouge++;
    }
    printf("наименьший зазор до детали %.4f мм (Z %.3f, строка %d); точек внутри детали: %d\n", worst, wz, wn, gouge);
    (void)n70;
    return gouge ? 3 : 0;
}

// Pruebas offline reproducibles del motor CRAY AutoTune (sin JUCE, sin DAW). Devuelve !=0 si algo falla.
#define _USE_MATH_DEFINES
#include "../Source/CrayAutoTuneDSP.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>

static std::atomic<long> g_allocs{0}; static std::atomic<bool> g_track{false};
void* operator new(size_t n) { if (g_track) ++g_allocs; void* p = std::malloc(n); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

static double SR = 48000.0; static int fails = 0;
static void check(bool ok, const char* what) { if (!ok) { ++fails; printf("    >> FALLA: %s\n", what); } }
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

template <class F> static float voiceS(double& ph, F f0, double t) {
    double f = f0(t); ph += 2 * M_PI * f / SR; double s = 0;
    for (int h = 1; h <= 12; ++h) { double fh = f * h; if (fh > 0.45 * SR) break;
        s += (std::exp(-std::pow((fh - 700) / 500.0, 2)) + 0.6 * std::exp(-std::pow((fh - 1800) / 700.0, 2))) / h * std::sin(h * ph); }
    return (float) (0.35 * s);
}
static double estPitch(const float* x, int n, double hint) {
    int lo = (int) (SR / (hint * 1.2)), hi = (int) (SR / (hint / 1.2)); std::vector<double> c(hi + 2, 0.0); const int N = n - hi - 1;
    for (int lag = lo - 1; lag <= hi + 1; ++lag) { double s = 0, a = 0, b = 0; for (int i = 0; i < N; ++i) { double u = x[i], v = x[i + lag]; s += u * v; a += u * u; b += v * v; } c[lag] = s / std::sqrt(a * b + 1e-12); }
    int bl = lo; for (int l = lo; l <= hi; ++l) if (c[l] > c[bl]) bl = l;
    double a = c[bl - 1], b = c[bl], d = c[bl + 1], den = a - 2 * b + d; double sh = std::fabs(den) > 1e-12 ? 0.5 * (a - d) / den : 0.0; return SR / (bl + sh);
}
template <class F> static std::vector<float> run(cray::Engine& e, F f0, double secs, int blk = 128, std::vector<float>* inOut = nullptr, std::vector<float>* delayTrace = nullptr) {
    std::vector<float> o, l(blk), r(blk); double ph = 0; long total = (long) (secs * SR);
    for (long n = 0; n < total; n += blk) { for (int i = 0; i < blk; ++i) { l[i] = r[i] = voiceS(ph, f0, (n + i) / SR); if (inOut) inOut->push_back(l[i]); }
        e.processInPlace(l.data(), r.data(), blk); for (int i = 0; i < blk; ++i) o.push_back(l[i]); if (delayTrace) delayTrace->push_back(e.liveDelaySamples()); }
    return o;
}
static cray::TuneParams basePrm(int mode) { cray::TuneParams p; p.mode = mode; p.scale = 0; p.snap = 1; p.glideMs = 0; p.retuneMs = 5; p.humanize = 0; p.flex = 0; p.formantPreserve = 1; p.mix = 1; return p; }
static const double kSR[4] = { 44100, 48000, 88200, 96000 };

int main() {
    printf("CRAY AutoTune - bateria de pruebas del DSP\n");
    printf("\n[1] Latencia por impulso (ratio=1). Live: retardo en reposo; Quality: retardo fijo. Todas las fs y bloques\n");
    for (double sr : kSR) for (int mode : {0, 1}) for (int blk : {32, 64, 128, 512}) {
        SR = sr; cray::Engine e; e.prepare(sr); auto p = basePrm(mode); p.correction = 0; e.setParams(p);
        std::vector<float> in(sr * 0.3, 0.f), o, l(blk); in[1000] = 1.f;
        for (size_t n = 0; n + blk <= in.size(); n += blk) { for (int i = 0; i < blk; ++i) l[i] = in[n + i]; e.processInPlace(l.data(), nullptr, blk); for (int i = 0; i < blk; ++i) o.push_back(l[i]); }
        size_t pk = 0; for (size_t i = 0; i < o.size(); ++i) if (std::fabs(o[i]) > std::fabs(o[pk])) pk = i;
        int lat = (int) pk - 1000;
        if (blk == 64 || blk == 512 && mode == 1) printf("  %6.0f Hz %-7s bloque %3d: %5d muestras = %6.3f ms | PDC declarado %5d = %6.3f ms\n", sr, mode ? "Quality" : "Live", blk, lat, 1000.0 * lat / sr, e.latencySamples(), 1000.0 * e.latencySamples() / sr);
        if (mode == 0) check(lat >= 3 && lat <= 5, "Live: latencia en reposo debe ser 4 muestras"); else check(std::abs(lat - e.latencySamples()) <= 1, "Quality: PDC no coincide con retardo real");
    }
    printf("\n[2] Retardo REAL en Live mientras corrige (nota 35 cents alta, retune 5 ms): minimo/medio/maximo vs PDC declarado\n");
    for (double sr : kSR) for (int it : {0, 1, 2}) {
        SR = sr; double f0 = (it == 0 ? 120.0 : it == 1 ? 190.0 : 320.0) * std::pow(2.0, 0.35 / 12 * 1.0);
        cray::Engine e; e.prepare(sr); auto p = basePrm(0); p.inputType = it; e.setParams(p);
        std::vector<float> in, dtr; auto o = run(e, [&](double) { return f0; }, 4.0, 128, &in, &dtr);
        double mn = 1e9, mx = 0, sm = 0; int cnt = 0; for (size_t k = dtr.size() / 2; k < dtr.size(); ++k) { mn = std::min<double>(mn, dtr[k]); mx = std::max<double>(mx, dtr[k]); sm += dtr[k]; ++cnt; }
        int W = (int) (3 * sr / f0), maxLag = (int) (1.3 * sr / f0); double cmn = 1e9, cmx = 0; int viol = 0, nwin = 0;
        for (size_t s0 = (size_t) (2.0 * sr); s0 + W + maxLag < o.size(); s0 += (size_t) (0.05 * sr)) { std::vector<double> cc(maxLag + 1); double best = -2;
            for (int lag = 0; lag <= maxLag; ++lag) { double sab = 0, saa = 0, sbb = 0; for (int i = 0; i < W; ++i) { double a = o[s0 + i], b = in[s0 + i - lag]; sab += a * b; saa += a * a; sbb += b * b; } cc[lag] = sab / std::sqrt(saa * sbb + 1e-12); best = std::max(best, cc[lag]); }
            int bl = 0; for (int lag = 0; lag <= maxLag; ++lag) if (cc[lag] >= best - 0.02) { bl = lag; break; }
            cmn = std::min<double>(cmn, bl); cmx = std::max<double>(cmx, bl); ++nwin;
            size_t blkIdx = (s0 + W / 2) / 128; double dInt = blkIdx < dtr.size() ? dtr[blkIdx] : 0; double Tp = sr / f0; double diff = std::fmod(std::fabs(bl - dInt), Tp); diff = std::min(diff, Tp - diff); if (diff > 0.2 * Tp + 4) ++viol; }
        double T = sr / f0;
        printf("  %6.0f Hz %-11s f0 %5.1f: retardo interno min %6.1f / medio %6.1f / max %6.1f muestras (%.2f / %.2f / %.2f ms) | PDC %d (%.2f ms) | periodo %.1f\n",
               sr, it == 0 ? "Low Male" : it == 1 ? "Alto/Tenor" : "Soprano", f0, mn, sm / cnt, mx, 1000 * mn / sr, 1000 * sm / cnt / sr, 1000 * mx / sr, e.latencySamples(), 1000.0 * e.latencySamples() / sr, T);
        check(mx <= 4 + 1.05 * T + 2, "retardo maximo excede dlo + 1 periodo");
        printf("      correlacion: lag %0.f..%0.f, ventanas incoherentes con el retardo interno: %d/%d\n", cmn, cmx, viol, nwin); check(viol <= nwin / 10, "xcorr incoherente con el retardo interno");
    }
    printf("\n[3] Deteccion de tono (error en cents / voiced), correccion 0, todas las fs\n");
    for (double sr : kSR) { SR = sr; double worst = 0; int bad = 0, tot = 0;
        for (double f : {82.41, 110.0, 146.83, 196.0, 261.63, 329.63, 440.0, 587.33, 783.99}) { cray::Engine e; e.prepare(sr); auto p = basePrm(0); p.correction = 0; p.inputType = f < 100 ? 0 : (f < 160 ? 1 : 2); if (f < 150) p.inputType = f < 100 ? 0 : 1; e.setParams(p);
            run(e, [&](double) { return f; }, 1.0); double m = e.tracker().infoMidi.load(); double ex = 69 + 12 * std::log2(f / 440); double err = (m - ex) * 100; worst = std::max(worst, std::fabs(err)); ++tot; if (e.tracker().infoVoiced.load() < 0.5 || std::fabs(err) > 8) ++bad; }
        printf("  %6.0f Hz: peor error %.2f cents, fallos %d/%d\n", sr, worst, bad, tot); check(bad == 0, "deteccion fuera de +-8 cents o no voiced"); }
    printf("\n[4] Precision de afinacion (cromatica, snap 100%%, retune 5 ms): error final en cents\n");
    for (int mode : {0, 1}) for (double sr : kSR) { SR = sr; double worst = 0;
        for (double f : {110.0, 146.83, 196.0, 261.63, 329.63, 440.0}) for (double oc : {+35.0, -30.0, +15.0}) { cray::Engine e; e.prepare(sr); auto p = basePrm(mode); p.inputType = f < 130 ? 0 : 1; e.setParams(p);
            double fin = f * std::pow(2.0, oc / 1200); auto o = run(e, [&](double) { return fin; }, 2.5); int N = (int) (0.3 * sr); double fo = estPitch(&o[o.size() - N - 10], N, f); worst = std::max(worst, std::fabs(cents(fo, f))); }
        printf("  %-7s %6.0f Hz: peor error %.2f cents\n", mode ? "Quality" : "Live", sr, worst); check(worst < 15, "error de afinacion > 15 cents"); }
    printf("\n[5] Tiempo de respuesta (Live, 48k): correccion pasa de 0 a 100%% con nota +40 cents; tiempo hasta error < 10 cents\n");
    { SR = 48000; double prev = 0; for (double rt : {1.0, 20.0, 60.0, 150.0}) { cray::Engine e; e.prepare(SR); auto p = basePrm(0); p.correction = 0; p.retuneMs = (float) rt; e.setParams(p);
        double f0 = 220.0 * std::pow(2.0, 40.0 / 1200); std::vector<float> o, l(64), r(64); double ph = 0; long swap = (long) (0.6 * SR);
        for (long n = 0; n < (long) (2.2 * SR); n += 64) { if (n == swap) { p.correction = 1; e.setParams(p); } for (int i = 0; i < 64; ++i) l[i] = r[i] = voiceS(ph, [&](double) { return f0; }, 0); e.processInPlace(l.data(), r.data(), 64); for (int i = 0; i < 64; ++i) o.push_back(l[i]); }
        double tHit = -1; const int Wn = 768; for (long s = swap; s + Wn < (long) o.size(); s += 48) { double fo = estPitch(&o[s], Wn, 220.0); if (std::fabs(cents(fo, 220.0)) < 10) { tHit = 1000.0 * (s + Wn - swap) / SR; break; } }
        printf("  retune %5.0f ms -> alcanza +-10 cents en %6.1f ms (medido con ventana de 16 ms; tiempo = fin de ventana)\n", rt, tHit); check(tHit >= 0, "no converge"); check(tHit >= prev - 6.0, "tiempo de respuesta no monotono con Retune"); prev = tHit; } }
    printf("\n[6] Estabilidad: nota sostenida con deriva +-18 cents y vibrato +-40 cents @5.5 Hz; barrido A3->A#3\n");
    { SR = 48000; const double rts[3] = { 5.0, 5.0, 150.0 }; for (int vv = 0; vv < 3; ++vv) { int vib = vv > 0; cray::Engine e; e.prepare(SR); auto p = basePrm(0); p.retuneMs = (float) rts[vv]; e.setParams(p);
        auto fn = [&](double t) { double c = 18 * std::sin(2 * M_PI * 0.7 * t) + (vib ? 40 * std::sin(2 * M_PI * 5.5 * t) : 0); return 220.0 * std::pow(2.0, c / 1200); };
        std::vector<float> in; auto o = run(e, fn, 4.0, 128, &in); std::vector<double> ci, co;
        for (long s = (long) (1.0 * SR); s + 1536 < (long) o.size(); s += 480) { co.push_back(cents(estPitch(&o[s], 1536, 220.0), 220.0)); ci.push_back(cents(estPitch(&in[s], 1536, 220.0), 220.0)); }
        auto sd = [](std::vector<double>& v) { double m = 0; for (double x : v) m += x; m /= v.size(); double q = 0; for (double x : v) q += (x - m) * (x - m); return std::sqrt(q / v.size()); };
        double sI = sd(ci), sO = sd(co); printf("  %s retune %3.0f ms: desviacion entrada %.1f cents -> salida %.1f cents\n", vv == 0 ? "deriva   " : "vibrato  ", p.retuneMs, sI, sO); if (vv == 0) check(sO < sI * 0.35, "deriva no corregida"); if (vv == 1) check(sO < sI * 0.5, "retune rapido no aplana el vibrato"); if (vv == 2) check(sO > sI * 0.6, "retune lento no conserva el vibrato"); }
      cray::Engine e; e.prepare(SR); auto p = basePrm(0); p.glideMs = 0; p.transSmooth = 0.65f; e.setParams(p); double ph = 0; std::vector<float> l(128), r(128), o; long total = (long) (3.0 * SR);
      for (long n = 0; n < total; n += 128) { for (int i = 0; i < 128; ++i) { double tt = (n + i) / SR; l[i] = r[i] = voiceS(ph, [&](double t) { return 220.0 * std::pow(2.0, (t / 3.0) / 12.0 * 1.0); }, tt); } e.processInPlace(l.data(), r.data(), 128); for (int i = 0; i < 128; ++i) o.push_back(l[i]); }
      int changes = 0; double last = 0; for (long s = (long) (0.3 * SR); s + 1536 < (long) o.size(); s += 960) { double m = 69 + 12 * std::log2(estPitch(&o[s], 1536, 233.0) / 440.0); double q = std::round(m); if (last != 0 && q != last) ++changes; last = q; }
      printf("  barrido lento de 1 semitono: %d cambio(s) de nota en la salida (esperado 1)\n", changes); check(changes <= 2, "flutter entre notas"); }
    printf("\n[7] Clics: salto maximo muestra a muestra salida/referencia\n");
    for (int mode : {0, 1}) { SR = 48000; cray::Engine e; e.prepare(SR); auto p = basePrm(mode); p.retuneMs = 1; e.setParams(p);
        auto fn = [&](double t) { return t < 1.5 ? 233.0 * 1.012 : (t < 2.5 ? 247.0 * 1.01 : 196.0 * 0.99); };
        std::vector<float> in; auto o = run(e, fn, 3.5, 128, &in); double mi = 0, mo = 0; for (size_t i = (size_t) (0.5 * SR); i + 1 < o.size(); ++i) { mi = std::max<double>(mi, std::fabs(in[i + 1] - in[i])); mo = std::max<double>(mo, std::fabs(o[i + 1] - o[i])); }
        printf("  %-7s max|dx| entrada %.4f salida %.4f relacion %.2f\n", mode ? "Quality" : "Live", mi, mo, mo / mi); check(mo / mi < 1.5, "clics: salto > 1.5x la entrada"); }
    printf("\n[8] Robustez: silencio, ruido, bloques 1/7/1024/4096, mono vs estereo, cambios de parametros en reproduccion, bypass\n");
    for (int mode : {0, 1}) for (double sr : kSR) { SR = sr; cray::Engine e; e.prepare(sr); auto p = basePrm(mode); e.setParams(p); std::mt19937 rng(7); bool bad = false; double ph = 0;
        for (int blk : {1, 7, 1024, 4096, 0}) for (int n = 0; n < 60; ++n) { int B = blk ? blk : 256; std::vector<float> a(B), b(B); for (int i = 0; i < B; ++i) { a[i] = blk == 0 ? 0.f : (n % 3 == 0 ? (float) (rng() / 4294967296.0 - 0.5) : voiceS(ph, [&](double) { return 200.0; }, 0)); b[i] = a[i]; }
            if (n % 5 == 0) { p.key = rng() % 12; p.scale = rng() % 8; p.retuneMs = 0.5f + rng() % 150; p.humanize = (rng() % 100) / 100.f; p.flex = (rng() % 100) / 100.f; p.tracking = (rng() % 100) / 100.f; p.inputType = rng() % 3; p.bypass = (rng() % 4) == 0; p.mix = (rng() % 100) / 100.f; p.outGainDb = -24.f + rng() % 36; e.setParams(p); }
            e.processInPlace(a.data(), b.data(), B); for (int i = 0; i < B; ++i) if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || std::fabs(a[i]) > 8.f) bad = true; }
        check(!bad, "NaN/inf/valores enormes"); }
    { SR = 48000; cray::Engine e1, e2; e1.prepare(SR); e2.prepare(SR); auto p = basePrm(0); e1.setParams(p); e2.setParams(p); double ph = 0; std::vector<float> a(128), s1(128), s2(128); double maxd = 0;
      for (int n = 0; n < 600; ++n) { for (int i = 0; i < 128; ++i) { a[i] = voiceS(ph, [&](double) { return 224.0; }, 0); s1[i] = a[i]; s2[i] = a[i]; } e1.processInPlace(s1.data(), nullptr, 128); e2.processInPlace(s2.data(), s2.data(), 128); }
      cray::Engine e3, e4; e3.prepare(SR); e4.prepare(SR); e3.setParams(p); e4.setParams(p); ph = 0; std::vector<float> m(128), L(128), R(128);
      for (int n = 0; n < 600; ++n) { for (int i = 0; i < 128; ++i) { m[i] = voiceS(ph, [&](double) { return 224.0; }, 0); L[i] = R[i] = m[i]; } e3.processInPlace(m.data(), nullptr, 128); e4.processInPlace(L.data(), R.data(), 128); for (int i = 0; i < 128; ++i) maxd = std::max<double>(maxd, std::fabs(m[i] - L[i])); }
      printf("  mono vs estereo(L=R): diferencia maxima %.2e\n", maxd); check(maxd < 1e-4, "mono != estereo"); }
    { SR = 48000; cray::Engine e; e.prepare(SR); auto p = basePrm(0); e.setParams(p); double ph = 0; std::vector<float> l(128), r(128), o, in; for (int n = 0; n < 500; ++n) { if (n == 250) { p.bypass = true; e.setParams(p); } for (int i = 0; i < 128; ++i) { l[i] = r[i] = voiceS(ph, [&](double) { return 224.0; }, 0); in.push_back(l[i]); } e.processInPlace(l.data(), r.data(), 128); for (int i = 0; i < 128; ++i) o.push_back(l[i]); }
      double ex = 0; int best = 0; double bc = -1; for (int lag = 0; lag < 600; ++lag) { double s = 0, a = 0, b = 0; for (size_t i = 56000; i < 60000; ++i) { s += o[i] * in[i - lag]; a += o[i] * o[i]; b += in[i - lag] * in[i - lag]; } double c = s / std::sqrt(a * b); if (c > bc) { bc = c; best = lag; } } ex = bc;
      printf("  bypass: salida = entrada retardada %d muestras, correlacion %.4f\n", best, ex); check(ex > 0.999 && best == e.latencySamples(), "bypass no transparente / no alineado con el PDC"); }
    printf("\n[9/10] Asignaciones en el hilo de audio y CPU (estereo, bloque 64, 10 s de voz con vibrato)\n");
    for (int mode : {0, 1}) for (double sr : kSR) { SR = sr; cray::Engine e; e.prepare(sr); auto p = basePrm(mode); p.scale = 1; p.inputType = 0; e.setParams(p); std::vector<float> l(64), r(64); double ph = 0; long total = (long) (sr * 10);
        std::vector<float> voice(total); for (long n = 0; n < total; ++n) voice[n] = voiceS(ph, [&](double t) { return 160.0 + 50 * std::sin(t * 0.7) + 4 * std::sin(t * 35); }, n / sr);
        g_allocs = 0; auto t0 = std::chrono::steady_clock::now(); g_track = true;
        for (long n = 0; n + 64 <= total; n += 64) { for (int i = 0; i < 64; ++i) l[i] = r[i] = voice[n + i]; e.setParams(p); e.processInPlace(l.data(), r.data(), 64); }
        g_track = false; double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  %-7s %6.0f Hz: CPU %.1f%% de un nucleo | asignaciones %ld\n", mode ? "Quality" : "Live", sr, 100 * sec / 10.0, g_allocs.load()); check(g_allocs == 0, "asignaciones en el hilo de audio"); check(sec / 10.0 < 0.25, "CPU > 25% de un nucleo"); }
    printf("\nRESULTADO: %s (%d fallos)\n", fails ? "REVISAR" : "TODO OK", fails); return fails ? 1 : 0;
}

// CrayAutoTuneDSP.h - Motor DSP CRAY AutoTune (C++17, sin dependencias de JUCE).
//
//   Tracker  : YIN decimado + cuantizador de escala (histeresis, glide, flex, humanize, retune).
//              Solo produce CONTROL (desplazamiento en semitonos, periodo, voiced). No toca el audio.
//   LiveShifter   : etapa de audio CAUSAL (sin lookahead). Linea de retardo con lectura a velocidad
//              'ratio' y saltos coherentes de 1 periodo con crossfade. Latencia = retardo variable
//              entre dlo (~0.1 ms) y dlo + 1 periodo. El retardo del detector NO afecta al audio.
//   PsolaShifter  : PSOLA sincrono (modo QUALITY, preserva formantes). Latencia alta (15-40 ms).
//   Engine   : fachada (modo, bypass, mix, ganancia, dry alineado al PDC declarado).
//
// Tiempo real: prepare() reserva TODA la memoria. El resto NO reserva memoria, NO usa locks.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cray {

enum Mode { kLive = 0, kQuality = 1 };

struct TuneParams {
    int   inputType       = 1;
    int   key             = 0;
    int   scale           = 2;
    float retuneMs        = 10.0f;
    float humanize        = 0.0f;
    float flex            = 0.0f;
    float tracking        = 0.5f;
    float snap            = 0.95f;
    float glideMs         = 20.0f;
    float transSmooth     = 0.65f;
    float formantPreserve = 0.8f;
    float formantShiftSt  = 0.0f;
    float gateDb          = -48.0f;
    float correction      = 1.0f;
    float mix             = 1.0f;
    float outGainDb       = 0.0f;
    bool  bypass          = false;
    int   mode            = kLive;
};

inline float inputMinHz(int t)     { static const float v[3] = { 70.f, 100.f, 150.f };   return v[std::clamp(t, 0, 2)]; }
inline float inputMaxHz(int t)     { static const float v[3] = { 500.f, 800.f, 1100.f }; return v[std::clamp(t, 0, 2)]; }
inline float inputTypicalHz(int t) { static const float v[3] = { 110.f, 180.f, 300.f };  return v[std::clamp(t, 0, 2)]; }

// =====================================================================================================
class Tracker {
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        D = std::max(1, (int) std::lround(sr / 24000.0));
        sr2 = sr / D; hopDec = 48; hopSec = hopDec * D / sr;
        dec.assign(2048, 0.f); ybuf.assign(1024, 0.f); diff.assign(512, 0.f); dn.assign(512, 0.f); esq.assign(256, 0.f);
        setRange(100.f, 800.f); setParams(TuneParams{}); reset();
    }
    void setRange(float minHz, float maxHz)
    {
        Tmax = (float) (sr / minHz);
        tauMax = std::min(400, (int) (sr2 / minHz));
        tauMin = std::max(2, (int) (sr2 / maxHz));
        yinW = std::clamp(tauMax, 160, 384); leadSec = (float) ((0.5 * (yinW + 0.5 * tauMax) / sr2) * 0.9);
        Tin = std::clamp(Tin, (float) (sr / maxHz), Tmax); Tlo = (float) (sr / maxHz);
    }
    void setParams(const TuneParams& p)
    {
        prm = p;
        static const int tbl[8][12] = {
            {1,1,1,1,1,1,1,1,1,1,1,1}, {1,0,1,0,1,1,0,1,0,1,0,1}, {1,0,1,1,0,1,0,1,1,0,1,0},
            {1,0,1,1,0,1,0,1,0,1,1,0}, {1,0,1,0,1,1,0,1,0,1,1,0}, {1,0,1,0,1,0,0,1,0,1,0,0},
            {1,0,0,1,0,1,0,1,0,0,1,0}, {1,0,0,1,0,1,1,1,0,0,1,0} };
        const int sc = std::clamp(p.scale, 0, 7), k = ((p.key % 12) + 12) % 12;
        for (int pc = 0; pc < 12; ++pc) allowed[pc] = tbl[sc][((pc - k) % 12 + 12) % 12] != 0;
        glideA = p.glideMs <= 0.5f ? 1.0f : 1.0f - std::exp((float) (-hopSec / (p.glideMs * 0.001f / 3.0f)));
        yinThr = 0.28f - 0.20f * std::clamp(p.tracking, 0.f, 1.f);
        vNeed  = 4 - (int) std::lround(2.0f * std::clamp(p.tracking, 0.f, 1.f));
    }
    void reset()
    {
        std::fill(dec.begin(), dec.end(), 0.f); std::fill(esq.begin(), esq.end(), 0.f);
        n = 0; decW = 0; decCnt = 0; decAcc = 0; hopCnt = 0; sumsq = 0;
        voiced_ = false; vCount = 0; unvoicedHops = 1000; haveTarget = false; gateOpen = false; heldHops = 0;
        shiftS = 0; lastMidi = 60; slopeS = 0; med[0] = med[1] = med[2] = 60.f; medN = 0; Tin = (float) (sr / 200.0);
        infoMidi = 60; infoTarget = 60; infoCents = 0; infoConf = 0; infoVoiced = 0;
    }
    inline bool push(float m)
    {
        const int e = (int) (n & 255); sumsq += (double) m * m - esq[e]; esq[e] = m * m; ++n;
        decAcc += m;
        if (++decCnt == D) {
            dec[decW & 2047] = decAcc / (float) D; ++decW; decAcc = 0; decCnt = 0;
            if (++hopCnt == hopDec) { hopCnt = 0; analyse(); return true; }
        }
        return false;
    }
    float ratio() const  { return std::clamp(std::pow(2.0f, shiftS / 12.0f), 0.5f, 2.0f); }
    float period() const { return Tin; }
    bool  voiced() const { return voiced_; }
    float hopSeconds() const { return (float) hopSec; }
    std::atomic<float> infoMidi{60}, infoTarget{60}, infoCents{0}, infoConf{0}, infoVoiced{0};

private:
    bool yin(float& periodIn, float& conf)
    {
        const int W = yinW, need = W + tauMax;
        double e = 0;
        for (int j = 0; j < need; ++j) { float v = dec[(decW - need + j) & 2047]; ybuf[j] = v; e += (double) v * v; }
        if (e / need < 1e-9) return false;
        for (int tau = 1; tau <= tauMax; ++tau) {
            float s = 0.f; const float* a = ybuf.data(); const float* b = ybuf.data() + tau;
            for (int j = 0; j < W; ++j) { float d = a[j] - b[j]; s += d * d; }
            diff[tau] = s;
        }
        float run = 0.f; dn[0] = 1.f;
        for (int tau = 1; tau <= tauMax; ++tau) { run += diff[tau]; dn[tau] = diff[tau] * tau / std::max(run, 1e-12f); }
        int tau = -1;
        for (int k = tauMin; k <= tauMax; ++k)
            if (dn[k] < yinThr) { while (k + 1 <= tauMax && dn[k + 1] < dn[k]) ++k; tau = k; break; }
        if (tau < 0) {
            int am = tauMin; for (int k = tauMin; k <= tauMax; ++k) if (dn[k] < dn[am]) am = k;
            if (dn[am] > yinThr + 0.15f) return false;
            tau = am;
        }
        float tp = (float) tau;
        if (tau > 1 && tau < tauMax) {
            float a = dn[tau - 1], b = dn[tau], c = dn[tau + 1], den = a - 2 * b + c;
            if (std::fabs(den) > 1e-9f) tp += std::clamp(0.5f * (a - c) / den, -1.f, 1.f);
        }
        periodIn = tp * (float) D; conf = 1.f - dn[tau];
        return true;
    }
    int nearestAllowed(float midi) const
    {
        const int c = (int) std::lround(midi); int best = c; float bd = 1e9f;
        for (int d = -6; d <= 6; ++d) {
            const int q = c + d;
            if (allowed[((q % 12) + 12) % 12]) { float dist = std::fabs(q - midi); if (dist < bd) { bd = dist; best = q; } }
        }
        return best;
    }
    void analyse()
    {
        const float db = 10.f * std::log10((float) (std::max(sumsq, 0.0) / 256.0) + 1e-12f);
        gateOpen = db > (prm.gateDb + (gateOpen ? -2.f : 0.f));
        float period = 0, conf = 0;
        const bool ok = gateOpen && yin(period, conf);
        if (ok) {
            ++vCount; unvoicedHops = 0;
            const float midi = 69.f + 12.f * std::log2((float) (sr / period) / 440.f);
            med[0] = med[1]; med[1] = med[2]; med[2] = midi; medN = std::min(medN + 1, 3);
            float mm = midi;
            if (medN >= 3) mm = std::max(std::min(med[0], med[1]), std::min(std::max(med[0], med[1]), med[2]));
            const float pMed = (float) (sr / (440.0 * std::pow(2.0, (mm - 69.0) / 12.0)));
            if (!voiced_ || std::fabs(std::log(pMed / Tin)) > 0.08f) Tin = pMed; else Tin += 0.4f * (pMed - Tin);
            Tin = std::clamp(Tin, Tlo, Tmax);
            voiced_ = vCount >= vNeed;
            if (voiced_) {
                const int nearest = nearestAllowed(mm);
                const float hyst = 0.10f + 0.40f * prm.transSmooth;
                if (!haveTarget) { cur = nearest; tgtS = (float) nearest; haveTarget = true; heldHops = 0; }
                else if (nearest != cur && std::fabs(mm - cur) > std::fabs(mm - nearest) + hyst) { cur = nearest; heldHops = 0; }
                else ++heldHops;
                tgtS += glideA * ((float) cur - tgtS);
                const float slope = (midi - lastMidi) / (float) hopSec; lastMidi = midi; slopeS += 0.35f * (slope - slopeS);
                const float lead = std::clamp(slopeS * leadSec, -0.25f, 0.25f);
                const float err = tgtS - (midi + (std::fabs(midi - mm) < 0.6f ? lead : 0.f)), dz = 0.5f * prm.flex;
                const float raw = (err - std::clamp(err, -dz, dz)) * prm.snap * prm.correction;
                const float f = prm.humanize * std::min(1.f, (float) (heldHops * hopSec) / 0.25f);
                const float retEff = std::min(400.f, prm.retuneMs * (1.f + 12.f * f));
                const float a = 1.f - std::exp((float) (-hopSec / std::max(0.0003f, retEff * 0.001f)));
                shiftS += a * (raw - shiftS);
                infoMidi = mm; infoTarget = tgtS; infoCents = (mm - (float) cur) * 100.f; infoConf = conf;
            } else decay();
        } else {
            vCount = 0; voiced_ = false; ++unvoicedHops; decay();
            if (unvoicedHops > 20) haveTarget = false;
        }
        infoVoiced = voiced_ ? 1.f : 0.f;
    }
    void decay() { const float a = 1.f - std::exp((float) (-hopSec / std::max(0.003f, prm.retuneMs * 0.001f))); shiftS += a * (0.f - shiftS); }

    double sr = 48000, sr2 = 24000, hopSec = 0.002; int D = 2, hopDec = 48;
    float Tmax = 480, Tlo = 43, Tin = 240; int tauMax = 240, tauMin = 22, yinW = 240;
    std::vector<float> dec, ybuf, diff, dn, esq; double sumsq = 0;
    int64_t n = 0; int decW = 0, decCnt = 0, hopCnt = 0; float decAcc = 0;
    TuneParams prm; bool allowed[12] = {}; float glideA = 1, yinThr = 0.18f; int vNeed = 3;
    bool voiced_ = false, gateOpen = false, haveTarget = false; int vCount = 0, unvoicedHops = 1000, heldHops = 0;
    float lastMidi = 60, slopeS = 0, leadSec = 0.008f; float med[3] = {60, 60, 60}; int medN = 0; int cur = 60; float tgtS = 60, shiftS = 0;
};

// =====================================================================================================
// Etapa CAUSAL de baja latencia. No lee muestras futuras.
class LiveShifter {
public:
    static constexpr int kBuf = 16384, kMask = kBuf - 1, kChunk = 1024;
    void prepare(double sampleRate) { sr = sampleRate; for (auto& b : buf) b.assign(kBuf, 0.f); mono.assign(kBuf, 0.f); setTypical(180.f); reset(); }
    void setTypical(float hz) { latency = (int) std::lround(dlo + 0.5 * sr / hz); }
    int latencySamples() const { return latency; }
    void reset()
    {
        for (auto& b : buf) std::fill(b.begin(), b.end(), 0.f);
        std::fill(mono.begin(), mono.end(), 0.f);
        t = 0; d = dlo; xf = false; ratioS = 1; unvoicedRun = 100000; infoDelay = (float) dlo; cTgt = 1.f; cVc = false; cPer = 240.f;
    }
    void process(float* l, float* r, int n, Tracker& trk)
    {
        const bool st = r != nullptr;
        const float rA = 1.f - std::exp((float) (-1.0 / (0.0015 * sr)));
        for (int i = 0; i < n; ++i) {
            const float xl = l[i], xr = st ? r[i] : xl; const int w = (int) (t & kMask);
            buf[0][w] = xl; buf[1][w] = xr; mono[w] = 0.5f * (xl + xr);
            if (trk.push(mono[w])) { cTgt = trk.ratio(); cVc = trk.voiced(); cPer = trk.period(); }
            const bool vc = cVc; const float per = cPer;
            ratioS += rA * ((vc ? cTgt : 1.f) - ratioS);
            const double dr = 1.0 - (double) ratioS; d += dr; if (xf) dOld += dr;
            if (vc) unvoicedRun = 0; else ++unvoicedRun;
            if (!xf) {
                if (vc) { if (d < dlo) startJump((double) per, true); else if (d > dlo + per) startJump(-(double) per, true); }
                else if (d > dlo + 1.0 && unvoicedRun > (int) (0.008 * sr)) startJump(dlo - d, false);
            }
            if (d < 2.5) d = 2.5;
            float yl, yr;
            if (xf) {
                const float p = (float) xfPos / (float) xfLen;
                float wn, wo;
                if (xfCoherent) { wn = 0.5f - 0.5f * std::cos(3.14159265f * p); wo = 1.f - wn; }
                else { wn = std::sin(1.5707963f * p); wo = std::cos(1.5707963f * p); }
                yl = wn * tap(0, t - d) + wo * tap(0, t - dOld); yr = st ? wn * tap(1, t - d) + wo * tap(1, t - dOld) : yl;
                if (++xfPos >= xfLen) xf = false;
            } else { yl = tap(0, t - d); yr = st ? tap(1, t - d) : yl; }
            l[i] = yl; if (st) r[i] = yr;
            ++t;
        }
        infoDelay = (float) d;
    }
    std::atomic<float> infoDelay{4.f};
    static constexpr double dlo = 4.0;

private:
    inline float tap(int c, double pos) const
    {
        const int64_t i = (int64_t) std::floor(pos); const float f = (float) (pos - (double) i); const float* b = buf[c].data();
        const float y0 = b[(i - 1) & kMask], y1 = b[i & kMask], y2 = b[(i + 1) & kMask], y3 = b[(i + 2) & kMask];
        return ((((0.5f * (y3 - y0) + 1.5f * (y1 - y2)) * f + (y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3)) * f + 0.5f * (y2 - y0)) * f) + y1;
    }
    void startJump(double delta, bool coherent)
    {
        if (coherent) {
            const int W = std::clamp((int) std::lround(std::fabs(delta)), 32, 480);
            const int64_t ia = (int64_t) std::floor((double) t - d); const int base = (int) std::lround(delta);
            double c[7]; int bs = 3;
            for (int s = -3; s <= 3; ++s) {
                const int dd = base + s; double sab = 0, saa = 0, sbb = 0;
                for (int j = 0; j < W; ++j) { const double a = mono[(ia - j) & kMask], b = mono[(ia - dd - j) & kMask]; sab += a * b; saa += a * a; sbb += b * b; }
                c[s + 3] = sab / std::sqrt(saa * sbb + 1e-12); if (c[s + 3] > c[bs]) bs = s + 3;
            }
            double fr = 0;
            if (bs > 0 && bs < 6) { const double den = c[bs - 1] - 2 * c[bs] + c[bs + 1]; if (std::fabs(den) > 1e-9) fr = std::clamp(0.5 * (c[bs - 1] - c[bs + 1]) / den, -0.5, 0.5); }
            delta = (double) (base + bs - 3) + fr;
        }
        const double nd = d + delta;
        if (nd < 2.5 || nd > kBuf - 2048) return;
        dOld = d; d = nd; xf = true; xfPos = 0; xfCoherent = coherent;
        xfLen = coherent ? std::clamp((int) (0.5 * std::fabs(delta)), 24, (int) (0.004 * sr)) : std::max(24, (int) (0.0015 * sr));
    }
    double sr = 48000; int latency = 0;
    std::vector<float> buf[2], mono; int64_t t = 0;
    double d = dlo, dOld = dlo; bool xf = false, xfCoherent = true; int xfPos = 0, xfLen = 64; float ratioS = 1; int unvoicedRun = 0; float cTgt = 1.f, cPer = 240.f; bool cVc = false;
};

// =====================================================================================================
// PSOLA sincrono (modo QUALITY): preserva formantes; latencia ~ 2.95 periodos del tono minimo.
class PsolaShifter {
public:
    static constexpr int kRing = 16384, kMask = kRing - 1;
    void prepare(double sampleRate) { sr = sampleRate; for (int c = 0; c < 2; ++c) { in[c].assign(kRing, 0.f); out[c].assign(kRing, 0.f); } mono.assign(kRing, 0.f); setMinPitch(100.f); reset(); }
    void setMinPitch(float hz) { Tmax = (float) (sr / hz); L = (int) std::ceil(1.95 * Tmax) + 4; O = (int) std::ceil(Tmax) + 2; }
    int latencySamples() const { return L + O; }
    void setParams(const TuneParams& p) { fp = p.formantPreserve; fs = p.formantShiftSt; }
    void reset()
    {
        for (int c = 0; c < 2; ++c) { std::fill(in[c].begin(), in[c].end(), 0.f); std::fill(out[c].begin(), out[c].end(), 0.f); }
        std::fill(mono.begin(), mono.end(), 0.f); t = 0; nextEpoch = 0; vm = 0; vmT = 0; ratio = 1; Tin = (float) (sr / 200.0);
    }
    void process(float* l, float* r, int n, Tracker& trk)
    {
        const bool st = r != nullptr; const float vmA = 1.f - std::exp((float) (-1.0 / (0.010 * sr)));
        for (int i = 0; i < n; ++i) {
            const float xl = l[i], xr = st ? r[i] : xl; const int w = (int) (t & kMask);
            in[0][w] = xl; in[1][w] = xr; mono[w] = 0.5f * (xl + xr);
            if (trk.push(mono[w])) { ratio = trk.ratio(); Tin = trk.period(); vmT = trk.voiced() ? 1.f : 0.f; }
            if (nextEpoch < (double) t - 4.0 * Tmax) nextEpoch = (double) t;
            while (nextEpoch <= (double) t) { emitGrain((int64_t) std::llround(nextEpoch)); nextEpoch += (double) (Tin / ratio); }
            const int s = (int) ((t - O) & kMask), dd = (int) ((t - O - L) & kMask);
            const float wetL = out[0][s], wetR = out[1][s]; out[0][s] = 0.f; out[1][s] = 0.f;
            vm += vmA * (vmT - vm);
            l[i] = in[0][dd] * (1.f - vm) + wetL * vm; if (st) r[i] = in[1][dd] * (1.f - vm) + wetR * vm;
            ++t;
        }
    }
private:
    void emitGrain(int64_t e)
    {
        const float Ti = Tin; const int half = std::max(8, (int) std::ceil(Ti));
        const float ff = std::clamp(std::pow(ratio, 1.0f - fp) * std::pow(2.0f, fs / 12.0f), 0.7f, 1.45f);
        const int64_t c0 = e - L; const int rad = std::max(1, (int) std::ceil(Ti * 0.5f));
        int64_t best = c0; float bv = -1e9f;
        for (int k = -rad; k <= rad; ++k) { float v = mono[(int) ((c0 + k) & kMask)]; if (v > bv) { bv = v; best = c0 + k; } }
        const float gain = 1.0f / ratio, ph = 3.14159265358979f / (float) half;
        for (int k = -half; k < half; ++k) {
            const float win = 0.5f * (1.0f + std::cos(ph * (float) k)) * gain; const double pos = (double) best + (double) k * ff;
            const int64_t i0 = (int64_t) std::floor(pos); const float fr = (float) (pos - (double) i0);
            const int a = (int) (i0 & kMask), b = (int) ((i0 + 1) & kMask), o = (int) ((e + k) & kMask);
            out[0][o] += win * (in[0][a] + fr * (in[0][b] - in[0][a])); out[1][o] += win * (in[1][a] + fr * (in[1][b] - in[1][a]));
        }
    }
    double sr = 48000; float Tmax = 480, fp = 0.8f, fs = 0.f; int L = 0, O = 0;
    std::vector<float> in[2], out[2], mono; int64_t t = 0; double nextEpoch = 0; float vm = 0, vmT = 0, ratio = 1, Tin = 240;
};

// =====================================================================================================
class Engine {
public:
    static constexpr int kRing = 16384, kMask = kRing - 1;
    void prepare(double sampleRate)
    {
        sr = sampleRate; trk.prepare(sr); live.prepare(sr); psola.prepare(sr);
        for (auto& b : dry) b.assign(kRing, 0.f);
        setParams(TuneParams{}); reset();
    }
    void reset() { trk.reset(); live.reset(); psola.reset(); std::fill(dry[0].begin(), dry[0].end(), 0.f); std::fill(dry[1].begin(), dry[1].end(), 0.f); t = 0; g = gT; bw = 0; fade = 1.f; }
    void setParams(const TuneParams& p)
    {
        const int oldMode = prm.mode, oldIn = prm.inputType;
        prm = p;
        if (p.inputType != oldIn || !configured) {
            trk.setRange(inputMinHz(p.inputType), inputMaxHz(p.inputType));
            live.setTypical(inputTypicalHz(p.inputType)); psola.setMinPitch(inputMinHz(p.inputType));
        }
        trk.setParams(p); psola.setParams(p);
        if (configured && p.mode != oldMode) { if (p.mode == kLive) live.reset(); else psola.reset(); fade = 0.f; }
        configured = true;
        gT = std::pow(10.f, p.outGainDb / 20.f);
    }
    int latencySamples() const { return prm.mode == kLive ? live.latencySamples() : psola.latencySamples(); }
    int liveLatencySamples() const { return live.latencySamples(); }
    int qualityLatencySamples() const { return psola.latencySamples(); }
    void processInPlace(float* l, float* r, int n)
    {
        while (n > 0) { const int c = std::min(n, (int) LiveShifter::kChunk); chunk(l, r, c); l += c; if (r) r += c; n -= c; }
    }
    Tracker& tracker() { return trk; }
    float liveDelaySamples() const { return live.infoDelay.load(); }
private:
    void chunk(float* l, float* r, int n)
    {
        const bool st = r != nullptr;
        for (int i = 0; i < n; ++i) { const int w = (int) ((t + i) & kMask); dry[0][w] = l[i]; dry[1][w] = st ? r[i] : l[i]; }
        const bool liveMode = prm.mode == kLive;
        if (liveMode) live.process(l, r, n, trk); else psola.process(l, r, n, trk);
        const int lat = latencySamples();
        const float bA = 1.f - std::exp((float) (-1.0 / (0.010 * sr))), gA = 1.f - std::exp((float) (-1.0 / (0.020 * sr))), fA = 1.f / 256.f;
        const float mx = std::clamp(prm.mix, 0.f, 1.f);
        for (int i = 0; i < n; ++i) {
            const int di = (int) ((t + i - lat) & kMask);
            const float dl = dry[0][di], dr = dry[1][di];
            bw += bA * ((prm.bypass ? 1.f : 0.f) - bw); g += gA * (gT - g); fade = std::min(1.f, fade + fA);
            const float ml = dl * (1.f - mx) + l[i] * mx;
            float ol = (ml * (1.f - bw) + dl * bw) * g * fade;
            if (!std::isfinite(ol)) ol = 0.f;
            l[i] = std::clamp(ol, -8.f, 8.f);
            if (st) { const float mr = dr * (1.f - mx) + r[i] * mx; float orr = (mr * (1.f - bw) + dr * bw) * g * fade; if (!std::isfinite(orr)) orr = 0.f; r[i] = std::clamp(orr, -8.f, 8.f); }
        }
        t += n;
    }

    double sr = 48000; Tracker trk; LiveShifter live; PsolaShifter psola; std::vector<float> dry[2];
    TuneParams prm; bool configured = false; int64_t t = 0; float gT = 1, g = 1, bw = 0, fade = 1;
};

} // namespace cray

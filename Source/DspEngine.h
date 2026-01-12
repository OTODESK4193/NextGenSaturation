// --- START OF FILE DspEngine.h ---

#pragma once
#include <JuceHeader.h>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

// ==============================================================================
// 1. High Precision Filter (True 1-Pole + TPT)
// ==============================================================================

class OnePoleFilter {
public:
    void reset() { z1 = 0.0; }
    inline double processLP(double in, double freq, double sr) {
        double b1 = 1.0 - std::exp(-juce::MathConstants<double>::twoPi * freq / sr);
        z1 += b1 * (in - z1);
        return z1;
    }
    inline double processHP(double in, double freq, double sr) {
        double lp = processLP(in, freq, sr);
        return in - lp;
    }
private:
    double z1 = 0.0;
};

class HighPrecisionFilter {
public:
    enum Type { LowPass = 0, HighPass = 1 };
    enum Slope { Slope6dB = 0, Slope12dB, Slope24dB, Slope48dB };

    void prepare(double sampleRate) {
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = 4096;
        spec.numChannels = 1;
        onePole.reset();
        for (auto& f : filters) { f.prepare(spec); f.reset(); }
    }

    void reset() {
        onePole.reset();
        for (auto& f : filters) f.reset();
    }

    void setParams(Type type, double freq, Slope slope) {
        if (currentType == type && std::abs(currentFreq - freq) < 0.01 && currentSlope == slope && spec.sampleRate == currentSampleRate) {
            return;
        }

        currentType = type;
        currentFreq = freq;
        currentSlope = slope;
        currentSampleRate = spec.sampleRate;

        isBypassed = false;
        if (type == LowPass && freq >= 19950.0) isBypassed = true;
        if (type == HighPass && freq <= 20.5) isBypassed = true;
        if (isBypassed) return;

        if (slope == Slope6dB) return;

        auto filterType = (type == LowPass) ? juce::dsp::StateVariableTPTFilterType::lowpass
            : juce::dsp::StateVariableTPTFilterType::highpass;

        int numStages = 1;
        if (slope == Slope12dB) numStages = 1;
        else if (slope == Slope24dB) numStages = 2;
        else if (slope == Slope48dB) numStages = 4;

        activeStages = numStages;

        for (int i = 0; i < 4; ++i) {
            if (i < numStages) {
                double q = 0.7071; // Default

                if (slope == Slope24dB) {
                    q = (i == 0) ? 0.5412 : 1.3066;
                }
                else if (slope == Slope48dB) {
                    if (i == 0) q = 0.5098;
                    else if (i == 1) q = 0.6013;
                    else if (i == 2) q = 0.8999;
                    else q = 2.5629;
                }

                filters[i].setType(filterType);
                filters[i].setCutoffFrequency(freq);
                filters[i].setResonance(q);
            }
        }
    }

    inline double process(double in) {
        if (isBypassed) return in;
        if (currentSlope == Slope6dB) {
            if (currentType == LowPass) return onePole.processLP(in, currentFreq, currentSampleRate);
            else return onePole.processHP(in, currentFreq, currentSampleRate);
        }
        double out = in;
        for (int i = 0; i < activeStages; ++i) {
            out = filters[i].processSample(0, out);
        }
        return out;
    }

private:
    juce::dsp::ProcessSpec spec;
    std::array<juce::dsp::StateVariableTPTFilter<double>, 4> filters;
    OnePoleFilter onePole;
    double currentSampleRate = 44100.0;
    Type currentType = LowPass;
    Slope currentSlope = Slope12dB;
    double currentFreq = 1000.0;
    int activeStages = 1;
    bool isBypassed = false;
};

// ==============================================================================
// 2. Saturation Core (Release Candidate v2)
// ==============================================================================

class SaturationCore {
public:
    void prepare(double sampleRate) {
        if (currentSampleRate == sampleRate) return;
        currentSampleRate = sampleRate;

        dcBlockerCoef = 1.0 - (220.0 / sampleRate);

        double ratio = sampleRate / 44100.0;
        tapeCoefBase = 1.0 / ratio;
        tapeDeemphCoef = 0.3 / ratio;

        transCoef = 0.1 / ratio;
        exciterCoef = std::pow(0.9, 1.0 / ratio);

        sagAttack = 1.0 - std::exp(-1.0 / (0.02 * sampleRate));
        sagRelease = 1.0 - std::exp(-1.0 / (0.10 * sampleRate));
    }

    void reset() {
        lastX = 0.0;
        lastF = 0.0;
        tapeFilterState = 0.0;
        tapeDeemphState = 0.0;
        transFilterState = 0.0;
        dcBlockerState = 0.0;
        lastX_DC = 0.0;
        sampleHoldVal = 0.0;
        sampleHoldCounter = 0.0;
        sagEnvelope = 0.0;
    }

    // --- Helper Math Functions ---
    inline double langevin(double x) {
        if (std::abs(x) < 1.0e-5) return x / 3.0;
        return (1.0 / std::tanh(x)) - (1.0 / x);
    }

    inline double intLangevin(double x) {
        if (std::abs(x) < 1.0e-5) return x * x / 6.0;
        double v = std::sinh(x) / x;
        return std::log(std::abs(v));
    }

    inline double intFrohlich(double x, double b) {
        if (std::abs(x) < 1.0e-5) return x * x / 2.0;
        double absX = std::abs(x);
        return (absX / b) - (std::log(1.0 + b * absX) / (b * b));
    }

    // --- ADAA Antiderivative Selector ---
    inline double getADAAFunc(double x, int type, double character) {
        switch (type) {
        case 0: // Analog Tape (Normalized 3x)
            return 3.0 * intLangevin(x);

        case 1: // Tube Triode (Normalized)
        {
            double k = 0.5 + character * 1.5;
            if (x > 0) return (x / k) - (std::log(1.0 + k * x) / (k * k));
            else return 0.5 * x * x;
        }
        case 2: // Tube Pentode
            return (0.5 * x * x) - (x * x * x * x * 0.08333333);
        case 3: // Transformer
            return intFrohlich(x, 0.5 + character * 0.5);
        case 4: // Console
            return std::sqrt(1.0 + x * x);
        case 5: // JFET
            return (0.5 * x * x) - ((0.2 + character * 0.3) * x * x * x / 3.0);
        case 6: // BJT (Refined: Linear at 0)
        {
            // y = (1 - exp(-kx))/k for x > 0
            // y = x for x <= 0
            // Int y (pos) = x/k + exp(-kx)/k^2 - 1/k^2 (Constant adjusted for continuity at 0)
            // Int y (neg) = 0.5 * x * x
            double k = 0.1 + character * 5.0;
            if (x > 0) return (x / k) + (std::exp(-k * x) / (k * k)) - (1.0 / (k * k));
            else return 0.5 * x * x;
        }
        case 7: // Diode (Normalized)
        {
            double k = 1.5 + character * 3.0;
            double absX = std::abs(x);
            return (absX + std::exp(-k * absX) / k) / k;
        }
        case 8: // Soft Tanh
            if (std::abs(x) > 10.0) return std::abs(x) - 0.693147;
            return std::log(std::cosh(x));
        case 9: // Hard Clip
            if (x < -1.0) return -x - 0.5;
            if (x > 1.0) return x - 0.5;
            return 0.5 * x * x;
        case 10: // Wavefold (Refined Range)
        {
            // y = sin(w * x)
            // Int y = -cos(w * x) / w
            double w = (0.5 + character * 2.5) * juce::MathConstants<double>::pi; // Range 0.5pi to 3.0pi
            return -std::cos(x * w) / w;
        }
        case 11: // Rectify
            return 0.5 * x * std::abs(x);
        default: return 0.0;
        }
    }

    // --- Auto Makeup Gain ---
    inline double getMakeupGain(int type) {
        switch (type) {
        case 2: return 1.2;  // Pentode
        case 3: return 1.1;  // Transformer
        case 5: return 1.4;  // JFET
        case 6: return 1.0;  // BJT (Now Unity at 0dB)
        case 10: return 3.2; // Wavefold (Compensate for 0.2x input scaling)
        default: return 1.0;
        }
    }

    // --- Main Process ---
    inline double process(double in, int type, double driveDB, double character) {
        // 1. Dynamic Bias (Sag)
        double sagMod = 1.0;
        if (type <= 6) {
            double inputPower = std::abs(in);
            if (inputPower > sagEnvelope) sagEnvelope += sagAttack * (inputPower - sagEnvelope);
            else sagEnvelope += sagRelease * (inputPower - sagEnvelope);

            double sagAmount = juce::jlimit(0.0, 1.0, driveDB / 12.0);
            sagMod = 1.0 - (sagEnvelope * 0.15 * sagAmount);
        }

        double drive = std::pow(10.0, driveDB / 20.0);
        double x = in * drive * sagMod;

        // Pre-Processing
        if (type == 0) { // Analog Tape
            double coef = (0.05 + 0.55 * character) * tapeCoefBase;
            double w = x - coef * tapeFilterState;
            tapeFilterState = w;
            x = w + coef * x;
        }
        if (type == 3) { // Transformer
            transFilterState += transCoef * (x - transFilterState);
            double low = transFilterState;
            x = x + low * (character * 2.0);
        }
        if (type == 8 && character > 0.0) { // Soft Tanh
            x += character * 0.5;
        }

        double dryRect = x;

        // Input Scaling for Wavefold
        if (type == 10) x *= 0.2; // Wavefold Tame

        // Core Saturation
        bool useADAA = (type <= 11);
        double out = 0.0;

        if (useADAA) {
            double Fx = getADAAFunc(x, type, character);
            if (std::abs(x - lastX) < 1.0e-6) {
                switch (type) {
                case 0: out = 3.0 * langevin(x); break;
                case 1: {
                    double k = 0.5 + character * 1.5;
                    out = (x > 0) ? (x / (1.0 + k * x)) : x;
                    break;
                }
                case 2: out = x - (x * x * x / 3.0); break;
                case 3: out = x / (1.0 + (0.5 + character * 0.5) * std::abs(x)); break;
                case 4: out = x / std::sqrt(1.0 + x * x); break;
                case 5: out = x - (0.2 + character * 0.3) * x * x; break;
                case 6: { // BJT (Refined)
                    double k = 0.1 + character * 5.0;
                    out = (x > 0) ? ((1.0 - std::exp(-k * x)) / k) : x;
                    break;
                }
                case 7: { // Diode
                    double k = 1.5 + character * 3.0;
                    out = (x > 0) ? ((1.0 - std::exp(-k * x)) / k) : ((-1.0 + std::exp(k * x)) / k);
                    break;
                }
                case 8: out = std::tanh(x); break;
                case 9: out = juce::jlimit(-1.0, 1.0, x); break;
                case 10: { // Wavefold (Refined Range)
                    double w = (0.5 + character * 2.5) * juce::MathConstants<double>::pi;
                    out = std::sin(x * w);
                    break;
                }
                case 11: out = std::abs(x); break;
                }
            }
            else {
                out = (Fx - lastF) / (x - lastX);
            }
            lastX = x;
            lastF = Fx;
        }
        else {
            switch (type) {
            case 12: // Bitcrush
            {
                double rateDiv = 1.0 + (character * 49.0);
                sampleHoldCounter += 1.0;
                if (sampleHoldCounter >= rateDiv) {
                    sampleHoldCounter = 0.0;
                    sampleHoldVal = x;
                }
                double heldSignal = sampleHoldVal;
                double bits = 16.0 - (character * 14.0);
                if (bits < 1.0) bits = 1.0;
                double steps = std::pow(2.0, bits);
                out = std::round(heldSignal * steps) / steps;
                break;
            }
            case 13: // Exciter
            {
                double hpf = x - exciterCoef * lastX;
                double k = 0.5;
                double drivenHPF = hpf * 1.5;
                double dist = (drivenHPF > 0) ? (drivenHPF / (1.0 + k * drivenHPF)) : drivenHPF;
                out = x + (character * 2.0) * dist;
                break;
            }
            default: out = std::tanh(x);
            }
        }

        // Post-Processing
        if (type == 0) { // Tape
            tapeDeemphState = (1.0 - tapeDeemphCoef) * tapeDeemphState + tapeDeemphCoef * out;
            out = tapeDeemphState;
        }
        if (type == 11) { // Rectify
            out = dryRect * (1.0 - character) + out * character;
        }

        out *= getMakeupGain(type);

        double dcOut = out - lastX_DC + dcBlockerCoef * dcBlockerState;
        lastX_DC = out;
        dcBlockerState = dcOut;

        return dcOut;
    }

private:
    double currentSampleRate = 0.0;

    double dcBlockerCoef = 0.995;
    double tapeCoefBase = 0.4;
    double tapeDeemphCoef = 0.3;
    double transCoef = 0.1;
    double exciterCoef = 0.9;

    double sagAttack = 0.0;
    double sagRelease = 0.0;

    double lastX = 0.0;
    double lastF = 0.0;
    double tapeFilterState = 0.0;
    double tapeDeemphState = 0.0;
    double transFilterState = 0.0;
    double lastX_DC = 0.0;
    double dcBlockerState = 0.0;

    double sampleHoldVal = 0.0;
    double sampleHoldCounter = 0.0;
    double sagEnvelope = 0.0;
};
# Brick2SDR

Qt6 console for the **Brick2** HF SDR (EU1SW / LinOObs). The radio is treated as an **ANAN-10E / Hermes II** on **OpenHPSDR Protocol 2**.

This is a Thetis-class station program: panadapter and waterfall, dual VFO, dual RX, WDSP-style DSP blocks, VAC, CAT, CWX, PureSignal, Apollo SWR, and Brick2 front-end control (step attenuator, LNA, 7 LPF via Alex words, 15 W PA).

## Hardware profile

| Item | Brick2 |
|---|---|
| Protocol | OpenHPSDR Protocol 2, UDP |
| Identity in software | ANAN-10E / Hermes II |
| Clock | 122.88 MHz (phase word, not Hz) |
| ADC / DAC | 14- or 16-bit ADC, 14-bit DAC |
| Receivers | 2 DDC + 1 DUC |
| Coverage | 100 kHz – 61 MHz |
| Sample rates | 48 / 96 / 192 / 384 kHz |
| PA | ~15 W, Apollo-style SWR/PWR |
| Linearization | PureSignal feedback SMA |
| Front end | 0–31 dB step ATT, LNA, 7 LPF |

## Protocol 2 ports

Host → radio: 1024 general, 1025 RX specific, 1026 TX specific, 1027 high priority, 1028 RX audio, 1029 TX I/Q.

Radio → host: 1024 discovery/response, 1025 high-priority status, 1026 mic, 1027 wideband, 1035+ DDC I/Q.

## Build (Windows)

Install Qt 6.5+ with MinGW or MSVC, then:

```bat
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
cmake --build build --config Release
```

## Run

1. Put the Brick2 and PC on the same LAN (DHCP or APIPA).
2. Start Brick2SDR, click **DISCOVER**.
3. For a UI checkout without hardware, click **START** (Brick2 simulator).
4. In **Setup → General**, leave Apollo enabled so SWR/PWR match the Brick2 bridge.

Use the **MW0LGE Thetis** ANAN build as the reference for gateware/firmware behaviour. Do not mix Hermes-Lite Protocol 1 stacks with this radio.

## Feature map (Thetis → this console)

- VFOs A/B, split, A>B / A<B / A<>B, band stack, memories
- Modes LSB USB DSB CWL CWU FM AM SAM DIGL DIGU SPEC DRM
- Filters F1–F10 / VAR, click-tune, wheel, zoom
- Displays: spectrum, panadapter, waterfall, panafall, panascope, scope, histogram
- DSP: AGC (off/long/slow/med/fast), NR, NR2, ANF, NB, NB2, SNB, BIN, SQL
- TX: MOX, TUN, VOX, COMP, CESSB, leveler, CFC, TX EQ, drive, mic
- PureSignal + two-tone linearity window
- VAC1/VAC2 settings, CAT TCP (Kenwood + Thetis ZZ)
- CW keyer / CWX, sidetone, iambic A/B, break-in
- RX2, diversity, multi-RX, DUP
- Meters: S, PWR, SWR, ALC, MIC, ADC overload, supply

DSP here uses **WDSP 2.10** (Warren Pratt, NR0V / TAPR OpenHPSDR-wdsp). FFTW 3 is required at runtime (`libfftw3-3.dll` is copied next to the exe).

Use the **MW0LGE Thetis** ANAN build as the reference for gateware/firmware behaviour. Do not mix Hermes-Lite Protocol 1 stacks with this radio.

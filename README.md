# Justified Level Control (JLC) Audio Plugin (VST3 / AU)

A master chain monitoring utility audio plugin built in C++ using the **JUCE framework** (v8.0+). Inspired by the clinical dark vector aesthetic of *Youlean Loudness Meter 2*, this plugin combines a real-time FFT spectrum analyzer, true RMS/LUFS loudness metering, scrolling oscilloscope, vectorscope, and dynamic interactive goal line.

---

## Key Features

- **Real-Time FFT Spectrum Analyzer**: 2048-point FFT with Hann windowing rendered with translucent pure cyan gradients across a logarithmic 20 Hz – 20 kHz frequency scale.
- **ITU-R BS.1770 LUFS / RMS Meter**: Accurate short-term loudness calculation with K-weighting pre-filtering.
- **Stereo L/R Split Metering**: Independent Left/Right channel volume tracking with dual side-by-side vertical meters.
- **Draggable Target Goal Line**: Interactive horizontal target line (default `-14.0 LUFS`). Drag vertically across the UI to set target limits.
- **Scrolling Oscilloscope & Vectorscope**: Real-time waveform scrolling and stereo phase correlation vectorscope.
- **Comprehensive Tooltip & Theme Support**: Integrated light/dark theme switcher and scaling controls.

---

## Project Structure

```
JustifiedLevelControl/
├── CMakeLists.txt              # Cross-platform CMake setup with JUCE FetchContent
├── README.md                   # Build and compilation guide
└── Source/
    ├── PluginProcessor.h       # Audio Processor DSP, FIFO, FFT engine, LUFS calculation
    ├── PluginProcessor.cpp     # K-weighting filter, processBlock, FFT spectrum generator
    ├── PluginEditor.h          # GUI headers & sub-components (Spectrum, VU Meter, Goal Line)
    └── PluginEditor.cpp        # 60 FPS vector renderer, interactive goal line drag, tooltips
```

---

## Build Prerequisites

1. **CMake**: Version 3.22 or higher.
2. **C++17 Compiler**:
   - **Windows**: Visual Studio 2022 (with "Desktop development with C++" workload) or MinGW-w64.
   - **macOS**: Xcode 14+ or Command Line Tools (`xcode-select --install`).
3. **Git**: Required by CMake `FetchContent` to download JUCE automatically during the build.

---

## Compilation Instructions

### 1. Building on Windows (VST3)

Open **PowerShell** or **Command Prompt** in the project folder:

```powershell
# Navigate to the project folder
cd path\to\JustifiedLevelControl

# Create a build directory
mkdir build
cd build

# Generate Visual Studio 2022 solution / CMake build files
cmake .. -G "Visual Studio 17 2022" -A x64

# Compile the VST3 plugin in Release mode
cmake --build . --config Release
```

After building:
- The compiled VST3 plugin will be located in: `build/JustifiedLevelControl_artefacts/Release/VST3/Justified Level Control.vst3`
- Copy `Justified Level Control.vst3` into your Windows VST3 directory:
  `C:\Program Files\Common Files\VST3\`

---

### 2. Building on macOS (VST3 & Audio Unit / AU)

Open **Terminal** in the project folder:

```bash
# Navigate to the project folder
cd /path/to/JustifiedLevelControl

# Create a build directory
mkdir build
cd build

# Generate Xcode project or Makefile
cmake .. -G Xcode

# Build both VST3 and AU targets in Release mode
cmake --build . --config Release
```

After building:
- **VST3 output**: `build/JustifiedLevelControl_artefacts/Release/VST3/Justified Level Control.vst3`
  - Install location: `~/Library/Audio/Plug-Ins/VST3/` or `/Library/Audio/Plug-Ins/VST3/`
- **Audio Unit (AU) output**: `build/JustifiedLevelControl_artefacts/Release/AU/Justified Level Control.component`
  - Install location: `~/Library/Audio/Plug-Ins/Components/` or `/Library/Audio/Plug-Ins/Components/`

---

## Testing in DAWs

1. Copy the built `.vst3` (or `.component` on macOS) file to your system plugin folder.
2. Open your DAW (e.g. Ableton Live, FL Studio, Reaper, Logic Pro, Cubase).
3. Rescan your VST3/AU plugins in the DAW preferences.
4. Insert **Justified Level Control** on your master channel.
5. Drag the cyan Goal Line vertically to customize your LUFS target level. Observe the meter turn red when master audio exceeds your target!

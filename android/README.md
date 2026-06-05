# Instruo Sampler — Android build

Touch-screen Android port of the granular sampler. Shares 100% of the DSP
code with the desktop build via the root `CMakeLists.txt`.

## Prerequisites

1. **Android Studio** (Iguana 2023.2.1 or newer) — install from
   <https://developer.android.com/studio>.
2. **SDK Platform 36** and **Build-Tools 36.0.0+** via Android Studio's
   SDK Manager.
3. **NDK 30.0.x** via Android Studio's SDK Manager (Tools → SDK Manager →
   SDK Tools tab → check "Show Package Details" → expand "NDK (Side by
   side)").
4. **A physical Android device** with USB debugging enabled, or an
   **AVD emulator image**.
5. **JDK 17** (Android Studio bundles its own JBR; system JDK 21 also works).

Edit `local.properties` to point at your SDK + NDK paths. The defaults assume:

```
sdk.dir=/home/kian/Android/Sdk
ndk.dir=/home/kian/Android/Sdk/ndk/30.0.14904198
```

## Building from the command line

```bash
cd android
./gradlew :app:assembleDebug
```

Output APK lands in `app/build/outputs/apk/debug/app-debug.apk`.

The first build can take **15–30 minutes** because it has to compile JUCE
plus the entire sampler DSP from source for two ABIs (arm64-v8a + x86_64).
Subsequent incremental builds take seconds.

## Installing and running

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.instruo.sampler/com.rmsl.juce.JuceActivity
```

To view logs while the app runs:

```bash
adb logcat -s sampler juce JUCE Oboe AAudio
```

## What's different from desktop

* UI is `MainComponentAndroid` (in [`../system/src/main_component_android.cpp`](../system/src/main_component_android.cpp)),
  not the desktop `MainComponent`.
* All rotary knob panels are replaced by horizontal bar sliders in
  slide-up `PanelSheet`s.
* The full-screen `TouchWaveformView` handles multi-touch voice triggering
  + per-voice scrubbing.
* Recording into per-layer 10-second buffers via the mic input.
* No file is loaded on launch (the desktop default sample isn't shipped).
* No VST3 build; standalone APK only.
* Locked landscape orientation.

## Known limitations (June 2026)

* The Instruo font is not packaged into the APK; the UI falls back to the
  system sans-serif font. To re-enable the custom font, copy
  `gui/fonts/elza-round-variable-light.otf` into `app/src/main/assets/`
  and update `AssetManager::get_resource_file` to read from `assets:`.
* Audio-thread file I/O for `Load Sample` still runs synchronously
  (same as desktop). On slow Android devices this can cause a brief audio
  glitch; move to a worker thread before production use.
* Touch contact area → voice level uses JUCE's `MouseEvent::pressure`
  where available; on devices that don't report pressure, all taps land
  at a fixed 0.8 level. See `TouchWaveformView::touch_level_from_event`.
* Only one finger may scrub an existing playhead at a time; additional
  finger grabs are ignored. Multi-touch voice triggering is fully
  supported (up to 8 simultaneous voices, capped by the voice pool).

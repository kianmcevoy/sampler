#ifndef GUI_MAIN_COMPONENT_ANDROID_H
#define GUI_MAIN_COMPONENT_ANDROID_H

#include "JuceHeader.h"

#if JUCE_ANDROID

#include "instrument/constants.hpp"
#include "instrument/parameter_data.hpp"
#include "interface/mode_controller.hpp"
#include "system/panel_sheet.hpp"
#include "system/touch_waveform.hpp"

#include "igui/igui.hpp"
#include "igui/instruo.hpp"

#include <array>
#include <map>
#include <memory>

class EngineAudioProcessor;
class EngineAudioProcessorEditor;

/** Touch-driven top-level component for the Android build.
 *
 *  Layout (landscape, scales to screen):
 *
 *      +----------------------------------------------------------+
 *      | [1][2][3][4][5][6][7][8] [V/L] [Global]                  |  top row (8 layer/voice + view + global)
 *      +----------------------------------------------------------+
 *      |                                                          |
 *      |               TouchWaveformView                          |  main touch surface
 *      |               + voice playheads                          |
 *      |                                                          |
 *      +----------------------------------------------------------+
 *      | [REC] [PLAY] [STOP] [ERASE] [LOAD]      [CTRLS] [MOD]    |  transport + panel tabs
 *      +----------------------------------------------------------+
 *
 *  CTRLS / MOD tap brings up a PanelSheet covering the bottom ~70% of the
 *  screen with horizontal bar sliders for the main / modulation panel.
 *
 *  Reuses the same ModeController, file-chooser handshake, snap-on-select,
 *  and per-layer parameter snapshots that the desktop MainComponent uses.
 */
class MainComponentAndroid:
    public juce::Component,
    public juce::Timer
{
public:
    MainComponentAndroid(EngineAudioProcessor& processor,
                         EngineAudioProcessorEditor& editor);
    ~MainComponentAndroid() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    // --- UI children ---
    TouchWaveformView                       waveform_view_;
    std::array<std::unique_ptr<igui::LedButton>, max_voices> layer_voice_buttons_;
    std::unique_ptr<igui::LedButton>        view_toggle_button_;     // Voice/Layer view
    std::unique_ptr<igui::LedButton>        global_button_;          // Global mode
    std::unique_ptr<igui::LedButton>        record_button_;
    std::unique_ptr<igui::LedButton>        play_button_;
    std::unique_ptr<igui::LedButton>        stop_button_;
    std::unique_ptr<igui::LedButton>        erase_button_;
    std::unique_ptr<igui::LedButton>        load_button_;
    std::unique_ptr<igui::LedButton>        controls_tab_button_;
    std::unique_ptr<igui::LedButton>        modulation_tab_button_;

    PanelSheet controls_sheet_;
    PanelSheet modulation_sheet_;

    // --- Mode / view state ---
    ModeController                          mode_controller_;
    int                                     current_layer_ { 0 };
    bool                                    layer_view_    { false };

    // --- Per-layer parameter snapshots (mirrors desktop MainComponent) ---
    struct LayerParamSnapshot
    {
        std::map<juce::String, float> sliders;
        std::map<juce::String, bool>  buttons;
        std::map<juce::String, int>   dropdowns;
    };
    std::array<LayerParamSnapshot, max_layers> layer_param_snapshots_ {};

    // --- File chooser handshake (SAF on Android via JUCE) ---
    std::shared_ptr<juce::FileChooser>      file_chooser_;
    void check_file_chooser_request();

    // --- Voice-button click handler (reused by all 8 buttons) ---
    void on_voice_button_clicked(size_t voice_index);

    // --- Per-voice / per-layer snapshot helpers ---
    VoiceLiveParams snapshot_juce_params() const;
    void            apply_params_to_juce(const VoiceLiveParams& p);
    void            save_juce_into_layer(size_t layer_index);
    void            restore_layer_into_juce(size_t layer_index);
    bool            is_per_layer_param(const juce::String& id) const;

    // --- JUCE param helpers (mirrors desktop equivalents) ---
    void  set_bool_juce  (const juce::String& id, bool value);
    bool  read_juce_bool (const juce::String& id) const;
    void  set_float_juce (const juce::String& id, float displayed_value);
    float read_float_juce(const juce::String& id, float fallback) const;
    void  set_choice_juce(const juce::String& id, int index);

    // --- Triggers (write JUCE momentary params for one block) ---
    void fire_trigger(const juce::String& id);

    // ERASE is destructive (zeros the layer buffer). To prevent accidental
    // taps from blowing the buffer away — especially since the bottom row
    // is dense with similar-sized buttons — a single tap arms the button
    // for ~2 s and only a second tap within that window actually fires the
    // erase. Tapping anything else cancels the arming.
    bool          erase_armed_ { false };
    juce::int64   erase_arm_at_ms_ { 0 };

    EngineAudioProcessor&        processor_;
    EngineAudioProcessorEditor&  editor_;
    std::unique_ptr<igui::FontLifetimeManager> font_lifetime_manager_;
};

#endif // JUCE_ANDROID

#endif

#ifndef SYSTEM_TOUCH_WAVEFORM_H
#define SYSTEM_TOUCH_WAVEFORM_H

/** TouchWaveformView — full-screen touch surface for the Android UI.
 *
 *  Renders the currently-selected layer's waveform (read from GuiOutputData)
 *  plus playhead cursors for every active voice on that layer. Multi-touch:
 *    - Touch-down on empty waveform area → pushes a TouchTriggerEvent into
 *      GuiInputData::touch_event_queue. The Instrument allocates a voice and
 *      starts it at the touched X (= start fraction) and Y-derived level on
 *      the currently selected layer.
 *    - Touch-down on (within ±N px of) an existing playhead → grabs that voice
 *      for scrubbing. While the finger drags, the GUI writes
 *      voice_scrub_slot / voice_scrub_position / voice_scrub_level so the
 *      audio thread can route a per-voice scrub via the existing Voice-mode
 *      scrub path inside Instrument. Multi-touch scrubbing is limited to one
 *      voice at a time (most recent grab wins); additional grabs are ignored.
 *
 *  Repaint is driven externally (MainComponentAndroid's 100 ms timer).
 *
 *  This component only compiles into the Android target; on desktop the
 *  existing rotary-knob WaveformDisplay in panels.cpp is used instead.
 */

#include "JuceHeader.h"
#include "instrument/constants.hpp"

#include <array>

class EngineAudioProcessor;

class TouchWaveformView : public juce::Component
{
public:
    explicit TouchWaveformView(EngineAudioProcessor& processor);
    ~TouchWaveformView() override = default;

    void paint(juce::Graphics& g) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp  (const juce::MouseEvent& e) override;

    // Pixels of slop around a playhead within which a touch is treated as
    // "grab this voice" rather than "launch a new voice". Generous because
    // fingers are imprecise. Exposed for layout tuning.
    static constexpr float playhead_grab_radius_px = 36.f;

    // Pixels of slop around the start / end loop-edge lines within which a
    // touch is treated as "grab this edge" rather than launching a voice.
    // Same generosity as playhead grabs.
    static constexpr float loop_edge_grab_radius_px = 36.f;

    // Wire the touch view to a setter the parent (MainComponentAndroid) can
    // provide: a callback for writing a JUCE float param by id ("start" /
    // "length"). Routing through this callback rather than touching JUCE
    // directly keeps TouchWaveformView free of the JUCE param helper code.
    void set_param_writer(std::function<void(const juce::String&, float)> writer);

private:
    // Push one TouchTriggerEvent onto the GUI→audio ring.
    void push_touch_event(float start_fraction, float level, int target_layer);

    // Find the voice slot whose playhead is closest to `touch_x` (on the
    // currently selected layer). Returns -1 if none is within
    // playhead_grab_radius_px.
    int find_voice_under_touch(float touch_x) const;

    // Convert touch contact area (estimated from the MouseEvent's pressure
    // or, when unavailable, a fixed fallback) into a level [min_level, 1].
    float touch_level_from_event(const juce::MouseEvent& e) const;

    // What this finger is doing for the lifetime of its touch. Mutually
    // exclusive states:
    //   None        : finger ignored (e.g. went down outside content area).
    //   ScrubVoice  : drag scrubs the voice at scrubbing_slot.
    //   DragStart   : drag sets the loop start fraction.
    //   DragEnd     : drag sets the loop end fraction (length is recomputed).
    enum class FingerKind { None, ScrubVoice, DragStart, DragEnd };

    static constexpr size_t max_fingers = 10;
    struct FingerState
    {
        bool       active         { false };
        FingerKind kind           { FingerKind::None };
        int        scrubbing_slot { -1 };
    };
    std::array<FingerState, max_fingers> fingers_ {};

    // Helpers: where on the X axis are the start / end loop-edge lines
    // currently drawn? Returns -1 if no loop is visible.
    float loop_edge_x_for_start(const juce::Rectangle<float>& bounds) const;
    float loop_edge_x_for_end  (const juce::Rectangle<float>& bounds) const;

    // Convert a touch X within `bounds` into a fraction in [0, 1].
    float fraction_from_x(const juce::Rectangle<float>& bounds, float touch_x) const;

    EngineAudioProcessor& processor_;
    std::function<void(const juce::String&, float)> param_writer_;
};

#endif

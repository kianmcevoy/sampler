#include "system/touch_waveform.hpp"

#if JUCE_ANDROID

#include "system/engine.hpp"
#include "igui/instruo.hpp"

#include <algorithm>
#include <cmath>

TouchWaveformView::TouchWaveformView(EngineAudioProcessor& processor):
    processor_{processor}
{
    this->setWantsKeyboardFocus(false);
    // Receive every finger as an independent mouse source.
    this->setInterceptsMouseClicks(true, false);
}

void TouchWaveformView::set_param_writer(std::function<void(const juce::String&, float)> writer)
{
    param_writer_ = std::move(writer);
}

float TouchWaveformView::loop_edge_x_for_start(const juce::Rectangle<float>& bounds) const
{
    const auto& gui_output = this->processor_.get_gui_output_data();
    if (gui_output.waveform_left.empty()) return -1.f;
    const int num_samples = static_cast<int>(gui_output.waveform_left.size());
    const int start_sample = gui_output.display_marker_start.load();
    if (start_sample < 0 || start_sample > num_samples) return -1.f;
    return bounds.getX() + (static_cast<float>(start_sample) / num_samples) * bounds.getWidth();
}

float TouchWaveformView::loop_edge_x_for_end(const juce::Rectangle<float>& bounds) const
{
    const auto& gui_output = this->processor_.get_gui_output_data();
    if (gui_output.waveform_left.empty()) return -1.f;
    const int num_samples = static_cast<int>(gui_output.waveform_left.size());
    const int end_sample = gui_output.display_marker_end.load();
    if (end_sample <= 0 || end_sample > num_samples) return -1.f;
    return bounds.getX() + (static_cast<float>(end_sample) / num_samples) * bounds.getWidth();
}

float TouchWaveformView::fraction_from_x(const juce::Rectangle<float>& bounds, float touch_x) const
{
    return juce::jlimit(0.f, 1.f, (touch_x - bounds.getX()) / bounds.getWidth());
}

void TouchWaveformView::paint(juce::Graphics& g)
{
    // Background + 2 px outline. Matches the desktop WaveformDisplay's look
    // so the visual identity is consistent across platforms.
    g.fillAll(juce::Colours::black);
    g.setColour(igui::colours::light_grey);
    g.drawRect(this->getLocalBounds(), 2);

    const auto& gui_output = this->processor_.get_gui_output_data();
    const auto& gui_input  = this->processor_.get_gui_input_data();

    if (!gui_output.waveform_ready.load() || gui_output.waveform_left.empty())
    {
        g.setColour(igui::colours::grey);
        g.drawText("Tap REC to record, or LOAD to load a sample",
                   this->getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto& waveform = gui_output.waveform_left;
    const auto bounds    = this->getLocalBounds().reduced(4).toFloat();
    const int   num_samples = static_cast<int>(waveform.size());
    const float width       = bounds.getWidth();
    const float height      = bounds.getHeight();
    const float centre_y    = bounds.getCentreY();

    // Display markers (gold tint for the active loop range).
    const int start_sample = gui_output.display_marker_start.load();
    const int end_sample   = gui_output.display_marker_end.load();
    if (start_sample >= 0 && end_sample > start_sample && end_sample <= num_samples)
    {
        const float start_x = bounds.getX() + (static_cast<float>(start_sample) / num_samples) * width;
        const float end_x   = bounds.getX() + (static_cast<float>(end_sample)   / num_samples) * width;
        g.setColour(igui::colours::gold.withAlpha(0.15f));
        g.fillRect(start_x, bounds.getY(), end_x - start_x, height);
    }

    // Waveform draw (min/max per pixel).
    const int samples_per_pixel = std::max(1, num_samples / static_cast<int>(width));
    juce::Path path;
    bool first = true;
    g.setColour(igui::colours::gold);
    for (int x = 0; x < static_cast<int>(width); ++x)
    {
        const int s0 = x * samples_per_pixel;
        const int s1 = std::min(s0 + samples_per_pixel, num_samples);
        if (s0 >= num_samples) break;
        float lo = 0.f, hi = 0.f;
        for (int s = s0; s < s1; ++s)
        {
            const float v = waveform[s];
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        const float xp   = bounds.getX() + x;
        const float ytop = centre_y - hi * height * 0.4f;
        const float ybot = centre_y - lo * height * 0.4f;
        if (first) { path.startNewSubPath(xp, ytop); first = false; }
        path.lineTo(xp, ytop);
        path.lineTo(xp, ybot);
    }
    g.strokePath(path, juce::PathStrokeType(1.f));

    // Loop boundary markers.
    if (start_sample >= 0 && end_sample > start_sample && end_sample <= num_samples)
    {
        const float start_x = bounds.getX() + (static_cast<float>(start_sample) / num_samples) * width;
        const float end_x   = bounds.getX() + (static_cast<float>(end_sample)   / num_samples) * width;
        g.setColour(igui::colours::gold.withAlpha(0.7f));
        g.drawLine(start_x, bounds.getY(), start_x, bounds.getBottom(), 2.f);
        g.drawLine(end_x,   bounds.getY(), end_x,   bounds.getBottom(), 2.f);
    }

    // Markers (slice grid / transient onsets).
    const int marker_count = gui_output.marker_count.load();
    if (marker_count > 0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        for (int i = 0; i < marker_count; ++i)
        {
            const int m = gui_output.marker_positions[i].load();
            if (m < 0 || m >= num_samples) continue;
            const float mx = bounds.getX() + (static_cast<float>(m) / num_samples) * width;
            g.drawLine(mx, bounds.getY(), mx, bounds.getBottom(), 1.f);
        }
    }

    // Voice playheads (only voices on the currently displayed layer). Larger
    // hit target than desktop because fingers are imprecise.
    const int selected_layer = gui_input.selected_layer.load();
    for (size_t i = 0; i < max_voices; ++i)
    {
        if (!gui_output.voice_active[i].load()) continue;
        if (gui_output.voice_layer[i].load() != selected_layer) continue;
        const float vp  = gui_output.voice_position[i].load();
        const float vol = gui_output.voice_volume[i].load();
        if (vp < 0.f || vp >= static_cast<float>(num_samples)) continue;

        const float vx = bounds.getX() + (vp / num_samples) * width;
        const float alpha = juce::jlimit(0.05f, 1.f, vol);
        g.setColour(igui::colours::gold.withAlpha(alpha));
        // Thicker line + small head circle for fingerable identification.
        g.drawLine(vx, bounds.getY(), vx, bounds.getBottom(), 3.f);
        const float head_r = 8.f + 6.f * vol;
        g.fillEllipse(vx - head_r * 0.5f, centre_y - head_r * 0.5f, head_r, head_r);
    }

    // Centre line.
    g.setColour(igui::colours::grey.withAlpha(0.5f));
    g.drawLine(bounds.getX(), centre_y, bounds.getRight(), centre_y, 1.f);

    // Recording overlay — pulsing red border + progress bar at the top.
    if (gui_output.is_recording.load())
    {
        const float prog = juce::jlimit(0.f, 1.f, gui_output.record_progress.load());
        g.setColour(juce::Colours::red.withAlpha(0.6f));
        g.drawRect(this->getLocalBounds(), 4);
        g.setColour(juce::Colours::red);
        g.fillRect(bounds.getX(), bounds.getY(), prog * width, 6.f);
    }
}

void TouchWaveformView::mouseDown(const juce::MouseEvent& e)
{
    const int finger_idx = e.source.getIndex();
    if (finger_idx < 0 || finger_idx >= static_cast<int>(max_fingers)) return;

    const auto bounds = this->getLocalBounds().reduced(4).toFloat();
    if (!bounds.contains(e.position)) return;

    auto& finger = this->fingers_[static_cast<size_t>(finger_idx)];
    finger.active = true;
    finger.kind = FingerKind::None;
    finger.scrubbing_slot = -1;

    // Priority order on touch-down:
    //   1. Within ±N px of a loop edge line  → drag that edge.
    //   2. Within ±N px of an existing playhead → scrub that voice.
    //   3. Empty area → launch a new voice.

    const float start_x = this->loop_edge_x_for_start(bounds);
    const float end_x   = this->loop_edge_x_for_end  (bounds);
    const bool has_loop = (start_x >= 0.f && end_x >= 0.f);

    if (has_loop)
    {
        const float dist_start = std::fabs(e.position.x - start_x);
        const float dist_end   = std::fabs(e.position.x - end_x);
        // Pick the closer edge (within slop).
        if (dist_start <= loop_edge_grab_radius_px
            && dist_start <= dist_end)
        {
            finger.kind = FingerKind::DragStart;
            // Immediately apply the touch so the edge tracks the finger
            // from frame 1, not just on subsequent drag events.
            this->mouseDrag(e);
            return;
        }
        if (dist_end <= loop_edge_grab_radius_px)
        {
            finger.kind = FingerKind::DragEnd;
            this->mouseDrag(e);
            return;
        }
    }

    // Try to grab a nearby playhead.
    const int grabbed = this->find_voice_under_touch(e.position.x);
    if (grabbed >= 0)
    {
        // Only one finger may scrub a voice at a time (newest wins).
        finger.kind = FingerKind::ScrubVoice;
        finger.scrubbing_slot = grabbed;
        auto& gui_in = this->processor_.get_gui_input_data();
        gui_in.voice_scrub_slot.store(grabbed);
        gui_in.voice_scrub_position.store(this->fraction_from_x(bounds, e.position.x));
        gui_in.voice_scrub_level.store(this->touch_level_from_event(e));
        return;
    }

    // Empty area — launch a new voice on the currently selected layer.
    const float start_fraction = this->fraction_from_x(bounds, e.position.x);
    const float level = this->touch_level_from_event(e);
    const int   layer = this->processor_.get_gui_input_data().selected_layer.load();
    this->push_touch_event(start_fraction, level, layer);
}

void TouchWaveformView::mouseDrag(const juce::MouseEvent& e)
{
    const int finger_idx = e.source.getIndex();
    if (finger_idx < 0 || finger_idx >= static_cast<int>(max_fingers)) return;
    auto& finger = this->fingers_[static_cast<size_t>(finger_idx)];
    if (!finger.active) return;

    const auto bounds = this->getLocalBounds().reduced(4).toFloat();
    const float frac  = this->fraction_from_x(bounds, e.position.x);

    switch (finger.kind)
    {
        case FingerKind::DragStart:
        {
            if (!param_writer_) return;
            // Read current end fraction so we can preserve the loop end as
            // the start moves — i.e. shrink/grow length to keep end fixed.
            const auto& gui_out = this->processor_.get_gui_output_data();
            if (gui_out.waveform_left.empty()) return;
            const int num_samples = static_cast<int>(gui_out.waveform_left.size());
            const float end_frac = num_samples > 0
                ? juce::jlimit(0.f, 1.f,
                    static_cast<float>(gui_out.display_marker_end.load()) / num_samples)
                : 1.f;
            const float new_start  = juce::jmin(frac, end_frac - 0.001f);
            const float new_length = juce::jmax(0.001f, end_frac - new_start);
            param_writer_("start",  new_start);
            param_writer_("length", new_length);
            break;
        }
        case FingerKind::DragEnd:
        {
            if (!param_writer_) return;
            const auto& gui_out = this->processor_.get_gui_output_data();
            if (gui_out.waveform_left.empty()) return;
            const int num_samples = static_cast<int>(gui_out.waveform_left.size());
            const float start_frac = num_samples > 0
                ? juce::jlimit(0.f, 1.f,
                    static_cast<float>(gui_out.display_marker_start.load()) / num_samples)
                : 0.f;
            const float new_end    = juce::jmax(frac, start_frac + 0.001f);
            const float new_length = juce::jlimit(0.001f, 1.f - start_frac,
                                                  new_end - start_frac);
            param_writer_("length", new_length);
            break;
        }
        case FingerKind::ScrubVoice:
        {
            if (finger.scrubbing_slot < 0) return;
            auto& gui_in = this->processor_.get_gui_input_data();
            if (gui_in.voice_scrub_slot.load() != finger.scrubbing_slot) return;
            gui_in.voice_scrub_position.store(frac);
            gui_in.voice_scrub_level.store(this->touch_level_from_event(e));
            break;
        }
        case FingerKind::None:
            break;
    }
}

void TouchWaveformView::mouseUp(const juce::MouseEvent& e)
{
    const int finger_idx = e.source.getIndex();
    if (finger_idx < 0 || finger_idx >= static_cast<int>(max_fingers)) return;
    auto& finger = this->fingers_[static_cast<size_t>(finger_idx)];

    if (finger.kind == FingerKind::ScrubVoice && finger.scrubbing_slot >= 0)
    {
        auto& gui_in = this->processor_.get_gui_input_data();
        if (gui_in.voice_scrub_slot.load() == finger.scrubbing_slot)
            gui_in.voice_scrub_slot.store(-1);
    }
    finger = FingerState{};
}

void TouchWaveformView::push_touch_event(float start_fraction, float level, int target_layer)
{
    auto& gui_in = this->processor_.get_gui_input_data();
    const auto write_idx = gui_in.touch_event_write_idx.load(std::memory_order_relaxed);
    const auto read_idx  = gui_in.touch_event_read_idx .load(std::memory_order_acquire);
    if (write_idx - read_idx >= GuiInputData::touch_event_queue_size)
    {
        // Queue full — audio thread hasn't drained yet. Drop the touch
        // rather than block; with a 16-deep queue and 100 ms timer this
        // should only happen under absurd finger storms.
        return;
    }
    auto& slot = gui_in.touch_event_queue[write_idx % GuiInputData::touch_event_queue_size];
    slot.start_fraction = start_fraction;
    slot.level          = level;
    slot.target_layer   = target_layer;
    gui_in.touch_event_write_idx.store(write_idx + 1, std::memory_order_release);
}

int TouchWaveformView::find_voice_under_touch(float touch_x) const
{
    const auto bounds   = this->getLocalBounds().reduced(4).toFloat();
    const auto& gui_out = this->processor_.get_gui_output_data();
    const auto& gui_in  = this->processor_.get_gui_input_data();
    const int selected_layer = gui_in.selected_layer.load();
    if (gui_out.waveform_left.empty()) return -1;
    const int num_samples = static_cast<int>(gui_out.waveform_left.size());

    int   best_slot = -1;
    float best_dist = playhead_grab_radius_px + 1.f;
    for (size_t i = 0; i < max_voices; ++i)
    {
        if (!gui_out.voice_active[i].load()) continue;
        if (gui_out.voice_layer[i].load() != selected_layer) continue;
        const float vp = gui_out.voice_position[i].load();
        if (vp < 0.f) continue;
        const float vx   = bounds.getX() + (vp / num_samples) * bounds.getWidth();
        const float dist = std::fabs(vx - touch_x);
        if (dist < best_dist)
        {
            best_dist = dist;
            best_slot = static_cast<int>(i);
        }
    }
    return best_slot;
}

float TouchWaveformView::touch_level_from_event(const juce::MouseEvent& e) const
{
    // JUCE on Android exposes touch contact area via getPressure() in [0, 1]
    // on devices that report it. On devices that don't, it returns 0 — fall
    // back to a moderate level so taps still produce audible voices.
    constexpr float min_level = 0.15f;
    constexpr float fallback  = 0.8f;
    const float pressure = e.pressure;
    if (pressure <= 0.f || pressure > 1.f) return fallback;
    return std::max(min_level, pressure);
}

#endif // JUCE_ANDROID

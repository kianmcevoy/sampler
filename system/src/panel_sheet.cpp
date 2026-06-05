#include "system/panel_sheet.hpp"

#if JUCE_ANDROID

#include "system/engine.hpp"
#include "igui/instruo.hpp"

#include <algorithm>
#include <cmath>

namespace
{

// ─── HorizontalBarSlider ─────────────────────────────────────────────────────
// A touch-friendly horizontal bar slider bound to a juce::AudioParameterFloat
// via NormalisableRange. Renders as: label on left ~25%, filled bar on right
// ~75%. Drag X within the bar maps to [range_min, range_max]. Bipolar sliders
// (those whose range straddles 0) render with a centre-zero fill that grows
// left or right of centre.
//
// The slider writes its parameter via setValueNotifyingHost on drag, so
// downstream state (gui_control_data, OSC, parameter automation) all see
// the change exactly as if a desktop rotary knob were rotated.
class HorizontalBarSlider : public juce::Component
{
public:
    HorizontalBarSlider(juce::AudioParameterFloat* param,
                        const juce::NormalisableRange<float>& range,
                        const juce::String& label):
        param_{param}, range_{range}, label_{label}
    {
        const float lo = range_.start;
        const float hi = range_.end;
        bipolar_ = (lo < 0.f && hi > 0.f);
    }

    void paint(juce::Graphics& g) override
    {
        const auto b = this->getLocalBounds().toFloat();
        const float label_w = b.getWidth() * 0.28f;
        const auto label_area = b.withWidth(label_w).reduced(6.f, 4.f);
        const auto bar_area   = b.withTrimmedLeft(label_w).reduced(6.f, 8.f);

        // Label
        g.setColour(igui::colours::light_grey);
        g.setFont(juce::Font(juce::FontOptions(bar_area.getHeight() * 0.5f)));
        g.drawText(label_, label_area, juce::Justification::centredLeft);

        // Track
        g.setColour(igui::colours::dark_grey);
        g.fillRoundedRectangle(bar_area, 6.f);
        g.setColour(igui::colours::grey);
        g.drawRoundedRectangle(bar_area, 6.f, 1.f);

        // Fill — param->get() returns the displayed (NormalisableRange-decoded)
        // value, NOT the raw [0,1] normalised storage.
        const float value      = param_->get();
        const float normalised = juce::jlimit(0.f, 1.f,
            (value - range_.start) / (range_.end - range_.start));

        g.setColour(igui::colours::gold);
        if (bipolar_)
        {
            // Centre-zero. Fill from centre toward edges.
            const float centre_x = bar_area.getCentreX();
            const float zero_norm = (-range_.start) / (range_.end - range_.start);
            const float zero_x    = bar_area.getX() + zero_norm * bar_area.getWidth();
            const float value_x   = bar_area.getX() + normalised * bar_area.getWidth();
            const float left      = std::min(zero_x, value_x);
            const float right     = std::max(zero_x, value_x);
            g.fillRoundedRectangle(left, bar_area.getY(), right - left, bar_area.getHeight(), 4.f);

            // Centre line for visual zero
            g.setColour(igui::colours::light_grey);
            g.drawLine(centre_x, bar_area.getY(), centre_x, bar_area.getBottom(), 1.f);
            juce::ignoreUnused(centre_x);
        }
        else
        {
            const float w = normalised * bar_area.getWidth();
            g.fillRoundedRectangle(bar_area.getX(), bar_area.getY(), w, bar_area.getHeight(), 4.f);
        }

        // Value text on the right
        g.setColour(igui::colours::white);
        g.setFont(juce::Font(juce::FontOptions(bar_area.getHeight() * 0.4f)));
        const auto text_area = bar_area.withTrimmedLeft(bar_area.getWidth() * 0.55f).withTrimmedRight(8.f);
        g.drawText(juce::String(value, 2), text_area, juce::Justification::centredRight);
    }

    void mouseDown(const juce::MouseEvent& e) override   { write_from_touch(e); }
    void mouseDrag(const juce::MouseEvent& e) override   { write_from_touch(e); }

private:
    void write_from_touch(const juce::MouseEvent& e)
    {
        const auto b = this->getLocalBounds().toFloat();
        const float label_w = b.getWidth() * 0.28f;
        const auto bar_area = b.withTrimmedLeft(label_w).reduced(6.f, 8.f);
        const float x = juce::jlimit(bar_area.getX(), bar_area.getRight(), e.position.x);
        const float normalised = (x - bar_area.getX()) / bar_area.getWidth();
        param_->setValueNotifyingHost(normalised);
        this->repaint();
    }

    juce::AudioParameterFloat*           param_;
    juce::NormalisableRange<float>       range_;
    juce::String                         label_;
    bool                                 bipolar_ { false };
};

// ─── HorizontalToggleButton ──────────────────────────────────────────────────
// Touch toggle bound to a juce::AudioParameterBool. Label on left, lit fill on
// right when ON. Tap toggles. Used for "buttons" in the control scheme.
class HorizontalToggleButton : public juce::Component
{
public:
    HorizontalToggleButton(juce::AudioParameterBool* param, const juce::String& label):
        param_{param}, label_{label} {}

    void paint(juce::Graphics& g) override
    {
        const auto b = this->getLocalBounds().toFloat();
        const float label_w = b.getWidth() * 0.28f;
        const auto label_area = b.withWidth(label_w).reduced(6.f, 4.f);
        const auto sw_area    = b.withTrimmedLeft(label_w).reduced(6.f, 12.f);

        g.setColour(igui::colours::light_grey);
        g.setFont(juce::Font(juce::FontOptions(sw_area.getHeight() * 0.7f)));
        g.drawText(label_, label_area, juce::Justification::centredLeft);

        const bool on = param_->get();
        g.setColour(on ? igui::colours::gold : igui::colours::dark_grey);
        g.fillRoundedRectangle(sw_area, 8.f);
        g.setColour(igui::colours::grey);
        g.drawRoundedRectangle(sw_area, 8.f, 1.f);

        // Knob indicator
        const float knob_diameter = sw_area.getHeight() - 6.f;
        const float knob_x = on
            ? sw_area.getRight() - knob_diameter - 4.f
            : sw_area.getX()     + 4.f;
        g.setColour(igui::colours::white);
        g.fillEllipse(knob_x, sw_area.getY() + 3.f, knob_diameter, knob_diameter);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        param_->setValueNotifyingHost(param_->convertTo0to1(param_->get() ? 0.f : 1.f));
        this->repaint();
    }

private:
    juce::AudioParameterBool* param_;
    juce::String              label_;
};

// ─── HorizontalTriggerButton ─────────────────────────────────────────────────
// Momentary touch button bound to a juce::AudioParameterBool. Tap writes
// `true` for one block; the engine clears it at end-of-block. Used for
// "triggers" in the control scheme.
class HorizontalTriggerButton : public juce::Component
{
public:
    HorizontalTriggerButton(juce::AudioParameterBool* param, const juce::String& label):
        param_{param}, label_{label} {}

    void paint(juce::Graphics& g) override
    {
        const auto b = this->getLocalBounds().toFloat();
        const auto inner = b.reduced(6.f, 8.f);

        g.setColour(pressed_ ? igui::colours::gold : igui::colours::dark_grey);
        g.fillRoundedRectangle(inner, 8.f);
        g.setColour(igui::colours::grey);
        g.drawRoundedRectangle(inner, 8.f, 1.f);

        g.setColour(pressed_ ? igui::colours::black : igui::colours::white);
        g.setFont(juce::Font(juce::FontOptions(inner.getHeight() * 0.5f)));
        g.drawText(label_, inner, juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        param_->setValueNotifyingHost(param_->convertTo0to1(1.f));
        pressed_ = true;
        this->repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        pressed_ = false;
        this->repaint();
    }

private:
    juce::AudioParameterBool* param_;
    juce::String              label_;
    bool                      pressed_ { false };
};

// ─── HorizontalDropdown ──────────────────────────────────────────────────────
// Touch picker bound to a juce::AudioParameterChoice. Renders as label + a
// row of fingerable option buttons. For a small choice count (current usage:
// 2 options each for "marker_type" and "note_route"), this is the simplest
// touch UI; for longer choice lists a popup picker would be better — not
// needed yet.
class HorizontalDropdown : public juce::Component
{
public:
    HorizontalDropdown(juce::AudioParameterChoice* param, const juce::String& label):
        param_{param}, label_{label}
    {
        choices_ = param_->getAllValueStrings();
    }

    void paint(juce::Graphics& g) override
    {
        const auto b = this->getLocalBounds().toFloat();
        const float label_w = b.getWidth() * 0.28f;
        const auto label_area = b.withWidth(label_w).reduced(6.f, 4.f);
        const auto buttons_area = b.withTrimmedLeft(label_w).reduced(6.f, 6.f);

        g.setColour(igui::colours::light_grey);
        g.setFont(juce::Font(juce::FontOptions(buttons_area.getHeight() * 0.5f)));
        g.drawText(label_, label_area, juce::Justification::centredLeft);

        const int N = choices_.size();
        if (N <= 0) return;
        const int selected = param_->getIndex();
        const float each_w = buttons_area.getWidth() / static_cast<float>(N);
        for (int i = 0; i < N; ++i)
        {
            const auto opt = juce::Rectangle<float>(
                buttons_area.getX() + i * each_w,
                buttons_area.getY(),
                each_w - 4.f,
                buttons_area.getHeight()).reduced(2.f, 0.f);

            const bool on = (i == selected);
            g.setColour(on ? igui::colours::gold : igui::colours::dark_grey);
            g.fillRoundedRectangle(opt, 6.f);
            g.setColour(igui::colours::grey);
            g.drawRoundedRectangle(opt, 6.f, 1.f);

            g.setColour(on ? igui::colours::black : igui::colours::white);
            g.setFont(juce::Font(juce::FontOptions(opt.getHeight() * 0.45f)));
            g.drawText(choices_[i], opt, juce::Justification::centred);
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        const auto b = this->getLocalBounds().toFloat();
        const float label_w = b.getWidth() * 0.28f;
        const auto buttons_area = b.withTrimmedLeft(label_w).reduced(6.f, 6.f);
        const int N = choices_.size();
        if (N <= 0) return;
        const float each_w = buttons_area.getWidth() / static_cast<float>(N);
        const int pick = juce::jlimit(0, N - 1,
            static_cast<int>((e.position.x - buttons_area.getX()) / each_w));
        param_->setValueNotifyingHost(param_->convertTo0to1(static_cast<float>(pick)));
        this->repaint();
    }

private:
    juce::AudioParameterChoice* param_;
    juce::String                label_;
    juce::StringArray           choices_;
};

} // namespace

// ─── PanelSheet ──────────────────────────────────────────────────────────────

PanelSheet::PanelSheet(EngineAudioProcessor& processor, const juce::String& panel_name):
    processor_{processor}, panel_name_{panel_name}
{
    this->setInterceptsMouseClicks(true, true);

    this->addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&grid_content_, false);
    viewport_.setScrollBarsShown(true, false);   // vertical only

    this->build_controls();
}

PanelSheet::~PanelSheet() = default;

void PanelSheet::paint(juce::Graphics& g)
{
    // Sheet background — slightly translucent so the waveform behind is hinted.
    g.fillAll(igui::colours::black.withAlpha(0.92f));
    g.setColour(igui::colours::gold);
    g.drawLine(0.f, 0.f, this->getWidth(), 0.f, 2.f);
}

void PanelSheet::set_on_dismiss(std::function<void()> on_dismiss)
{
    on_dismiss_ = std::move(on_dismiss);
}

void PanelSheet::resized()
{
    // Viewport fills the sheet with a small top margin for the gold rule;
    // grid layout (and thus the scrollable content height) is computed in
    // GridContent::resized().
    constexpr int top_pad = 8;
    viewport_.setBounds(0, top_pad, this->getWidth(), this->getHeight() - top_pad);

    // Set the grid's logical width to match the viewport (minus the
    // scrollbar's width) and let GridContent::resized compute the height
    // from the row count.
    const int scrollbar_w = viewport_.getScrollBarThickness();
    const int content_w   = juce::jmax(0, viewport_.getWidth() - scrollbar_w);
    // Provisional height — resized() below will reflow and update.
    grid_content_.setSize(content_w, this->getHeight());
    grid_content_.resized();
}

void PanelSheet::GridContent::resized()
{
    // 3-column grid. Each cell is row_h tall; columns share the width
    // equally. Total content height is computed from the row count so the
    // viewport knows when to scroll.
    constexpr int columns  = 3;
    constexpr int row_h    = 64;     // ≥ 48 dp; comfortable finger target
    constexpr int hgap     = 8;
    constexpr int vgap     = 6;
    constexpr int side_pad = 12;
    constexpr int top_pad  = 8;

    const int W = this->getWidth();
    const int col_w = (W - 2 * side_pad - hgap * (columns - 1)) / columns;

    const int n = static_cast<int>(this->children.size());
    const int rows = (n + columns - 1) / columns;
    const int content_h = top_pad + rows * row_h + (rows - 1) * vgap + top_pad;
    this->setSize(W, content_h);

    int x = side_pad;
    int y = top_pad;
    int col = 0;
    for (auto* c : this->children)
    {
        c->setBounds(x, y, col_w, row_h);
        ++col;
        if (col >= columns)
        {
            col = 0;
            x = side_pad;
            y += row_h + vgap;
        }
        else
        {
            x += col_w + hgap;
        }
    }
}

void PanelSheet::build_controls()
{
    auto& ap      = this->processor_;
    auto& builder = ap.get_gui_control_builder();

    // Mirror panels.cpp's parallel walk through param vectors so we know each
    // param's type (float / bool / trigger / choice / voice_button) and its
    // declaration-order panel name. Skip voice buttons — they live in the top
    // row of MainComponentAndroid, not in panel sheets.
    size_t float_i = 0, bool_i = 0, trig_i = 0, choice_i = 0, voice_i = 0;

    for (size_t i = 0; i < ap.all_params.size(); ++i)
    {
        const bool keep = (builder.panel_for(i) == panel_name_);
        auto* param = ap.all_params[i];

        if (voice_i < ap.voice_button_params.size()
         && param == ap.voice_button_params[voice_i])
        {
            ++voice_i;       // skip — voice buttons handled by MainComponentAndroid
            continue;
        }
        if (float_i < ap.float_params.size()
         && param == ap.float_params[float_i])
        {
            if (keep)
            {
                const auto& range = ap.float_param_ranges[float_i];
                const auto label  = ap.float_params[float_i]->getName(32);
                control_widgets_.emplace_back(
                    std::make_unique<HorizontalBarSlider>(ap.float_params[float_i], range, label));
                grid_content_.addAndMakeVisible(control_widgets_.back().get());
                grid_content_.children.push_back(control_widgets_.back().get());
            }
            ++float_i;
            continue;
        }
        // Triggers and toggle buttons are both AudioParameterBool — distinguish
        // by which vector the param appears in. trigger_params and bool_params
        // are disjoint by construction (engine.cpp populates them from
        // get_trigger_identifiers / get_button_identifiers respectively).
        if (trig_i < ap.trigger_params.size()
         && param == ap.trigger_params[trig_i])
        {
            if (keep)
            {
                const auto label = ap.trigger_params[trig_i]->getName(32);
                control_widgets_.emplace_back(
                    std::make_unique<HorizontalTriggerButton>(ap.trigger_params[trig_i], label));
                grid_content_.addAndMakeVisible(control_widgets_.back().get());
                grid_content_.children.push_back(control_widgets_.back().get());
            }
            ++trig_i;
            continue;
        }
        if (bool_i < ap.bool_params.size()
         && param == ap.bool_params[bool_i])
        {
            if (keep)
            {
                const auto label = ap.bool_params[bool_i]->getName(32);
                control_widgets_.emplace_back(
                    std::make_unique<HorizontalToggleButton>(ap.bool_params[bool_i], label));
                grid_content_.addAndMakeVisible(control_widgets_.back().get());
                grid_content_.children.push_back(control_widgets_.back().get());
            }
            ++bool_i;
            continue;
        }
        if (choice_i < ap.choice_params.size()
         && param == ap.choice_params[choice_i])
        {
            if (keep)
            {
                const auto label = ap.choice_params[choice_i]->getName(32);
                control_widgets_.emplace_back(
                    std::make_unique<HorizontalDropdown>(ap.choice_params[choice_i], label));
                grid_content_.addAndMakeVisible(control_widgets_.back().get());
                grid_content_.children.push_back(control_widgets_.back().get());
            }
            ++choice_i;
            continue;
        }
    }
}

#endif // JUCE_ANDROID

#include "system/main_component_android.hpp"

#if JUCE_ANDROID

#include "system/asset_manager.hpp"
#include "system/editor.hpp"
#include "system/engine.hpp"
#include "interface/voice_param_table.hpp"

#include <functional>

namespace
{
// Use the existing igui InstruoLedTextButtonElement so visuals match the
// desktop look. The Android UI overlays the bottom transport row and top
// voice row on top of the touch waveform; sizes are set in resized().
std::unique_ptr<igui::LedButton> make_label_button(
    const juce::String& label,
    igui::InstruoLedTextButtonElement::IndicationStyle style
        = igui::InstruoLedTextButtonElement::IndicationStyle::Outline)
{
    return std::make_unique<igui::LedButton>(
        new igui::InstruoLedTextButtonElement(label, style));
}
} // namespace

MainComponentAndroid::MainComponentAndroid(EngineAudioProcessor& processor,
                                           EngineAudioProcessorEditor& editor):
    waveform_view_{processor},
    controls_sheet_   {processor, "main"},
    modulation_sheet_ {processor, "modulation"},
    mode_controller_{
        processor.get_gui_input_data(),
        processor.get_gui_output_data(),
        [this]() { return this->snapshot_juce_params(); },
        [this](const VoiceLiveParams& p) { this->apply_params_to_juce(p); }},
    processor_{processor},
    editor_{editor},
    font_lifetime_manager_(igui::initialise_instruo_font(
        AssetManager::get_resource_file("gui/fonts/elza-round-variable-light.otf")))
{
    juce::ignoreUnused(this->editor_);

    // Top row: 8 layer/voice buttons + view toggle + global.
    for (size_t i = 0; i < max_voices; ++i)
    {
        auto label = juce::String(static_cast<int>(i + 1));
        layer_voice_buttons_[i] = make_label_button(label);
        const size_t voice_idx = i;
        layer_voice_buttons_[i]->set_on_click_function(
            [this, voice_idx]() { this->on_voice_button_clicked(voice_idx); });
        this->addAndMakeVisible(layer_voice_buttons_[i].get());
    }

    view_toggle_button_ = make_label_button("V/L");
    view_toggle_button_->set_on_click_function([this]() {
        this->layer_view_ = !this->layer_view_;
        this->processor_.get_gui_input_data().layer_view.store(this->layer_view_);
        if (this->layer_view_)
        {
            // Drop voice selection on entering layer view.
            this->mode_controller_.deselect_voice();
        }
    });
    this->addAndMakeVisible(view_toggle_button_.get());

    global_button_ = make_label_button("Global");
    global_button_->set_on_click_function([this]() {
        // ModeController's tick reads the JUCE "global" param state. Toggle it.
        const bool now = this->read_juce_bool("global");
        this->set_bool_juce("global", !now);
    });
    this->addAndMakeVisible(global_button_.get());

    // Bottom transport row.
    record_button_     = make_label_button("REC");
    play_button_       = make_label_button("PLAY");
    stop_button_       = make_label_button("STOP");
    erase_button_      = make_label_button("ERASE");
    load_button_       = make_label_button("LOAD");
    controls_tab_button_   = make_label_button("CTRLS");
    modulation_tab_button_ = make_label_button("MOD");

    record_button_->set_on_click_function([this]() {
        // If a record is in progress, REC ends it; otherwise it starts a new one.
        if (this->processor_.get_gui_output_data().is_recording.load())
            this->processor_.get_gui_input_data().record_stop_request.store(true);
        else
            this->processor_.get_gui_input_data().record_start_request.store(true);
    });
    play_button_ ->set_on_click_function([this]() { this->fire_trigger("play"); });
    stop_button_ ->set_on_click_function([this]() { this->fire_trigger("stop"); });
    // ERASE is destructive — first tap arms (LED goes full bright + button
    // changes label visual), second tap within 2 s fires. Tapping anything
    // else cancels (handled below in the other buttons' click paths via
    // erase_armed_ reset).
    erase_button_->set_on_click_function([this]() {
        const auto now = juce::Time::currentTimeMillis();
        if (this->erase_armed_ && (now - this->erase_arm_at_ms_) < 2000)
        {
            // Confirmation — fire the erase, disarm.
            this->processor_.get_gui_input_data().erase_request.store(true);
            this->erase_armed_ = false;
        }
        else
        {
            // Arm: do not erase yet. The timer's LED-brightness pulse will
            // make it visually obvious.
            this->erase_armed_      = true;
            this->erase_arm_at_ms_  = now;
        }
    });
    load_button_ ->set_on_click_function([this]() { this->fire_trigger("load_sample"); });

    controls_tab_button_  ->set_on_click_function([this]() {
        modulation_sheet_.setVisible(false);
        controls_sheet_  .setVisible(!controls_sheet_.isVisible());
        this->resized();
    });
    modulation_tab_button_->set_on_click_function([this]() {
        controls_sheet_  .setVisible(false);
        modulation_sheet_.setVisible(!modulation_sheet_.isVisible());
        this->resized();
    });

    this->addAndMakeVisible(record_button_.get());
    this->addAndMakeVisible(play_button_.get());
    this->addAndMakeVisible(stop_button_.get());
    this->addAndMakeVisible(erase_button_.get());
    this->addAndMakeVisible(load_button_.get());
    this->addAndMakeVisible(controls_tab_button_.get());
    this->addAndMakeVisible(modulation_tab_button_.get());

    // Touch view writes start / length params via a callback so it doesn't
    // need to know about JUCE param helpers itself. Routed through the
    // standard `set_float_juce` path so OSC + autosave + ParameterInterface
    // all see the change just as if a slider were dragged.
    waveform_view_.set_param_writer([this](const juce::String& id, float value) {
        this->set_float_juce(id, value);
    });
    this->addAndMakeVisible(waveform_view_);

    // Sheets are child components but start hidden; resized() positions them
    // over the bottom 70% of the screen.
    this->addChildComponent(controls_sheet_);
    this->addChildComponent(modulation_sheet_);

    controls_sheet_.set_on_dismiss([this]() {
        controls_sheet_.setVisible(false);
        this->resized();
    });
    modulation_sheet_.set_on_dismiss([this]() {
        modulation_sheet_.setVisible(false);
        this->resized();
    });

    // Seed layer 0's snapshot so initial layer switches restore sane values.
    for (size_t i = 0; i < max_layers; ++i) this->save_juce_into_layer(i);

    // Default mode = Global.
    this->set_bool_juce("global", true);

    this->startTimer(100);
}

MainComponentAndroid::~MainComponentAndroid()
{
    this->stopTimer();
}

void MainComponentAndroid::paint(juce::Graphics& g)
{
    g.fillAll(igui::colours::black);
}

void MainComponentAndroid::resized()
{
    // Inset away from system bars + nav buttons / gesture region. JUCE
    // populates Display::safeAreaInsets from the WindowInsets API on
    // Android, but in landscape some devices/JUCE versions report the
    // nav-bar inset on a different edge than where the nav bar actually
    // is (the OS sometimes reports rotation-naive portrait coordinates).
    // To be robust regardless of which edge it lands on, apply the
    // max(left,right) inset to both horizontal edges, and max(top,bottom)
    // to both vertical edges. Costs at most a small symmetric margin on
    // the already-safe side; eliminates the wrong-side crop bug.
    auto bounds = this->getLocalBounds();
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto insets = d->safeAreaInsets;
        const int hpad = juce::jmax(insets.getLeft(),   insets.getRight());
        const int vpad = juce::jmax(insets.getTop(),    insets.getBottom());
        bounds = bounds.withTrimmedLeft  (hpad)
                       .withTrimmedRight (hpad)
                       .withTrimmedTop   (vpad)
                       .withTrimmedBottom(vpad);
    }
    const int x0 = bounds.getX();
    const int y0 = bounds.getY();
    const int W  = bounds.getWidth();
    const int H  = bounds.getHeight();

    constexpr int row_h_top    = 72;
    constexpr int row_h_bottom = 88;
    constexpr int pad          = 8;

    // Top row: 8 layer/voice + V/L + Global, evenly spaced.
    const int top_buttons = static_cast<int>(max_voices) + 2;
    const int top_btn_w   = (W - pad * (top_buttons + 1)) / top_buttons;
    int x = x0 + pad;
    for (size_t i = 0; i < max_voices; ++i)
    {
        layer_voice_buttons_[i]->setBounds(x, y0 + pad, top_btn_w, row_h_top - pad);
        x += top_btn_w + pad;
    }
    view_toggle_button_->setBounds(x, y0 + pad, top_btn_w, row_h_top - pad);
    x += top_btn_w + pad;
    global_button_     ->setBounds(x, y0 + pad, top_btn_w, row_h_top - pad);

    // Bottom row: REC PLAY STOP ERASE LOAD ... CTRLS MOD.
    const int bottom_y  = y0 + H - row_h_bottom;
    const int bot_btns  = 7;
    const int bot_btn_w = (W - pad * (bot_btns + 1)) / bot_btns;
    int bx = x0 + pad;
    record_button_        ->setBounds(bx, bottom_y + pad, bot_btn_w, row_h_bottom - 2 * pad); bx += bot_btn_w + pad;
    play_button_          ->setBounds(bx, bottom_y + pad, bot_btn_w, row_h_bottom - 2 * pad); bx += bot_btn_w + pad;
    stop_button_          ->setBounds(bx, bottom_y + pad, bot_btn_w, row_h_bottom - 2 * pad); bx += bot_btn_w + pad;
    erase_button_         ->setBounds(bx, bottom_y + pad, bot_btn_w, row_h_bottom - 2 * pad); bx += bot_btn_w + pad;
    load_button_          ->setBounds(bx, bottom_y + pad, bot_btn_w, row_h_bottom - 2 * pad); bx += bot_btn_w + pad;
    controls_tab_button_  ->setBounds(bx, bottom_y + pad, bot_btn_w, row_h_bottom - 2 * pad); bx += bot_btn_w + pad;
    modulation_tab_button_->setBounds(bx, bottom_y + pad, bot_btn_w, row_h_bottom - 2 * pad);

    // Waveform fills the middle (between top button row and bottom transport).
    waveform_view_.setBounds(x0 + pad,
                             y0 + row_h_top,
                             W - 2 * pad,
                             H - row_h_top - row_h_bottom - pad);

    // Sheets cover the bottom 70% of the available middle area when visible,
    // floating above the waveform but below the bottom transport row.
    const int sheet_top_y = y0 + row_h_top + static_cast<int>((H - row_h_top - row_h_bottom) * 0.30f);
    const int sheet_h     = (y0 + H - row_h_bottom) - sheet_top_y;
    controls_sheet_  .setBounds(x0, sheet_top_y, W, sheet_h);
    modulation_sheet_.setBounds(x0, sheet_top_y, W, sheet_h);
}

void MainComponentAndroid::timerCallback()
{
    this->check_file_chooser_request();

    const auto& gui_output = this->processor_.get_gui_output_data();
    const auto& gui_input  = this->processor_.get_gui_input_data();

    // Layer view: top buttons' LED brightness = per-layer summed envelope.
    // Voice view: per-voice volume.
    for (size_t i = 0; i < max_voices; ++i)
    {
        const float brightness = this->layer_view_
            ? gui_output.layer_summed_envelope[i].load()
            : gui_output.voice_volume[i].load();
        layer_voice_buttons_[i]->view_led().brightness().set(juce::jlimit(0.f, 1.f, brightness));
    }

    // View toggle / global button LEDs reflect their current state.
    view_toggle_button_->view_led().brightness().set(this->layer_view_ ? 1.f : 0.2f);
    const bool global_on = this->read_juce_bool("global");
    global_button_     ->view_led().brightness().set(global_on ? 1.f : 0.2f);

    // Record LED = recording state.
    const bool is_rec = gui_output.is_recording.load();
    record_button_->view_led().brightness().set(is_rec ? 1.f : 0.2f);

    // ERASE arming auto-expires after 2 s. While armed, the button LED
    // pulses bright so it's visually distinct from the other transport
    // buttons (and the user gets a clear "this will actually erase" cue).
    if (this->erase_armed_)
    {
        const auto now = juce::Time::currentTimeMillis();
        if (now - this->erase_arm_at_ms_ >= 2000)
        {
            this->erase_armed_ = false;
            erase_button_->view_led().brightness().set(0.2f);
        }
        else
        {
            erase_button_->view_led().brightness().set(1.f);
        }
    }
    else
    {
        erase_button_->view_led().brightness().set(0.2f);
    }

    // Pump the mode controller (same as desktop). It returns whether Global
    // should be on; we sync the JUCE param.
    const auto desired = this->mode_controller_.tick(this->read_juce_bool("global"));
    this->set_bool_juce("global", desired.global_on);

    // Repaint the waveform every tick so playheads animate (100 ms cadence is
    // fine — matches the desktop refresh rate).
    waveform_view_.repaint();
    juce::ignoreUnused(gui_input);
}

void MainComponentAndroid::on_voice_button_clicked(size_t voice_index)
{
    if (this->layer_view_)
    {
        const int new_layer = static_cast<int>(voice_index);
        if (new_layer != this->current_layer_)
        {
            const int old_layer = this->current_layer_;
            this->current_layer_ = new_layer;
            this->processor_.get_gui_input_data().selected_layer.store(new_layer);
            this->save_juce_into_layer(static_cast<size_t>(old_layer));
            this->restore_layer_into_juce(static_cast<size_t>(new_layer));
        }
        return;
    }
    this->mode_controller_.on_voice_button_clicked(voice_index);
    this->set_bool_juce("global", this->mode_controller_.global_on());
}

void MainComponentAndroid::check_file_chooser_request()
{
    auto& gui_out = this->processor_.get_gui_output_data();
    auto& gui_in  = this->processor_.get_gui_input_data();

    if (gui_out.request_file_chooser.load())
    {
        gui_out.request_file_chooser.store(false);

        // JUCE's FileChooser on Android uses Storage Access Framework
        // internally (juce::FileChooser::launchAsync routes through SAF
        // when running on Android). No JNI bridge required.
        file_chooser_ = std::make_shared<juce::FileChooser>(
            "Select an audio file to load",
            juce::File{},
            "*.wav;*.aif;*.aiff;*.flac");

        const auto flags = juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles;
        file_chooser_->launchAsync(flags, [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file != juce::File{})
            {
                this->processor_.get_gui_input_data().sample_file_path
                    = file.getFullPathName().toStdString();
                this->processor_.get_gui_input_data().file_path_ready.store(true);
            }
        });
    }

    if (gui_out.file_loaded.load())
    {
        gui_out.file_loaded.store(false);
        gui_in.file_path_ready.store(false);
    }
}

void MainComponentAndroid::fire_trigger(const juce::String& id)
{
    for (auto* tp : this->processor_.trigger_params)
    {
        if (tp->getParameterID() == id)
        {
            tp->setValueNotifyingHost(tp->convertTo0to1(1.f));
            return;
        }
    }
}

// --- JUCE param helpers (mirror MainComponent's) -----------------------------

void MainComponentAndroid::set_bool_juce(const juce::String& id, bool value)
{
    for (auto* bp : this->processor_.bool_params)
        if (bp->getParameterID() == id)
        {
            bp->setValueNotifyingHost(bp->convertTo0to1(value ? 1.f : 0.f));
            return;
        }
}

bool MainComponentAndroid::read_juce_bool(const juce::String& id) const
{
    for (const auto* bp : this->processor_.bool_params)
        if (bp->getParameterID() == id) return bp->get();
    return false;
}

void MainComponentAndroid::set_float_juce(const juce::String& id, float displayed_value)
{
    for (size_t i = 0; i < this->processor_.float_params.size(); ++i)
    {
        auto* fp = this->processor_.float_params[i];
        if (fp->getParameterID() == id)
        {
            const auto& range = this->processor_.float_param_ranges[i];
            fp->setValueNotifyingHost(range.convertTo0to1(
                juce::jlimit(range.start, range.end, displayed_value)));
            return;
        }
    }
}

float MainComponentAndroid::read_float_juce(const juce::String& id, float fallback) const
{
    for (size_t i = 0; i < this->processor_.float_params.size(); ++i)
    {
        const auto* fp = this->processor_.float_params[i];
        if (fp->getParameterID() == id)
        {
            const auto& range = this->processor_.float_param_ranges[i];
            return range.convertFrom0to1(fp->get());
        }
    }
    return fallback;
}

void MainComponentAndroid::set_choice_juce(const juce::String& id, int index)
{
    for (auto* cp : this->processor_.choice_params)
        if (cp->getParameterID() == id)
        {
            cp->setValueNotifyingHost(cp->convertTo0to1(static_cast<float>(index)));
            return;
        }
}

bool MainComponentAndroid::is_per_layer_param(const juce::String& id) const
{
    return id != "voice_view" && id != "layer_view" && id != "global";
}

void MainComponentAndroid::save_juce_into_layer(size_t layer_index)
{
    auto& snap = this->layer_param_snapshots_[layer_index];
    snap.sliders.clear();
    snap.buttons.clear();
    snap.dropdowns.clear();
    for (size_t i = 0; i < this->processor_.float_params.size(); ++i)
    {
        const auto* fp = this->processor_.float_params[i];
        const auto id = fp->getParameterID();
        if (!is_per_layer_param(id)) continue;
        const auto& range = this->processor_.float_param_ranges[i];
        snap.sliders[id] = range.convertFrom0to1(fp->get());
    }
    for (const auto* bp : this->processor_.bool_params)
    {
        const auto id = bp->getParameterID();
        if (!is_per_layer_param(id)) continue;
        snap.buttons[id] = bp->get();
    }
    for (const auto* cp : this->processor_.choice_params)
    {
        const auto id = cp->getParameterID();
        if (!is_per_layer_param(id)) continue;
        snap.dropdowns[id] = cp->getIndex();
    }
}

void MainComponentAndroid::restore_layer_into_juce(size_t layer_index)
{
    const auto& snap = this->layer_param_snapshots_[layer_index];
    for (const auto& [id, value] : snap.sliders)   this->set_float_juce(id, value);
    for (const auto& [id, value] : snap.buttons)   this->set_bool_juce (id, value);
    for (const auto& [id, value] : snap.dropdowns) this->set_choice_juce(id, value);
}

void MainComponentAndroid::apply_params_to_juce(const VoiceLiveParams& p)
{
    for (const auto& e : voice_param_floats) this->set_float_juce(e.id, p.*(e.field));
    for (const auto& e : voice_param_bools)  this->set_bool_juce (e.id, p.*(e.field));
}

VoiceLiveParams MainComponentAndroid::snapshot_juce_params() const
{
    VoiceLiveParams out;
    for (const auto& e : voice_param_floats) out.*(e.field) = this->read_float_juce(e.id, out.*(e.field));
    for (const auto& e : voice_param_bools)  out.*(e.field) = this->read_juce_bool (e.id);
    return out;
}

#endif // JUCE_ANDROID

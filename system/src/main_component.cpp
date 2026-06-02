#include "system/main_component.hpp"

#include "idsp/functions.hpp"
#include "interface/voice_param_table.hpp"
#include "system/asset_manager.hpp"
#include "system/editor.hpp"
#include "system/engine.hpp"

#include <functional>

static constexpr bool operator==(const MainComponent::WindowSize& a, const MainComponent::WindowSize& b)
{
    return &a == &b;
}

static constexpr float panel_spacer_size = 10;
static constexpr int settings_pane_width = 300;
static constexpr int settings_pane_full_width = settings_pane_width + panel_spacer_size;

MainComponent::MainComponent(EngineAudioProcessor& processor_, EngineAudioProcessorEditor& editor_):
    window_full_size{
        .standard = {
            .width = static_cast<int>(processor_.get_gui_control_builder().get_panel_width()),
            .height = static_cast<int>(processor_.get_gui_control_builder().get_panel_height()),
        },
        .standard_with_settings = {
            .width = static_cast<int>(processor_.get_gui_control_builder().get_panel_width()) + settings_pane_full_width,
            .height = static_cast<int>(processor_.get_gui_control_builder().get_panel_height()),
        },
    },
    processor{processor_},
    editor{editor_},
    font_lifetime_manager(igui::initialise_instruo_font(AssetManager::get_resource_file("gui/fonts/elza-round-variable-light.otf"))),
    main_panel(processor_),
    modulation_panel(processor_),
    settings_panel(processor_),
    main_tab_button(new igui::InstruoLedTextButtonElement(
        "Main", igui::InstruoLedTextButtonElement::IndicationStyle::Outline)),
    modulation_tab_button(new igui::InstruoLedTextButtonElement(
        "Modulation", igui::InstruoLedTextButtonElement::IndicationStyle::Outline)),
    current_window_full_size{std::cref(this->window_full_size.standard)},
    window_scale{processor_.get_gui_scale_value_ref()},
    bounds_manager(*this),
    mode_controller_(
        processor_.get_gui_input_data(),
        processor_.get_gui_output_data(),
        [this]() { return this->snapshot_juce_params(); },
        [this](const VoiceLiveParams& p) { this->apply_params_to_juce(p); })
{
    this->addAndMakeVisible(this->main_panel);
    this->addChildComponent(this->modulation_panel);
    this->addChildComponent(this->settings_panel);

    this->hide_settings_panel();

    this->addAndMakeVisible(this->main_tab_button);
    this->addAndMakeVisible(this->modulation_tab_button);
    this->main_tab_button.set_on_click_function([this]() { this->set_active_panel("main"); });
    this->modulation_tab_button.set_on_click_function([this]() { this->set_active_panel("modulation"); });

    this->editor.setConstrainer(&this->bounds_manager);
    this->editor.setResizable(false, true);
    this->refresh_window_size();

    this->set_using_native_title_bar();

    // Start timer to check for file chooser requests (check every 100ms)
    this->startTimer(100);

    // Default mode is Global (no Auto state any more) — light the Global LED.
    this->set_bool_juce("global", true);

    // Seed every layer's snapshot from the current JUCE defaults so the first
    // layer-switch restores sane values instead of zeroing everything.
    for (size_t i = 0; i < max_layers; ++i) this->save_juce_into_layer(i);

    // Initial panel selection.
    this->set_active_panel("main");
}

MainComponent::~MainComponent()
{
    this->stopTimer();
    this->cancelPendingUpdate();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(igui::colours::black);
}

void MainComponent::resized()
{
    const auto bounds = this->getLocalBounds();

    {
        const float actual_height = bounds.getHeight();
        const float full_height = this->current_window_full_size.get().height;
        const float scale = actual_height / full_height;
        this->window_scale = scale;
    }

    const float design_panel_width = static_cast<float>(this->window_full_size.standard.width);

    this->settings_panel.setBounds(this->scale_to_fit(juce::Rectangle<float>(design_panel_width + panel_spacer_size, 0, settings_pane_width, this->current_window_full_size.get().height)).toNearestIntEdges());

    // Both project panels share the same canvas footprint; visibility (one at
    // a time) is governed by set_active_panel().
    const auto project_panel_bounds = this->scale_to_fit(
        juce::Rectangle<float>(0, 0, design_panel_width, this->window_full_size.standard.height)
    ).toNearestIntEdges();
    this->main_panel.setBounds(project_panel_bounds);
    this->modulation_panel.setBounds(project_panel_bounds);

    // Tab buttons sit in the top-left of the window, clear of the y=215
    // control row and the settings cog (top-right corner).
    this->main_tab_button.setBounds      (this->scale_to_fit(juce::Rectangle<float>( 20.f, 8.f,  90.f, 28.f)).toNearestIntEdges());
    this->modulation_tab_button.setBounds(this->scale_to_fit(juce::Rectangle<float>(120.f, 8.f, 140.f, 28.f)).toNearestIntEdges());
}

void MainComponent::set_active_panel(const juce::String& name)
{
    this->active_panel_ = name;
    this->main_panel.setVisible      (name == "main");
    this->modulation_panel.setVisible(name == "modulation");
    this->main_tab_button.view_led().brightness().set      (name == "main"       ? 1.f : 0.2f);
    this->modulation_tab_button.view_led().brightness().set(name == "modulation" ? 1.f : 0.2f);
}

void MainComponent::parentHierarchyChanged()
{
    this->set_using_native_title_bar();
}

void MainComponent::handleAsyncUpdate()
{
    this->repaint();
}

void MainComponent::timerCallback()
{
    this->check_file_chooser_request();

    // Enforce the voice_view ↔ layer_view radio + publish layer_view state.
    this->enforce_view_radio();

    const auto& gui_output = this->processor.get_gui_output_data();

    if (this->layer_view_)
    {
        // Layer view: voice buttons take their brightness from each layer's
        // summed envelope (not per-voice), and their colour from current vs
        // other-active. Mode controller still ticks (so the Global JUCE param
        // stays consistent) but the per-voice visuals it drives are
        // overwritten by refresh_layer_button_visuals.
        this->refresh_layer_button_visuals();
    }
    else
    {
        // Voice view: per-voice LED brightness (current behaviour).
        for (size_t i = 0; i < this->main_panel.num_voice_buttons(); ++i)
        {
            const size_t slot = this->main_panel.voice_slot_for_button(i);
            this->main_panel.set_voice_brightness(i, gui_output.voice_volume[slot].load());
        }
    }

    // Publish the gesture state so the audio thread knows whether to treat
    // the position slider as a scrub command. No longer mirror playback
    // position back into the slider — avoids constant redraws on the GUI thread.
    const bool position_dragged = this->main_panel.is_slider_being_gestured("position");
    this->processor.get_gui_input_data().position_scrubbing.store(position_dragged);

    // Grey out the pitch slider when timestretch is OFF — its value is
    // forced to 0 inside Voice, so visually communicating "ignored" helps.
    const bool ts_on = this->read_juce_bool("timestretch");
    this->main_panel.set_slider_alpha("pitch", ts_on ? 1.f : 0.4f);

    // Pump the mode controller. It decides transitions; we just wire the
    // JUCE button reads/writes and refresh voice-button selection visuals.
    const auto desired = this->mode_controller_.tick(this->read_juce_bool("global"));
    this->set_bool_juce("global", desired.global_on);
    if (!this->layer_view_)
    {
        this->refresh_voice_button_visuals();
    }
}

void MainComponent::enforce_view_radio()
{
    const bool voice_on = this->read_juce_bool("voice_view");
    const bool layer_on = this->read_juce_bool("layer_view");

    bool new_layer_view = this->layer_view_;
    if (voice_on && layer_on)
    {
        // Both on — the one the user just toggled is the loser. Drop the
        // one we previously had on and keep the new one.
        if (this->layer_view_)
        {
            new_layer_view = false;          // user just turned voice_view ON
            this->set_bool_juce("layer_view", false);
        }
        else
        {
            new_layer_view = true;           // user just turned layer_view ON
            this->set_bool_juce("voice_view", false);
        }
    }
    else if (!voice_on && !layer_on)
    {
        // Both off — restore the previous view (voice view is the default).
        if (this->layer_view_) this->set_bool_juce("layer_view", true);
        else                   this->set_bool_juce("voice_view", true);
    }
    else
    {
        new_layer_view = layer_on;
    }

    if (new_layer_view != this->layer_view_)
    {
        this->layer_view_ = new_layer_view;
        this->processor.get_gui_input_data().layer_view.store(new_layer_view);
        if (new_layer_view)
        {
            // Entering layer view: drop any voice selection so Voice mode
            // doesn't survive into a context where the buttons mean layers.
            this->mode_controller_.deselect_voice();
            this->refresh_voice_button_visuals();
        }
    }
}

void MainComponent::refresh_layer_button_visuals()
{
    const auto& gui_output = this->processor.get_gui_output_data();
    for (size_t i = 0; i < this->main_panel.num_voice_buttons(); ++i)
    {
        // Voice button index 0..7 doubles as layer index 0..7 in layer view.
        const size_t layer_idx = this->main_panel.voice_slot_for_button(i);
        const float  env = gui_output.layer_summed_envelope[layer_idx].load();
        const bool   has = gui_output.layer_has_active_voices[layer_idx].load();
        this->main_panel.set_voice_brightness(i, env);

        const bool is_current = (static_cast<int>(layer_idx) == this->current_layer_);
        juce::Colour bg;
        if (is_current)
            bg = igui::colours::gold.withAlpha(0.25f);
        else if (has)
            bg = igui::colours::grey.withAlpha(0.25f);
        else
            bg = igui::colours::dark_grey;
        this->main_panel.set_voice_background_colour(i, bg);
    }
}

void MainComponent::on_voice_button_clicked(size_t voice_index)
{
    if (this->layer_view_)
    {
        // Layer view: voice button N selects layer N. selected_voice stays
        // at -1 throughout layer view (cleared on entry by enforce_view_radio).
        const int new_layer = static_cast<int>(voice_index);
        if (new_layer != this->current_layer_)
        {
            // Snap-on-select for layers: save current slider/button/dropdown
            // values into the layer we're leaving, then restore the layer
            // we're entering. Publish selected_layer before restoring JUCE
            // params so the audio thread switches layer context before it sees
            // the new slider values — the Instrument re-anchors on layer change.
            const int old_layer = this->current_layer_;
            this->current_layer_ = new_layer;
            this->processor.get_gui_input_data().selected_layer.store(new_layer);
            this->save_juce_into_layer(static_cast<size_t>(old_layer));
            this->restore_layer_into_juce(static_cast<size_t>(new_layer));
        }
        this->refresh_layer_button_visuals();
        return;
    }

    this->mode_controller_.on_voice_button_clicked(voice_index);
    // Sync the global JUCE param to the new mode IMMEDIATELY so the next
    // timer tick doesn't see stale state.
    this->set_bool_juce("global", this->mode_controller_.global_on());
    this->refresh_voice_button_visuals();
}

void MainComponent::set_bool_juce(const juce::String& id, bool value)
{
    for (auto* bp : this->processor.bool_params)
    {
        if (bp->getParameterID() == id)
        {
            bp->setValueNotifyingHost(bp->convertTo0to1(value ? 1.f : 0.f));
            return;
        }
    }
}

bool MainComponent::read_juce_bool(const juce::String& id) const
{
    for (const auto* bp : this->processor.bool_params)
    {
        if (bp->getParameterID() == id) return bp->get();
    }
    return false;
}

void MainComponent::set_float_juce(const juce::String& id, float displayed_value)
{
    for (size_t i = 0; i < this->processor.float_params.size(); ++i)
    {
        auto* fp = this->processor.float_params[i];
        if (fp->getParameterID() == id)
        {
            const auto& range = this->processor.float_param_ranges[i];
            fp->setValueNotifyingHost(range.convertTo0to1(
                juce::jlimit(range.start, range.end, displayed_value)));
            return;
        }
    }
}

float MainComponent::read_float_juce(const juce::String& id, float fallback) const
{
    for (size_t i = 0; i < this->processor.float_params.size(); ++i)
    {
        const auto* fp = this->processor.float_params[i];
        if (fp->getParameterID() == id)
        {
            const auto& range = this->processor.float_param_ranges[i];
            return range.convertFrom0to1(fp->get());
        }
    }
    return fallback;
}

void MainComponent::set_choice_juce(const juce::String& id, int index)
{
    for (auto* cp : this->processor.choice_params)
    {
        if (cp->getParameterID() == id)
        {
            cp->setValueNotifyingHost(cp->convertTo0to1(static_cast<float>(index)));
            return;
        }
    }
}

bool MainComponent::is_per_layer_param(const juce::String& id) const
{
    // View / session state — not part of any layer's snapshot.
    return id != "voice_view"
        && id != "layer_view"
        && id != "global";
}

void MainComponent::save_juce_into_layer(size_t layer_index)
{
    auto& snap = this->layer_param_snapshots_[layer_index];
    snap.sliders.clear();
    snap.buttons.clear();
    snap.dropdowns.clear();
    for (size_t i = 0; i < this->processor.float_params.size(); ++i)
    {
        const auto* fp = this->processor.float_params[i];
        const auto id = fp->getParameterID();
        if (!is_per_layer_param(id)) continue;
        const auto& range = this->processor.float_param_ranges[i];
        snap.sliders[id] = range.convertFrom0to1(fp->get());
    }
    for (const auto* bp : this->processor.bool_params)
    {
        const auto id = bp->getParameterID();
        if (!is_per_layer_param(id)) continue;
        snap.buttons[id] = bp->get();
    }
    for (const auto* cp : this->processor.choice_params)
    {
        const auto id = cp->getParameterID();
        if (!is_per_layer_param(id)) continue;
        snap.dropdowns[id] = cp->getIndex();
    }
}

void MainComponent::restore_layer_into_juce(size_t layer_index)
{
    const auto& snap = this->layer_param_snapshots_[layer_index];
    for (const auto& [id, value] : snap.sliders)   this->set_float_juce(id, value);
    for (const auto& [id, value] : snap.buttons)   this->set_bool_juce (id, value);
    for (const auto& [id, value] : snap.dropdowns) this->set_choice_juce(id, value);
}

void MainComponent::refresh_voice_button_visuals()
{
    const int selected = this->mode_controller_.selected_voice();
    for (size_t i = 0; i < this->main_panel.num_voice_buttons(); ++i)
    {
        const size_t slot = this->main_panel.voice_slot_for_button(i);
        const bool is_selected = (static_cast<int>(slot) == selected);
        this->main_panel.set_voice_selected(i, is_selected);
    }
}

void MainComponent::apply_params_to_juce(const VoiceLiveParams& p)
{
    for (const auto& e : voice_param_floats) this->set_float_juce(e.id, p.*(e.field));
    for (const auto& e : voice_param_bools)  this->set_bool_juce (e.id, p.*(e.field));
}

VoiceLiveParams MainComponent::snapshot_juce_params() const
{
    VoiceLiveParams out;
    for (const auto& e : voice_param_floats) out.*(e.field) = this->read_float_juce(e.id, out.*(e.field));
    for (const auto& e : voice_param_bools)  out.*(e.field) = this->read_juce_bool (e.id);
    return out;
}

void MainComponent::check_file_chooser_request()
{
    // Check if parameter interface is requesting a file chooser (read from output)
    if (this->processor.get_gui_output_data().request_file_chooser.load())
    {
        // Reset the request flag
        this->processor.get_gui_output_data().request_file_chooser.store(false);
        
        // Open file chooser dialog
        this->file_chooser = std::make_shared<juce::FileChooser>(
            "Select an audio file to load",
            juce::File{},
            "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg"
        );
        
        auto chooser_flags = juce::FileBrowserComponent::openMode 
                           | juce::FileBrowserComponent::canSelectFiles;
        
        this->file_chooser->launchAsync(chooser_flags, [this](const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file != juce::File{})
            {
                // Store the file path in gui_input_data (written by GUI, read by audio thread)
                this->processor.get_gui_input_data().sample_file_path = file.getFullPathName().toStdString();
                this->processor.get_gui_input_data().file_path_ready.store(true);
            }
        });
    }
    
    // Check if audio thread has finished loading the file
    if (this->processor.get_gui_output_data().file_loaded.load())
    {
        // Reset both flags for next load
        this->processor.get_gui_output_data().file_loaded.store(false);
        this->processor.get_gui_input_data().file_path_ready.store(false);
    }
}

void MainComponent::open_audio_midi_settings_window()
{
    this->editor.open_audio_midi_settings();
}

void MainComponent::show_settings_panel()
{
    this->settings_panel.setVisible(true);
    this->main_panel.settings_menu_button.staging_parameter().set_as_bool(true);
    this->refresh_window_size();
}

void MainComponent::hide_settings_panel()
{
    this->settings_panel.setVisible(false);
    this->main_panel.settings_menu_button.staging_parameter().set_as_bool(false);
    this->refresh_window_size();
}

bool MainComponent::toggle_settings_panel()
{
    if (this->settings_panel.isVisible())
        this->hide_settings_panel();
    else
        this->show_settings_panel();
    return this->settings_panel.isVisible();
}

void MainComponent::set_window_size_scale(float scale)
{
    float full_width = this->current_window_full_size.get().width;
    float full_height = this->current_window_full_size.get().height;
    const int width = std::round(full_width * scale);
    const int height = std::round(full_height * scale);
    this->editor.setSize(width, height);
}

MainComponent::WindowSize MainComponent::get_current_window_full_size() const
{
    return this->current_window_full_size.get();
}

void MainComponent::refresh_window_size()
{
    if (this->settings_panel.isVisible())
    {
        this->current_window_full_size = this->window_full_size.standard_with_settings;
    }
    else
    {
        this->current_window_full_size = this->window_full_size.standard;
    }

    bounds_manager.update();

    this->set_window_size_scale(this->scale_to_fit(1.f));
}

MainComponent::BoundsManager::BoundsManager(const MainComponent& main_comp):
main_component{main_comp}
{
    this->update();
}

void MainComponent::BoundsManager::update()
{
    this->update_aspect_ratio();
    this->update_size_limits();
}

void MainComponent::BoundsManager::update_aspect_ratio()
{
    const double width_d = this->main_component.current_window_full_size.get().width;
    const double height_d = this->main_component.current_window_full_size.get().height;
    const double aspect_ratio = width_d / height_d;
    this->setFixedAspectRatio(aspect_ratio);
}

void MainComponent::BoundsManager::update_size_limits()
{
    const auto display_area = this->get_display_area();

    constexpr float min_screen_ratio = 0.2f;
    const auto min_display_area = display_area * min_screen_ratio;
    constexpr float max_screen_ratio = 1.f;
    const auto max_display_area = display_area * max_screen_ratio;

    this->setMinimumSize(min_display_area.getWidth(), min_display_area.getHeight());
}

juce::Rectangle<int> MainComponent::BoundsManager::get_display_area() const
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto display_area = displays.getPrimaryDisplay()->userArea;
    return display_area;
}

void MainComponent::set_using_native_title_bar()
{
    this->editor.set_using_native_title_bar(true);
}

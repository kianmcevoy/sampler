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

    // Initial mode is Auto: light up the Auto button, leave Global off.
    this->set_bool_juce("auto",   true);
    this->set_bool_juce("global", false);

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

    // Drive voice button LED brightness from the audio thread's per-voice
    // amplitude so the LED fades with the envelope.
    const auto& gui_output = this->processor.get_gui_output_data();
    for (size_t i = 0; i < this->main_panel.num_voice_buttons(); ++i)
    {
        const size_t slot = this->main_panel.voice_slot_for_button(i);
        this->main_panel.set_voice_brightness(i, gui_output.voice_volume[slot].load());
    }

    // Pump the mode controller. It decides transitions; we just wire the
    // JUCE button reads/writes and refresh voice-button selection visuals.
    const auto desired = this->mode_controller_.tick(
        this->read_juce_bool("auto"),
        this->read_juce_bool("global"));
    this->set_bool_juce("auto",   desired.auto_on);
    this->set_bool_juce("global", desired.global_on);
    this->refresh_voice_button_visuals();
}

void MainComponent::on_voice_button_clicked(size_t voice_index)
{
    this->mode_controller_.on_voice_button_clicked(voice_index);
    // Sync the auto/global JUCE params to the new mode IMMEDIATELY so the
    // next timer tick doesn't see stale state (e.g. auto still true while we
    // just entered Voice mode) and misinterpret it as a user click on Auto.
    const bool in_auto = (this->mode_controller_.mode() == ModeController::Mode::Auto);
    this->set_bool_juce("auto",   in_auto);
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
            const float width = range.end - range.start;
            const float normalised = (width != 0.f)
                ? juce::jlimit(0.f, 1.f, (displayed_value - range.start) / width)
                : 0.f;
            fp->setValueNotifyingHost(normalised);
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

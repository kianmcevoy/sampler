#include "system/main_component.hpp"

#include "idsp/functions.hpp"
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
    settings_panel(processor_),
    current_window_full_size{std::cref(this->window_full_size.standard)},
    window_scale{processor_.get_gui_scale_value_ref()},
    bounds_manager(*this)
{
    this->addAndMakeVisible(this->main_panel);
    this->addChildComponent(this->settings_panel);

    this->hide_settings_panel();

    this->editor.setConstrainer(&this->bounds_manager);
    this->editor.setResizable(false, true);
    this->refresh_window_size();

    this->set_using_native_title_bar();
    
    // Start timer to check for file chooser requests (check every 100ms)
    this->startTimer(100);

    // Initial mode is Auto: light up the Auto button, leave Global off.
    this->set_bool_juce("auto",   true);
    this->set_bool_juce("global", false);
    this->processor.get_gui_input_data().global_mode.store(false);
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

    this->main_panel.setBounds(this->scale_to_fit(juce::Rectangle<float>(0, 0, design_panel_width, this->window_full_size.standard.height)).toNearestIntEdges());
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

    // Drive voice button LEDs from the audio thread's per-voice activity flag.
    const auto& gui_output = this->processor.get_gui_output_data();
    for (size_t i = 0; i < this->main_panel.num_voice_buttons(); ++i)
    {
        const size_t slot = this->main_panel.voice_slot_for_button(i);
        this->main_panel.set_voice_active(i, gui_output.voice_active[slot].load());
    }

    // --- Auto/Global radio reconciliation ---
    // The Auto and Global JUCE bools are user-clickable. Compare them against
    // our internal "truth" (selected_voice_ + global_on_) to detect a click,
    // run the mode transition, then re-assert the desired state so that any
    // accidental toggle (e.g. clicking Auto while already in Auto) is corrected.
    const bool global_juce = this->read_juce_bool("global");
    const bool auto_juce   = this->read_juce_bool("auto");

    if (global_juce != this->global_on_)
    {
        // User flipped Global directly.
        if (global_juce) this->enter_global_mode();
        else             this->enter_auto_mode();
    }
    else
    {
        const bool currently_in_auto = (this->selected_voice_ == -1 && !this->global_on_);
        if (auto_juce && !currently_in_auto)
        {
            // User clicked Auto while in Voice or Global mode.
            this->enter_auto_mode();
        }
        // (Other auto-flip cases — e.g. user clicked Auto while already in
        // Auto, toggling it off — are corrected by the re-assertion below.)
    }

    // Re-assert button state to match our mode (corrects rogue clicks).
    const bool in_auto = (this->selected_voice_ == -1 && !this->global_on_);
    this->set_bool_juce("auto",   in_auto);
    this->set_bool_juce("global", this->global_on_);

    // Publish global_mode to the audio thread once per tick.
    this->processor.get_gui_input_data().global_mode.store(this->global_on_);
}

void MainComponent::on_voice_button_clicked(size_t voice_index)
{
    const int requested = static_cast<int>(voice_index);
    // Re-clicking the currently-selected voice deselects → Auto mode.
    if (this->selected_voice_ == requested) this->enter_auto_mode();
    else                                    this->enter_voice_mode(voice_index);
}

void MainComponent::enter_voice_mode(size_t voice_index)
{
    const int new_selection = static_cast<int>(voice_index);
    const bool was_no_voice = (this->selected_voice_ == -1);

    // Cache current sliders as "global" params on the first transition away
    // from no-voice state, mirroring the existing behavior.
    if (was_no_voice && !this->global_params_cached_)
    {
        this->global_params_cache_  = this->snapshot_juce_params();
        this->global_params_cached_ = true;
    }
    else if (was_no_voice)
    {
        // Already cached — refresh in case the user nudged sliders in
        // auto/global mode before selecting a voice.
        this->global_params_cache_ = this->snapshot_juce_params();
    }

    this->selected_voice_ = new_selection;
    this->global_on_      = false;
    this->processor.get_gui_input_data().selected_voice.store(new_selection);

    // Snap sliders to the voice's live values if it's active.
    const auto& gui_output = this->processor.get_gui_output_data();
    if (gui_output.voice_active[static_cast<size_t>(new_selection)].load())
    {
        this->apply_params_to_juce(this->read_voice_snapshot(static_cast<size_t>(new_selection)));
    }

    this->set_bool_juce("auto",   false);
    this->set_bool_juce("global", false);
    this->refresh_voice_button_visuals();
}

void MainComponent::enter_auto_mode()
{
    const bool leaving_voice = (this->selected_voice_ >= 0);

    if (leaving_voice && this->global_params_cached_)
    {
        this->apply_params_to_juce(this->global_params_cache_);
    }

    this->selected_voice_ = -1;
    this->global_on_      = false;
    this->processor.get_gui_input_data().selected_voice.store(-1);

    this->set_bool_juce("auto",   true);
    this->set_bool_juce("global", false);
    this->refresh_voice_button_visuals();
}

void MainComponent::enter_global_mode()
{
    const bool was_voice = (this->selected_voice_ >= 0);

    // Cache sliders if entering from voice mode so returning to auto can
    // restore them. (Coming from auto, the sliders ARE the global params.)
    if (was_voice && !this->global_params_cached_)
    {
        this->global_params_cache_  = this->snapshot_juce_params();
        this->global_params_cached_ = true;
    }

    this->selected_voice_ = -1;
    this->global_on_      = true;
    this->processor.get_gui_input_data().selected_voice.store(-1);

    this->set_bool_juce("auto",   false);
    this->set_bool_juce("global", true);
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

void MainComponent::refresh_voice_button_visuals()
{
    for (size_t i = 0; i < this->main_panel.num_voice_buttons(); ++i)
    {
        const size_t slot = this->main_panel.voice_slot_for_button(i);
        const bool is_selected = (static_cast<int>(slot) == this->selected_voice_);
        this->main_panel.set_voice_selected(i, is_selected);
    }
}

void MainComponent::apply_params_to_juce(const VoiceLiveParams& p)
{
    const auto set_float = [this](const juce::String& id, float displayed_value)
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
    };
    const auto set_bool = [this](const juce::String& id, bool value)
    {
        this->set_bool_juce(id, value);
    };

    set_float("start",            p.start);
    set_float("length",           p.length);
    set_float("speed",            p.speed);
    set_float("level",            p.level);
    set_float("pan",              p.pan);
    set_float("time",             p.time);
    set_float("skew",             p.skew);
    set_float("shape",            p.shape);
    set_float("envelope_speed",   p.envelope_speed);
    set_float("envelope_start",   p.envelope_start);
    set_float("envelope_length",  p.envelope_length);
    set_float("envelope_level",   p.envelope_level);
    set_float("envelope_pan",     p.envelope_pan);
    set_bool ("loop",             p.loop);
    set_bool ("loop_envelope",    p.loop_envelope);
    set_bool ("envelope_sync",    p.envelope_sync);
}

VoiceLiveParams MainComponent::snapshot_juce_params() const
{
    VoiceLiveParams out;

    const auto get_float = [this](const juce::String& id, float fallback) -> float
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
    };
    const auto get_bool = [this](const juce::String& id, bool fallback) -> bool
    {
        for (const auto* bp : this->processor.bool_params)
        {
            if (bp->getParameterID() == id) return bp->get();
        }
        return fallback;
    };

    out.start            = get_float("start",           out.start);
    out.length           = get_float("length",          out.length);
    out.speed            = get_float("speed",           out.speed);
    out.level            = get_float("level",           out.level);
    out.pan              = get_float("pan",             out.pan);
    out.time             = get_float("time",            out.time);
    out.skew             = get_float("skew",            out.skew);
    out.shape            = get_float("shape",           out.shape);
    out.envelope_speed   = get_float("envelope_speed",  out.envelope_speed);
    out.envelope_start   = get_float("envelope_start",  out.envelope_start);
    out.envelope_length  = get_float("envelope_length", out.envelope_length);
    out.envelope_level   = get_float("envelope_level",  out.envelope_level);
    out.envelope_pan     = get_float("envelope_pan",    out.envelope_pan);
    out.loop             = get_bool ("loop",            out.loop);
    out.loop_envelope    = get_bool ("loop_envelope",   out.loop_envelope);
    out.envelope_sync    = get_bool ("envelope_sync",   out.envelope_sync);
    return out;
}

VoiceLiveParams MainComponent::read_voice_snapshot(size_t voice_index) const
{
    const auto& src = this->processor.get_gui_output_data().voice_params_snapshot[voice_index];
    VoiceLiveParams out;
    out.start           = src.start.load();
    out.length          = src.length.load();
    out.speed           = src.speed.load();
    out.level           = src.level.load();
    out.pan             = src.pan.load();
    out.loop            = src.loop.load();
    out.time            = src.time.load();
    out.skew            = src.skew.load();
    out.shape           = src.shape.load();
    out.loop_envelope   = src.loop_envelope.load();
    out.envelope_sync   = src.envelope_sync.load();
    out.envelope_speed  = src.envelope_speed.load();
    out.envelope_start  = src.envelope_start.load();
    out.envelope_length = src.envelope_length.load();
    out.envelope_level  = src.envelope_level.load();
    out.envelope_pan    = src.envelope_pan.load();
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

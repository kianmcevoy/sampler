#ifndef SYSTEM_PANEL_SHEET_H
#define SYSTEM_PANEL_SHEET_H

#include "JuceHeader.h"

#if JUCE_ANDROID

#include <memory>
#include <vector>

class EngineAudioProcessor;

/** PanelSheet — slide-up overlay sheet hosting the touch-friendly controls
 *  for one panel ("main" or "modulation").
 *
 *  Reads the control declarations from the existing GuiControlBuilder and
 *  re-renders sliders as **horizontal bar sliders** (full sheet width,
 *  ≥ 48 dp tall), buttons as fingerable touch toggles, triggers as momentary
 *  buttons, and dropdowns as in-sheet pickers. All controls bind to the same
 *  juce::AudioProcessorValueTreeState parameters the desktop panels use, so
 *  parameter behaviour, snap-on-select, OSC routing, etc. all work unchanged.
 *
 *  Sliding is handled by parent MainComponentAndroid via setBounds animation;
 *  PanelSheet itself just lays out children inside its current bounds.
 *
 *  Construction is panel-name parameterised:
 *      PanelSheet sheet(processor, "main");
 *      PanelSheet sheet(processor, "modulation");
 *
 *  PanelSheet owns its child controls; on destruction they release their
 *  parameter attachments cleanly.
 */
class PanelSheet : public juce::Component
{
public:
    PanelSheet(EngineAudioProcessor& processor, const juce::String& panel_name);
    ~PanelSheet() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Tap anywhere on the dimmed backdrop dismisses the sheet — caller
    // wires `set_on_dismiss(...)` and arranges the parent to act on it
    // (typically: animate setBounds off-screen).
    void set_on_dismiss(std::function<void()> on_dismiss);

private:
    void build_controls();

    // Inner container that holds the controls grid. Lives inside a Viewport
    // so vertical overflow can be scrolled with a finger.
    class GridContent : public juce::Component
    {
    public:
        void resized() override;
        std::vector<juce::Component*> children;
    };

    EngineAudioProcessor&                       processor_;
    juce::String                                panel_name_;
    juce::Viewport                              viewport_;
    GridContent                                 grid_content_;
    std::vector<std::unique_ptr<juce::Component>> control_widgets_;
    std::function<void()>                       on_dismiss_;
};

#endif // JUCE_ANDROID

#endif

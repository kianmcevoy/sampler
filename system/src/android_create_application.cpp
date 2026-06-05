// Android-only: provide the `juce_CreateApplication` entry point with
// explicit default visibility so JUCE's Android event bootstrap (which
// does a runtime `dlsym("juce_CreateApplication")` on the loaded .so) can
// find it. The Standalone plugin client module would provide a definition
// itself, but our build compiles with `-fvisibility=hidden` so the default
// symbol is hidden and the dlsym silently returns null. That manifests as
// "the JuceActivity comes up but no JUCE Component is ever attached" —
// i.e. the blank screen we hit on first run.
//
// We define JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 on the Standalone
// target (see root CMakeLists.txt) so JUCE skips its built-in definition
// and uses this one instead.

#include "JuceHeader.h"

#if JUCE_ANDROID && JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace juce
{

// Mirror of the JUCE-internal StandaloneFilterApp. Same body as
// juce_audio_plugin_client_Standalone.cpp's class; we re-declare it here
// because the JUCE one is now omitted via the custom-app define.
class StandaloneFilterApp final : public JUCEApplication
{
public:
    StandaloneFilterApp()
    {
        PropertiesFile::Options options;
        options.applicationName     = CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "";
        appProperties.setStorageParameters (options);
    }

    const String getApplicationName() override              { return CharPointer_UTF8 (JucePlugin_Name); }
    const String getApplicationVersion() override           { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override              { return true; }
    void anotherInstanceStarted (const String&) override    {}

    void initialise (const String&) override
    {
        mainWindow.reset (new StandaloneFilterWindow (
            getApplicationName(),
            LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
            createPluginHolder()));

        if (mainWindow != nullptr)
        {
            // StandalonePluginHolder defaults `muteInput` to true to avoid
            // accidental feedback on desktop. On the sampler the mic input is
            // the recording source — keeping it muted means REC captures
            // silence until the user discovers the audio-settings dialog.
            // Unmute on startup so the record path works out of the box.
            mainWindow->pluginHolder->getMuteInputValue().setValue (false);

            mainWindow->setVisible (true);
        }
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            Timer::callAfterDelay (100, []()
            {
                if (auto app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

protected:
    ApplicationProperties appProperties;
    std::unique_ptr<StandaloneFilterWindow> mainWindow;

private:
    std::unique_ptr<StandalonePluginHolder> createPluginHolder()
    {
        const Array<StandalonePluginHolder::PluginInOuts> channelConfig;
        return std::make_unique<StandalonePluginHolder> (
            appProperties.getUserSettings(),
            /*takeOwnershipOfManager=*/false,
            /*preferredDefaultDeviceName=*/String{},
            /*preferredSetupOptions=*/nullptr,
            channelConfig,
            /*autoOpenMidiDevices=*/true);
    }
};

} // namespace juce

// Default-visibility re-export of juce_CreateApplication. JUCE's
// juce_juceEventsAndroidStartApp does `DynamicLibrary(dllPath).getFunction(...)`
// on this symbol from inside the loaded .so, so it must be visible in the
// dynamic symbol table.
extern "C" __attribute__((visibility("default")))
juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new juce::StandaloneFilterApp();
}

#endif

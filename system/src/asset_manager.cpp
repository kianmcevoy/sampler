#include "system/asset_manager.hpp"

juce::String AssetManager::get_real_resource_path(const juce::String& resource_path)
{
    return AssetManager::get_resource_file(resource_path).getFullPathName();
}

juce::File AssetManager::get_resource_file(const juce::String& resource_path)
{
#if JUCE_ANDROID
    // On Android, assets are packaged into the APK and accessed via JUCE's
    // `commonApplicationDataDirectory`, which on Android resolves to the app
    // assets path. JUCE callers should treat the returned File as read-only;
    // its existsAsFile() / etc behave as on a normal filesystem.
    const auto base = juce::File::getSpecialLocation(juce::File::SpecialLocationType::commonApplicationDataDirectory);
    return base.getChildFile(resource_path);
#else
    const auto exe_file = juce::File::getSpecialLocation(juce::File::SpecialLocationType::currentExecutableFile);
    const auto asset_file = exe_file
        .getParentDirectory()
        .getParentDirectory()
        .getChildFile("Resources")
        .getChildFile(resource_path);
    return asset_file;
#endif
}

juce::File AssetManager::get_user_file(const juce::String& product_name, const juce::String& user_path)
{
    const juce::File user_folder = [&product_name](){
        auto folder = juce::File::getSpecialLocation(juce::File::SpecialLocationType::userApplicationDataDirectory)
            .getChildFile("Instruo");
        if (!product_name.isEmpty())
        {
            folder = folder.getChildFile(product_name);
        }
        return folder;
    }();

    if (!user_folder.exists())
    {
        const auto result = user_folder.createDirectory();
    }
    jassert(user_folder.exists());

    const auto user_file = user_folder.getChildFile(user_path);
    return user_file;
}

juce::File AssetManager::get_user_file(const juce::String& user_path)
{
    return AssetManager::get_user_file(JucePlugin_Name, user_path);
}

bool AssetManager::copy_file(const juce::File& source, const juce::File& destination, bool replace)
{
    if (replace || !destination.exists())
    {
        return source.copyFileTo(destination);
    }
    return true;
}

bool AssetManager::copy_directory(const juce::File& source, const juce::File& destination, bool replace)
{
    if (replace || !destination.exists())
    {
        return source.copyDirectoryTo(destination);
    }

    // Copy each file recursively
    if (source.isDirectory() && destination.createDirectory().wasOk())
    {
        for (auto& f : source.findChildFiles(juce::File::findFiles, false))
        {
            if (!copy_file(f, destination.getChildFile(f.getFileName()), replace))
            {
                return false;
            }
        }

        for (auto& f : source.findChildFiles(juce::File::findDirectories, false))
        {
            if (!copy_directory(f, destination.getChildFile(f.getFileName()), replace))
            {
                return false;
            }
        }

        return true;
    }

    return false;
}

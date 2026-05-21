#ifndef GUI_ASSET_LOADER_H
#define GUI_ASSET_LOADER_H

#include "JuceHeader.h"

class AssetManager
{
    public:
        AssetManager() = default;
        ~AssetManager() = default;

        static juce::String get_real_resource_path(const juce::String& resource_path);

        static juce::File get_resource_file(const juce::String& resource_path);

        static juce::File get_user_file(const juce::String& product_name, const juce::String& user_path);

        static juce::File get_user_file(const juce::String& user_path);


        static bool copy_file(const juce::File& source, const juce::File& destination, bool replace);

        static bool copy_directory(const juce::File& source, const juce::File& destination, bool replace);
};

#endif

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

// Factory presets (built in) + user presets (XML files in the user's app-data folder).
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : state (s) {}

    juce::StringArray getFactoryNames() const;
    juce::StringArray getUserNames() const;
    juce::StringArray getAllNames() const;

    bool isFactory (const juce::String& name) const;
    bool load   (const juce::String& name);           // message thread
    bool save   (const juce::String& name);           // user presets only
    bool remove (const juce::String& name);
    bool step   (int delta);                          // next / previous in the combined list

    juce::String getCurrentName() const;
    juce::File   getUserDirectory() const;

private:
    juce::File fileFor (const juce::String& name) const;
    void applyValues (const juce::String& name, const std::map<juce::String, float>& values);

    juce::AudioProcessorValueTreeState& state;
};

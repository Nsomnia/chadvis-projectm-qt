#pragma once

#include "JuceTypes.hpp"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vc::audio::juce {

class JucePluginHost {
public:
    JucePluginHost();
    ~JucePluginHost();

    bool loadPlugin(const String& pluginPath);
    bool loadPluginFromDescription(const PluginDescription& desc);
    void unloadPlugin();
    
    bool isPluginLoaded() const { return pluginInstance_ != nullptr; }
    String getPluginName() const;
    String getPluginFormat() const;
    
    void prepareToPlay(double sampleRate, int blockSize);
    void processBlock(AudioBuffer<float>& buffer);
    void releaseResources();
    
    AudioProcessor* getProcessor() const { return pluginInstance_.get(); }
    AudioProcessorEditor* getEditor() const { return pluginInstance_ ? pluginInstance_->getActiveEditor() : nullptr; }
    
    void setParameter(int parameterIndex, float value);
    float getParameter(int parameterIndex) const;
    String getParameterName(int parameterIndex) const;
    std::vector<std::pair<String, float>> getAllParameters() const;
    
    void scanPlugins(const File& directory, std::function<void(const std::vector<PluginDescription>&)> callback);
    static std::vector<PluginDescription> getAvailablePluginFormats();
    
    vc::Signal<std::string> pluginLoaded;
    vc::Signal<std::string> pluginUnloaded;
    vc::Signal<std::string> errorOccurred;
    
private:
    std::unique_ptr<AudioPluginInstance> pluginInstance_;
    std::unique_ptr<AudioPluginFormatManager> formatManager_;
    std::unique_ptr<KnownPluginList> knownPlugins_;
    
    double sampleRate_{44100.0};
    int blockSize_{512};
    bool prepared_{false};
};

}

#include "JucePluginHost.hpp"
#include "core/Logger.hpp"

#include <juce_audio_processors/juce_audio_processors.h>

namespace vc::audio::juce {

using namespace ::juce;

JucePluginHost::JucePluginHost() {
    formatManager_ = std::make_unique<AudioPluginFormatManager>();
    formatManager_->addDefaultFormats();
    
    knownPlugins_ = std::make_unique<KnownPluginList>();
}

JucePluginHost::~JucePluginHost() {
    unloadPlugin();
}

bool JucePluginHost::loadPlugin(const String& pluginPath) {
    File pluginFile(pluginPath);
    
    if (!pluginFile.exists()) {
        LOG_ERROR("Plugin file not found: {}", pluginPath.toStdString());
        errorOccurred.emit("Plugin file not found: " + pluginPath.toStdString());
        return false;
    }
    
    OwnedArray<PluginDescription> descriptions;
    
    for (int i = 0; i < formatManager_->getNumFormats(); ++i) {
        auto* format = formatManager_->getFormat(i);
        if (format->fileMightContainThisPluginType(pluginPath)) {
            format->findAllTypesForFile(descriptions, pluginPath);
            if (!descriptions.isEmpty()) break;
        }
    }
    
    if (descriptions.isEmpty()) {
        LOG_ERROR("No plugin found in file: {}", pluginPath.toStdString());
        errorOccurred.emit("No valid plugin found in file");
        return false;
    }
    
    return loadPluginFromDescription(*descriptions[0]);
}

bool JucePluginHost::loadPluginFromDescription(const PluginDescription& desc) {
    String errorMessage;
    
    auto plugin = formatManager_->createPluginInstance(desc, sampleRate_, blockSize_, errorMessage);
    
    if (!plugin) {
        LOG_ERROR("Failed to create plugin instance: {}", errorMessage.toStdString());
        errorOccurred.emit(errorMessage.toStdString());
        return false;
    }
    
    unloadPlugin();
    
    pluginInstance_ = std::move(plugin);
    
    if (prepared_) {
        pluginInstance_->prepareToPlay(sampleRate_, blockSize_);
    }
    
    LOG_INFO("Plugin loaded: {} ({})", 
             pluginInstance_->getName().toStdString(),
             desc.pluginFormatName.toStdString());
    
    pluginLoaded.emit(pluginInstance_->getName().toStdString());
    return true;
}

void JucePluginHost::unloadPlugin() {
    if (pluginInstance_) {
        String name = pluginInstance_->getName();
        pluginInstance_.reset();
        LOG_INFO("Plugin unloaded: {}", name.toStdString());
        pluginUnloaded.emit(name.toStdString());
    }
}

String JucePluginHost::getPluginName() const {
    return pluginInstance_ ? pluginInstance_->getName() : String();
}

String JucePluginHost::getPluginFormat() const {
    if (!pluginInstance_) return String();
    
    return pluginInstance_->getPluginDescription().pluginFormatName;
}

void JucePluginHost::prepareToPlay(double sampleRate, int blockSize) {
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;
    prepared_ = true;
    
    if (pluginInstance_) {
        pluginInstance_->prepareToPlay(sampleRate, blockSize);
    }
}

void JucePluginHost::processBlock(AudioBuffer<float>& buffer) {
    if (!pluginInstance_) return;
    
    MidiBuffer midiBuffer;
    pluginInstance_->processBlock(buffer, midiBuffer);
}

void JucePluginHost::releaseResources() {
    prepared_ = false;
    
    if (pluginInstance_) {
        pluginInstance_->releaseResources();
    }
}

void JucePluginHost::setParameter(int parameterIndex, float value) {
    if (!pluginInstance_) return;
    
    if (auto* param = pluginInstance_->getParameters()[parameterIndex]) {
        param->setValue(value);
    }
}

float JucePluginHost::getParameter(int parameterIndex) const {
    if (!pluginInstance_) return 0.0f;
    
    if (auto* param = pluginInstance_->getParameters()[parameterIndex]) {
        return param->getValue();
    }
    return 0.0f;
}

String JucePluginHost::getParameterName(int parameterIndex) const {
    if (!pluginInstance_) return String();
    
    if (auto* param = pluginInstance_->getParameters()[parameterIndex]) {
        return param->getName(64);
    }
    return String();
}

std::vector<std::pair<String, float>> JucePluginHost::getAllParameters() const {
    std::vector<std::pair<String, float>> params;
    
    if (!pluginInstance_) return params;
    
    auto& parameters = pluginInstance_->getParameters();
    params.reserve(parameters.size());
    
    for (auto* param : parameters) {
        params.emplace_back(param->getName(64), param->getValue());
    }
    
    return params;
}

void JucePluginHost::scanPlugins(const File& directory, 
                                  std::function<void(const std::vector<PluginDescription>&)> callback) {
    LOG_INFO("Scanning plugins in: {}", directory.getFullPathName().toStdString());
    
    auto scanner = [this, directory, callback]() {
        std::vector<PluginDescription> found;
        
        for (int i = 0; i < formatManager_->getNumFormats(); ++i) {
            auto* format = formatManager_->getFormat(i);
            
            if (!format->canScanForPlugins()) continue;
            
            OwnedArray<PluginDescription> descriptions;
            format->searchPathsForPlugins(
                FileSearchPath(directory.getFullPathName()),
                descriptions,
                true
            );
            
            for (auto* desc : descriptions) {
                found.push_back(*desc);
            }
        }
        
        if (callback) {
            callback(found);
        }
    };
    
    std::thread(scanner).detach();
}

std::vector<PluginDescription> JucePluginHost::getAvailablePluginFormats() {
    std::vector<PluginDescription> formats;
    
    return formats;
}

}

// ownvst3_wrapper.cpp
#include "../include/ownvst3.h"
#include "../include/ownvst3_wrapper.h"
#include <vector>
#include <cstring>

using namespace OwnVst3Host;

// Helper structure for string cache
struct StringCache {
    static const int MAX_STRINGS = 64;
    char* cache[MAX_STRINGS];
    int index;
    
    StringCache() : index(0) {
        for (int i = 0; i < MAX_STRINGS; i++) {
            cache[i] = nullptr;
        }
    }
    
    ~StringCache() {
        clear();
    }
    
    const char* addString(const std::string& str) {
        // Free old string if exists
        if (cache[index]) {
            free(cache[index]);
            cache[index] = nullptr;
        }
        
        // Duplicate new string
        #ifdef _WIN32
            char* dupStr = _strdup(str.c_str());
        #else
            // UNIX, Linux, macOS system
            char* dupStr = strdup(str.c_str());
        #endif
        
        // Save current position and increment index
        const char* result = cache[index];
        index = (index + 1) % MAX_STRINGS;
        
        return result;
    }
    
    void clear() {
        for (int i = 0; i < MAX_STRINGS; i++) {
            if (cache[i]) {
                free(cache[i]);
                cache[i] = nullptr;
            }
        }
        index = 0;
    }
};

// Global string cache
static StringCache g_stringCache;

VST3PluginHandle VST3Plugin_Create() {
    return new Vst3Plugin();
}

void VST3Plugin_Destroy(VST3PluginHandle handle) {
    if (handle) {
        delete static_cast<Vst3Plugin*>(handle);
    }
}

bool VST3Plugin_LoadPlugin(VST3PluginHandle handle, const char* pluginPath) {
    if (!handle) return false;
    return static_cast<Vst3Plugin*>(handle)->loadPlugin(pluginPath);
}

bool VST3Plugin_CreateEditor(VST3PluginHandle handle, void* windowHandle) {
    if (!handle) return false;
    return static_cast<Vst3Plugin*>(handle)->createEditor(windowHandle);
}

void VST3Plugin_CloseEditor(VST3PluginHandle handle) {
    if (handle) {
        static_cast<Vst3Plugin*>(handle)->closeEditor();
    }
}

void VST3Plugin_ResizeEditor(VST3PluginHandle handle, int width, int height) {
    if (handle) {
        static_cast<Vst3Plugin*>(handle)->resizeEditor(width, height);
    }
}

int VST3Plugin_GetParameterCount(VST3PluginHandle handle) {
    if (!handle) return 0;
    return static_cast<Vst3Plugin*>(handle)->getParameters().size();
}

bool VST3Plugin_GetParameterAt(VST3PluginHandle handle, int index, VST3ParameterC* parameter) {
    if (!handle || !parameter) return false;
    
    auto params = static_cast<Vst3Plugin*>(handle)->getParameters();
    if (index < 0 || index >= params.size()) return false;
    
    auto& param = params[index];
    parameter->id = param.id;
    parameter->name = g_stringCache.addString(param.name);
    parameter->minValue = param.minValue;
    parameter->maxValue = param.maxValue;
    parameter->defaultValue = param.defaultValue;
    parameter->currentValue = param.currentValue;
    
    return true;
}

bool VST3Plugin_SetParameter(VST3PluginHandle handle, int paramId, double value) {
    if (!handle) return false;
    return static_cast<Vst3Plugin*>(handle)->setParameter(paramId, value);
}

double VST3Plugin_GetParameter(VST3PluginHandle handle, int paramId) {
    if (!handle) return 0.0;
    return static_cast<Vst3Plugin*>(handle)->getParameter(paramId);
}

bool VST3Plugin_Initialize(VST3PluginHandle handle, double sampleRate, int maxBlockSize) {
    if (!handle) return false;
    return static_cast<Vst3Plugin*>(handle)->initialize(sampleRate, maxBlockSize);
}

bool VST3Plugin_ProcessAudio(VST3PluginHandle handle, AudioBufferC* buffer) {
    if (!handle || !buffer) return false;
    
    AudioBuffer audioBuffer;
    audioBuffer.inputs = buffer->inputs;
    audioBuffer.outputs = buffer->outputs;
    audioBuffer.numChannels = buffer->numChannels;
    audioBuffer.numSamples = buffer->numSamples;
    
    return static_cast<Vst3Plugin*>(handle)->processAudio(audioBuffer);
}

bool VST3Plugin_ProcessMidi(VST3PluginHandle handle, const MidiEventC* events, int eventCount) {
    if (!handle || !events || eventCount <= 0) return false;
    
    std::vector<MidiEvent> midiEvents;
    for (int i = 0; i < eventCount; i++) {
        MidiEvent event;
        event.status = events[i].status;
        event.data1 = events[i].data1;
        event.data2 = events[i].data2;
        event.sampleOffset = events[i].sampleOffset;
        midiEvents.push_back(event);
    }
    
    return static_cast<Vst3Plugin*>(handle)->processMidi(midiEvents);
}

bool VST3Plugin_IsInstrument(VST3PluginHandle handle) {
    if (!handle) return false;
    return static_cast<Vst3Plugin*>(handle)->isInstrument();
}

bool VST3Plugin_IsEffect(VST3PluginHandle handle) {
    if (!handle) return false;
    return static_cast<Vst3Plugin*>(handle)->isEffect();
}

const char* VST3Plugin_GetName(VST3PluginHandle handle) {
    if (!handle) return "";
    return g_stringCache.addString(static_cast<Vst3Plugin*>(handle)->getName());
}

const char* VST3Plugin_GetVendor(VST3PluginHandle handle) {
    if (!handle) return "";
    return g_stringCache.addString(static_cast<Vst3Plugin*>(handle)->getVendor());
}

const char* VST3Plugin_GetVersion(VST3PluginHandle handle) {
    if (!handle) return "";
    return g_stringCache.addString(static_cast<Vst3Plugin*>(handle)->getVersion());
}

const char* VST3Plugin_GetPluginInfo(VST3PluginHandle handle) {
    if (!handle) return "";
    return g_stringCache.addString(static_cast<Vst3Plugin*>(handle)->getPluginInfo());
}

void VST3Plugin_ClearStringCache() {
    g_stringCache.clear();
}
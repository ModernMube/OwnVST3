// ownvst3.h
#pragma once

#include <string>
#include <memory>
#include <vector>

#include <public.sdk/source/vst/hosting/plugprovider.h>
#include <public.sdk/source/vst/hosting/module.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/eventlist.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>
#include <public.sdk/source/vst/hosting/processdata.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstprocesscontext.h>
#include <pluginterfaces/gui/iplugview.h>

// ownvst3.h definíciója
#ifdef _WIN32
    #ifdef OWN_VST3_HOST_EXPORTS
        #define OWN_VST3_HOST_API __declspec(dllexport)
    #else
        #define OWN_VST3_HOST_API __declspec(dllimport)
    #endif
#else
    #define OWN_VST3_HOST_API __attribute__((visibility("default")))
#endif

namespace OwnVst3Host {

// Forward declarations
class Vst3PluginImpl;

// Structure for VST3 parameter representation
struct Vst3Parameter {
    int id;                // Parameter ID
    std::string name;      // Parameter name
    double minValue;       // Minimum value
    double maxValue;       // Maximum value
    double defaultValue;   // Default value
    double currentValue;   // Current value
};

// Structure for audio processing buffer
struct AudioBuffer {
    float** inputs;        // Input channels array
    float** outputs;       // Output channels array
    int numChannels;       // Number of channels
    int numSamples;        // Number of samples per channel
};

// Structure for MIDI event representation
struct MidiEvent {
    int status;            // MIDI status byte (note on/off, CC, etc.)
    int data1;             // MIDI data1 byte (note number, CC number)
    int data2;             // MIDI data2 byte (velocity, CC value)
    int sampleOffset;      // Sample position within the block
};

class OWN_VST3_HOST_API Vst3Plugin {
public:
    // Constructor and destructor
    Vst3Plugin();
    ~Vst3Plugin();

    // Loads a VST3 plugin from the specified path
    bool loadPlugin(const std::string& pluginPath);
    
    // Editor management functions
    bool createEditor(void* windowHandle);  // Creates and attaches editor to window
    void closeEditor();                     // Closes the editor
    void resizeEditor(int width, int height); // Resizes the editor window
    
    // Parameter management functions
    std::vector<Vst3Parameter> getParameters(); // Gets all plugin parameters
    bool setParameter(int paramId, double value); // Sets parameter value
    double getParameter(int paramId);           // Gets parameter value
    
    // Initializes the plugin with sample rate and block size
    bool initialize(double sampleRate, int maxBlockSize);
    
    // Process audio through the plugin
    bool processAudio(AudioBuffer& buffer);
    
    // Process MIDI events
    bool processMidi(const std::vector<MidiEvent>& events);
    
    // Plugin type checking functions
    bool isInstrument();  // Checks if plugin is an instrument
    bool isEffect();      // Checks if plugin is an effect
    
    // Plugin information functions
    std::string getName();       // Gets plugin name
    std::string getVendor();     // Gets plugin vendor
    std::string getVersion();    // Gets plugin version
    std::string getPluginInfo(); // Gets formatted plugin information

private:
    std::unique_ptr<Vst3PluginImpl> impl;  // Implementation object (PIMPL pattern)

    VST3::Hosting::Module::Ptr module = nullptr;  // VST3 module pointer

    Steinberg::IPtr<Steinberg::Vst::PlugProvider> plugProvider = nullptr;  // Plugin provider
    Steinberg::IPtr<Steinberg::Vst::IComponent> component = nullptr;       // Component interface
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller = nullptr; // Controller interface
    
    std::vector<Vst3Parameter> parameters;  // Cached parameter list
};

} // namespace OwnVst3Host
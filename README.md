<div align="center">
  <img src="Ownaudiologo.png" alt="Logó" width="600"/>
</div>

<a href="https://www.buymeacoffee.com/ModernMube">
  <img src="https://img.shields.io/badge/Support-Buy%20Me%20A%20Coffe-orange" alt="Buy Me a Coffe">
</a>

# OwnVst3

A lightweight C++ wrapper for loading and working with VST3 plugins.

## Overview

OwnVst3Host is a C++ library that provides a simplified interface for hosting VST3 plugins in your applications. It handles the complex details of the VST3 SDK and provides an easy-to-use API for loading plugins, manipulating parameters, processing audio, and handling MIDI events.

## Features

- Load VST3 plugins from file paths
- Get plugin information (name, vendor, etc.)
- Create and manage plugin editor UI
- List, get, and set plugin parameters
- Process audio through plugins
- Send MIDI events to instrument plugins
- Detect plugin types (instrument vs effect)
- Cross-platform support (Windows, macOS, Linux)

## Requirements

- C++17 or higher
- VST3 SDK (Steinberg)
- Windows, macOS, or Linux operating system

## Git clone

```bash
git clone --recurse-submodules https://github.com/ModernMube/OwnVST3.git
```

## Installation

1. Include the OwnVst3Host header and source files in your project
2. Make sure the VST3 SDK is properly set up and linked
3. When building as a DLL, define:
   - `OWN_VST3_HOST_EXPORTS` for the C++ API
   - `OWN_VST3_WRAPPER_EXPORTS` for the C wrapper API

## Build Windows

```bash
mkdir build
cd build
cmake ..
cmake --build .
```
## Build Mac and Linux

```bash
mkdir build
cd build
cmake ..
make
```

## Basic Usage

```cpp
#include "ownvst3.h"
#include <iostream>
#include <vector>

int main() {
    // Create plugin instance
    OwnVst3Host::Vst3Plugin plugin;
    
    // Load a VST3 plugin
    if (plugin.loadPlugin("C:/Path/To/YourPlugin.vst3")) {
        // Initialize with sample rate and block size
        plugin.initialize(44100.0, 512);
        
        // Print plugin information
        std::cout << "Plugin loaded: " << plugin.getName() << std::endl;
        std::cout << plugin.getPluginInfo() << std::endl;
        
        // Get parameters
        auto params = plugin.getParameters();
        std::cout << "Parameters:" << std::endl;
        for (const auto& param : params) {
            std::cout << "  " << param.name << " (ID: " << param.id << ")" << std::endl;
        }
        
        // Create audio buffer for processing
        float* inputBuffer[2] = { /* your input data */ };
        float* outputBuffer[2] = { /* buffer for output data */ };
        
        OwnVst3Host::AudioBuffer buffer;
        buffer.inputs = inputBuffer;
        buffer.outputs = outputBuffer;
        buffer.numChannels = 2;
        buffer.numSamples = 512;
        
        // Process audio
        plugin.processAudio(buffer);
        
        // Or send MIDI events if it's an instrument
        if (plugin.isInstrument()) {
            std::vector<OwnVst3Host::MidiEvent> midiEvents;
            // Add note-on event
            OwnVst3Host::MidiEvent noteOn;
            noteOn.status = 0x90; // Note on, channel 0
            noteOn.data1 = 60;    // C4
            noteOn.data2 = 100;   // Velocity
            noteOn.sampleOffset = 0;
            midiEvents.push_back(noteOn);
            
            plugin.processMidi(midiEvents);
        }
    }
    
    return 0;
}
```

## API Documentation

### Vst3Plugin Class

The main class for interacting with VST3 plugins.

#### Constructor/Destructor
- `Vst3Plugin()` - Creates a new VST3 plugin instance
- `~Vst3Plugin()` - Cleans up and releases resources

#### Plugin Loading and Initialization
- `bool loadPlugin(const std::string& pluginPath)` - Loads a VST3 plugin from the specified path
- `bool initialize(double sampleRate, int maxBlockSize)` - Initializes the plugin with sample rate and block size

#### Editor Management
- `bool createEditor(void* windowHandle)` - Creates and attaches the plugin's editor to a window
- `void closeEditor()` - Closes the plugin editor
- `void resizeEditor(int width, int height)` - Resizes the plugin editor window
- `bool getEditorSize(int& width, int& height)` - Gets the editor's preferred size
- `void processIdle()` - Processes idle events (should be called periodically from UI thread)
- `bool isEditorOpen()` - Checks if the editor window is currently open

#### Parameter Management
- `std::vector<Vst3Parameter> getParameters()` - Gets all available parameters
- `int getParameterCount()` - Gets total parameter count
- `bool getParameterInfo(int index, Vst3Parameter& outParam)` - Gets parameter by index
- `bool setParameter(int paramId, double value)` - Sets a parameter value
- `double getParameter(int paramId)` - Gets a parameter value

#### Audio and MIDI Processing
- `bool processAudio(AudioBuffer& buffer)` - Processes audio through the plugin
- `bool processMidi(const std::vector<MidiEvent>& events)` - Sends MIDI events to the plugin
- `int getActualInputChannels()` - Gets the actual input channel count accepted by the plugin
- `int getActualOutputChannels()` - Gets the actual output channel count accepted by the plugin

#### Transport State
- `void setTempo(double bpm)` - Sets the playback tempo (BPM)
- `void setTransportState(bool playing)` - Sets the transport playing state
- `void resetTransportPosition()` - Resets the transport sample position counter

#### Plugin Type
- `bool isInstrument()` - Checks if the plugin is an instrument
- `bool isEffect()` - Checks if the plugin is an effect
- `bool isMidiOnly()` - Checks if the plugin accepts MIDI but has no audio output

#### Plugin Information
- `std::string getName()` - Gets the plugin name
- `std::string getVendor()` - Gets the plugin vendor
- `std::string getVersion()` - Gets the plugin version
- `std::string getPluginInfo()` - Gets formatted plugin information

### C Wrapper API

A C-compatible wrapper is available for use with languages that can interface with C but not C++. Include `ownvst3_wrapper.h` for access.

#### Functions
- `VST3PluginHandle VST3Plugin_Create()` - Creates a new plugin instance
- `void VST3Plugin_Destroy(VST3PluginHandle handle)` - Destroys a plugin instance
- `bool VST3Plugin_LoadPlugin(VST3PluginHandle handle, const char* pluginPath)` - Loads a plugin
- `bool VST3Plugin_Initialize(VST3PluginHandle handle, double sampleRate, int maxBlockSize)` - Initializes the plugin
- `bool VST3Plugin_CreateEditor(VST3PluginHandle handle, void* windowHandle)` - Creates editor
- `void VST3Plugin_CloseEditor(VST3PluginHandle handle)` - Closes editor
- `void VST3Plugin_ResizeEditor(VST3PluginHandle handle, int width, int height)` - Resizes editor
- `bool VST3Plugin_GetEditorSize(VST3PluginHandle handle, int* width, int* height)` - Gets editor size
- `void VST3Plugin_ProcessIdle(VST3PluginHandle handle)` - Processes idle events
- `bool VST3Plugin_IsEditorOpen(VST3PluginHandle handle)` - Checks if editor is currently open
- `int VST3Plugin_GetParameterCount(VST3PluginHandle handle)` - Gets parameter count
- `bool VST3Plugin_GetParameterAt(VST3PluginHandle handle, int index, VST3ParameterC* parameter)` - Gets parameter by index
- `bool VST3Plugin_SetParameter(VST3PluginHandle handle, int paramId, double value)` - Sets parameter
- `double VST3Plugin_GetParameter(VST3PluginHandle handle, int paramId)` - Gets parameter value
- `bool VST3Plugin_ProcessAudio(VST3PluginHandle handle, AudioBufferC* buffer)` - Processes audio
- `bool VST3Plugin_ProcessMidi(VST3PluginHandle handle, const MidiEventC* events, int eventCount)` - Processes MIDI
- `int VST3Plugin_GetActualInputChannels(VST3PluginHandle handle)` - Gets actual input channels
- `int VST3Plugin_GetActualOutputChannels(VST3PluginHandle handle)` - Gets actual output channels
- `void VST3Plugin_SetTempo(VST3PluginHandle handle, double bpm)` - Sets playback tempo
- `void VST3Plugin_SetTransportState(VST3PluginHandle handle, bool isPlaying)` - Sets transport state
- `void VST3Plugin_ResetTransportPosition(VST3PluginHandle handle)` - Resets transport position
- `bool VST3Plugin_IsInstrument(VST3PluginHandle handle)` - Checks if instrument
- `bool VST3Plugin_IsEffect(VST3PluginHandle handle)` - Checks if effect
- `bool VST3Plugin_IsMidiOnly(VST3PluginHandle handle)` - Checks if plugin is MIDI only
- `const char* VST3Plugin_GetName(VST3PluginHandle handle)` - Gets plugin name
- `const char* VST3Plugin_GetVendor(VST3PluginHandle handle)` - Gets plugin vendor
- `const char* VST3Plugin_GetVersion(VST3PluginHandle handle)` - Gets plugin version
- `const char* VST3Plugin_GetPluginInfo(VST3PluginHandle handle)` - Gets plugin info
- `void VST3Plugin_ClearStringCache()` - Clears the internal string cache

## License

This project is available under the MIT License.

## Acknowledgements

Based on the VST3 SDK by Steinberg Media Technologies GmbH.

## Support My Work

If you find this project helpful, consider buying me a coffee!

<a href="https://www.buymeacoffee.com/ModernMube" 
    target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/arial-yellow.png" 
    alt="Buy Me A Coffee" 
    style="height: 60px !important;width: 217px !important;" >
 </a>

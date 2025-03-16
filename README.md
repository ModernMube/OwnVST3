# OwnVst3Host

A lightweight C++ wrapper for loading and working with VST3 plugins.

## Overview

OwnVst3Host is a C++ library that provides a simplified interface for hosting VST3 plugins in your applications. It handles the complex details of the VST3 SDK and provides an easy-to-use API for loading plugins, manipulating parameters, processing audio, and handling MIDI events.

## Features

- Load VST3 plugins from file paths
- Get plugin information (name, vendor, version, etc.)
- Create and manage plugin editor UI
- List, get, and set plugin parameters
- Process audio through plugins
- Send MIDI events to instrument plugins
- Detect plugin types (instrument vs effect)
- Cross-platform support (Windows, macOS, Linux)

## Requirements

- C++11 or higher
- VST3 SDK (Steinberg)
- Windows, macOS, or Linux operating system

## Git clone

```bash
git clone --recurse-submodules https://github.com/felhasznaloneved/projekted.git
```

## Installation

1. Include the OwnVst3Host header and source files in your project
2. Make sure the VST3 SDK is properly set up and linked
3. Define `OWN_VST3_HOST_EXPORTS` when building the library as a DLL

## Build

```bash
mkdir build
cd build
cmake ..
cmake --build .
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

#### Parameter Management
- `std::vector<Vst3Parameter> getParameters()` - Gets all available parameters
- `bool setParameter(int paramId, double value)` - Sets a parameter value
- `double getParameter(int paramId)` - Gets a parameter value

#### Audio and MIDI Processing
- `bool processAudio(AudioBuffer& buffer)` - Processes audio through the plugin
- `bool processMidi(const std::vector<MidiEvent>& events)` - Sends MIDI events to the plugin

#### Plugin Information
- `bool isInstrument()` - Checks if the plugin is an instrument
- `bool isEffect()` - Checks if the plugin is an effect
- `std::string getName()` - Gets the plugin name
- `std::string getVendor()` - Gets the plugin vendor
- `std::string getVersion()` - Gets the plugin version
- `std::string getPluginInfo()` - Gets formatted plugin information

## License

This project is available under the MIT License.

## Acknowledgements

Based on the VST3 SDK by Steinberg Media Technologies GmbH.

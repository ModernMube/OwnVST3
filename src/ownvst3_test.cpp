#include "../include/ownvst3.h"
#include "../include/ownvst3_wrapper.h"
#include <iostream>
#include <string>
#include <vector>
#include <cmath> 

int main(int argc, char* argv[]) {
    
    // Plugin path
    //std::string pluginPath = "C:\\Program Files\\Common Files\\VST3\\iZotope\\reverb.vst3";
    std::string pluginPath = "--- Plugin fullpath and plugin name ---";
    
    
    if (argc > 1) {
        pluginPath = argv[1];
    }
    
    // Initialize wrapper
    OwnVst3Host::Vst3Plugin plugin;
    
    std::cout << "Loading VST3 plugin: " << pluginPath << std::endl;
    
    // Load plugin
    if (!plugin.loadPlugin(pluginPath)) {
        std::cerr << "Error: Failed to load VST3 plugin!" << std::endl;
        return 1;
    }
    
    // Print plugin info
    std::cout << "Plugin name: " << plugin.getName() << std::endl;
    std::cout << "Plugin type: " << (plugin.isInstrument() ? "Instrument" : "Effect") << std::endl;
    
    // Initialize plugin
    if (!plugin.initialize(44100.0, 512)) {
        std::cerr << "Error: Failed to initialize VST3 plugin!" << std::endl;
        return 1;
    }
    
    // Query parameters
    std::vector<OwnVst3Host::Vst3Parameter> params = plugin.getParameters();
    std::cout << "\nPlugin parameters (" << params.size() << "):\n";
    
    for (const auto& param : params) {
        std::cout << "  - ID: " << param.id << ", Name: " << param.name 
                  << ", Value: " << param.currentValue << std::endl;
    }
    
    // Create test audio buffer
    OwnVst3Host::AudioBuffer buffer;
    buffer.numChannels = 2;
    buffer.numSamples = 512;
    
    // Initialize test buffer
    float* inputL = new float[buffer.numSamples]();
    float* inputR = new float[buffer.numSamples]();
    float* outputL = new float[buffer.numSamples]();
    float* outputR = new float[buffer.numSamples]();
    
    float* inputChannels[2] = { inputL, inputR };
    float* outputChannels[2] = { outputL, outputR };
    
    buffer.inputs = inputChannels;
    buffer.outputs = outputChannels;
    
    // Generate test sine wave
    for (int i = 0; i < buffer.numSamples; i++) {
        inputL[i] = 0.5f * sinf(2.0f * 3.14159f * 440.0f * i / 44100.0f);
        inputR[i] = inputL[i];
    }
    
    // Audio processing
    //std::cout << "\nTesting audio processing..." << std::endl;
    if (!plugin.processAudio(buffer)) {
        std::cerr << "Error: Audio processing failed!" << std::endl;
    } else {
        std::cout << "Audio processing successful!" << std::endl;
    }
    
    // Test MIDI message
    std::cout << "\nTesting MIDI message..." << std::endl;
    
    OwnVst3Host::MidiEvent noteOn;
    noteOn.status = 0x90;  // Note On, Channel 1
    noteOn.data1 = 60;     // C4
    noteOn.data2 = 100;    // Velocity
    noteOn.sampleOffset = 0;
    
    std::vector<OwnVst3Host::MidiEvent> midiEvents = { noteOn };
    
    if (!plugin.processMidi(midiEvents)) {
        std::cerr << "Error: MIDI processing failed!" << std::endl;
    } else {
        std::cout << "MIDI processing successful!" << std::endl;
    }
    
    // Free memory
    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;
    
    // End of main
    std::cout << "\nTest execution completed." << std::endl;
    std::cout << "Press Enter to exit...";
    std::cin.get(); // This allows for a clean shutdown
    return 0;
}
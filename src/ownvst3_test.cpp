#include "../include/ownvst3.h"
#include "../include/ownvst3_wrapper.h"
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    // Plugin elérési útvonala
    //std::string pluginPath = "C:\\Program Files\\Common Files\\VST3\\iZotope\\RX 8 De-reverb.vst3";
    std::string pluginPath = "C:\\Program Files\\Common Files\\VST3\\TDR Nova.vst3";
    
    
    if (argc > 1) {
        pluginPath = argv[1];
    }
    
    // Wrapper inicializálása
    OwnVst3Host::Vst3Plugin plugin;
    
    std::cout << "VST3 plugin betöltése: " << pluginPath << std::endl;
    
    // Plugin betöltése
    if (!plugin.loadPlugin(pluginPath)) {
        std::cerr << "Hiba: Nem sikerült betölteni a VST3 plugint!" << std::endl;
        return 1;
    }
    
    // Plugin adatok kiírása
    std::cout << "Plugin név: " << plugin.getName() << std::endl;
    std::cout << "Plugin típus: " << (plugin.isInstrument() ? "Instrument" : "Effect") << std::endl;
    
    // Plugin inicializálása
    if (!plugin.initialize(44100.0, 512)) {
        std::cerr << "Hiba: Nem sikerült inicializálni a VST3 plugint!" << std::endl;
        return 1;
    }
    
    // Paraméterek lekérdezése
    std::vector<OwnVst3Host::Vst3Parameter> params = plugin.getParameters();
    std::cout << "\nPlugin paraméterek (" << params.size() << " db):\n";
    
    for (const auto& param : params) {
        std::cout << "  - ID: " << param.id << ", Név: " << param.name 
                  << ", Érték: " << param.currentValue << std::endl;
    }
    
    // Teszt audio buffer létrehozása
    OwnVst3Host::AudioBuffer buffer;
    buffer.numChannels = 2;
    buffer.numSamples = 512;
    
    // Teszt buffer inicializálása
    float* inputL = new float[buffer.numSamples]();
    float* inputR = new float[buffer.numSamples]();
    float* outputL = new float[buffer.numSamples]();
    float* outputR = new float[buffer.numSamples]();
    
    float* inputChannels[2] = { inputL, inputR };
    float* outputChannels[2] = { outputL, outputR };
    
    buffer.inputs = inputChannels;
    buffer.outputs = outputChannels;
    
    // Teszt sin hullám generálása
    for (int i = 0; i < buffer.numSamples; i++) {
        inputL[i] = 0.5f * sinf(2.0f * 3.14159f * 440.0f * i / 44100.0f);
        inputR[i] = inputL[i];
    }
    
    // Audio processzálás
    //std::cout << "\nAudio processzálás tesztelése..." << std::endl;
    if (!plugin.processAudio(buffer)) {
        std::cerr << "Hiba: Nem sikerült az audio processzálás!" << std::endl;
    } else {
        std::cout << "Audio processzálás sikeres!" << std::endl;
    }
    
    // MIDI üzenet tesztelése
    std::cout << "\nMIDI üzenet tesztelése..." << std::endl;
    
    OwnVst3Host::MidiEvent noteOn;
    noteOn.status = 0x90;  // Note On, Channel 1
    noteOn.data1 = 60;     // C4
    noteOn.data2 = 100;    // Velocity
    noteOn.sampleOffset = 0;
    
    std::vector<OwnVst3Host::MidiEvent> midiEvents = { noteOn };
    
    if (!plugin.processMidi(midiEvents)) {
        std::cerr << "Hiba: Nem sikerült a MIDI processzálás!" << std::endl;
    } else {
        std::cout << "MIDI processzálás sikeres!" << std::endl;
    }
    
    // Memória felszabadítása
    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;
    
    // A main végére
    std::cout << "\nA teszt futása befejeződött." << std::endl;
    std::cout << "Nyomjon Enter-t a kilépéshez...";
    std::cin.get(); // Ez lehetőséget ad a szabályos leállásra
    return 0;
}
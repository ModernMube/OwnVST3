// ownvst3.cpp
#include "../include/ownvst3.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "base/source/fobject.h"

#include <iostream>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <locale>
    #include <codecvt>  
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace OwnVst3Host {

class Vst3PluginImpl : public FObject {
public:
    // Constructor: Initializes default values for sample rate and block size
    Vst3PluginImpl() : sampleRate(44100.0), blockSize(512) {}
    
    // Destructor: Ensures plugin is unloaded properly
    ~Vst3PluginImpl() { unloadPlugin(); }

    // Loads a VST3 plugin from the specified path
    bool loadPlugin(const std::string& pluginPath) {
        try {
            // Load the VST3 module
            std::string error;
            module = VST3::Hosting::Module::create(pluginPath, error);
            if (!module) {
                std::cerr << "Failed to load VST3 module: " << pluginPath << std::endl;
                return false;
            }
            
            // Get factory interface
            factory = module->getFactory().get();
            if (!factory || factory->countClasses() == 0) {
                std::cerr << "Failed to create factory interface" << std::endl;
                return false;
            }
            
            // Find component and controller class IDs
            FUID componentID, controllerID;
            std::string componentName;
            
            bool foundComponent = findComponentAndControllerIDs(factory, componentID, controllerID, componentName);
            if (!foundComponent) {
                std::cerr << "No suitable components found in VST3 module" << std::endl;
                return false;
            }
            
            // Create component instance
            tresult result = factory->createInstance(componentID, IComponent::iid, (void**)&component);
            
            if (result != kResultOk || !component) {
                std::cerr << "Component creation failed" << std::endl;
                return false;
            }
            
            // Create and initialize host application, initialize component
            Steinberg::Vst::HostApplication hostApp;
            if (component->initialize(&hostApp) != kResultOk) {
                std::cerr << "Component initialization failed" << std::endl;
                return false;
            }
            
            // Activate component
            if (component->setActive(true) != kResultOk) {
                std::cerr << "Component activation failed" << std::endl;
                return false;
            }
            
            // Create controller instance
            if (controllerID.isValid()) {
                result = factory->createInstance(controllerID, IEditController::iid, 
                                               (void**)&controller);
                
                if (result != kResultOk || !controller) {
                    std::cerr << "Controller creation failed, trying to get from component" << std::endl;
                    
                    // Try to get controller from component
                    FUnknownPtr<IEditController> compController(component);
                    if (compController) {
                        controller = compController;
                    } else {
                        // Create our own controller using factory
                        TUID tuid;
                        if (component->getControllerClassId(tuid) == kResultOk) {
                            FUID iid(tuid);
                            if(iid.isValid()) {
                                factory->createInstance(tuid, IEditController::iid, (void**)&controller);
                            }
                        }
                    }
                }
                
                // Initialize controller if created successfully
                if (controller) {
                    Steinberg::Vst::HostApplication hostApp;
                    controller->initialize(&hostApp);
                    
                    // Connect component and controller
                    connectComponentAndController();
                }
            }
            
            // Get audio processor interface
            processor = FUnknownPtr<IAudioProcessor>(component);
            if (!processor) {
                std::cerr << "Plugin does not implement IAudioProcessor interface" << std::endl;
                return false;
            }
            
            // Setup processing
            setupProcessing();
            
            // Update parameters
            updateParameters();
            
            std::cout << "VST3 plugin loaded successfully: " << componentName << std::endl;
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "Error occurred while loading plugin: " << e.what() << std::endl;
            return false;
        }
    }

    void unloadPlugin() {
        try {
            // 1. Távolítsuk el a nézetet
            if (view) {
                view->removed();
                view = nullptr;
            }
            
            // 2. Deaktiválás, ha még aktív
            if (component) {
                try {
                    component->setActive(false);
                } catch (...) {}
            }
            
            // 3. Minden referenciát null-ra állítunk, de nem hívunk terminate-et
            view = nullptr;
            processor = nullptr;
            
            // Fontos: Elengedjük a referenciákat, de nem hívunk terminate-et
            controller = nullptr;
            component = nullptr;
            factory = nullptr;
            
            // 4. A module objektum felszabadításával a VST3 SDK kezeli a belső komponensek felszabadítását
            module = nullptr;
        }
        catch (...) {
            // Kivételek elnyomása
        }
    }

    // Creates and attaches the plugin's editor to a window
    bool createEditor(void* windowHandle) {
        if (!controller) return false;
        
        // Create editor view
        view = controller->createView(ViewType::kEditor);
        if (!view) {
            std::cerr << "Failed to create editor" << std::endl;
            return false;
        }
        
        // Try to attach to window with platform-specific type
        if (view->attached(windowHandle, kPlatformStringWin) != kResultOk) 
        {
            if(view->attached(windowHandle, kPlatformStringMac) != kResultOk) 
            {
                if(view->attached(windowHandle, kPlatformStringLinux) != kResultOk)
                {
                    std::cerr << "Failed to attach editor to window" << std::endl;
                    view = nullptr;
                    return false;
                }
            }
        }
        
        return true;
    }
    
    // Closes the plugin editor
    void closeEditor() {
        if (view) {
            view->removed();
            view = nullptr;
        }
    }
    
    // Resizes the plugin editor window
    void resizeEditor(int width, int height) {
        if (view) {
            ViewRect viewRect(0, 0, width, height);
            view->onSize(&viewRect);
        }
    }
    
    // Initializes the plugin with specified sample rate and block size
    bool initialize(double newSampleRate, int newBlockSize) {
        if (!processor) return false;
        
        sampleRate = newSampleRate;
        blockSize = newBlockSize;
        
        return setupProcessing();
    }
    
    // Sets up audio processing with current settings
    bool setupProcessing() {
        if (!processor) return false;
        
        // Initialize ProcessSetup structure
        ProcessSetup setup;
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = blockSize;
        setup.sampleRate = sampleRate;
        
        // Setup processor
        if (processor->setupProcessing(setup) != kResultOk) {
            std::cerr << "Failed to setup processing" << std::endl;
            return false;
        }
        
        // Activate buses
        int numInputs = component->getBusCount(kAudio, kInput);
        int numOutputs = component->getBusCount(kAudio, kOutput);
        
        for (int i = 0; i < numInputs; i++) {
            component->activateBus(kAudio, kInput, i, true);
        }
        
        for (int i = 0; i < numOutputs; i++) {
            component->activateBus(kAudio, kOutput, i, true);
        }
        
        return true;
    }
    
    // Gets all available parameters from the plugin
    std::vector<Vst3Parameter> getParameters() {
        updateParameters();
        return parameters;
    }
    
    // Updates the internal parameter cache
    void updateParameters() {
        parameters.clear();
        
        if (!controller) return;
        
        int count = controller->getParameterCount();
        
        for (int i = 0; i < count; i++) {
            ParameterInfo info;
            if (controller->getParameterInfo(i, info) == kResultOk) {
                Vst3Parameter param;
                param.id = info.id;
                param.name = tchar_to_utf8(info.title);
                param.minValue = 0.0;
                param.maxValue = 1.0;
                param.defaultValue = info.defaultNormalizedValue;
                param.currentValue = controller->getParamNormalized(info.id);
                parameters.push_back(param);
            }
        }
    }
    
    // Sets a parameter value by ID
    bool setParameter(int paramId, double value) {
        if (!controller) return false;
        
        controller->setParamNormalized(paramId, value);
        return true;
    }
    
    // Gets a parameter value by ID
    double getParameter(int paramId) {
        if (!controller) return 0.0;
        
        return controller->getParamNormalized(paramId);
    }
    
    // Processes audio through the plugin
    bool processAudio(AudioBuffer& buffer) {
        if (!processor) return false;
        
        Steinberg::Vst::ProcessData data;
        data.numSamples = buffer.numSamples;
        
        // Setup audio data
        Steinberg::Vst::AudioBusBuffers inputBuffers;
        inputBuffers.numChannels = buffer.numChannels;
        inputBuffers.channelBuffers32 = buffer.inputs;
        inputBuffers.silenceFlags = 0;
        
        Steinberg::Vst::AudioBusBuffers outputBuffers;
        outputBuffers.numChannels = buffer.numChannels;
        outputBuffers.channelBuffers32 = buffer.outputs;
        outputBuffers.silenceFlags = 0;
        
        data.inputs = &inputBuffers;
        data.outputs = &outputBuffers;
        data.numInputs = 1;
        data.numOutputs = 1;
        
        // Initialize missing fields
        data.processMode = Steinberg::Vst::ProcessModes::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::SymbolicSampleSizes::kSample32;
        data.processContext = nullptr; // Or provide a valid ProcessContext
        data.inputParameterChanges = nullptr; // Or provide valid IParameterChanges
        data.outputParameterChanges = nullptr; // Or provide valid IParameterChanges
        
        // Process audio
        if (processor->process(data) != Steinberg::kResultOk) {
            std::cerr << "Error during audio processing" << std::endl;
            return false;
        }
        
        return true;
    }
    
    // Processes MIDI events through the plugin
    bool processMidi(const std::vector<MidiEvent>& events) {
        if (!processor) return false;
        
        // Convert MIDI events to VST3 events
        EventList eventList;
        
        for (const auto& event : events) {
            Event e = {};
            e.busIndex = 0;
            e.sampleOffset = event.sampleOffset;
            e.ppqPosition = 0.0;
            
            // MIDI type event
            if ((event.status & 0xF0) == 0x90) {  // Note On
                e.type = Event::kNoteOnEvent;
                e.noteOn.channel = event.status & 0x0F;
                e.noteOn.pitch = event.data1;
                e.noteOn.velocity = event.data2 / 127.0f;
                e.noteOn.length = 0;
                e.noteOn.tuning = 0.0f;
                e.noteOn.noteId = -1;
            }
            else if ((event.status & 0xF0) == 0x80) {  // Note Off
                e.type = Event::kNoteOffEvent;
                e.noteOff.channel = event.status & 0x0F;
                e.noteOff.pitch = event.data1;
                e.noteOff.velocity = event.data2 / 127.0f;
                e.noteOff.tuning = 0.0f;
                e.noteOff.noteId = -1;
            }
            else if ((event.status & 0xF0) == 0xB0) {  // Controller
                e.type = Event::kLegacyMIDICCOutEvent;
                e.midiCCOut.channel = event.status & 0x0F;
                e.midiCCOut.controlNumber = event.data1;
                e.midiCCOut.value = event.data2 / 127.0f;
            }
            
            eventList.addEvent(e);
        }
        
        // Setup ProcessData
        ProcessData data;
        data.numSamples = 0;
        data.inputs = nullptr;
        data.outputs = nullptr;
        data.inputEvents = &eventList;
        data.outputEvents = nullptr;
        
        // Process MIDI
        if (processor->process(data) != kResultOk) {
            std::cerr << "Error during MIDI processing" << std::endl;
            return false;
        }
        
        return true;
    }
    
    // Checks if the plugin is an instrument (accepts MIDI, produces audio)
    bool isInstrument() {
        if (!component) return false;
        
        int numEventInputs = component->getBusCount(kEvent, kInput);
        int numAudioOutputs = component->getBusCount(kAudio, kOutput);
        
        return (numEventInputs > 0 && numAudioOutputs > 0);
    }
    
    // Checks if the plugin is an effect (processes audio)
    bool isEffect() {
        if (!component) return false;
        
        int numAudioInputs = component->getBusCount(kAudio, kInput);
        int numAudioOutputs = component->getBusCount(kAudio, kOutput);
        
        return (numAudioInputs > 0 && numAudioOutputs > 0);
    }
    
    // Gets the plugin name
    std::string getName() {
        if (!component || !factory) return "";
        
        // Get component name
        PClassInfo classInfo;
        for (int32 i = 0; i < factory->countClasses(); i++) {
            if (factory->getClassInfo(i, &classInfo) == kResultOk) {
                if (strcmp(classInfo.category, kVstAudioEffectClass) == 0) {
                    return classInfo.name;
                }
            }
        }
        
        return module ? module->getName() : ""; // Fallback: module name
    }
    
    // Gets the plugin vendor
    std::string getVendor() {
        if (!factory) return "";
        
        // Get factory info for vendor
        PFactoryInfo factoryInfo;
        if (factory->getFactoryInfo(&factoryInfo) == kResultOk) {
            return factoryInfo.vendor;
        }
        
        return "";
    }
    
    // Gets formatted plugin information
    std::string getPluginInfo() {
        std::string info;
        info += "Name: " + getName() + "\n";
        info += "Vendor: " + getVendor() + "\n";
        
        // Number of input/output buses
        if (component) {
            int numInputs = component->getBusCount(kAudio, kInput);
            int numOutputs = component->getBusCount(kAudio, kOutput);
            info += "Audio buses: " + std::to_string(numInputs) + " input(s), " + 
                    std::to_string(numOutputs) + " output(s)\n";
                    
            int numEventInputs = component->getBusCount(kEvent, kInput);
            if (numEventInputs > 0) {
                info += "MIDI input: Yes\n";
            } else {
                info += "MIDI input: No\n";
            }
        }
        
        return info;
    }

    
    private:
    // Finds component and controller IDs from the factory
    bool findComponentAndControllerIDs(IPluginFactory* factory, FUID& componentID, 
                                        FUID& controllerID, std::string& componentName) {
        bool foundComponent = false;
        PClassInfo classInfo;

        // Iterate through all classes
        for (int32 i = 0; i < factory->countClasses(); i++) {
            if (factory->getClassInfo(i, &classInfo) == kResultOk) {
            // Look for component
                if (strcmp(classInfo.category, kVstAudioEffectClass) == 0) {
                    componentID = FUID(classInfo.cid);
                    componentName = classInfo.name;
                    foundComponent = true;

                    // Get controller ID from component
                    FUnknownPtr<IComponent> tempComponent;
                    if (factory->createInstance(componentID, IComponent::iid, 
                    (void**)&tempComponent) == kResultOk && tempComponent) {
                        TUID _ctrID;
                        if (tempComponent->getControllerClassId(_ctrID) == kResultOk) {
                            FUID ctrID(_ctrID);
                            if(ctrID.isValid()) {
                                controllerID = ctrID;
                            }
                        }

                        tempComponent->terminate();
                    }

                    break;
                }
            }
        }

        return foundComponent;
    }

    // Connects component and controller with connection points
    void connectComponentAndController() {
        
        if (!component || !controller) {
            return;
        }
        
        // Get connection points
        FUnknownPtr<IConnectionPoint> componentCP(component);
        FUnknownPtr<IConnectionPoint> controllerCP(controller);
        
        // Connect if both support connection point interface
        if (componentCP && controllerCP) {
            componentCP->connect(controllerCP);
            controllerCP->connect(componentCP);
        }
    }

    VST3::Hosting::Module::Ptr module = nullptr;  // VST3 module pointer
    IPluginFactory* factory = nullptr;             // Plugin factory

    IComponent* component = nullptr;               // Component interface
    IEditController* controller = nullptr;         // Edit controller interface
    IAudioProcessor* processor = nullptr;          // Audio processor interface
    IPlugView* view = nullptr;                     // Plugin view interface

    std::vector<Vst3Parameter> parameters;         // Parameter cache
    double sampleRate;                             // Current sample rate
    int blockSize;                                 // Current block size

    #ifdef _WIN32
    // Converts TCHAR to UTF-8 string
    std::string tchar_to_utf8(const Steinberg::Vst::TChar* tcharStr) {
        std::string result;
        if (!tcharStr) return result;
        
        #ifdef UNICODE
        // For Unicode, convert wchar_t
        std::wstring wstr(reinterpret_cast<const wchar_t*>(tcharStr));
        // Determine conversion size
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
        // Allocate string and convert
        if (size_needed > 0) {
            result.resize(size_needed);
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size_needed, NULL, NULL);
        }
        #else
        // For non-Unicode, copy string directly
        result = tcharStr;
        #endif
        
        return result;
        }
    #else        
        // Converts TCHAR to UTF-8 string
        std::string tchar_to_utf8(const Steinberg::Vst::TChar* tcharStr) {
            std::string result;
            if (!tcharStr) return result;
            
        #ifdef UNICODE
            // For Unicode, convert wchar_t
            std::wstring wstr(reinterpret_cast<const wchar_t*>(tcharStr));
            
            // UNIX/macOS conversion using C++11 codecvt
            try {
                std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
                result = converter.to_bytes(wstr);
            } catch (const std::exception& e) {
                // Fallback in case of conversion error
                result.clear();
                for (size_t i = 0; i < wstr.length(); ++i) {
                    // Simple fallback conversion (only works for ASCII subset)
                    if (wstr[i] <= 127) {
                        result.push_back(static_cast<char>(wstr[i]));
                    } else {
                        result.push_back('?'); // Replace non-ASCII with ?
                    }
                }
            }
        #else
            // For non-Unicode, copy string directly
            result = tcharStr;
        #endif

            return result;
        }
    #endif
    };

    // Vst3Plugin implementation - Add OWN_VST3_HOST_API to fix the inconsistent dll linkage warning
    Vst3Plugin::Vst3Plugin() : impl(new Vst3PluginImpl()) {}

    Vst3Plugin::~Vst3Plugin() {}

    bool Vst3Plugin::loadPlugin(const std::string& pluginPath) {
        return impl->loadPlugin(pluginPath);
    }

    bool Vst3Plugin::createEditor(void* windowHandle) {
        return impl->createEditor(windowHandle);
    }

    void Vst3Plugin::closeEditor() {
        impl->closeEditor();
    }

    void Vst3Plugin::resizeEditor(int width, int height) {
        impl->resizeEditor(width, height);
    }

    std::vector<Vst3Parameter> Vst3Plugin::getParameters() {
        return impl->getParameters();
    }

    bool Vst3Plugin::setParameter(int paramId, double value) {
        return impl->setParameter(paramId, value);
    }

    double Vst3Plugin::getParameter(int paramId) {
        return impl->getParameter(paramId);
    }

    bool Vst3Plugin::initialize(double sampleRate, int maxBlockSize) {
        return impl->initialize(sampleRate, maxBlockSize);
    }

    bool Vst3Plugin::processAudio(AudioBuffer& buffer) {
        return impl->processAudio(buffer);
    }

    bool Vst3Plugin::processMidi(const std::vector<MidiEvent>& events) {
        return impl->processMidi(events);
    }

    bool Vst3Plugin::isInstrument() {
        return impl->isInstrument();
    }

    bool Vst3Plugin::isEffect() {
        return impl->isEffect();
    }

    std::string Vst3Plugin::getName() {
        return impl->getName();
    }

    std::string Vst3Plugin::getVendor() {
        return impl->getVendor();
    }

    // std::string Vst3Plugin::getVersion() {
    //     return impl->getVersion();
    // }

    std::string Vst3Plugin::getPluginInfo() {
        return impl->getPluginInfo();
    }

} // namespace OwnVst3Host
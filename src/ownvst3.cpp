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
#include <mutex>
#include <vector>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <locale>
    #include <codecvt>
#endif

#if defined(__linux__) || defined(__APPLE__)
    #include <sys/select.h>
    #include <unistd.h>
    #include <time.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace OwnVst3Host {

#if defined(__linux__) || defined(__APPLE__)
// IRunLoop implementation for Linux and macOS
// Required for VST3 plugins to handle timers and event handlers properly
// especially for dropdown menus and popup windows
class LinuxRunLoop : public Linux::IRunLoop {
public:
    LinuxRunLoop() : refCount(1) {}
    virtual ~LinuxRunLoop() {}

    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler, Linux::FileDescriptor fd) override {
        if (!handler) return kInvalidArgument;
        std::lock_guard<std::mutex> lock(mutex);
        eventHandlers.push_back({handler, fd});
        return kResultOk;
    }

    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override {
        if (!handler) return kInvalidArgument;
        std::lock_guard<std::mutex> lock(mutex);
        eventHandlers.erase(
            std::remove_if(eventHandlers.begin(), eventHandlers.end(),
                [handler](const EventHandlerEntry& e) { return e.handler == handler; }),
            eventHandlers.end());
        return kResultOk;
    }

    tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler, Linux::TimerInterval milliseconds) override {
        if (!handler) return kInvalidArgument;
        std::lock_guard<std::mutex> lock(mutex);
        timerHandlers.push_back({handler, milliseconds, 0});
        return kResultOk;
    }

    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override {
        if (!handler) return kInvalidArgument;
        std::lock_guard<std::mutex> lock(mutex);
        timerHandlers.erase(
            std::remove_if(timerHandlers.begin(), timerHandlers.end(),
                [handler](const TimerHandlerEntry& e) { return e.handler == handler; }),
            timerHandlers.end());
        return kResultOk;
    }

    // Process events and timers - should be called periodically from UI thread
    void processEvents(uint64 currentTimeMs) {
        std::lock_guard<std::mutex> lock(mutex);

        // Process file descriptor events
        if (!eventHandlers.empty()) {
            fd_set readfds;
            FD_ZERO(&readfds);
            int maxfd = 0;
            for (const auto& entry : eventHandlers) {
                FD_SET(entry.fd, &readfds);
                if (entry.fd > maxfd) maxfd = entry.fd;
            }

            struct timeval tv = {0, 0}; // Non-blocking
            if (select(maxfd + 1, &readfds, nullptr, nullptr, &tv) > 0) {
                for (const auto& entry : eventHandlers) {
                    if (FD_ISSET(entry.fd, &readfds)) {
                        entry.handler->onFDIsSet(entry.fd);
                    }
                }
            }
        }

        // Process timers
        for (auto& entry : timerHandlers) {
            if (currentTimeMs - entry.lastCallTime >= entry.interval) {
                entry.handler->onTimer();
                entry.lastCallTime = currentTimeMs;
            }
        }
    }

    // FUnknown interface
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, Linux::IRunLoop::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() override { return ++refCount; }
    uint32 PLUGIN_API release() override {
        uint32 r = --refCount;
        if (r == 0) delete this;
        return r;
    }

private:
    struct EventHandlerEntry {
        Linux::IEventHandler* handler;
        Linux::FileDescriptor fd;
    };

    struct TimerHandlerEntry {
        Linux::ITimerHandler* handler;
        Linux::TimerInterval interval;
        uint64 lastCallTime;
    };

    std::vector<EventHandlerEntry> eventHandlers;
    std::vector<TimerHandlerEntry> timerHandlers;
    std::mutex mutex;
    std::atomic<uint32> refCount;
};
#endif

// IPlugFrame implementation for handling plugin editor window frame
// Also implements IRunLoop on Linux and macOS for proper event handling
class PlugFrame : public IPlugFrame
#if defined(__linux__) || defined(__APPLE__)
    , public Linux::IRunLoop
#endif
{
public:
    PlugFrame() : refCount(1) {
#if defined(__linux__) || defined(__APPLE__)
        runLoop = new LinuxRunLoop();
#endif
    }
    virtual ~PlugFrame() {
#if defined(__linux__) || defined(__APPLE__)
        if (runLoop) runLoop->release();
#endif
    }

    // IPlugFrame interface
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
        if (!view || !newSize) return kInvalidArgument;
        // Accept the resize request - the actual window resize is handled by the host
        return kResultOk;
    }

#if defined(__linux__) || defined(__APPLE__)
    // IRunLoop interface (Linux and macOS)
    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler, Linux::FileDescriptor fd) override {
        return runLoop ? runLoop->registerEventHandler(handler, fd) : kResultFalse;
    }

    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override {
        return runLoop ? runLoop->unregisterEventHandler(handler) : kResultFalse;
    }

    tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler, Linux::TimerInterval milliseconds) override {
        return runLoop ? runLoop->registerTimer(handler, milliseconds) : kResultFalse;
    }

    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override {
        return runLoop ? runLoop->unregisterTimer(handler) : kResultFalse;
    }

    void processEvents(uint64 currentTimeMs) {
        if (runLoop) runLoop->processEvents(currentTimeMs);
    }
#endif

    // FUnknown interface
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            addRef();
            return kResultOk;
        }
#if defined(__linux__) || defined(__APPLE__)
        if (FUnknownPrivate::iidEqual(iid, Linux::IRunLoop::iid)) {
            *obj = static_cast<Linux::IRunLoop*>(this);
            addRef();
            #ifdef __APPLE__
            std::cout << "[PlugFrame] Plugin requested IRunLoop interface on macOS" << std::endl;
            #endif
            return kResultOk;
        }
#endif
        // Log unhandled interface requests for diagnostics
        #ifdef __APPLE__
        char iidStr[33];
        FUID(iid).toString(iidStr);
        std::cout << "[PlugFrame] Plugin requested unknown interface: " << iidStr << std::endl;
        #endif
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() override { return ++refCount; }
    uint32 PLUGIN_API release() override {
        uint32 r = --refCount;
        if (r == 0) delete this;
        return r;
    }

private:
    std::atomic<uint32> refCount;
#if defined(__linux__) || defined(__APPLE__)
    LinuxRunLoop* runLoop = nullptr;
#endif
};

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
            
            // Initialize component with persistent host application instance
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
                    // Use persistent host application instance
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

        // Create and set plug frame for proper popup/dropdown handling
        if (!plugFrame) {
            plugFrame = new PlugFrame();
        }
        view->setFrame(plugFrame);

        // Try to attach to window with platform-specific type
        if (view->attached(windowHandle, kPlatformTypeHWND) != kResultOk)
        {
            if(view->attached(windowHandle, kPlatformTypeNSView) != kResultOk)
            {
                if(view->attached(windowHandle, kPlatformTypeX11EmbedWindowID) != kResultOk)
                {
                    std::cerr << "Failed to attach editor to window" << std::endl;
                    view->setFrame(nullptr);
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
            view->setFrame(nullptr);
            view->removed();
            view = nullptr;
        }
        if (plugFrame) {
            plugFrame->release();
            plugFrame = nullptr;
        }
    }
    
    // Resizes the plugin editor window
    void resizeEditor(int width, int height) {
        if (view) {
            ViewRect viewRect(0, 0, width, height);
            view->onSize(&viewRect);
        }
    }

    // Gets the plugin editor's preferred size
    bool getEditorSize(int& width, int& height) {
        // If view already exists, use it
        if (view) {
            ViewRect rect;
            if (view->getSize(&rect) == kResultOk) {
                width = rect.getWidth();
                height = rect.getHeight();
                return true;
            }
        }

        // If no view exists, try to create a temporary one to get size
        if (controller) {
            IPlugView* tempView = controller->createView(ViewType::kEditor);
            if (tempView) {
                ViewRect rect;
                if (tempView->getSize(&rect) == kResultOk) {
                    width = rect.getWidth();
                    height = rect.getHeight();
                    tempView->release();
                    return true;
                }
                tempView->release();
            }
        }

        width = 0;
        height = 0;
        return false;
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

    // Gets the plugin version
    std::string getVersion() {
        if (!factory) return "";

        // Try to get version from PClassInfo2 (IPluginFactory2)
        IPluginFactory2* factory2 = nullptr;
        if (factory->queryInterface(IPluginFactory2::iid, (void**)&factory2) == kResultOk && factory2) {
            PClassInfo2 classInfo2;
            for (int32 i = 0; i < factory2->countClasses(); i++) {
                if (factory2->getClassInfo2(i, &classInfo2) == kResultOk) {
                    if (strcmp(classInfo2.category, kVstAudioEffectClass) == 0) {
                        factory2->release();
                        return classInfo2.version;
                    }
                }
            }
            factory2->release();
        }

        return "1.0.0"; // Default version if not available
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

    // Process idle events - should be called periodically from UI thread
    // This is essential for proper popup menu handling on all platforms
    void processIdle() {
#if defined(__linux__) || defined(__APPLE__)
        // On Linux and macOS, process registered timers and event handlers
        // This is essential for proper dropdown menu and popup window handling
        if (plugFrame) {
            // Get current time in milliseconds
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64 currentTimeMs = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            plugFrame->processEvents(currentTimeMs);
        }
#endif
#ifdef _WIN32
        // Process any pending Windows messages
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
#endif
    }

    // Check if editor is currently open
    bool isEditorOpen() const {
        return view != nullptr;
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

    // Host application instance - MUST be declared FIRST to ensure it is:
    // 1. Constructed first (before component/controller use it)
    // 2. Destructed LAST (after component/controller are released)
    Steinberg::Vst::HostApplication hostApp;

    VST3::Hosting::Module::Ptr module = nullptr;  // VST3 module pointer
    IPluginFactory* factory = nullptr;             // Plugin factory

    IComponent* component = nullptr;               // Component interface
    IEditController* controller = nullptr;         // Edit controller interface
    IAudioProcessor* processor = nullptr;          // Audio processor interface
    IPlugView* view = nullptr;                     // Plugin view interface
    PlugFrame* plugFrame = nullptr;                // Plugin frame for editor window

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

    bool Vst3Plugin::getEditorSize(int& width, int& height) {
        return impl->getEditorSize(width, height);
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

    std::string Vst3Plugin::getVersion() {
        return impl->getVersion();
    }

    std::string Vst3Plugin::getPluginInfo() {
        return impl->getPluginInfo();
    }

    void Vst3Plugin::processIdle() {
        impl->processIdle();
    }

    bool Vst3Plugin::isEditorOpen() {
        return impl->isEditorOpen();
    }

} // namespace OwnVst3Host
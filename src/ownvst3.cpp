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
#include <atomic>
#include <array>
#include <functional>
#include <unordered_map>

#ifdef _WIN32
    #define NOMINMAX
    #include <windows.h>
#else
    #include <locale>
    #include <codecvt>
#endif

#ifdef __linux__
    #include <sys/select.h>
    #include <poll.h>
    #include <unistd.h>
    #include <time.h>
#endif

#ifdef __APPLE__
    #include <CoreFoundation/CoreFoundation.h>

    static constexpr CFTimeInterval MACOS_IDLE_TIMER_INTERVAL = 0.02;

    extern "C" void OwnVst3_CloseChildWindows(void* nsViewHandle);
    extern "C" void OwnVst3_ProcessIdleMacOS(void);
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace OwnVst3Host {

// ---------------------------------------------------------------------------
// ParamChangeSPSC – lock-free Single-Producer / Single-Consumer ring buffer
//
// Producer: UI/host thread (setParameter).
// Consumer: audio thread (processAudio).
//
// Properties:
//   - No heap allocation after construction.
//   - No hash collisions (FIFO queue, not a direct-mapped cache).
//   - If the queue is full, push() returns false; the latest value is still
//     stored in lastSetValues so a subsequent setParameter() retries delivery.
//   - head and tail are on separate cache lines to avoid false sharing.
// ---------------------------------------------------------------------------
static constexpr size_t PARAM_QUEUE_CAPACITY = 1024;

struct ParamChange {
    uint32_t id;
    double   value;
};

class ParamChangeSPSC {
public:
    bool push(uint32_t id, double value) {
        size_t t    = tail.load(std::memory_order_relaxed);
        size_t next = (t + 1u) % PARAM_QUEUE_CAPACITY;
        if (next == head.load(std::memory_order_acquire))
            return false; // full
        buf[t] = {id, value};
        tail.store(next, std::memory_order_release);
        return true;
    }

    size_t popAll(ParamChange* out, size_t maxOut) {
        size_t h     = head.load(std::memory_order_relaxed);
        size_t t     = tail.load(std::memory_order_acquire);
        size_t count = 0;
        while (h != t && count < maxOut) {
            out[count++] = buf[h];
            h = (h + 1u) % PARAM_QUEUE_CAPACITY;
        }
        head.store(h, std::memory_order_release);
        return count;
    }

private:
    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
    alignas(64) ParamChange buf[PARAM_QUEUE_CAPACITY];
};

// ---------------------------------------------------------------------------

#ifdef __linux__
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

    void processEvents(uint64 currentTimeMs) {
        std::lock_guard<std::mutex> lock(mutex);

        if (!eventHandlers.empty()) {
            std::vector<struct pollfd> pollfds;
            pollfds.reserve(eventHandlers.size());
            for (const auto& entry : eventHandlers) {
                struct pollfd pfd;
                pfd.fd = entry.fd;
                pfd.events = POLLIN;
                pfd.revents = 0;
                pollfds.push_back(pfd);
            }
            int result = poll(pollfds.data(), pollfds.size(), 0);
            if (result > 0) {
                for (size_t i = 0; i < pollfds.size(); ++i) {
                    if (pollfds[i].revents & POLLIN)
                        eventHandlers[i].handler->onFDIsSet(eventHandlers[i].fd);
                }
            }
        }

        for (auto& entry : timerHandlers) {
            if (currentTimeMs - entry.lastCallTime >= entry.interval) {
                entry.handler->onTimer();
                entry.lastCallTime = currentTimeMs;
            }
        }
    }

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
    struct EventHandlerEntry { Linux::IEventHandler* handler; Linux::FileDescriptor fd; };
    struct TimerHandlerEntry { Linux::ITimerHandler* handler; Linux::TimerInterval interval; uint64 lastCallTime; };

    std::vector<EventHandlerEntry> eventHandlers;
    std::vector<TimerHandlerEntry> timerHandlers;
    std::mutex mutex;
    std::atomic<uint32> refCount;
};
#endif

class PlugFrame : public IPlugFrame
#ifdef __linux__
    , public Linux::IRunLoop
#endif
{
public:
    PlugFrame() : refCount(1) {
#ifdef __linux__
        runLoop = new LinuxRunLoop();
#endif
    }
    virtual ~PlugFrame() {
#ifdef __linux__
        if (runLoop) runLoop->release();
#endif
    }

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
        if (!view || !newSize) return kInvalidArgument;
        return kResultOk;
    }

#ifdef __linux__
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

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            addRef();
            return kResultOk;
        }
#ifdef __linux__
        if (FUnknownPrivate::iidEqual(iid, Linux::IRunLoop::iid)) {
            *obj = static_cast<Linux::IRunLoop*>(this);
            addRef();
            return kResultOk;
        }
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
#ifdef __linux__
    LinuxRunLoop* runLoop = nullptr;
#endif
};

// IComponentHandler + IComponentHandler2.
// IComponentHandler2 is required by JUCE-based hosts (T-Racks 6): they
// dereference the QueryInterface result without checking the return code.
// restartComponent is intentionally a no-op: calling component or controller
// methods from this callback can arrive on a non-main thread (JUCE internal
// thread), which causes NSView/AppKit corruption on macOS.
class OwnComponentHandler : public Steinberg::Vst::IComponentHandler,
                            public Steinberg::Vst::IComponentHandler2 {
public:
    explicit OwnComponentHandler() : refCount(1) {}

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override { return Steinberg::kResultOk; }

    Steinberg::tresult PLUGIN_API setDirty(Steinberg::TBool) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API requestOpenEditor(Steinberg::FIDString) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API startGroupEdit() override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API finishGroupEdit() override { return Steinberg::kResultOk; }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IComponentHandler::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            addRef();
            return Steinberg::kResultOk;
        }
        if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IComponentHandler2::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler2*>(this);
            addRef();
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return ++refCount; }
    Steinberg::uint32 PLUGIN_API release() override {
        Steinberg::uint32 r = --refCount;
        if (r == 0) delete this;
        return r;
    }

private:
    std::atomic<Steinberg::uint32> refCount;
};

class Vst3PluginImpl : public FObject {
public:
    Vst3PluginImpl() : sampleRate(44100.0), blockSize(512) {}
    ~Vst3PluginImpl() { unloadPlugin(); }

    bool loadPlugin(const std::string& pluginPath) {
        try {
            std::string error;
            module = VST3::Hosting::Module::create(pluginPath, error);
            if (!module) {
                std::cerr << "Failed to load VST3 module: " << pluginPath << std::endl;
                return false;
            }

            factory = module->getFactory().get();
            if (!factory || factory->countClasses() == 0) {
                std::cerr << "Failed to create factory interface" << std::endl;
                return false;
            }

            FUID componentID, controllerID;
            std::string componentName;

            if (!findComponentAndControllerIDs(factory, componentID, controllerID, componentName)) {
                std::cerr << "No suitable components found in VST3 module" << std::endl;
                return false;
            }

            tresult result = factory->createInstance(componentID, IComponent::iid, (void**)&component);
            if (result != kResultOk || !component) {
                std::cerr << "Component creation failed" << std::endl;
                return false;
            }

            if (component->initialize(&hostApp) != kResultOk) {
                std::cerr << "Component initialization failed" << std::endl;
                return false;
            }

            if (controllerID.isValid()) {
                result = factory->createInstance(controllerID, IEditController::iid, (void**)&controller);
                if (result != kResultOk || !controller) {
                    FUnknownPtr<IEditController> compController(component);
                    if (compController) {
                        controller = compController;
                    } else {
                        TUID tuid;
                        if (component->getControllerClassId(tuid) == kResultOk) {
                            FUID iid(tuid);
                            if (iid.isValid())
                                factory->createInstance(tuid, IEditController::iid, (void**)&controller);
                        }
                    }
                }

                if (controller) {
                    controller->initialize(&hostApp);

                    componentHandler = new OwnComponentHandler();
                    componentHandler->addRef();
                    if (controller->setComponentHandler(componentHandler) != kResultOk)
                        std::cerr << "setComponentHandler failed (non-fatal)" << std::endl;

                    connectComponentAndController();
                }
            }

            processor = FUnknownPtr<IAudioProcessor>(component);
            if (!processor) {
                std::cerr << "Plugin does not implement IAudioProcessor interface" << std::endl;
                return false;
            }

            setupProcessing();

            if (component->setActive(true) != kResultOk) {
                std::cerr << "Component activation failed" << std::endl;
                return false;
            }
            isActive.store(true, std::memory_order_release);

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
#ifdef __APPLE__
            if (editorNSViewHandle) {
                OwnVst3_CloseChildWindows(editorNSViewHandle);
                editorNSViewHandle = nullptr;
            }
            if (idleTimer) {
                CFRunLoopTimerInvalidate(idleTimer);
                CFRelease(idleTimer);
                idleTimer = nullptr;
            }
#endif
            if (view) {
                view->setFrame(nullptr);
                view->removed();
                view = nullptr;
            }
            if (plugFrame) {
                plugFrame->release();
                plugFrame = nullptr;
            }

            if (component && isActive.load(std::memory_order_acquire)) {
                try { component->setActive(false); } catch (...) {}
                isActive.store(false, std::memory_order_release);
            }

            if (componentHandler) {
                componentHandler->release();
                componentHandler = nullptr;
            }

            view = nullptr;
            processor = nullptr;
            controller = nullptr;
            component = nullptr;
            factory = nullptr;
            module = nullptr;
        }
        catch (...) {}
    }

    bool createEditor(void* windowHandle) {
        if (!controller) return false;

        view = controller->createView(ViewType::kEditor);
        if (!view) {
            std::cerr << "Failed to create editor" << std::endl;
            return false;
        }

        if (!plugFrame)
            plugFrame = new PlugFrame();
        view->setFrame(plugFrame);

#ifdef _WIN32
        const auto platformType = kPlatformTypeHWND;
#elif defined(__APPLE__)
        const auto platformType = kPlatformTypeNSView;
#elif defined(__linux__)
        const auto platformType = kPlatformTypeX11EmbedWindowID;
#else
        #error "Unsupported platform"
#endif
        if (view->attached(windowHandle, platformType) != kResultOk) {
            std::cerr << "Failed to attach editor to window" << std::endl;
            view->setFrame(nullptr);
            view = nullptr;
            return false;
        }

#ifdef __APPLE__
        editorNSViewHandle = windowHandle;

        if (!idleTimer) {
            CFRunLoopTimerContext context = {0, this, nullptr, nullptr, nullptr};
            idleTimer = CFRunLoopTimerCreate(
                kCFAllocatorDefault,
                CFAbsoluteTimeGetCurrent() + MACOS_IDLE_TIMER_INTERVAL,
                MACOS_IDLE_TIMER_INTERVAL,
                0, 0,
                [](CFRunLoopTimerRef, void* info) {
                    static_cast<Vst3PluginImpl*>(info)->processIdle();
                },
                &context);
            if (idleTimer)
                CFRunLoopAddTimer(CFRunLoopGetMain(), idleTimer, kCFRunLoopDefaultMode);
        }
#endif
        return true;
    }

    void closeEditor() {
#ifdef __APPLE__
        if (idleTimer) {
            CFRunLoopTimerInvalidate(idleTimer);
            CFRelease(idleTimer);
            idleTimer = nullptr;
        }
        if (editorNSViewHandle) {
            OwnVst3_CloseChildWindows(editorNSViewHandle);
            editorNSViewHandle = nullptr;
        }
#endif
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

    void resizeEditor(int width, int height) {
        if (view) {
            ViewRect viewRect(0, 0, width, height);
            view->onSize(&viewRect);
        }
    }

    bool getEditorSize(int& width, int& height) {
        if (view) {
            ViewRect rect;
            if (view->getSize(&rect) == kResultOk) {
                width = rect.getWidth();
                height = rect.getHeight();
                return true;
            }
        }
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

    bool initialize(double newSampleRate, int newBlockSize) {
        if (!processor) return false;

        sampleRate = newSampleRate;
        blockSize  = newBlockSize;

        if (isActive.load(std::memory_order_acquire) && component) {
            component->setActive(false);
            isActive.store(false, std::memory_order_release);
        }

        bool result = setupProcessing();

        if (result && component && component->setActive(true) == kResultOk)
            isActive.store(true, std::memory_order_release);

        return result && isActive.load(std::memory_order_acquire);
    }

    bool setupProcessing() {
        if (!processor) return false;

        numInputBuses  = component->getBusCount(kAudio, kInput);
        numOutputBuses = component->getBusCount(kAudio, kOutput);

        if (numInputBuses > 0 || numOutputBuses > 0) {
            SpeakerArrangement stereo = SpeakerArr::kStereo;
            SpeakerArrangement mono   = SpeakerArr::kMono;

            std::vector<SpeakerArrangement> inputArr(numInputBuses,  stereo);
            std::vector<SpeakerArrangement> outputArr(numOutputBuses, stereo);

            tresult busResult = processor->setBusArrangements(
                inputArr.data(),  numInputBuses,
                outputArr.data(), numOutputBuses);

            if (busResult != kResultOk) {
                std::fill(inputArr.begin(), inputArr.end(), mono);
                processor->setBusArrangements(
                    inputArr.data(),  numInputBuses,
                    outputArr.data(), numOutputBuses);
            }
        }

        actualInputChannels  = 0;
        actualOutputChannels = 0;
        for (int i = 0; i < numInputBuses; i++) {
            BusInfo info = {};
            if (component->getBusInfo(kAudio, kInput, i, info) == kResultOk)
                actualInputChannels += info.channelCount;
        }
        for (int i = 0; i < numOutputBuses; i++) {
            BusInfo info = {};
            if (component->getBusInfo(kAudio, kOutput, i, info) == kResultOk)
                actualOutputChannels += info.channelCount;
        }

        // Activate only the primary bus (index 0).
        // Secondary buses (sidechain, aux) are deactivated because we have no
        // audio data to supply for them. The plugin must not access buffers for
        // inactive buses per the VST3 spec.
        if (numInputBuses > 0)
            component->activateBus(kAudio, kInput, 0, true);
        for (int i = 1; i < numInputBuses; ++i)
            component->activateBus(kAudio, kInput, i, false);

        if (numOutputBuses > 0)
            component->activateBus(kAudio, kOutput, 0, true);
        for (int i = 1; i < numOutputBuses; ++i)
            component->activateBus(kAudio, kOutput, i, false);

        ProcessSetup setup;
        setup.processMode        = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = blockSize;
        setup.sampleRate         = sampleRate;

        if (processor->setupProcessing(setup) != kResultOk) {
            std::cerr << "Failed to setup processing" << std::endl;
            return false;
        }

        processContextRequirements = 0;
        {
            FUnknownPtr<IProcessContextRequirements> pcr(processor);
            if (pcr)
                processContextRequirements = pcr->getProcessContextRequirements();
        }

        // Pre-allocate bus buffer arrays (no heap on audio thread).
        inBusBuffers.assign(numInputBuses,   AudioBusBuffers{});
        outBusBuffers.assign(numOutputBuses, AudioBusBuffers{});

        // Pre-allocate a stereo silent buffer for secondary/sidechain buses.
        // Even though those buses are deactivated, we provide valid (zero-filled)
        // pointers to guard against buggy plugins that still read inactive buses.
        int safeBlock = std::max(1, blockSize);
        silentBuffer.assign(static_cast<size_t>(safeBlock) * 2, 0.0f);
        silentChannelPtrs[0] = silentBuffer.data();
        silentChannelPtrs[1] = silentBuffer.data() + safeBlock;

        // Pre-allocate parameter change queue slots.
        inputParamChanges.setMaxParameters(static_cast<int32>(PARAM_QUEUE_CAPACITY));
        outputParamChanges.setMaxParameters(static_cast<int32>(PARAM_QUEUE_CAPACITY));

        return true;
    }

    std::vector<Vst3Parameter> getParameters() {
        updateParameters();
        return parameters;
    }

    void updateParameters() {
        parameters.clear();
        if (!controller) return;

        int count = controller->getParameterCount();

        std::lock_guard<std::mutex> lock(paramMutex);
        for (int i = 0; i < count; i++) {
            ParameterInfo info;
            if (controller->getParameterInfo(i, info) == kResultOk) {
                Vst3Parameter param;
                param.id           = info.id;
                param.name         = tchar_to_utf8(info.title);
                param.minValue     = 0.0;
                param.maxValue     = 1.0;
                param.defaultValue = info.defaultNormalizedValue;
                param.currentValue = controller->getParamNormalized(info.id);
                parameters.push_back(param);

                // Seed the last-set cache so getParameter() returns the plugin's
                // current value before any setParameter() call has been made.
                lastSetValues.emplace(static_cast<uint32_t>(info.id), param.currentValue);
            }
        }
    }

    int getParameterCount() const {
        return controller ? controller->getParameterCount() : 0;
    }

    bool getParameterInfo(int index, Vst3Parameter& outParam) {
        if (!controller) return false;

        ParameterInfo info;
        if (controller->getParameterInfo(index, info) != kResultOk)
            return false;

        outParam.id           = info.id;
        outParam.name         = tchar_to_utf8(info.title);
        outParam.minValue     = 0.0;
        outParam.maxValue     = 1.0;
        outParam.defaultValue = info.defaultNormalizedValue;

        // Read current value from our cache to avoid calling into the controller
        // (which holds an internal mutex shared with the audio thread on some plugins).
        bool hasCached = false;
        double cached  = 0.0;
        {
            std::lock_guard<std::mutex> lock(paramMutex);
            auto it = lastSetValues.find(static_cast<uint32_t>(info.id));
            if (it != lastSetValues.end()) {
                hasCached = true;
                cached    = it->second;
            }
        }
        outParam.currentValue = hasCached ? cached : controller->getParamNormalized(info.id);
        return true;
    }

    // Delivers a parameter change to the audio thread via the lock-free SPSC queue.
    // Also caches the value so getParameter() returns the correct value immediately,
    // without querying the controller (which can deadlock with the audio thread on
    // plugins that share an internal mutex between controller and processor).
    bool setParameter(int paramId, double value) {
        if (!controller) return false;

        uint32_t uid = static_cast<uint32_t>(paramId);

        {
            std::lock_guard<std::mutex> lock(paramMutex);
            lastSetValues[uid] = value;
        }

        // If the queue is full (extremely unlikely at 1024 entries), the value is
        // already in lastSetValues and will be sent on the next setParameter() call.
        if (!paramQueue.push(uid, value))
            std::cerr << "Parameter queue full – change for id=" << uid << " dropped (audio thread backpressure)" << std::endl;

        return true;
    }

    // Returns the most recently set value for the given parameter.
    // Reads from the host-side cache to avoid calling controller->getParamNormalized(),
    // which acquires the plugin's internal mutex and can deadlock with the audio thread.
    double getParameter(int paramId) {
        uint32_t uid = static_cast<uint32_t>(paramId);
        {
            std::lock_guard<std::mutex> lock(paramMutex);
            auto it = lastSetValues.find(uid);
            if (it != lastSetValues.end())
                return it->second;
        }
        // Fall back to controller for parameters not yet explicitly set by the host.
        if (controller) return controller->getParamNormalized(paramId);
        return 0.0;
    }

    bool processAudio(AudioBuffer& buffer) {
        if (!processor) return false;
        if (!isActive.load(std::memory_order_acquire)) return false;

        ProcessData data = {};
        data.numSamples = buffer.numSamples;

        // Update the pre-allocated bus buffer arrays (no heap allocation).
        if (numInputBuses > 0) {
            inBusBuffers[0].numChannels      = buffer.numChannels;
            inBusBuffers[0].channelBuffers32 = buffer.inputs;
            inBusBuffers[0].silenceFlags     = 0;
        }
        // Secondary input buses (sidechain, aux) are deactivated. Provide silent,
        // valid pointers as a safety guard against buggy plugins.
        for (int i = 1; i < numInputBuses; ++i) {
            inBusBuffers[i].numChannels      = 2;
            inBusBuffers[i].channelBuffers32 = silentChannelPtrs;
            inBusBuffers[i].silenceFlags     = 0xFFFFFFFFFFFFFFFFull;
        }

        if (numOutputBuses > 0) {
            outBusBuffers[0].numChannels      = buffer.numChannels;
            outBusBuffers[0].channelBuffers32 = buffer.outputs;
            outBusBuffers[0].silenceFlags     = 0;
        }
        for (int i = 1; i < numOutputBuses; ++i) {
            outBusBuffers[i].numChannels      = 2;
            outBusBuffers[i].channelBuffers32 = silentChannelPtrs;
            outBusBuffers[i].silenceFlags     = 0;
        }

        data.inputs     = numInputBuses  > 0 ? inBusBuffers.data()  : nullptr;
        data.outputs    = numOutputBuses > 0 ? outBusBuffers.data() : nullptr;
        data.numInputs  = numInputBuses;
        data.numOutputs = numOutputBuses;

        data.processMode        = kRealtime;
        data.symbolicSampleSize = kSample32;

        double localSamplePos = currentSamplePos.load(std::memory_order_relaxed);
        double localBpm       = currentBpm.load(std::memory_order_relaxed);
        bool   localPlaying   = isTransportPlaying.load(std::memory_order_relaxed);

        // Always fill all ProcessContext fields regardless of IProcessContextRequirements.
        // JUCE-based plugins internally access fields they did not declare, reading
        // uninitialised values if we use selective filling – which causes distortion.
        ProcessContext ctx = {};
        ctx.state = ProcessContext::kTempoValid
                  | ProcessContext::kTimeSigValid
                  | ProcessContext::kProjectTimeMusicValid
                  | ProcessContext::kBarPositionValid;
        if (localPlaying) ctx.state |= ProcessContext::kPlaying;
        ctx.sampleRate         = sampleRate;
        ctx.projectTimeSamples = static_cast<int64>(localSamplePos);
        ctx.tempo              = localBpm;
        ctx.timeSigNumerator   = 4;
        ctx.timeSigDenominator = 4;
        ctx.projectTimeMusic   = (localSamplePos / sampleRate) * (localBpm / 60.0);
        ctx.barPositionMusic   = 0.0;
        data.processContext    = &ctx;

        // Drain parameter changes from the SPSC queue.
        // No mutex needed: this is the sole consumer (audio thread).
        // clearQueue() resets used-count without freeing memory.
        inputParamChanges.clearQueue();
        {
            ParamChange changes[PARAM_QUEUE_CAPACITY];
            size_t numChanges = paramQueue.popAll(changes, PARAM_QUEUE_CAPACITY);
            for (size_t ci = 0; ci < numChanges; ++ci) {
                int32 idx = 0;
                IParamValueQueue* q = inputParamChanges.addParameterData(
                    static_cast<ParamID>(changes[ci].id), idx);
                if (q) {
                    int32 pointIndex = 0;
                    q->addPoint(0, changes[ci].value, pointIndex);
                }
            }
        }
        data.inputParameterChanges = &inputParamChanges;

        outputParamChanges.clearQueue();
        data.outputParameterChanges = &outputParamChanges;

        if (processor->process(data) != kResultOk) {
            std::cerr << "Error during audio processing" << std::endl;
            return false;
        }

        currentSamplePos.store(localSamplePos + data.numSamples, std::memory_order_relaxed);

        // Output parameter changes (plugin → host automation read-back) are
        // intentionally not fed back into the input queue. Doing so creates an
        // infinite feedback loop that floods the main thread and freezes the UI.

        return true;
    }

    bool processMidi(const std::vector<MidiEvent>& events) {
        if (!processor) return false;

        EventList eventList;

        for (const auto& event : events) {
            Event e = {};
            e.busIndex      = 0;
            e.sampleOffset  = event.sampleOffset;
            e.ppqPosition   = 0.0;

            if ((event.status & 0xF0) == 0x90) {
                e.type                = Event::kNoteOnEvent;
                e.noteOn.channel      = event.status & 0x0F;
                e.noteOn.pitch        = event.data1;
                e.noteOn.velocity     = event.data2 / 127.0f;
                e.noteOn.length       = 0;
                e.noteOn.tuning       = 0.0f;
                e.noteOn.noteId       = -1;
            }
            else if ((event.status & 0xF0) == 0x80) {
                e.type                 = Event::kNoteOffEvent;
                e.noteOff.channel      = event.status & 0x0F;
                e.noteOff.pitch        = event.data1;
                e.noteOff.velocity     = event.data2 / 127.0f;
                e.noteOff.tuning       = 0.0f;
                e.noteOff.noteId       = -1;
            }
            else if ((event.status & 0xF0) == 0xB0) {
                e.type                       = Event::kLegacyMIDICCOutEvent;
                e.midiCCOut.channel          = static_cast<Steinberg::int8>(event.status & 0x0F);
                e.midiCCOut.controlNumber    = static_cast<Steinberg::uint8>(event.data1);
                // LegacyMIDICCOutEvent::value is int8 in [0, 127] – do NOT normalise.
                e.midiCCOut.value            = static_cast<Steinberg::int8>(event.data2);
            }

            eventList.addEvent(e);
        }

        // Zero-initialise so processContext, inputParameterChanges and other pointer
        // fields are null rather than garbage. A plugin that dereferences a null
        // processContext during a MIDI-only call is violating the VST3 spec, but
        // a plugin that dereferences an uninitialised pointer will crash hard.
        ProcessData data = {};
        data.processMode        = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples         = 0;
        data.numInputs          = 0;
        data.numOutputs         = 0;
        data.inputEvents        = &eventList;

        if (processor->process(data) != kResultOk) {
            std::cerr << "Error during MIDI processing" << std::endl;
            return false;
        }

        return true;
    }

    bool isInstrument() {
        if (!component) return false;
        return component->getBusCount(kEvent, kInput) > 0 &&
               component->getBusCount(kAudio, kOutput) > 0;
    }

    bool isEffect() {
        if (!component) return false;
        return component->getBusCount(kAudio, kInput) > 0 &&
               component->getBusCount(kAudio, kOutput) > 0;
    }

    bool isMidiOnly() {
        if (!component) return false;
        return component->getBusCount(kEvent, kInput) > 0 &&
               component->getBusCount(kAudio, kOutput) == 0;
    }

    std::string getName() {
        if (!component || !factory) return "";
        PClassInfo classInfo;
        for (int32 i = 0; i < factory->countClasses(); i++) {
            if (factory->getClassInfo(i, &classInfo) == kResultOk &&
                strcmp(classInfo.category, kVstAudioEffectClass) == 0)
                return classInfo.name;
        }
        return module ? module->getName() : "";
    }

    std::string getVendor() {
        if (!factory) return "";
        PFactoryInfo factoryInfo;
        if (factory->getFactoryInfo(&factoryInfo) == kResultOk)
            return factoryInfo.vendor;
        return "";
    }

    std::string getVersion() {
        if (!factory) return "";
        IPluginFactory2* factory2 = nullptr;
        if (factory->queryInterface(IPluginFactory2::iid, (void**)&factory2) == kResultOk && factory2) {
            PClassInfo2 classInfo2;
            for (int32 i = 0; i < factory2->countClasses(); i++) {
                if (factory2->getClassInfo2(i, &classInfo2) == kResultOk &&
                    strcmp(classInfo2.category, kVstAudioEffectClass) == 0) {
                    factory2->release();
                    return classInfo2.version;
                }
            }
            factory2->release();
        }
        return "1.0.0";
    }

    std::string getPluginInfo() {
        std::string info;
        info += "Name: " + getName() + "\n";
        info += "Vendor: " + getVendor() + "\n";
        if (component) {
            int numInputs  = component->getBusCount(kAudio, kInput);
            int numOutputs = component->getBusCount(kAudio, kOutput);
            info += "Audio buses: " + std::to_string(numInputs) + " input(s), " +
                    std::to_string(numOutputs) + " output(s)\n";
            info += std::string("MIDI input: ") +
                    (component->getBusCount(kEvent, kInput) > 0 ? "Yes" : "No") + "\n";
        }
        return info;
    }

    void processIdle() {
#ifdef __linux__
        if (plugFrame) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64 currentTimeMs = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            plugFrame->processEvents(currentTimeMs);
        }
#endif
#ifdef __APPLE__
        OwnVst3_ProcessIdleMacOS();
#endif
    }

    bool isEditorOpen() const { return view != nullptr; }

    int getActualInputChannels()  const { return actualInputChannels; }
    int getActualOutputChannels() const { return actualOutputChannels; }

    void setTempo(double bpm)           { currentBpm.store(bpm, std::memory_order_relaxed); }
    void setTransportState(bool playing){ isTransportPlaying.store(playing, std::memory_order_relaxed); }
    void resetTransportPosition()       { currentSamplePos.store(0.0, std::memory_order_relaxed); }

private:
    bool findComponentAndControllerIDs(IPluginFactory* factory, FUID& componentID,
                                        FUID& controllerID, std::string& componentName) {
        bool foundComponent = false;
        PClassInfo classInfo;

        for (int32 i = 0; i < factory->countClasses(); i++) {
            if (factory->getClassInfo(i, &classInfo) != kResultOk) continue;
            if (strcmp(classInfo.category, kVstAudioEffectClass) != 0) continue;

            componentID   = FUID(classInfo.cid);
            componentName = classInfo.name;
            foundComponent = true;

            // Create a temporary component instance solely to retrieve the
            // controller class ID. Call initialize() before getControllerClassId()
            // and terminate() afterward to comply with the VST3 spec – calling
            // terminate() without initialize() is undefined behaviour and crashes
            // some plugins.
            FUnknownPtr<IComponent> tempComponent;
            if (factory->createInstance(componentID, IComponent::iid, (void**)&tempComponent) == kResultOk
                && tempComponent) {
                if (tempComponent->initialize(&hostApp) == kResultOk) {
                    TUID _ctrID;
                    if (tempComponent->getControllerClassId(_ctrID) == kResultOk) {
                        FUID ctrID(_ctrID);
                        if (ctrID.isValid())
                            controllerID = ctrID;
                    }
                    tempComponent->terminate();
                }
            }
            break;
        }
        return foundComponent;
    }

    void connectComponentAndController() {
        if (!component || !controller) return;
        FUnknownPtr<IConnectionPoint> componentCP(component);
        FUnknownPtr<IConnectionPoint> controllerCP(controller);
        if (componentCP && controllerCP) {
            componentCP->connect(controllerCP);
            controllerCP->connect(componentCP);
        }
    }

    // hostApp MUST be first: constructed before and destructed after all VST3 objects.
    Steinberg::Vst::HostApplication hostApp;

    VST3::Hosting::Module::Ptr module   = nullptr;
    IPluginFactory*  factory            = nullptr;
    IComponent*      component          = nullptr;
    IEditController* controller         = nullptr;
    IAudioProcessor* processor          = nullptr;
    IPlugView*       view               = nullptr;
    PlugFrame*       plugFrame          = nullptr;

    std::vector<Vst3Parameter> parameters;
    double sampleRate;
    int    blockSize;

    OwnComponentHandler* componentHandler = nullptr;

    // Atomic flag – written by UI thread (initialize, loadPlugin) and read by
    // audio thread (processAudio). Must be atomic to avoid undefined behaviour.
    std::atomic<bool> isActive{false};

    int numInputBuses        = 1;
    int numOutputBuses       = 1;
    int actualInputChannels  = 2;
    int actualOutputChannels = 2;

    Steinberg::uint32 processContextRequirements = 0;

    // Pre-allocated bus buffer arrays – sized in setupProcessing(), reused each callback.
    std::vector<AudioBusBuffers> inBusBuffers;
    std::vector<AudioBusBuffers> outBusBuffers;

    // Silent stereo buffer for secondary (sidechain/aux) buses.
    // Provides valid non-null pointers for deactivated buses as a safety guard.
    std::vector<float> silentBuffer;
    float* silentChannelPtrs[2] = {nullptr, nullptr};

    // Pre-allocated parameter change queues – clearQueue() resets without freeing.
    ParameterChanges inputParamChanges;
    ParameterChanges outputParamChanges;

    // Lock-free SPSC queue: UI thread pushes, audio thread pops.
    // No hash collision possible (unlike the previous direct-mapped cache).
    ParamChangeSPSC paramQueue;

    // Mutex protecting lastSetValues – never held on the audio thread.
    std::mutex paramMutex;

    // Last value set by the host for each parameter ID.
    // getParameter() reads this instead of controller->getParamNormalized() to
    // avoid acquiring the plugin's internal mutex from the UI thread while the
    // audio thread may be inside process() holding the same mutex.
    std::unordered_map<uint32_t, double> lastSetValues;

    // Transport state – written by UI thread, read by audio thread.
    std::atomic<double> currentBpm{120.0};
    std::atomic<double> currentSamplePos{0.0};
    std::atomic<bool>   isTransportPlaying{false};

#ifdef __APPLE__
    CFRunLoopTimerRef idleTimer     = nullptr;
    void*             editorNSViewHandle = nullptr;
#endif

#ifdef _WIN32
    std::string tchar_to_utf8(const Steinberg::Vst::TChar* tcharStr) {
        std::string result;
        if (!tcharStr) return result;
        std::wstring wstr(reinterpret_cast<const wchar_t*>(tcharStr));
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
        if (size_needed > 0) {
            result.resize(size_needed);
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size_needed, NULL, NULL);
        }
        return result;
    }
#else
    std::string tchar_to_utf8(const Steinberg::Vst::TChar* tcharStr) {
        std::string result;
        if (!tcharStr) return result;
        std::wstring wstr(reinterpret_cast<const wchar_t*>(tcharStr));
        try {
            std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
            result = converter.to_bytes(wstr);
        } catch (...) {
            for (wchar_t ch : wstr)
                result.push_back(ch <= 127 ? static_cast<char>(ch) : '?');
        }
        return result;
    }
#endif
};


Vst3Plugin::Vst3Plugin() : impl(new Vst3PluginImpl()) {}
Vst3Plugin::~Vst3Plugin() {}

bool        Vst3Plugin::loadPlugin(const std::string& p)         { return impl->loadPlugin(p); }
bool        Vst3Plugin::createEditor(void* h)                    { return impl->createEditor(h); }
void        Vst3Plugin::closeEditor()                            { impl->closeEditor(); }
void        Vst3Plugin::resizeEditor(int w, int h)               { impl->resizeEditor(w, h); }
bool        Vst3Plugin::getEditorSize(int& w, int& h)            { return impl->getEditorSize(w, h); }
std::vector<Vst3Parameter> Vst3Plugin::getParameters()           { return impl->getParameters(); }
int         Vst3Plugin::getParameterCount()                      { return impl->getParameterCount(); }
bool        Vst3Plugin::getParameterInfo(int i, Vst3Parameter& p){ return impl->getParameterInfo(i, p); }
bool        Vst3Plugin::setParameter(int id, double v)           { return impl->setParameter(id, v); }
double      Vst3Plugin::getParameter(int id)                     { return impl->getParameter(id); }
bool        Vst3Plugin::initialize(double sr, int bs)            { return impl->initialize(sr, bs); }
bool        Vst3Plugin::processAudio(AudioBuffer& b)             { return impl->processAudio(b); }
bool        Vst3Plugin::processMidi(const std::vector<MidiEvent>& e) { return impl->processMidi(e); }
bool        Vst3Plugin::isInstrument()                           { return impl->isInstrument(); }
bool        Vst3Plugin::isEffect()                               { return impl->isEffect(); }
bool        Vst3Plugin::isMidiOnly()                             { return impl->isMidiOnly(); }
std::string Vst3Plugin::getName()                                { return impl->getName(); }
std::string Vst3Plugin::getVendor()                              { return impl->getVendor(); }
std::string Vst3Plugin::getVersion()                             { return impl->getVersion(); }
std::string Vst3Plugin::getPluginInfo()                          { return impl->getPluginInfo(); }
void        Vst3Plugin::processIdle()                            { impl->processIdle(); }
bool        Vst3Plugin::isEditorOpen()                           { return impl->isEditorOpen(); }
int         Vst3Plugin::getActualInputChannels()                 { return impl->getActualInputChannels(); }
int         Vst3Plugin::getActualOutputChannels()                { return impl->getActualOutputChannels(); }
void        Vst3Plugin::setTempo(double bpm)                     { impl->setTempo(bpm); }
void        Vst3Plugin::setTransportState(bool p)                { impl->setTransportState(p); }
void        Vst3Plugin::resetTransportPosition()                 { impl->resetTransportPosition(); }

} // namespace OwnVst3Host

// ownvst3_wrapper.h
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
    #ifdef OWN_VST3_WRAPPER_EXPORTS
        #define OWN_VST3_WRAPPER_API __declspec(dllexport)
    #else
        #define OWN_VST3_WRAPPER_API __declspec(dllimport)
    #endif
#else
    #define OWN_VST3_WRAPPER_API __attribute__((visibility("default")))
#endif

// Opaque handle to the plugin
typedef void* VST3PluginHandle;

// Parameter structure
typedef struct {
    int id;
    const char* name;
    double minValue;
    double maxValue;
    double defaultValue;
    double currentValue;
} VST3ParameterC;

// Audio buffer structure
typedef struct {
    float** inputs;
    float** outputs;
    int numChannels;
    int numSamples;
} AudioBufferC;

// MIDI event structure
typedef struct {
    int status;
    int data1;
    int data2;
    int sampleOffset;
} MidiEventC;

// Create a new VST3 plugin instance
OWN_VST3_WRAPPER_API VST3PluginHandle VST3Plugin_Create();

// Destroy a VST3 plugin instance
OWN_VST3_WRAPPER_API void VST3Plugin_Destroy(VST3PluginHandle handle);

// Load a VST3 plugin
OWN_VST3_WRAPPER_API bool VST3Plugin_LoadPlugin(VST3PluginHandle handle, const char* pluginPath);

// Create plugin editor
OWN_VST3_WRAPPER_API bool VST3Plugin_CreateEditor(VST3PluginHandle handle, void* windowHandle);

// Close plugin editor
OWN_VST3_WRAPPER_API void VST3Plugin_CloseEditor(VST3PluginHandle handle);

// Resize plugin editor
OWN_VST3_WRAPPER_API void VST3Plugin_ResizeEditor(VST3PluginHandle handle, int width, int height);

// Get plugin editor size
OWN_VST3_WRAPPER_API bool VST3Plugin_GetEditorSize(VST3PluginHandle handle, int* width, int* height);

// Get parameter count
OWN_VST3_WRAPPER_API int VST3Plugin_GetParameterCount(VST3PluginHandle handle);

// Get parameter by index
OWN_VST3_WRAPPER_API bool VST3Plugin_GetParameterAt(VST3PluginHandle handle, int index, VST3ParameterC* parameter);

// Set parameter value
OWN_VST3_WRAPPER_API bool VST3Plugin_SetParameter(VST3PluginHandle handle, int paramId, double value);

// Get parameter value
OWN_VST3_WRAPPER_API double VST3Plugin_GetParameter(VST3PluginHandle handle, int paramId);

// Initialize plugin
OWN_VST3_WRAPPER_API bool VST3Plugin_Initialize(VST3PluginHandle handle, double sampleRate, int maxBlockSize);

// Process audio
OWN_VST3_WRAPPER_API bool VST3Plugin_ProcessAudio(VST3PluginHandle handle, AudioBufferC* buffer);

// Process MIDI events
OWN_VST3_WRAPPER_API bool VST3Plugin_ProcessMidi(VST3PluginHandle handle, const MidiEventC* events, int eventCount);

// Check if plugin is an instrument
OWN_VST3_WRAPPER_API bool VST3Plugin_IsInstrument(VST3PluginHandle handle);

// Check if plugin is an effect
OWN_VST3_WRAPPER_API bool VST3Plugin_IsEffect(VST3PluginHandle handle);

// Plugin information
OWN_VST3_WRAPPER_API const char* VST3Plugin_GetName(VST3PluginHandle handle);
OWN_VST3_WRAPPER_API const char* VST3Plugin_GetVendor(VST3PluginHandle handle);
// OWN_VST3_WRAPPER_API const char* VST3Plugin_GetVersion(VST3PluginHandle handle);
OWN_VST3_WRAPPER_API const char* VST3Plugin_GetPluginInfo(VST3PluginHandle handle);

// Helper function to clear string cache
OWN_VST3_WRAPPER_API void VST3Plugin_ClearStringCache();

#ifdef __cplusplus
}
#endif
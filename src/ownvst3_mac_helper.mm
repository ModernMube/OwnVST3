// ownvst3_mac_helper.mm
// Objective-C++ helper for macOS-specific editor window management.
// Handles popup/dropdown cleanup, main-thread event processing, and window configuration.

#import <Cocoa/Cocoa.h>
#include <CoreFoundation/CoreFoundation.h>

// ---------------------------------------------------------------------------
// OwnVst3_CloseEditorPopups
// Dismisses all open menus and closes orphaned popup windows when the editor is closed.
// ---------------------------------------------------------------------------
extern "C" void OwnVst3_CloseChildWindows(void* nsViewHandle) {
    if (!nsViewHandle) return;

    @autoreleasepool {
        // 1. Cancel any open menu tracking (NSMenu-based dropdowns).
        //    This is a class method that dismisses ALL currently tracking menus.
        //    It must be called BEFORE view->removed() to ensure the menu's
        //    modal tracking loop exits cleanly.
        [NSMenu cancelTrackingWithoutAnimation];

        NSView* view = (__bridge NSView*)nsViewHandle;
        NSWindow* editorWindow = [view window];
        if (!editorWindow) return;

        // 2. Close registered child windows (addChildWindow: relationship)
        NSArray<NSWindow*>* childWindows = [[editorWindow childWindows] copy];
        for (NSWindow* child in childWindows) {
            [editorWindow removeChildWindow:child];
            [child orderOut:nil];
        }

        // 3. Close orphaned popup-level windows.
        //    VST plugins may create popup/dropdown windows that are NOT registered
        //    as child windows. These appear at NSPopUpMenuWindowLevel or higher.
        //    We close any visible popup-level windows to prevent orphaned dropdowns.
        for (NSWindow* window in [[NSApplication sharedApplication] windows]) {
            if (window == editorWindow) continue;
            if (![window isVisible]) continue;

            NSWindowLevel level = [window level];
            if (level >= NSPopUpMenuWindowLevel) {
                [window orderOut:nil];
            }
        }
    }
}

// ---------------------------------------------------------------------------
// OwnVst3_ProcessIdleMacOS
// Processes pending run loop events on the main thread.
// Uses the CURRENT run loop mode so it works during modal tracking loops
// (dropdown menus, popup menus, etc.).
// Safe to call from any thread - silently returns if not on the main thread.
// ---------------------------------------------------------------------------
extern "C" void OwnVst3_ProcessIdleMacOS(void) {
    // Only process events on the main thread.
    // When called from a background thread (e.g., C# System.Threading.Timer),
    // we must not try to pump the main run loop as this causes threading issues.
    if (![NSThread isMainThread]) return;

    @autoreleasepool {
        // Use the CURRENT run loop mode instead of kCFRunLoopDefaultMode.
        // During modal tracking (dropdown menus), the mode is NSEventTrackingRunLoopMode.
        // Using kCFRunLoopDefaultMode would miss events in the tracking mode.
        CFRunLoopRef mainLoop = CFRunLoopGetMain();
        CFStringRef currentMode = CFRunLoopCopyCurrentMode(mainLoop);
        if (currentMode) {
            CFRunLoopRunInMode(currentMode, 0, true);
            CFRelease(currentMode);
        }
    }
}

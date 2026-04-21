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
        //    cancelTrackingWithoutAnimation is an instance method, so we
        //    find the main menu and any contextual menus to cancel them.
        //    This must be called BEFORE view->removed() to ensure the menu's
        //    modal tracking loop exits cleanly.
        NSMenu* mainMenu = [[NSApplication sharedApplication] mainMenu];
        if (mainMenu) {
            [mainMenu cancelTrackingWithoutAnimation];
        }

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
//
// IMPORTANT – DO NOT call CFRunLoopRunInMode() from here.
//
// This function is called from two contexts:
//   1. From our CFRunLoopTimer callback – we are already INSIDE the run loop.
//      Calling CFRunLoopRunInMode() would create a nested run loop, which
//      causes CATransaction flush observers to fire inside JUCE's modal event
//      loop. This corrupts CoreGraphics / AppKit internal pointer state and
//      results in a PAC-authentication crash in CGGlyphBuilderLockBitmaps.
//   2. From the C# "VST Editor Idle Thread" – that thread is NOT the main
//      thread, so the isMainThread guard below exits immediately anyway.
//
// The Avalonia run loop on the main thread already pumps all events the
// VST3 plugin needs. The CFRunLoopTimer firing IS the periodic idle tick;
// re-entering the loop from within the callback is both unnecessary and
// dangerous.
// ---------------------------------------------------------------------------
extern "C" void OwnVst3_ProcessIdleMacOS(void) {
    // Guard: only safe to touch AppKit/CoreFoundation objects on the main thread.
    if (![NSThread isMainThread]) return;

    // Intentionally empty on macOS:
    // Avalonia's run loop is already running and will process all events.
    // The timer callback that called us IS the idle tick. No nested loop needed.
}


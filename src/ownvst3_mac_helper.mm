// ownvst3_mac_helper.mm
// Objective-C++ helper for macOS-specific editor window management.
// Handles child window cleanup (popups, dropdowns) when closing the VST editor.

#import <Cocoa/Cocoa.h>

extern "C" void OwnVst3_CloseChildWindows(void* nsViewHandle) {
    if (!nsViewHandle) return;

    @autoreleasepool {
        NSView* view = (__bridge NSView*)nsViewHandle;
        NSWindow* window = [view window];
        if (!window) return;

        // Close all child windows (popups, dropdowns, tooltips)
        // These are created by VST plugins for menus and may become orphaned
        // if the editor is closed while a dropdown is open.
        NSArray<NSWindow*>* childWindows = [[window childWindows] copy];
        for (NSWindow* childWindow in childWindows) {
            [window removeChildWindow:childWindow];
            [childWindow orderOut:nil];
        }
    }
}

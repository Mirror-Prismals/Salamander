#import <Cocoa/Cocoa.h>

struct Vst3UiWindow {
    NSWindow* window;
    NSView* contentView;
};

static void Vst3UI_EnsureApp() {
    NSApplication* app = [NSApplication sharedApplication];
    if (app && [app activationPolicy] == NSApplicationActivationPolicyProhibited) {
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    }
}

Vst3UiWindow* Vst3UI_CreateWindow(const char* title, int width, int height) {
    Vst3UI_EnsureApp();
    NSString* nsTitle = title ? [NSString stringWithUTF8String:title] : @"VST3";
    NSRect rect = NSMakeRect(0, 0, width > 0 ? width : 640, height > 0 ? height : 480);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable;
    NSWindow* window = [[NSWindow alloc] initWithContentRect:rect
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    [window setTitle:nsTitle];
    [window center];
    NSView* content = [[NSView alloc] initWithFrame:rect];
    [window setContentView:content];
    auto* handle = new Vst3UiWindow{window, content};
    return handle;
}

void* Vst3UI_GetContentView(Vst3UiWindow* window) {
    if (!window || !window->contentView) return nullptr;
    return (__bridge void*)window->contentView;
}

void Vst3UI_ShowWindow(Vst3UiWindow* window) {
    if (!window || !window->window) return;
    [window->window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

void Vst3UI_HideWindow(Vst3UiWindow* window) {
    if (!window || !window->window) return;
    [window->window orderOut:nil];
}

void Vst3UI_SetWindowSize(Vst3UiWindow* window, int width, int height) {
    if (!window || !window->window) return;
    NSSize size = NSMakeSize(width > 0 ? width : 640, height > 0 ? height : 480);
    [window->window setContentSize:size];
}

void Vst3UI_CloseWindow(Vst3UiWindow* window) {
    if (!window) return;
    if (window->window) {
        [window->window orderOut:nil];
        [window->window close];
    }
    delete window;
}

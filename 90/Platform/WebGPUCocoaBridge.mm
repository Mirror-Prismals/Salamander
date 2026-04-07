#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#include "Platform/PlatformTypes.h"

namespace {
    CAMetalLayer* getOrCreateMetalLayer(PlatformWindowHandle window) {
        if (!window) return nil;
        NSWindow* nsWindow = glfwGetCocoaWindow(window);
        if (!nsWindow) return nil;

        NSView* contentView = [nsWindow contentView];
        if (!contentView) return nil;

        if (![contentView wantsLayer]) {
            [contentView setWantsLayer:YES];
        }

        CALayer* existingLayer = [contentView layer];
        CAMetalLayer* metalLayer = nil;
        if ([existingLayer isKindOfClass:[CAMetalLayer class]]) {
            metalLayer = (CAMetalLayer*)existingLayer;
        } else {
            metalLayer = [CAMetalLayer layer];
            if (!metalLayer) return nil;
            [contentView setLayer:metalLayer];
        }

        NSScreen* screen = [nsWindow screen];
        if (!screen) screen = [NSScreen mainScreen];
        CGFloat scale = 1.0;
        if (screen && [screen respondsToSelector:@selector(backingScaleFactor)]) {
            scale = [screen backingScaleFactor];
        }
        if (scale <= 0.0) scale = 1.0;
        metalLayer.contentsScale = scale;
        metalLayer.frame = contentView.bounds;
        metalLayer.drawableSize = CGSizeMake(contentView.bounds.size.width * scale,
                                             contentView.bounds.size.height * scale);
        return metalLayer;
    }
}

extern "C" void* SalamanderPlatformGetCocoaWindow(PlatformWindowHandle window) {
    if (!window) return nullptr;
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    return (__bridge void*)nsWindow;
}

extern "C" void* SalamanderPlatformGetOrCreateMetalLayer(PlatformWindowHandle window) {
    CAMetalLayer* layer = getOrCreateMetalLayer(window);
    return (__bridge void*)layer;
}

extern "C" void SalamanderPlatformResizeMetalLayerDrawable(PlatformWindowHandle window,
                                                           int width,
                                                           int height) {
    CAMetalLayer* layer = getOrCreateMetalLayer(window);
    if (!layer) return;

    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    NSView* contentView = nsWindow ? [nsWindow contentView] : nil;
    if (!contentView) return;

    NSScreen* screen = [nsWindow screen];
    if (!screen) screen = [NSScreen mainScreen];
    CGFloat scale = 1.0;
    if (screen && [screen respondsToSelector:@selector(backingScaleFactor)]) {
        scale = [screen backingScaleFactor];
    }
    if (scale <= 0.0) scale = 1.0;
    layer.contentsScale = scale;
    layer.frame = contentView.bounds;

    if (width > 0 && height > 0) {
        layer.drawableSize = CGSizeMake((CGFloat)width, (CGFloat)height);
    } else {
        layer.drawableSize = CGSizeMake(contentView.bounds.size.width * scale,
                                        contentView.bounds.size.height * scale);
    }
}

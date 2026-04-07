#pragma once

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include "Host/PlatformInput.h"

#ifdef __APPLE__
extern "C" void* SalamanderPlatformGetCocoaWindow(PlatformWindowHandle window);
extern "C" void* SalamanderPlatformGetOrCreateMetalLayer(PlatformWindowHandle window);
extern "C" void SalamanderPlatformResizeMetalLayerDrawable(PlatformWindowHandle window, int width, int height);
#endif

namespace PlatformInput {
    namespace {
        int toGlfwKey(Key key) {
            switch (key) {
                case Key::Num0: return GLFW_KEY_0;
                case Key::Num1: return GLFW_KEY_1;
                case Key::Num2: return GLFW_KEY_2;
                case Key::Num3: return GLFW_KEY_3;
                case Key::Num4: return GLFW_KEY_4;
                case Key::Num5: return GLFW_KEY_5;
                case Key::Num6: return GLFW_KEY_6;
                case Key::Num7: return GLFW_KEY_7;
                case Key::Num8: return GLFW_KEY_8;
                case Key::Num9: return GLFW_KEY_9;
                case Key::W: return GLFW_KEY_W;
                case Key::A: return GLFW_KEY_A;
                case Key::C: return GLFW_KEY_C;
                case Key::H: return GLFW_KEY_H;
                case Key::I: return GLFW_KEY_I;
                case Key::J: return GLFW_KEY_J;
                case Key::K: return GLFW_KEY_K;
                case Key::L: return GLFW_KEY_L;
                case Key::M: return GLFW_KEY_M;
                case Key::N: return GLFW_KEY_N;
                case Key::O: return GLFW_KEY_O;
                case Key::P: return GLFW_KEY_P;
                case Key::Q: return GLFW_KEY_Q;
                case Key::T: return GLFW_KEY_T;
                case Key::U: return GLFW_KEY_U;
                case Key::X: return GLFW_KEY_X;
                case Key::Y: return GLFW_KEY_Y;
                case Key::Z: return GLFW_KEY_Z;
                case Key::S: return GLFW_KEY_S;
                case Key::D: return GLFW_KEY_D;
                case Key::F: return GLFW_KEY_F;
                case Key::E: return GLFW_KEY_E;
                case Key::G: return GLFW_KEY_G;
                case Key::LeftAlt: return GLFW_KEY_LEFT_ALT;
                case Key::RightAlt: return GLFW_KEY_RIGHT_ALT;
                case Key::Space: return GLFW_KEY_SPACE;
                case Key::Tab: return GLFW_KEY_TAB;
                case Key::Enter: return GLFW_KEY_ENTER;
                case Key::KpEnter: return GLFW_KEY_KP_ENTER;
                case Key::DeleteKey: return GLFW_KEY_DELETE;
                case Key::Backspace: return GLFW_KEY_BACKSPACE;
                case Key::LeftBracket: return GLFW_KEY_LEFT_BRACKET;
                case Key::RightBracket: return GLFW_KEY_RIGHT_BRACKET;
                case Key::ArrowUp: return GLFW_KEY_UP;
                case Key::ArrowDown: return GLFW_KEY_DOWN;
                case Key::ArrowLeft: return GLFW_KEY_LEFT;
                case Key::ArrowRight: return GLFW_KEY_RIGHT;
                case Key::Key6: return GLFW_KEY_6;
                case Key::Key7: return GLFW_KEY_7;
                case Key::Key8: return GLFW_KEY_8;
                case Key::Kp6: return GLFW_KEY_KP_6;
                case Key::Kp7: return GLFW_KEY_KP_7;
                case Key::Kp8: return GLFW_KEY_KP_8;
                case Key::KpAdd: return GLFW_KEY_KP_ADD;
                case Key::KpSubtract: return GLFW_KEY_KP_SUBTRACT;
                case Key::Equal: return GLFW_KEY_EQUAL;
                case Key::Minus: return GLFW_KEY_MINUS;
                case Key::Period: return GLFW_KEY_PERIOD;
                case Key::LeftSuper: return GLFW_KEY_LEFT_SUPER;
                case Key::RightSuper: return GLFW_KEY_RIGHT_SUPER;
                case Key::LeftControl: return GLFW_KEY_LEFT_CONTROL;
                case Key::RightControl: return GLFW_KEY_RIGHT_CONTROL;
                case Key::LeftShift: return GLFW_KEY_LEFT_SHIFT;
                case Key::RightShift: return GLFW_KEY_RIGHT_SHIFT;
                case Key::R: return GLFW_KEY_R;
                case Key::B: return GLFW_KEY_B;
                case Key::V: return GLFW_KEY_V;
                case Key::Escape: return GLFW_KEY_ESCAPE;
            }
            return GLFW_KEY_UNKNOWN;
        }

        int toGlfwMouseButton(MouseButton button) {
            switch (button) {
                case MouseButton::Left: return GLFW_MOUSE_BUTTON_LEFT;
                case MouseButton::Right: return GLFW_MOUSE_BUTTON_RIGHT;
                case MouseButton::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
            }
            return GLFW_MOUSE_BUTTON_LEFT;
        }

        int toGlfwCursorMode(CursorMode mode) {
            switch (mode) {
                case CursorMode::Normal: return GLFW_CURSOR_NORMAL;
                case CursorMode::Disabled: return GLFW_CURSOR_DISABLED;
            }
            return GLFW_CURSOR_NORMAL;
        }

        int toGlfwStandardCursor(StandardCursor cursor) {
            switch (cursor) {
                case StandardCursor::Arrow: return GLFW_ARROW_CURSOR;
                case StandardCursor::HorizontalResize: return GLFW_HRESIZE_CURSOR;
            }
            return GLFW_ARROW_CURSOR;
        }
    }

    bool InitializeWindowing() {
        return glfwInit() == GLFW_TRUE;
    }

    void TerminateWindowing() {
        glfwTerminate();
    }

    void SetNoApiContextHint() {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    PlatformWindowHandle CreateWindow(int width, int height, const char* title) {
        return glfwCreateWindow(width, height, title, nullptr, nullptr);
    }

    void DestroyWindow(PlatformWindowHandle window) {
        if (!window) return;
        glfwDestroyWindow(window);
    }

    void MakeContextCurrent(PlatformWindowHandle window) {
        glfwMakeContextCurrent(window);
    }

    void SwapBuffers(PlatformWindowHandle window) {
        if (!window) return;
        glfwSwapBuffers(window);
    }

    void* GetProcAddress(const char* procName) {
        if (!procName) return nullptr;
        return reinterpret_cast<void*>(glfwGetProcAddress(procName));
    }

    void* GetNativeWindowHandle(PlatformWindowHandle window) {
#ifdef __APPLE__
        return SalamanderPlatformGetCocoaWindow(window);
#else
        (void)window;
        return nullptr;
#endif
    }

    void* GetOrCreateMetalLayerHandle(PlatformWindowHandle window) {
#ifdef __APPLE__
        return SalamanderPlatformGetOrCreateMetalLayer(window);
#else
        (void)window;
        return nullptr;
#endif
    }

    void ResizeMetalLayerDrawable(PlatformWindowHandle window, int width, int height) {
#ifdef __APPLE__
        SalamanderPlatformResizeMetalLayerDrawable(window, width, height);
#else
        (void)window;
        (void)width;
        (void)height;
#endif
    }

    double GetTimeSeconds() {
        return glfwGetTime();
    }

    bool IsKeyDown(PlatformWindowHandle window, Key key) {
        return window && glfwGetKey(window, toGlfwKey(key)) == GLFW_PRESS;
    }

    bool IsMouseButtonDown(PlatformWindowHandle window, MouseButton button) {
        return window && glfwGetMouseButton(window, toGlfwMouseButton(button)) == GLFW_PRESS;
    }

    void GetCursorPosition(PlatformWindowHandle window, double& x, double& y) {
        x = 0.0;
        y = 0.0;
        if (!window) return;
        glfwGetCursorPos(window, &x, &y);
    }

    void GetWindowSize(PlatformWindowHandle window, int& width, int& height) {
        width = 0;
        height = 0;
        if (!window) return;
        glfwGetWindowSize(window, &width, &height);
    }

    void GetFramebufferSize(PlatformWindowHandle window, int& width, int& height) {
        width = 0;
        height = 0;
        if (!window) return;
        glfwGetFramebufferSize(window, &width, &height);
    }

    void SetWindowUserPointer(PlatformWindowHandle window, void* ptr) {
        if (!window) return;
        glfwSetWindowUserPointer(window, ptr);
    }

    void* GetWindowUserPointer(PlatformWindowHandle window) {
        if (!window) return nullptr;
        return glfwGetWindowUserPointer(window);
    }

    void SetFramebufferSizeCallback(PlatformWindowHandle window, FramebufferSizeCallback callback) {
        if (!window) return;
        glfwSetFramebufferSizeCallback(window, callback);
    }

    void SetCursorPositionCallback(PlatformWindowHandle window, CursorPositionCallback callback) {
        if (!window) return;
        glfwSetCursorPosCallback(window, callback);
    }

    void SetScrollCallback(PlatformWindowHandle window, ScrollCallback callback) {
        if (!window) return;
        glfwSetScrollCallback(window, callback);
    }

    CursorHandle CreateCursor(const CursorImage& image, int hotX, int hotY) {
        if (image.width <= 0 || image.height <= 0 || !image.pixels) return nullptr;
        GLFWimage glfwImage;
        glfwImage.width = image.width;
        glfwImage.height = image.height;
        glfwImage.pixels = image.pixels;
        return glfwCreateCursor(&glfwImage, hotX, hotY);
    }

    CursorHandle CreateStandardCursor(StandardCursor cursor) {
        return glfwCreateStandardCursor(toGlfwStandardCursor(cursor));
    }

    void SetCursor(PlatformWindowHandle window, CursorHandle cursor) {
        if (!window) return;
        glfwSetCursor(window, cursor);
    }

    void SetCursorMode(PlatformWindowHandle window, CursorMode mode) {
        if (!window) return;
        glfwSetInputMode(window, GLFW_CURSOR, toGlfwCursorMode(mode));
    }

    void SetWindowShouldClose(PlatformWindowHandle window, bool shouldClose) {
        if (!window) return;
        glfwSetWindowShouldClose(window, shouldClose ? GLFW_TRUE : GLFW_FALSE);
    }

    bool WindowShouldClose(PlatformWindowHandle window) {
        return window && glfwWindowShouldClose(window);
    }

    void PollEvents() {
        glfwPollEvents();
    }
}

#pragma once

#include <cstdint>
#include "Platform/PlatformTypes.h"

struct GLFWcursor;

namespace PlatformInput {
    enum class Key : uint8_t {
        Num0,
        Num1,
        Num2,
        Num3,
        Num4,
        Num5,
        Num6,
        Num7,
        Num8,
        Num9,
        W,
        A,
        C,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        T,
        U,
        X,
        Y,
        Z,
        S,
        D,
        F,
        E,
        G,
        LeftAlt,
        RightAlt,
        Space,
        Tab,
        Enter,
        KpEnter,
        DeleteKey,
        Backspace,
        LeftBracket,
        RightBracket,
        ArrowUp,
        ArrowDown,
        ArrowLeft,
        ArrowRight,
        Key6,
        Key7,
        Key8,
        Kp6,
        Kp7,
        Kp8,
        KpAdd,
        KpSubtract,
        Equal,
        Minus,
        Period,
        LeftSuper,
        RightSuper,
        LeftControl,
        RightControl,
        LeftShift,
        RightShift,
        R,
        B,
        V,
        Escape,
    };

    enum class MouseButton : uint8_t {
        Left,
        Right,
        Middle,
    };

    enum class CursorMode : uint8_t {
        Normal,
        Disabled,
    };

    enum class StandardCursor : uint8_t {
        Arrow,
        HorizontalResize,
    };

    struct CursorImage {
        int width = 0;
        int height = 0;
        unsigned char* pixels = nullptr;
    };

    using CursorHandle = GLFWcursor*;
    using FramebufferSizeCallback = void(*)(PlatformWindowHandle, int, int);
    using CursorPositionCallback = void(*)(PlatformWindowHandle, double, double);
    using ScrollCallback = void(*)(PlatformWindowHandle, double, double);

    bool InitializeWindowing();
    void TerminateWindowing();
    void SetNoApiContextHint();
    PlatformWindowHandle CreateWindow(int width, int height, const char* title);
    void DestroyWindow(PlatformWindowHandle window);
    void MakeContextCurrent(PlatformWindowHandle window);
    void SwapBuffers(PlatformWindowHandle window);
    void* GetProcAddress(const char* procName);
    void* GetNativeWindowHandle(PlatformWindowHandle window);
    void* GetOrCreateMetalLayerHandle(PlatformWindowHandle window);
    void ResizeMetalLayerDrawable(PlatformWindowHandle window, int width, int height);

    double GetTimeSeconds();
    bool IsKeyDown(PlatformWindowHandle window, Key key);
    bool IsMouseButtonDown(PlatformWindowHandle window, MouseButton button);
    void GetCursorPosition(PlatformWindowHandle window, double& x, double& y);
    void GetWindowSize(PlatformWindowHandle window, int& width, int& height);
    void GetFramebufferSize(PlatformWindowHandle window, int& width, int& height);
    void SetWindowUserPointer(PlatformWindowHandle window, void* ptr);
    void* GetWindowUserPointer(PlatformWindowHandle window);
    void SetFramebufferSizeCallback(PlatformWindowHandle window, FramebufferSizeCallback callback);
    void SetCursorPositionCallback(PlatformWindowHandle window, CursorPositionCallback callback);
    void SetScrollCallback(PlatformWindowHandle window, ScrollCallback callback);
    CursorHandle CreateCursor(const CursorImage& image, int hotX, int hotY);
    CursorHandle CreateStandardCursor(StandardCursor cursor);
    void SetCursor(PlatformWindowHandle window, CursorHandle cursor);
    void SetCursorMode(PlatformWindowHandle window, CursorMode mode);
    void SetWindowShouldClose(PlatformWindowHandle window, bool shouldClose);
    bool WindowShouldClose(PlatformWindowHandle window);
    void PollEvents();
}

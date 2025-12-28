#include <GLFW/glfw3.h>
#include <jack/jack.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <atomic>
#include <mutex>

// ---------------- Constants ----------------

static const size_t SAMPLE_RATE = 44100;
static const size_t BLOCK_SIZE = 256;  // ~5.8 ms at 44.1 kHz
const float SPACEBAR_HEIGHT = 60.0f;    // waveform drawing area height

// ---------------- Stepped Gradient Settings ----------------

// We use 4 base colors: Redish, Orange, Yellow, Green
// and 23 steps per transition (total steps = 23 * 3 = 69).
const int STEPS_PER_COLOR = 23;
const int NUM_BASE_COLORS = 4;
const int TOTAL_STEPS = STEPS_PER_COLOR * (NUM_BASE_COLORS - 1);

// Base colors (RGB values in [0..1])
const float BASE_COLORS[NUM_BASE_COLORS][3] = {
    {1.0f, 0.2f, 0.3f},  // Redish (at 20 Hz)
    {1.0f, 0.6f, 0.2f},  // Orange (at 200 Hz)
    {1.0f, 1.0f, 0.2f},  // Yellow (at 400 Hz)
    {0.2f, 1.0f, 0.2f}   // Green  (at 1200 Hz)
};

// ---------------- Helper Functions ----------------

inline float clamp(float x, float lower, float upper) {
    return (x < lower) ? lower : (x > upper ? upper : x);
}

inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

inline float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// ---------------- Frequency -> Color Mapping ----------------
//
// This function uses a stepped gradient technique. It maps the input frequency (Hz)
// in the range [20, 1200] (using logarithmic scaling) into a stepped index over TOTAL_STEPS.
// Then it divides that index by STEPS_PER_COLOR to determine which segment (base color pair)
// and what fraction (t) to use for interpolation.
inline void frequencyToColor(float freq, float& r, float& g, float& b) {
    const float fMin = 20.0f;
    const float fMax = 1200.0f;
    // Clamp and map frequency logarithmically
    float clampedFreq = clamp(freq, fMin, fMax);
    float norm = (std::log10(clampedFreq) - std::log10(fMin)) /
        (std::log10(fMax) - std::log10(fMin)); // norm in [0,1]

    // Map normalized value to a stepped index in [0, TOTAL_STEPS]
    float colorIndex = norm * TOTAL_STEPS; // float value [0, TOTAL_STEPS]
    // Divide by STEPS_PER_COLOR to get a value in [0, NUM_BASE_COLORS-1]
    float stepIndex = colorIndex / float(STEPS_PER_COLOR);
    int colorSegment = int(stepIndex);
    // Clamp so that we don't go out of bounds when accessing base colors.
    if (colorSegment >= NUM_BASE_COLORS - 1) {
        colorSegment = NUM_BASE_COLORS - 2;
        stepIndex = float(NUM_BASE_COLORS - 1); // so t becomes 1.0
    }
    float t = stepIndex - float(colorSegment); // fractional part

    // Optional: Apply smoothstep to t for a smoother transition.
    t = smoothstep(t);

    // Interpolate between the two base colors.
    r = lerp(BASE_COLORS[colorSegment][0], BASE_COLORS[colorSegment + 1][0], t);
    g = lerp(BASE_COLORS[colorSegment][1], BASE_COLORS[colorSegment + 1][1], t);
    b = lerp(BASE_COLORS[colorSegment][2], BASE_COLORS[colorSegment + 1][2], t);
}

// ---------------- Simple FFT ----------------

struct Complex {
    float r, i;
};

inline Complex add(const Complex& a, const Complex& b) {
    return { a.r + b.r, a.i + b.i };
}
inline Complex sub(const Complex& a, const Complex& b) {
    return { a.r - b.r, a.i - b.i };
}
inline Complex mul(const Complex& a, const Complex& b) {
    return { a.r * b.r - a.i * b.i, a.r * b.i + a.i * b.r };
}

// In-place Cooley–Tukey FFT
static void fft(std::vector<Complex>& data) {
    size_t n = data.size();
    if (n <= 1) return;

    std::vector<Complex> even(n / 2), odd(n / 2);
    for (size_t i = 0; i < n / 2; i++) {
        even[i] = data[2 * i];
        odd[i] = data[2 * i + 1];
    }
    fft(even);
    fft(odd);
    for (size_t k = 0; k < n / 2; k++) {
        float angle = -2.0f * 3.14159265358979f * float(k) / float(n);
        Complex wk = { std::cos(angle), std::sin(angle) };
        Complex temp = mul(wk, odd[k]);
        data[k] = add(even[k], temp);
        data[k + n / 2] = sub(even[k], temp);
    }
}

// ---------------- Dominant Frequency ----------------
//
// Computes the dominant frequency of a block of audio data using FFT.
static float computeDominantFrequency(const std::vector<float>& block, size_t sampleRate) {
    size_t n = block.size();
    std::vector<Complex> buffer(n);
    for (size_t i = 0; i < n; i++) {
        buffer[i] = { block[i], 0.0f };
    }
    fft(buffer);
    size_t half = n / 2;
    float maxMag = 0.0f;
    size_t maxIdx = 1; // skip DC at index 0
    for (size_t k = 1; k < half; k++) {
        float mag = std::sqrt(buffer[k].r * buffer[k].r + buffer[k].i * buffer[k].i);
        if (mag > maxMag) {
            maxMag = mag;
            maxIdx = k;
        }
    }
    float freq = (sampleRate * maxIdx) / float(n);
    return freq;
}

// ---------------- Global State ----------------

static std::atomic<bool> isRecording{ false };  // toggled by 'R'
static std::mutex dataMutex;
static std::vector<float> audioData;  // recorded audio data

// We'll store one color per audio block.
struct Color { float r, g, b; }; // colors in [0..1]
static std::vector<Color> blockColors;
static size_t blockCount = 0; // number of blocks

jack_client_t* jackClient = nullptr;
jack_port_t* inputPort = nullptr;

// -------------- JACK Callback ----------------

static int processAudio(jack_nframes_t nframes, void* /*arg*/) {
    jack_default_audio_sample_t* inBuf =
        (jack_default_audio_sample_t*)jack_port_get_buffer(inputPort, nframes);
    if (isRecording) {
        std::lock_guard<std::mutex> lock(dataMutex);
        for (jack_nframes_t i = 0; i < nframes; i++) {
            audioData.push_back(inBuf[i]);
        }
    }
    return 0;
}

// -------------- Offline Block Processing ----------------
//
// Splits the recorded audio into blocks, computes the dominant frequency for each,
// then maps that frequency to a stepped color using our new technique.
static void computeBlockColors() {
    std::lock_guard<std::mutex> lock(dataMutex);
    size_t dataSize = audioData.size();
    blockColors.clear();
    if (dataSize < BLOCK_SIZE) {
        blockCount = 0;
        return;
    }
    blockCount = (dataSize + BLOCK_SIZE - 1) / BLOCK_SIZE;  // ceiling division
    blockColors.resize(blockCount);
    for (size_t b = 0; b < blockCount; b++) {
        size_t start = b * BLOCK_SIZE;
        size_t end = ((start + BLOCK_SIZE) < dataSize) ? (start + BLOCK_SIZE) : dataSize;
        std::vector<float> block(end - start);
        for (size_t i = start; i < end; i++) {
            block[i - start] = audioData[i];
        }
        // Pad last block if needed.
        while (block.size() < BLOCK_SIZE) {
            block.push_back(block.back());
        }
        float domFreq = computeDominantFrequency(block, SAMPLE_RATE);
        float rr, gg, bb;
        frequencyToColor(domFreq, rr, gg, bb);
        blockColors[b] = { rr, gg, bb };
    }
}

// -------------- UI Drawing ----------------
//
// Draws the waveform (the "spacebar" area). When recording, the waveform is drawn in black.
// Once processing is complete, each block is drawn in its computed stepped color.
static void drawWaveform(float windowWidth, float windowHeight) {
    float bx = 0.0f;
    float by = (windowHeight - SPACEBAR_HEIGHT) / 2.0f;
    float bw = windowWidth;
    float bh = SPACEBAR_HEIGHT;

    // Draw the background bar.
    glColor3f(0.3f, 0.3f, 0.3f);
    glBegin(GL_QUADS);
    glVertex3f(bx, by, 0);
    glVertex3f(bx + bw, by, 0);
    glVertex3f(bx + bw, by + bh, 0);
    glVertex3f(bx, by + bh, 0);
    glEnd();

    std::lock_guard<std::mutex> lock(dataMutex);
    if (audioData.size() < 2) return;

    glBegin(GL_LINE_STRIP);
    float scaleX = bw / float(audioData.size() - 1);
    for (size_t i = 0; i < audioData.size(); i++) {
        if (isRecording) {
            glColor3f(0.0f, 0.0f, 0.0f);  // Black while recording.
        }
        else {
            size_t b = i / BLOCK_SIZE;
            if (b >= blockColors.size()) b = blockColors.size() - 1;
            Color c = blockColors[b];
            glColor3f(c.r, c.g, c.b);
        }
        float sample = audioData[i];
        float normalized = (sample + 1.0f) * 0.5f;  // map [-1,1] -> [0,1]
        float waveH = bh * 0.8f;
        float waveY = by + (bh - waveH) * 0.5f;
        float yPos = waveY + normalized * waveH;
        float xPos = bx + i * scaleX;
        glVertex3f(xPos, yPos, 1.0f);
    }
    glEnd();
}

// A simple "recording" indicator near the top center.
static void drawBurningIndicator(int windowWidth) {
    if (!isRecording) return;
    float radius = 20.0f;
    float centerX = windowWidth * 0.5f;
    float centerY = 50.0f;
    glColor3f(1.0f, 0.65f, 0.0f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(centerX, centerY);
    int segs = 30;
    for (int i = 0; i <= segs; i++) {
        float angle = 2.0f * 3.1415926f * float(i) / float(segs);
        float dx = std::cos(angle) * radius;
        float dy = std::sin(angle) * radius;
        glVertex2f(centerX + dx, centerY + dy);
    }
    glEnd();
}

// -------------- GLFW Callbacks ----------------

static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -100, 100);
    glMatrixMode(GL_MODELVIEW);
}

static void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        isRecording = !isRecording;
        if (!isRecording) {
            std::cout << "Recording stopped. Processing blocks...\n";
            computeBlockColors();
            std::cout << "Processing done.\n";
        }
        else {
            std::cout << "Recording started.\n";
            std::lock_guard<std::mutex> lock(dataMutex);
            audioData.clear();
            blockColors.clear();
            blockCount = 0;
        }
    }
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

// -------------- Main ----------------

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return -1;
    }
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primary);
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "Scuffed Frequency DAW", primary, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetKeyCallback(window, keyCallback);
    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);
    framebufferSizeCallback(window, winW, winH);

    // Initialize JACK
    jackClient = jack_client_open("ScuffedFreqDAW", JackNullOption, nullptr);
    if (!jackClient) {
        std::cerr << "JACK server not running?\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    jack_set_process_callback(jackClient, processAudio, nullptr);
    inputPort = jack_port_register(jackClient, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    if (!inputPort) {
        std::cerr << "Could not register JACK input port.\n";
        jack_client_close(jackClient);
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    if (jack_activate(jackClient)) {
        std::cerr << "Cannot activate JACK client.\n";
        jack_client_close(jackClient);
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    const char** ports = jack_get_ports(jackClient, nullptr, JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsPhysical | JackPortIsOutput);
    if (ports) {
        if (ports[0]) {
            if (jack_connect(jackClient, ports[0], jack_port_name(inputPort))) {
                std::cerr << "Cannot connect input port\n";
            }
        }
        jack_free(ports);
    }

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glfwGetWindowSize(window, &winW, &winH);
        drawWaveform((float)winW, (float)winH);
        drawBurningIndicator(winW);
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    jack_deactivate(jackClient);
    jack_client_close(jackClient);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

#pragma once
#include "../Host.h"

namespace PerfSystemLogic {
    namespace {
        void loadPerfConfig(PerfContext& perf) {
            std::ifstream f("Host/perf.json");
            if (!f.is_open()) {
                std::cerr << "PerfSystem: could not open Host/perf.json" << std::endl;
                perf.enabled = false;
                perf.configLoaded = true;
                return;
            }
            json data;
            try {
                data = json::parse(f);
            } catch (...) {
                std::cerr << "PerfSystem: failed to parse Host/perf.json" << std::endl;
                perf.enabled = false;
                perf.configLoaded = true;
                return;
            }
            perf.enabled = data.value("enabled", false);
            perf.reportInterval = data.value("intervalSeconds", 1.0);
            perf.allowlist.clear();
            if (data.contains("allowlist") && data["allowlist"].is_array()) {
                for (const auto& entry : data["allowlist"]) {
                    if (entry.is_string()) {
                        perf.allowlist.insert(entry.get<std::string>());
                    }
                }
            }
            perf.configLoaded = true;
        }
    }

    void UpdatePerf(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.perf) return;
        PerfContext& perf = *baseSystem.perf;
        if (!perf.configLoaded) {
            loadPerfConfig(perf);
        }
        if (!perf.enabled) return;

        double now = glfwGetTime();
        if (perf.lastReportTime <= 0.0) {
            perf.lastReportTime = now;
            return;
        }
        double elapsed = now - perf.lastReportTime;
        if (elapsed < perf.reportInterval) return;

        double fps = elapsed > 0.0 ? static_cast<double>(perf.frameCount) / elapsed : 0.0;
        std::cout << "[Perf] " << perf.frameCount << " frames in "
                  << elapsed << "s (~" << fps << " fps)" << std::endl;

        std::vector<std::pair<std::string, double>> sortedTotals;
        sortedTotals.reserve(perf.totalsMs.size());
        for (const auto& [name, total] : perf.totalsMs) {
            sortedTotals.emplace_back(name, total);
        }
        std::sort(sortedTotals.begin(), sortedTotals.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        for (const auto& entry : sortedTotals) {
            int count = perf.counts.count(entry.first) ? perf.counts[entry.first] : 0;
            double avg = count > 0 ? entry.second / static_cast<double>(count) : 0.0;
            std::cout << "[Perf] " << entry.first << ": total "
                      << entry.second << " ms, avg " << avg << " ms" << std::endl;
        }

        perf.totalsMs.clear();
        perf.counts.clear();
        perf.frameCount = 0;
        perf.lastReportTime = now;
    }
}

#pragma once

#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "display.h"

namespace ddc {

// A DDC/CI monitor whose writes are coalesced on a worker thread that also re-reads it while idle to pick up outside changes.
class DdcDisplay : public display::Display {
public:
    DdcDisplay(HANDLE physicalMonitor, std::wstring name, DWORD maxRaw, DWORD currentRaw);
    ~DdcDisplay() override;

    DdcDisplay(const DdcDisplay&) = delete;
    DdcDisplay& operator=(const DdcDisplay&) = delete;

    // display::Display
    std::wstring name() const override { return m_name; }
    bool isConnected() const override { return true; }
    int getBrightness() override { return m_percent; }
    void setBrightness(int percent) override;

private:
    void workerLoop();
    void writeVerified(DWORD raw);

    HANDLE m_handle;
    std::wstring m_name;
    DWORD m_maxRaw;
    std::atomic<int> m_percent;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    int m_pending = -1;  // Latest requested percent, -1 = nothing to send
    bool m_stop = false;
    unsigned m_generation = 0;  // Bumped on every setBrightness, guarded by m_mutex
    std::thread m_worker;
};

// Find all monitors that answer DDC/CI brightness queries, ordered left to right
std::vector<std::unique_ptr<DdcDisplay>> enumerate();

} // namespace ddc

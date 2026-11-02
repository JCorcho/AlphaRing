#include "PlayStation.h"

#include "Input.h"
#include "log/Log.h"
#include "filesystem/Filesystem.h"
#include "render/imgui/Toast.h"

#include <Windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <tlhelp32.h>

// Report layouts follow the Linux hid-playstation driver and SDL's HIDAPI
// PS4/PS5 drivers.
//
// Input, after the report id (and, over Bluetooth, its header):
//   DualShock 4 (USB 0x01, Bluetooth 0x01 or 0x11) and DualSense over
//   Bluetooth before its first output report (0x01, "simple"):
//     0 LX, 1 LY, 2 RX, 3 RY, 4 hat|face, 5 shoulders/sticks/menu, 6 PS|pad, 7 L2, 8 R2
//   DualSense (USB 0x01, Bluetooth 0x31):
//     0 LX, 1 LY, 2 RX, 3 RY, 4 L2, 5 R2, 6 counter, 7 hat|face, 8 shoulders/sticks/menu, 9 PS|pad|mute
namespace AlphaRing::Input::PlayStation {
    namespace {
        constexpr USHORT k_sony = 0x054C;

        struct Model { USHORT pid; bool dualsense; bool dongle; const char* name; };
        constexpr Model k_models[] = {
            {0x05C4, false, false, "DualShock 4"},
            {0x09CC, false, false, "DualShock 4"},
            {0x0BA0, false, true,  "DualShock 4 (wireless adapter)"},
            {0x0CE6, true,  false, "DualSense"},
            {0x0DF2, true,  false, "DualSense Edge"},
        };

        // the PlayStation player colors, dimmed a little
        constexpr uint8_t k_playerColors[4][3] = {{0, 40, 255}, {255, 16, 16}, {16, 230, 32}, {255, 30, 170}};
        constexpr uint8_t k_noPlayerColor[3] = {40, 40, 40};
        // DualSense player LEDs (five, bits from the left): 1 = middle, 2 = inner pair, ...
        constexpr uint8_t k_playerLeds[4] = {0x04, 0x0A, 0x15, 0x1B};

        uint32_t Crc32(const uint8_t* data, size_t length, uint32_t crc = 0xFFFFFFFF) {
            for (size_t i = 0; i < length; ++i) {
                crc ^= data[i];
                for (int b = 0; b < 8; ++b)
                    crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
            return crc;
        }

        // Bluetooth output reports end in a CRC-32 of 0xA2 (the HID output
        // header) and the report.
        void SealBluetooth(uint8_t* report, size_t length) {
            const uint8_t seed = 0xA2;
            uint32_t crc = ~Crc32(report, length - 4, Crc32(&seed, 1));
            memcpy(report + length - 4, &crc, 4);
        }

        struct Device {
            std::wstring path;
            std::string name;
            HANDLE handle = INVALID_HANDLE_VALUE;
            bool dualsense = false;
            bool dongle = false;     // Sony's wireless adapter: reports even with no controller synced
            bool bluetooth = false;
            bool writable = false;
            USHORT inputLength = 0;
            USHORT outputLength = 0;
            int slot = -1;
            std::atomic<bool> alive{true};
            HANDLE wake = nullptr; // an output is waiting
            std::thread reader;

            // under s_lock
            XINPUT_GAMEPAD pad{};
            bool guide = false;
            DWORD packet = 0;
            bool hasReport = false;
            int battery = -1;        // percent, -1 unknown
            bool charging = false;
            bool lowWarned = false;
            uint8_t rumbleLow = 0, rumbleHigh = 0;
            int player = -1;
            bool outputDirty = true;
            bool lightbarSetup = false;
            uint8_t sequence = 0;
        };

        SRWLOCK s_lock = SRWLOCK_INIT; // the devices' state and the slot table
        // Never destroyed: at process exit a destructor of a std::thread
        // still running (the manager's, a device's reader) would terminate MCC.
        std::vector<std::unique_ptr<Device>>& s_devices = *new std::vector<std::unique_ptr<Device>>();
        Device* s_slots[4] = {};
        int s_slotPlayer[4] = {-1, -1, -1, -1};
        // Where each controller was (by device path: a Bluetooth path holds the
        // controller's address), to put it back there, also after a restart,
        // so each player keeps their controller. Saved next to the DLL.
        std::map<std::wstring, int> s_lastSlot;
        bool s_lastSlotDirty = false;

        const std::string& SlotFile() {
            static std::string path = AlphaRing::Filesystem::ModuleFile("alpha_ring_controllers.cfg");
            return path;
        }

        std::string Utf8(const std::wstring& text) {
            int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string result(size > 0 ? size - 1 : 0, '\0');
            if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
            return result;
        }

        std::wstring Wide(const std::string& text) {
            int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
            std::wstring result(size > 0 ? size - 1 : 0, L'\0');
            if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
            return result;
        }

        // "<slot> <path>" per line
        void LoadSlots() {
            std::ifstream file(SlotFile());
            std::string line;
            while (std::getline(file, line)) {
                if (line.size() < 3 || line[0] < '0' || line[0] > '3' || line[1] != ' ') continue;
                s_lastSlot[Wide(line.substr(2))] = line[0] - '0';
            }
        }

        void SaveSlots(const std::map<std::wstring, int>& slots) {
            std::ofstream file(SlotFile(), std::ios::trunc);
            for (auto& [path, slot] : slots)
                file << slot << ' ' << Utf8(path) << '\n';
        }

        std::atomic<bool> s_enabled{true};
        std::atomic<bool> s_running{false};
        HANDLE s_rescan = nullptr;
        std::thread* s_manager = nullptr; // never destroyed, as above
        bool s_ds4windows = false;        // DS4Windows runs: it translates the controllers already

        bool DS4WindowsRunning() {
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE) return false;
            PROCESSENTRY32W entry{sizeof(PROCESSENTRY32W)};
            bool found = false;
            for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !found; ok = Process32NextW(snapshot, &entry))
                found = _wcsicmp(entry.szExeFile, L"DS4Windows.exe") == 0;
            CloseHandle(snapshot);
            return found;
        }

        SHORT Axis(uint8_t value, bool invert) {
            int v = ((int)value - 128) * 256 + (value > 128 ? (value - 128) * 2 : 0);
            if (invert) v = -v - 1;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            return (SHORT)v;
        }

        WORD Hat(uint8_t hat) {
            static const WORD k_hat[8] = {
                XINPUT_GAMEPAD_DPAD_UP,
                XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT,
                XINPUT_GAMEPAD_DPAD_RIGHT,
                XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT,
                XINPUT_GAMEPAD_DPAD_DOWN,
                XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT,
                XINPUT_GAMEPAD_DPAD_LEFT,
                XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT,
            };
            return hat < 8 ? k_hat[hat] : 0;
        }

        // sticks at 0-3; the face/hat byte, the next and the PS byte at
        // `buttons`; the triggers at `l2`/`r2`
        void Decode(const uint8_t* d, int buttons, int l2, int r2, XINPUT_GAMEPAD& pad, bool& guide) {
            pad.sThumbLX = Axis(d[0], false);
            pad.sThumbLY = Axis(d[1], true);
            pad.sThumbRX = Axis(d[2], false);
            pad.sThumbRY = Axis(d[3], true);
            pad.bLeftTrigger = d[l2];
            pad.bRightTrigger = d[r2];

            uint8_t b0 = d[buttons], b1 = d[buttons + 1], b2 = d[buttons + 2];
            WORD w = Hat(b0 & 0x0F);
            if (b0 & 0x10) w |= XINPUT_GAMEPAD_X;              // Square
            if (b0 & 0x20) w |= XINPUT_GAMEPAD_A;              // Cross
            if (b0 & 0x40) w |= XINPUT_GAMEPAD_B;              // Circle
            if (b0 & 0x80) w |= XINPUT_GAMEPAD_Y;              // Triangle
            if (b1 & 0x01) w |= XINPUT_GAMEPAD_LEFT_SHOULDER;  // L1
            if (b1 & 0x02) w |= XINPUT_GAMEPAD_RIGHT_SHOULDER; // R1
            if (b1 & 0x10) w |= XINPUT_GAMEPAD_BACK;           // Share / Create
            if (b1 & 0x20) w |= XINPUT_GAMEPAD_START;          // Options
            if (b1 & 0x40) w |= XINPUT_GAMEPAD_LEFT_THUMB;     // L3
            if (b1 & 0x80) w |= XINPUT_GAMEPAD_RIGHT_THUMB;    // R3
            if (b2 & 0x02) w |= XINPUT_GAMEPAD_BACK;           // touchpad click (the scoreboard, as DS4Windows does)
            pad.wButtons = w;
            guide = (b2 & 0x01) != 0;                          // PS
        }

        // Battery from a full report's status byte. DualShock 4 (data[29]):
        // level 0-10 (11 = full on the cable), bit 4 = on the cable.
        // DualSense (data[52]): level 0-10, high nibble 0 = on battery, 1 =
        // charging, 2 = full.
        void BatteryDS4(const uint8_t* data, int& battery, bool& charging) {
            uint8_t status = data[29];
            int level = status & 0x0F;
            charging = (status & 0x10) && level < 11;
            battery = level >= 10 ? 100 : level * 10 + 5;
        }

        void BatteryDualSense(const uint8_t* data, int& battery, bool& charging) {
            uint8_t status = data[52];
            int level = status & 0x0F;
            charging = (status >> 4) == 1;
            battery = (status >> 4) == 2 ? 100 : (std::min)(level * 10 + 5, 100);
        }

        // battery stays -1 when the report has none (the Bluetooth "simple" ones)
        bool Parse(Device& dev, const uint8_t* report, DWORD length, XINPUT_GAMEPAD& pad, bool& guide,
                   int& battery, bool& charging) {
            battery = -1;
            charging = false;
            if (length < 11) return false;
            switch (report[0]) {
                case 0x01:
                    // the wireless adapter with no controller synced to it
                    if (dev.dongle && length > 31 && (report[31] & 0x04)) return false;
                    if (dev.dualsense && !dev.bluetooth) {
                        Decode(report + 1, 7, 4, 5, pad, guide);       // DualSense, USB
                        if (length > 53) BatteryDualSense(report + 1, battery, charging);
                    } else {
                        Decode(report + 1, 4, 7, 8, pad, guide);       // DualShock 4, or DualSense Bluetooth "simple"
                        if (!dev.bluetooth && length > 30) BatteryDS4(report + 1, battery, charging);
                    }
                    return true;
                case 0x11:
                    if (dev.dualsense || length < 13) return false;
                    Decode(report + 3, 4, 7, 8, pad, guide);           // DualShock 4, Bluetooth
                    if (length > 32) BatteryDS4(report + 3, battery, charging);
                    return true;
                case 0x31:
                    if (!dev.dualsense || length < 13) return false;
                    Decode(report + 2, 7, 4, 5, pad, guide);           // DualSense, Bluetooth
                    if (length > 54) BatteryDualSense(report + 2, battery, charging);
                    return true;
                default:
                    return false;
            }
        }

        // Rumble, lightbar and (DualSense) player LEDs in one output report.
        std::vector<uint8_t> BuildOutput(Device& dev) {
            std::vector<uint8_t> out(std::max<size_t>(dev.outputLength, 78), 0);
            const uint8_t* color = (dev.player >= 0 && dev.player < 4) ? k_playerColors[dev.player] : k_noPlayerColor;
            size_t length;
            if (!dev.dualsense) {
                uint8_t* c;
                if (dev.bluetooth) {
                    out[0] = 0x11;
                    out[1] = 0xC4; // HID + CRC, 4 ms reports
                    c = &out[3];
                    length = 78;
                } else {
                    out[0] = 0x05;
                    c = &out[1];
                    length = 32;
                }
                c[0] = 0x03;       // motors + lightbar
                c[3] = dev.rumbleHigh;
                c[4] = dev.rumbleLow;
                c[5] = color[0];
                c[6] = color[1];
                c[7] = color[2];
            } else {
                uint8_t* c;
                if (dev.bluetooth) {
                    out[0] = 0x31;
                    out[1] = (uint8_t)(dev.sequence << 4);
                    out[2] = 0x10;
                    dev.sequence = (dev.sequence + 1) & 0x0F;
                    c = &out[3];
                    length = 78;
                } else {
                    out[0] = 0x02;
                    c = &out[1];
                    length = 48;
                }
                c[0] = 0x01 | 0x02;             // compatible rumble, haptics select
                c[1] = 0x04 | 0x10;             // lightbar, player LEDs
                c[2] = dev.rumbleHigh;
                c[3] = dev.rumbleLow;
                c[38] = 0x04;                   // rumble for newer firmware
                if (!dev.lightbarSetup) {
                    c[38] |= 0x02;              // take the lightbar over from the startup animation
                    c[41] = 0x02;
                    dev.lightbarSetup = true;
                }
                c[43] = (dev.player >= 0 && dev.player < 4) ? k_playerLeds[dev.player] : 0;
                c[44] = color[0];
                c[45] = color[1];
                c[46] = color[2];
            }
            if (dev.bluetooth)
                SealBluetooth(out.data(), length);
            // Windows wants the collection's output report length exactly
            out.resize(dev.outputLength >= length ? dev.outputLength : length);
            return out;
        }

        void WriteOutput(Device& dev, OVERLAPPED& ov) {
            AcquireSRWLockExclusive(&s_lock);
            if (!dev.outputDirty || !dev.writable) {
                ReleaseSRWLockExclusive(&s_lock);
                return;
            }
            dev.outputDirty = false;
            auto report = BuildOutput(dev);
            ReleaseSRWLockExclusive(&s_lock);

            ResetEvent(ov.hEvent);
            DWORD written = 0;
            if (!WriteFile(dev.handle, report.data(), (DWORD)report.size(), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING)
                return;
            if (WaitForSingleObject(ov.hEvent, 250) != WAIT_OBJECT_0)
                CancelIoEx(dev.handle, &ov);
            GetOverlappedResult(dev.handle, &ov, &written, TRUE);
        }

        void ReadLoop(Device* dev) {
            OVERLAPPED readOv{}, writeOv{};
            readOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            writeOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            std::vector<uint8_t> buffer(std::max<USHORT>(dev->inputLength, 64));
            ULONGLONG lastReport = GetTickCount64();

            while (dev->alive && s_running) {
                WriteOutput(*dev, writeOv);

                ResetEvent(readOv.hEvent);
                DWORD read = 0;
                if (!ReadFile(dev->handle, buffer.data(), (DWORD)buffer.size(), nullptr, &readOv) &&
                    GetLastError() != ERROR_IO_PENDING) {
                    LOG_INFO("PlayStation: {} stopped answering ({})", dev->name, GetLastError());
                    break;
                }
                HANDLE events[2] = {readOv.hEvent, dev->wake};
                DWORD waited = WaitForMultipleObjects(2, events, FALSE, 1000);
                if (waited != WAIT_OBJECT_0)
                    CancelIoEx(dev->handle, &readOv);
                BOOL ok = GetOverlappedResult(dev->handle, &readOv, &read, TRUE);
                if (!ok) {
                    DWORD error = GetLastError();
                    if (error == ERROR_OPERATION_ABORTED) {
                        // no report within a second, or an output to send: keep going,
                        // unless the controller has been silent too long (it sends ~250 a second)
                        if (GetTickCount64() - lastReport > 5000) {
                            LOG_INFO("PlayStation: {} went silent", dev->name);
                            break;
                        }
                        continue;
                    }
                    LOG_INFO("PlayStation: {} disconnected ({})", dev->name, error);
                    break;
                }
                XINPUT_GAMEPAD pad{};
                bool guide = false;
                int battery;
                bool charging;
                if (!Parse(*dev, buffer.data(), read, pad, guide, battery, charging))
                    continue;
                lastReport = GetTickCount64();
                bool warnLow = false;
                AcquireSRWLockExclusive(&s_lock);
                if (!dev->hasReport || memcmp(&pad, &dev->pad, sizeof(pad)) != 0 || guide != dev->guide)
                    ++dev->packet;
                dev->pad = pad;
                dev->guide = guide;
                dev->hasReport = true;
                if (battery >= 0) {
                    dev->battery = battery;
                    dev->charging = charging;
                    // once per discharge below 20%
                    if (!charging && battery < 20 && !dev->lowWarned) {
                        dev->lowWarned = true;
                        warnLow = true;
                    } else if (charging || battery >= 30) {
                        dev->lowWarned = false;
                    }
                }
                int slot = dev->slot;
                ReleaseSRWLockExclusive(&s_lock);
                if (warnLow)
                    Toast::Show("%s (Controller %d): battery low", dev->name.c_str(), slot + 1);
            }

            CancelIoEx(dev->handle, nullptr);
            CloseHandle(readOv.hEvent);
            CloseHandle(writeOv.hEvent);
            dev->alive = false;
            if (s_rescan) SetEvent(s_rescan); // the manager frees its slot
        }

        const Model* FindModel(USHORT vid, USHORT pid) {
            if (vid != k_sony) return nullptr;
            for (auto& model : k_models)
                if (model.pid == pid) return &model;
            return nullptr;
        }

        std::unique_ptr<Device> Open(const std::wstring& path) {
            HANDLE probe = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (probe == INVALID_HANDLE_VALUE) return nullptr;
            HIDD_ATTRIBUTES attributes{sizeof(HIDD_ATTRIBUTES)};
            bool known = HidD_GetAttributes(probe, &attributes) && FindModel(attributes.VendorID, attributes.ProductID);
            CloseHandle(probe);
            if (!known) return nullptr;
            const Model* model = FindModel(attributes.VendorID, attributes.ProductID);

            bool writable = true;
            HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                writable = false;
                handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            }
            if (handle == INVALID_HANDLE_VALUE) {
                LOG_INFO("PlayStation: can't open a {} ({}) - another program may have it to itself", model->name, GetLastError());
                return nullptr;
            }

            PHIDP_PREPARSED_DATA preparsed = nullptr;
            HIDP_CAPS caps{};
            bool gamepad = false;
            if (HidD_GetPreparsedData(handle, &preparsed)) {
                gamepad = HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 0x01 &&
                          (caps.Usage == 0x05 || caps.Usage == 0x04);
                HidD_FreePreparsedData(preparsed);
            }
            if (!gamepad || caps.InputReportByteLength < 10) {
                CloseHandle(handle);
                return nullptr;
            }

            auto dev = std::make_unique<Device>();
            dev->path = path;
            dev->handle = handle;
            dev->dualsense = model->dualsense;
            dev->dongle = model->dongle;
            // over Bluetooth the reports are longer than USB's 64 bytes
            dev->bluetooth = caps.InputReportByteLength > 64;
            dev->writable = writable;
            dev->inputLength = caps.InputReportByteLength;
            dev->outputLength = caps.OutputReportByteLength;
            dev->name = std::string(model->name) + (dev->bluetooth ? " (Bluetooth)" : " (USB)");
            dev->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            return dev;
        }

        std::vector<std::wstring> HidPaths() {
            std::vector<std::wstring> paths;
            GUID guid;
            HidD_GetHidGuid(&guid);
            HDEVINFO info = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (info == INVALID_HANDLE_VALUE) return paths;
            SP_DEVICE_INTERFACE_DATA data{sizeof(SP_DEVICE_INTERFACE_DATA)};
            for (DWORD i = 0; SetupDiEnumDeviceInterfaces(info, nullptr, &guid, i, &data); ++i) {
                DWORD size = 0;
                SetupDiGetDeviceInterfaceDetailW(info, &data, nullptr, 0, &size, nullptr);
                if (size == 0) continue;
                std::vector<uint8_t> buffer(size);
                auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
                detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
                if (!SetupDiGetDeviceInterfaceDetailW(info, &data, detail, size, nullptr, nullptr))
                    continue;
                // Sony's vendor id is in the path over USB ("vid_054c") and
                // Bluetooth ("vid&0002054c"); nothing else is opened
                std::wstring path = detail->DevicePath;
                std::wstring lower = path;
                for (auto& ch : lower) ch = towlower(ch);
                if (lower.find(L"054c") != std::wstring::npos)
                    paths.push_back(std::move(path));
            }
            SetupDiDestroyDeviceInfoList(info);
            return paths;
        }

        // Under s_lock. A free slot: no XInput controller in it and none of ours,
        // the one the controller had last time if it's free.
        int FreeSlot(const std::wstring& path, DWORD realMask) {
            auto usable = [&](int slot) { return !(realMask & (1u << slot)) && !s_slots[slot]; };
            auto last = s_lastSlot.find(path);
            if (last != s_lastSlot.end() && usable(last->second)) return last->second;
            for (int slot = 0; slot < 4; ++slot)
                if (usable(slot)) return slot;
            return -1;
        }

        void Place(Device* dev, int slot) {
            if (dev->slot >= 0 && s_slots[dev->slot] == dev) s_slots[dev->slot] = nullptr;
            dev->slot = slot;
            if (slot >= 0) {
                s_slots[slot] = dev;
                auto last = s_lastSlot.find(dev->path);
                if (last == s_lastSlot.end() || last->second != slot) {
                    s_lastSlot[dev->path] = slot;
                    s_lastSlotDirty = true;
                }
                dev->player = s_slotPlayer[slot];
            } else {
                dev->player = -1;
            }
            dev->outputDirty = true;
            if (dev->wake) SetEvent(dev->wake);
        }

        void CloseDevice(std::unique_ptr<Device>& dev) {
            dev->alive = false;
            if (dev->wake) SetEvent(dev->wake);
            CancelIoEx(dev->handle, nullptr);
            if (dev->reader.joinable()) dev->reader.join();
            CloseHandle(dev->handle);
            if (dev->wake) CloseHandle(dev->wake);
        }

        void Refresh() {
            bool ds4windows = DS4WindowsRunning();
            if (ds4windows != s_ds4windows) {
                s_ds4windows = ds4windows;
                LOG_INFO("PlayStation: DS4Windows {}", ds4windows ? "is running, leaving the controllers to it" : "closed");
                if (ds4windows && s_enabled)
                    Toast::Show("DS4Windows is running: PlayStation controllers are left to it");
            }
            bool enabled = s_enabled && !ds4windows;
            DWORD realMask = Input::RealConnectedMask();

            // drop the controllers that went away (or all, when turned off)
            std::vector<std::unique_ptr<Device>> gone;
            AcquireSRWLockExclusive(&s_lock);
            for (auto it = s_devices.begin(); it != s_devices.end();) {
                if (!enabled || !(*it)->alive) {
                    Place(it->get(), -1);
                    gone.push_back(std::move(*it));
                    it = s_devices.erase(it);
                } else {
                    ++it;
                }
            }
            ReleaseSRWLockExclusive(&s_lock);
            for (auto& dev : gone) {
                if (enabled) Toast::Show("%s disconnected", dev->name.c_str());
                else Toast::Show("%s released", dev->name.c_str());
                CloseDevice(dev);
            }
            if (!enabled) return;

            // new ones
            for (auto& path : HidPaths()) {
                bool known = false;
                AcquireSRWLockShared(&s_lock);
                for (auto& dev : s_devices) known |= dev->path == path;
                ReleaseSRWLockShared(&s_lock);
                if (known) continue;
                auto dev = Open(path);
                if (!dev) continue;
                Device* raw = dev.get();
                AcquireSRWLockExclusive(&s_lock);
                Place(raw, FreeSlot(path, realMask));
                s_devices.push_back(std::move(dev));
                int slot = raw->slot;
                ReleaseSRWLockExclusive(&s_lock);
                raw->reader = std::thread(ReadLoop, raw);
                LOG_INFO("PlayStation: {} in slot {}", raw->name, slot);
                if (slot >= 0)
                    Toast::Show("%s connected as Controller %d", raw->name.c_str(), slot + 1);
                else
                    Toast::Show("%s connected, but all four controller slots are taken", raw->name.c_str());
            }

            // an XInput controller took a slot one of ours had: move ours
            AcquireSRWLockExclusive(&s_lock);
            for (auto& dev : s_devices) {
                if (dev->slot >= 0 && (realMask & (1u << dev->slot)))
                    Place(dev.get(), -1);
                if (dev->slot < 0) {
                    int slot = FreeSlot(dev->path, realMask);
                    if (slot >= 0) {
                        Place(dev.get(), slot);
                        Toast::Show("%s is now Controller %d", dev->name.c_str(), slot + 1);
                    }
                }
            }
            ReleaseSRWLockExclusive(&s_lock);
        }

        void ManagerLoop() {
            AcquireSRWLockExclusive(&s_lock);
            LoadSlots();
            ReleaseSRWLockExclusive(&s_lock);
            while (s_running) {
                Refresh();
                AcquireSRWLockExclusive(&s_lock);
                bool dirty = s_lastSlotDirty;
                s_lastSlotDirty = false;
                auto slots = s_lastSlot;
                ReleaseSRWLockExclusive(&s_lock);
                if (dirty) SaveSlots(slots);
                // a controller arriving sends WM_DEVICECHANGE (RequestRescan); this is the fallback
                WaitForSingleObject(s_rescan, 10000);
            }
            AcquireSRWLockExclusive(&s_lock);
            auto devices = std::move(s_devices);
            s_devices.clear();
            for (auto& slot : s_slots) slot = nullptr;
            ReleaseSRWLockExclusive(&s_lock);
            for (auto& dev : devices) CloseDevice(dev);
        }
    }

    void Start() {
        if (s_running.exchange(true)) return;
        s_rescan = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        s_manager = new std::thread(ManagerLoop);
    }

    void Stop() {
        if (!s_running.exchange(false)) return;
        SetEvent(s_rescan);
        // At process exit the loader may already have ended our threads.
        if (s_manager && s_manager->joinable()) s_manager->detach();
    }

    void SetEnabled(bool enabled) {
        if (s_enabled.exchange(enabled) != enabled) {
            LOG_INFO("PlayStation: native support {}", enabled ? "on" : "off");
            RequestRescan();
        }
    }

    bool Enabled() { return s_enabled; }

    void RequestRescan() {
        if (s_rescan) SetEvent(s_rescan);
    }

    bool GetState(DWORD slot, XINPUT_STATE* state, bool guide) {
        if (slot >= 4 || !state) return false;
        AcquireSRWLockShared(&s_lock);
        Device* dev = s_slots[slot];
        bool ok = dev && dev->alive && dev->hasReport;
        if (ok) {
            state->dwPacketNumber = dev->packet;
            state->Gamepad = dev->pad;
            if (guide && dev->guide) state->Gamepad.wButtons |= 0x0400;
        }
        ReleaseSRWLockShared(&s_lock);
        return ok;
    }

    DWORD ConnectedMask() {
        DWORD mask = 0;
        AcquireSRWLockShared(&s_lock);
        for (int slot = 0; slot < 4; ++slot)
            if (s_slots[slot] && s_slots[slot]->alive) mask |= 1u << slot;
        ReleaseSRWLockShared(&s_lock);
        return mask;
    }

    bool SetVibration(DWORD slot, const XINPUT_VIBRATION* vibration) {
        if (slot >= 4 || !vibration) return false;
        AcquireSRWLockExclusive(&s_lock);
        Device* dev = s_slots[slot];
        bool ok = dev && dev->alive;
        if (ok) {
            uint8_t low = (uint8_t)(vibration->wLeftMotorSpeed >> 8);
            uint8_t high = (uint8_t)(vibration->wRightMotorSpeed >> 8);
            if (low != dev->rumbleLow || high != dev->rumbleHigh) {
                dev->rumbleLow = low;
                dev->rumbleHigh = high;
                dev->outputDirty = true;
                SetEvent(dev->wake);
            }
        }
        ReleaseSRWLockExclusive(&s_lock);
        return ok;
    }

    void SetSlotPlayer(DWORD slot, int player) {
        if (slot >= 4) return;
        AcquireSRWLockExclusive(&s_lock);
        if (s_slotPlayer[slot] != player) {
            s_slotPlayer[slot] = player;
            if (Device* dev = s_slots[slot]) {
                dev->player = player;
                dev->outputDirty = true;
                SetEvent(dev->wake);
            }
        }
        ReleaseSRWLockExclusive(&s_lock);
    }

    std::string SlotName(DWORD slot) {
        if (slot >= 4) return {};
        AcquireSRWLockShared(&s_lock);
        std::string name;
        if (Device* dev = s_slots[slot]) {
            name = dev->name;
            if (dev->battery >= 0)
                name += ", " + std::to_string(dev->battery) + "%" + (dev->charging ? " charging" : "");
        }
        ReleaseSRWLockShared(&s_lock);
        return name;
    }
}

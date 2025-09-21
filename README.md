# SysMonTray — CPU & RAM Gauges

SysMonTray is a lightweight Windows desktop application that provides **real‑time monitoring of CPU and RAM usage** in a clean, modern interface.  
It displays two circular gauges that automatically scale to fit the application window, giving you a quick at‑a‑glance overview of your system’s load.

---

## ✨ Features

- **CPU Usage Monitoring**
  - Reads system CPU usage using **Processor Information → % Processor Utility** (the same counter used by Task Manager).
  - Automatically falls back to the legacy **% Processor Time** counter if the preferred one is unavailable.
  - Smooths readings with a light exponential moving average to reduce spikes.

- **RAM Usage Monitoring**
  - Reads total vs available physical memory using `GlobalMemoryStatusEx`.
  - Displays usage as a percentage, matching Task Manager’s “Memory %”.

- **Modern Dark UI**
  - Clean circular gauges styled with accent colors (blue for CPU, teal for RAM).
  - Gauges dynamically resize to fit the app window dimensions.
  - Uses Segoe UI fonts for a Windows 10/11‑style look.

- **System Tray Integration**
  - Minimizes to the tray when minimized.
  - Restores on double‑clicking the tray icon.

- **Lightweight**
  - Single small executable, no background services or installers.
  - Uses standard Windows APIs (no external dependencies).

---

## 📷 Screenshot (Concept)

```
 ------------------------------------
 |  SysMonTray - CPU & RAM           |
 |                                   |
 |   [ CPU Gauge ]   [ RAM Gauge ]   |
 |                                   |
 ------------------------------------
```

---

## 🛠 Build Instructions

1. Open **Visual Studio 2022**.
2. Create a new **Windows Desktop Application → Empty Project**.
3. Add `SysMonTray.cpp` to the project.
4. Set project properties (for **All Configurations**, preferably **x64** build):
   - **C/C++ → Language → C++ Language Standard** → `/std:c++17` (or later)
   - **General → Character Set** → *Use Unicode Character Set*
   - **Linker → Input → Additional Dependencies** → `pdh.lib; comctl32.lib`
   - **Linker → System → Subsystem** → *Windows* (uses `wWinMain`)

5. Build and run.

---

## 🔧 Technical Notes

- **CPU Counter**  
  The app attempts to open `\Processor Information(_Total)\% Processor Utility`.  
  - On modern Windows builds this provides the same reading as Task Manager.  
  - If not available, it falls back to `\Processor(_Total)\% Processor Time`.  

- **Smoothing**  
  CPU readings are smoothed with an exponential moving average (`EMA α=0.4`) to reduce jitter.

- **RAM Counter**  
  Uses `GlobalMemoryStatusEx` to calculate `(Total – Available) / Total * 100`.

---

## 📄 License

This project is provided as‑is for educational and monitoring purposes.  
You are free to modify and distribute it for personal or internal use.

---

## 👤 Author

Programmed by **Bob Paydar**
2025

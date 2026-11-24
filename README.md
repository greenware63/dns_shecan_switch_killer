# Shecan Kill Switch

A Windows desktop application that monitors Shecan DNS and uses Windows Defender Firewall to block outbound internet traffic when Shecan DNS is unavailable. The interface displays DNS health, monitoring statistics, and an event log, and includes dark and light themes.

## How it works

- Checks the Shecan DNS servers configured on active network adapters every two seconds.
- Supports the Shecan DNS addresses `178.22.122.100` and `185.51.200.2`, as well as a local Shecan resolver at `127.0.0.1` or `::1`.
- When automatic mode is enabled, blocks outbound traffic if the configured resolver is unhealthy and restores the saved firewall policy when DNS becomes healthy.
- Provides controls for an immediate check and for restoring the saved firewall policy.
- Removes its firewall rules and attempts to restore the saved policy when the application exits.

> **Important:** This application changes Windows Defender Firewall policy and requires Administrator privileges. On startup it captures the current firewall policy and enables the kill switch. If Shecan DNS is not configured or reachable, internet access may be blocked until DNS recovers or you use **Restore Firewall**. Keep the application running while automatic protection is needed.

## Requirements

- Windows
- CMake 3.16 or newer
- A C++17 compiler supported by your Qt kit (such as MSVC or MinGW)
- Qt 6 with the **Widgets** and **Concurrent** modules

## Build

Install Qt 6 and a compatible C++ toolchain, then configure and build from a terminal:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/<your-kit>"
cmake --build build --config Release
```

Replace the `CMAKE_PREFIX_PATH` value with the directory for your installed Qt kit. Run the resulting `ShecanKillSwitch` executable as Administrator.

## Configure DNS

Configure an active network adapter to use Shecan DNS (`178.22.122.100` and/or `185.51.200.2`), or configure it to use the local Shecan DNS resolver (`127.0.0.1` or `::1`) and make sure that resolver is running. The application monitors the DNS configuration; it does not change adapter DNS settings or install Shecan software.

## License

No license has been specified for this project.

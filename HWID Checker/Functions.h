#pragma once
#include "Includes.h"

struct RGBColor {
    int r, g, b;
};

struct CLIConfig {
    bool quiet = false;
    bool showHelp = false;
    bool showVersion = false;
    bool headless = false;
    bool noUpdate = false;
    std::string exportPath;
};

namespace Checks {
    void collectMotherboardSerial();
    void collectCPUId();
    void collectDiskSerial();
    void collectBIOSSerial();
    void collectMAC();
    void collectUUID();
    void checkForUpdate();
}

namespace Helper {
    extern CLIConfig cliConfig;
    extern std::vector<std::pair<std::string, std::string>> g_hwids;
    extern std::string g_repoUrl;

    void setupConsole();
    std::string runWMIC(const std::string& alias, const std::string& property);
    std::string runPowerShell(const std::string& command);
    std::string fetchURL(const std::wstring& url);
    bool isAdmin();
    void autoElevate();
    void addHWID(const std::string& name, const std::string& value);
    void displayResults();
    void exportResultsJSON();
    std::string getTimestampISO();
    std::string escapeJSON(const std::string& s);
    std::string trim(const std::string& s);
    CLIConfig parseCLI(int argc, char* argv[]);
    void showHelp();
    void showVersion();

    void initLogging();
    void closeLogging();
    void logWrite(const std::string& message);
}
namespace Color {
    void setForegroundColor(const RGBColor& aColor);
    inline RGBColor Green = { 0, 255, 0 };
    inline RGBColor Yellow = { 255, 255, 0 };
    inline RGBColor Cyan = { 0, 255, 255 };
    inline RGBColor LightGray = { 211, 211, 211 };
    inline RGBColor Red = { 255, 0, 0 };
}

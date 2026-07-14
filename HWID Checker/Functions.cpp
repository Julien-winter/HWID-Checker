#include "Functions.h"

namespace Helper {
    std::mutex logMutex;
    std::mutex dataMutex;
    std::ofstream logFile;
    bool logEnabled = false;
    std::string g_logPath;
    CLIConfig cliConfig;
    std::vector<std::pair<std::string, std::string>> g_hwids;
    std::string g_appName = "HWID Checker";
    std::string g_appVersion = "1.0.2";
    std::string g_repoUrl = "Julien-winter/HWID-Checker";
    std::vector<DiskInfo> g_disks;
    std::vector<MACInfo> g_macs;
    std::string g_cpuSerial;
    std::string g_biosSerial;
    std::string g_moboSerial;
    std::string g_uuid;
}

std::string Helper::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    std::string result = s.substr(start, end - start + 1);
    while (!result.empty() && result.back() == '.')
        result.pop_back();
    return result;
}

std::string Helper::escapeJSON(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    return out;
}

std::string Helper::getTimestampISO() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    gmtime_s(&tm, &time);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

void Helper::setupConsole() {
    SetConsoleTitleA(("HWID Checker v" + g_appVersion).c_str());
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

bool Helper::isAdmin() {
    BOOL elevated = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elev;
        DWORD size = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elev, size, &size))
            elevated = elev.TokenIsElevated;
        CloseHandle(hToken);
    }
    return elevated != FALSE;
}

void Helper::autoElevate() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    ShellExecuteW(NULL, L"runas", path, GetCommandLineW(), NULL, SW_SHOW);
}

std::string Helper::runWMIC(const std::string& alias, const std::string& property) {
    std::string psResult = runPowerShell(alias + " " + property);
    if (!psResult.empty())
        return psResult;

    std::string cmd = "wmic " + alias + " get " + property + " /format:csv 2>nul";
    std::FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }
    char buf[512];
    std::string output;
    while (std::fgets(buf, sizeof(buf), pipe) != NULL) output += buf;
    _pclose(pipe);
    if (output.empty()) return "";
    std::stringstream ss(output);
    std::string line;
    bool first = true;
    while (std::getline(ss, line)) {
        if (first) { first = false; continue; }
        if (line.empty()) continue;
        size_t comma = line.find(',');
        if (comma != std::string::npos) {
            std::string val = trim(line.substr(comma + 1));
            if (!val.empty() && val != property) return val;
        }
    }
    return "";
}

std::string Helper::runPowerShell(const std::string& wmicArgs) {
    std::string psCmd;
    if (wmicArgs.find("baseboard") != std::string::npos)
        psCmd = "powershell -Command \"(Get-WmiObject Win32_BaseBoard).SerialNumber\" 2>nul";
    else if (wmicArgs.find("cpu") != std::string::npos && wmicArgs.find("ProcessorId") != std::string::npos)
        psCmd = "powershell -Command \"(Get-WmiObject Win32_Processor).ProcessorId\" 2>nul";
    else if (wmicArgs.find("diskdrive_multi") != std::string::npos)
        psCmd = "powershell -Command \"(Get-WmiObject Win32_DiskDrive).SerialNumber -join ','\" 2>nul";
    else if (wmicArgs.find("diskdrive") != std::string::npos)
        psCmd = "powershell -Command \"(Get-WmiObject Win32_DiskDrive).SerialNumber\" 2>nul";
    else if (wmicArgs.find("bios") != std::string::npos)
        psCmd = "powershell -Command \"(Get-WmiObject Win32_BIOS).SerialNumber\" 2>nul";
    else if (wmicArgs.find("nic_multi") != std::string::npos)
        psCmd = "powershell -Command \"(Get-NetAdapter | Where-Object {$_.Status -eq 'Up'}).MacAddress -join ','\" 2>nul";
    else if (wmicArgs.find("nic") != std::string::npos)
        psCmd = "powershell -Command \"(Get-NetAdapter | Where-Object {$_.Status -eq 'Up'}).MacAddress\" 2>nul";
    else if (wmicArgs.find("csproduct") != std::string::npos)
        psCmd = "powershell -Command \"(Get-WmiObject Win32_ComputerSystemProduct).UUID\" 2>nul";
    else
        return "";

    logWrite("[PS] Running: " + psCmd);
    std::FILE* pipe = _popen(psCmd.c_str(), "r");
    if (!pipe) {
        logWrite("[PS] Failed to run PowerShell");
        return "";
    }
    char buf[512];
    std::string output;
    while (std::fgets(buf, sizeof(buf), pipe) != NULL) output += buf;
    _pclose(pipe);
    std::string result = trim(output);
    logWrite("[PS] Result: '" + result + "'");
    return result;
}

void Helper::addHWID(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(dataMutex);
    g_hwids.push_back({name, value.empty() ? "N/A" : value});
    logWrite("[HWID] " + name + ": " + (value.empty() ? "N/A" : value));
}

void Helper::displayResults() {
    if (cliConfig.quiet) return;

    Color::setForegroundColor(Color::LightGray);

    if (!g_disks.empty()) {
        size_t maxModel = 5;
        for (const auto& d : g_disks)
            maxModel = (std::max)(maxModel, d.model.size());
        maxModel += 4;

        std::cout << "Model" << std::string(maxModel > 5 ? maxModel - 5 : 0, ' ') << "SerialNumber\n";
        for (const auto& d : g_disks) {
            Color::setForegroundColor(Color::Green);
            std::cout << d.model;
            size_t pad = maxModel - d.model.size();
            if (pad > 0) std::cout << std::string(pad, ' ');
            Color::setForegroundColor(Color::Yellow);
            std::cout << d.serial << "\n";
        }
        std::cout << "\n";
    }

    Color::setForegroundColor(Color::LightGray);
    std::cout << "CPU\nSerialNumber\n";
    if (g_cpuSerial.empty()) {
        Color::setForegroundColor(Color::Red);
        std::cout << "Unknown\n";
    } else {
        Color::setForegroundColor(Color::Yellow);
        std::cout << g_cpuSerial << "\n";
    }
    std::cout << "\n";

    Color::setForegroundColor(Color::LightGray);
    std::cout << "BIOS\nSerialNumber\n";
    Color::setForegroundColor(g_biosSerial.empty() ? Color::Red : Color::Yellow);
    std::cout << (g_biosSerial.empty() ? "N/A" : g_biosSerial) << "\n\n";

    Color::setForegroundColor(Color::LightGray);
    std::cout << "Motherboard\nSerialNumber\n";
    Color::setForegroundColor(g_moboSerial.empty() ? Color::Red : Color::Yellow);
    std::cout << (g_moboSerial.empty() ? "N/A" : g_moboSerial) << "\n\n";

    Color::setForegroundColor(Color::LightGray);
    std::cout << "smBIOS UUID\nUUID\n";
    Color::setForegroundColor(g_uuid.empty() ? Color::Red : Color::Yellow);
    std::cout << (g_uuid.empty() ? "N/A" : g_uuid) << "\n\n";

    if (!g_macs.empty()) {
        size_t maxAddr = 16;
        for (const auto& m : g_macs)
            maxAddr = (std::max)(maxAddr, m.address.size());
        maxAddr += 4;

        Color::setForegroundColor(Color::LightGray);
        std::cout << "Physical Address" << std::string(maxAddr > 16 ? maxAddr - 16 : 0, ' ') << "Transport Name\n";
        std::cout << "=========================================================\n";
        for (const auto& m : g_macs) {
            Color::setForegroundColor(Color::Green);
            std::cout << m.address;
            size_t pad = maxAddr - m.address.size();
            if (pad > 0) std::cout << std::string(pad, ' ');
            Color::setForegroundColor(Color::LightGray);
            std::cout << m.transport << "\n";
        }
    }

    Color::setForegroundColor(Color::LightGray);
}

void Helper::copyToClipboard() {
    if (!cliConfig.copy) return;
    std::string text;
    for (const auto& h : g_hwids)
        text += h.first + ": " + h.second + "\r\n";
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (hMem) {
            memcpy(GlobalLock(hMem), text.c_str(), text.size() + 1);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        }
        CloseClipboard();
        if (!cliConfig.quiet)
            std::cout << "[+] Results copied to clipboard\n";
    }
}

void Helper::exportResultsJSON() {
    if (cliConfig.exportPath.empty()) return;
    std::ofstream f(cliConfig.exportPath);
    if (!f.is_open()) {
        logWrite("[EXPORT] Failed to open: " + cliConfig.exportPath);
        if (!cliConfig.quiet)
            std::cout << "[!] Could not write to " << cliConfig.exportPath << "\n";
        return;
    }
    f << "{\n";
    f << "  \"timestamp\": \"" << escapeJSON(getTimestampISO()) << "\",\n";
    f << "  \"program\": \"" << escapeJSON(g_appName) << "\",\n";
    f << "  \"version\": \"" << escapeJSON(g_appVersion) << "\",\n";
    f << "  \"hwids\": {\n";
    for (size_t i = 0; i < g_hwids.size(); i++) {
        f << "    \"" << escapeJSON(g_hwids[i].first) << "\": \""
          << escapeJSON(g_hwids[i].second) << "\"";
        if (i < g_hwids.size() - 1) f << ",";
        f << "\n";
    }
    f << "  }\n";
    f << "}\n";
    f.close();
    logWrite("[EXPORT] Saved to " + cliConfig.exportPath);
}

void Helper::exportResultsCSV() {
    if (cliConfig.exportPath.empty()) return;
    std::ofstream f(cliConfig.exportPath);
    if (!f.is_open()) {
        logWrite("[EXPORT] Failed to open: " + cliConfig.exportPath);
        if (!cliConfig.quiet)
            std::cout << "[!] Could not write to " << cliConfig.exportPath << "\n";
        return;
    }
    f << "Name,Value\n";
    for (const auto& h : g_hwids) {
        std::string name = h.first;
        std::string value = h.second;
        if (name.find(',') != std::string::npos) name = "\"" + name + "\"";
        if (value.find(',') != std::string::npos) value = "\"" + value + "\"";
        f << name << "," << value << "\n";
    }
    f.close();
    logWrite("[EXPORT] Saved CSV to " + cliConfig.exportPath);
}

CLIConfig Helper::parseCLI(int argc, char* argv[]) {
    CLIConfig config;
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--help") { config.showHelp = true; return config; }
        else if (arg == "--version") { config.showVersion = true; return config; }
        else if (arg == "--quiet") config.quiet = true;
        else if (arg == "--headless") config.headless = true;
        else if (arg == "--no-update") config.noUpdate = true;
        else if (arg == "--copy") config.copy = true;
        else if (arg == "--export" && i + 1 < argc) config.exportPath = argv[++i];
    }
    return config;
}

void Helper::showHelp() {
    std::cout << g_appName << " v" << g_appVersion << " - Hardware ID Collector\n\n";
    std::cout << "Usage: HWIDChecker.exe [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --help              Show this help message\n";
    std::cout << "  --version           Show version\n";
    std::cout << "  --quiet             Only output HWID values\n";
    std::cout << "  --headless          Skip prompts, useful for scripts\n";
    std::cout << "  --no-update         Skip auto-update check\n";
    std::cout << "  --copy              Copy results to clipboard\n";
    std::cout << "  --export FILE       Export to FILE (.json or .csv)\n\n";
    std::cout << "Requires Administrator privileges.\n";
}

void Helper::showVersion() {
    std::cout << g_appName << " v" << g_appVersion << "\n";
}

void Helper::initLogging() {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring dir = std::wstring(tempPath) + L"HWID-Checker";
    CreateDirectoryW(dir.c_str(), NULL);

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time);
    char filename[64];
    snprintf(filename, sizeof(filename), "hwid-%04d-%02d-%02d-%02d%02d.log",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);

    std::wstring logPath = dir + L"\\" + std::wstring(filename, filename + strlen(filename));
    int len = WideCharToMultiByte(CP_UTF8, 0, logPath.c_str(), -1, NULL, 0, NULL, NULL);
    g_logPath.resize(len - 1);
    WideCharToMultiByte(CP_UTF8, 0, logPath.c_str(), -1, &g_logPath[0], len, NULL, NULL);

    logFile.open(logPath, std::ios::out | std::ios::app);
    if (logFile.is_open()) {
        logEnabled = true;
        logWrite("=== " + g_appName + " v" + g_appVersion + " started at " + getTimestampISO() + " ===");
    }
}

void Helper::closeLogging() {
    if (logEnabled && logFile.is_open()) {
        logWrite("=== " + g_appName + " finished ===");
        logFile.close();
        logEnabled = false;
    }
}

void Helper::logWrite(const std::string& message) {
    if (!logEnabled || !logFile.is_open()) return;
    std::lock_guard<std::mutex> lock(logMutex);
    logFile << message << std::endl;
    logFile.flush();
}

std::string Helper::fetchURL(const std::wstring& url) {
    std::string result;
    HINTERNET hSession = WinHttpOpen(L"HWID-Checker/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        logWrite("[UPDATE] WinHttpOpen failed");
        return "";
    }

    URL_COMPONENTS urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t host[256] = {0}, path[1024] = {0};
    urlComp.lpszHostName = host;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = path;
    urlComp.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &urlComp)) {
        logWrite("[UPDATE] WinHttpCrackUrl failed");
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host, urlComp.nPort, 0);
    if (!hConnect) {
        logWrite("[UPDATE] WinHttpConnect failed");
        WinHttpCloseHandle(hSession);
        return "";
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        logWrite("[UPDATE] WinHttpOpenRequest failed");
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    WinHttpReceiveResponse(hRequest, NULL);

    DWORD bytesRead = 0;
    char buffer[4096];
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer) - 1, &bytesRead)) {
        if (bytesRead == 0) break;
        buffer[bytesRead] = '\0';
        result += buffer;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (result.empty())
        logWrite("[UPDATE] Empty response from GitHub");
    return result;
}

void Checks::checkForUpdate() {
    Helper::logWrite("[UPDATE] Checking for updates...");
    std::string json = Helper::fetchURL(L"https://api.github.com/repos/" +
        std::wstring(Helper::g_repoUrl.begin(), Helper::g_repoUrl.end()) + L"/releases/latest");

    if (json.empty()) {
        if (!Helper::cliConfig.quiet)
            std::cout << "[!] Could not check for updates\n";
        return;
    }

    std::string tag;
    auto tagPos = json.find("\"tag_name\":\"");
    if (tagPos != std::string::npos) {
        tagPos += 12;
        auto endPos = json.find("\"", tagPos);
        if (endPos != std::string::npos)
            tag = json.substr(tagPos, endPos - tagPos);
    }

    if (tag.empty()) {
        Helper::logWrite("[UPDATE] Could not parse tag from response");
        return;
    }

    Helper::logWrite("[UPDATE] Latest: " + tag + ", Current: v" + Helper::g_appVersion);

    if (tag != "v" + Helper::g_appVersion && tag != Helper::g_appVersion) {
        Color::setForegroundColor(Color::Yellow);
        std::cout << "[!] New version available: " << tag << "\n";
        Color::setForegroundColor(Color::LightGray);
    }
}

void Color::setForegroundColor(const RGBColor& aColor) {
    printf("\x1b[38;2;%d;%d;%dm", aColor.r, aColor.g, aColor.b);
}

void Checks::collectAll() {
    Helper::logWrite("[PS] Running collectAll...");

    char tmpBuf[MAX_PATH] = {0};
    GetEnvironmentVariableA("TEMP", tmpBuf, MAX_PATH);
    std::string tmpPath = strlen(tmpBuf) > 0 ? tmpBuf : "C:\\Windows\\Temp";
    std::string scriptPath = tmpPath + "\\hwid_collect.ps1";

    {
        std::ofstream script(scriptPath);
        script << R"(
$mb = (Get-CimInstance Win32_BaseBoard -ErrorAction SilentlyContinue).SerialNumber
$cpu = (Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue).ProcessorId
$bios = (Get-CimInstance Win32_BIOS -ErrorAction SilentlyContinue).SerialNumber
$uuid = (Get-CimInstance Win32_ComputerSystemProduct -ErrorAction SilentlyContinue).UUID
$disks = Get-CimInstance Win32_DiskDrive -ErrorAction SilentlyContinue | ForEach-Object { $_.Model + '|' + $_.SerialNumber }
$macs = Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object { $_.Status -eq 'Up' } | ForEach-Object { $_.MacAddress + '|' + $_.InterfaceDescription }
Write-Output "===MB==="
if ($mb) { Write-Output $mb }
Write-Output "===CPU==="
if ($cpu) { Write-Output $cpu }
Write-Output "===BIOS==="
if ($bios) { Write-Output $bios }
Write-Output "===UUID==="
if ($uuid) { Write-Output $uuid }
Write-Output "===DISKS==="
if ($disks) { $disks | ForEach-Object { Write-Output $_ } }
Write-Output "===MACS==="
if ($macs) { $macs | ForEach-Object { Write-Output $_ } }
Write-Output "===END==="
)";
    }

    std::string psCmd = "powershell -NoProfile -ExecutionPolicy Bypass -File \"" + scriptPath + "\" 2>nul";
    std::FILE* pipe = _popen(psCmd.c_str(), "r");
    if (!pipe) {
        Helper::logWrite("[PS] Failed to run collectAll");
        return;
    }

    char buf[512];
    std::string output;
    while (std::fgets(buf, sizeof(buf), pipe) != NULL) output += buf;
    _pclose(pipe);

    DeleteFileA(scriptPath.c_str());

    Helper::logWrite("[PS] collectAll raw output length: " + std::to_string(output.size()));
    Helper::logWrite("[PS] collectAll output:\n" + output);

    auto extract = [&](const std::string& marker) -> std::string {
        auto start = output.find(marker);
        if (start == std::string::npos) return "";
        start += marker.size();
        while (start < output.size() && (output[start] == '\r' || output[start] == '\n')) start++;
        auto end = output.find("===", start);
        if (end == std::string::npos) end = output.size();
        std::string val = output.substr(start, end - start);
        while (!val.empty() && (val.back() == '\r' || val.back() == '\n')) val.pop_back();
        while (!val.empty() && (val.front() == '\r' || val.front() == '\n')) val.erase(val.begin());
        return val;
    };

    Helper::g_moboSerial = extract("===MB===");
    Helper::g_cpuSerial = extract("===CPU===");
    std::string biosRaw = extract("===BIOS===");
    std::string biosLower = biosRaw;
    std::transform(biosLower.begin(), biosLower.end(), biosLower.begin(), ::tolower);
    if (biosLower.find("o.e.m") != std::string::npos || biosLower.find("oem") != std::string::npos ||
        biosLower.find("to be filled") != std::string::npos || biosLower.find("system") != std::string::npos ||
        biosLower.find("default") != std::string::npos)
        biosRaw.clear();
    Helper::g_biosSerial = biosRaw;
    Helper::g_uuid = extract("===UUID===");

    std::string disksRaw = extract("===DISKS===");
    std::stringstream dss(disksRaw);
    std::string dline;
    while (std::getline(dss, dline)) {
        dline = Helper::trim(dline);
        if (dline.empty()) continue;
        size_t sep = dline.find('|');
        if (sep != std::string::npos) {
            DiskInfo info;
            info.model = Helper::trim(dline.substr(0, sep));
            info.serial = Helper::trim(dline.substr(sep + 1));
            if (!info.model.empty()) Helper::g_disks.push_back(info);
        }
    }
    if (Helper::g_disks.empty()) { DiskInfo f; f.model = "Unknown"; f.serial = "N/A"; Helper::g_disks.push_back(f); }

    std::string macsRaw = extract("===MACS===");
    std::stringstream mss(macsRaw);
    std::string mline;
    while (std::getline(mss, mline)) {
        mline = Helper::trim(mline);
        if (mline.empty()) continue;
        size_t sep = mline.find('|');
        if (sep != std::string::npos) {
            MACInfo info;
            info.address = Helper::trim(mline.substr(0, sep));
            info.transport = Helper::trim(mline.substr(sep + 1));
            if (!info.address.empty()) Helper::g_macs.push_back(info);
        }
    }
    if (Helper::g_macs.empty()) { MACInfo f; f.address = "N/A"; f.transport = "N/A"; Helper::g_macs.push_back(f); }

    Helper::addHWID("Motherboard Serial", Helper::g_moboSerial);
    Helper::addHWID("CPU ID", Helper::g_cpuSerial);
    Helper::addHWID("BIOS Serial", Helper::g_biosSerial);
    Helper::addHWID("System UUID", Helper::g_uuid);
    for (const auto& d : Helper::g_disks) Helper::addHWID("Disk: " + d.model, d.serial);
    for (const auto& m : Helper::g_macs) Helper::addHWID("MAC: " + m.address, m.transport);

    Helper::logWrite("[PS] collectAll done - " + std::to_string(Helper::g_hwids.size()) + " entries");
}

void Checks::collectMotherboardSerial() {
    std::string val = Helper::runWMIC("baseboard", "SerialNumber");
    Helper::g_moboSerial = val;
    Helper::addHWID("Motherboard Serial", val);
}

void Checks::collectCPUId() {
    std::string val = Helper::runWMIC("cpu", "ProcessorId");
    Helper::logWrite("[PS] Running CPU serial collection...");
    
    std::string serial = "";
    std::string psCmd = "powershell -Command \"(Get-CimInstance Win32_Processor).ProcessorId\" 2>nul";
    std::FILE* pipe = _popen(psCmd.c_str(), "r");
    if (pipe) {
        char buf[512];
        std::string output;
        while (std::fgets(buf, sizeof(buf), pipe) != NULL) output += buf;
        _pclose(pipe);
        serial = Helper::trim(output);
    }
    
    if (serial.empty()) {
        std::string wmicCmd = "wmic cpu get ProcessorId /format:csv 2>nul";
        std::FILE* wpipe = _popen(wmicCmd.c_str(), "r");
        if (wpipe) {
            char buf[512];
            std::string output;
            while (std::fgets(buf, sizeof(buf), wpipe) != NULL) output += buf;
            _pclose(wpipe);
            std::stringstream ss(output);
            std::string line;
            bool first = true;
            while (std::getline(ss, line)) {
                if (first) { first = false; continue; }
                if (line.empty()) continue;
                size_t comma = line.find(',');
                if (comma != std::string::npos) {
                    std::string v = Helper::trim(line.substr(comma + 1));
                    if (!v.empty() && v != "ProcessorId") { serial = v; break; }
                }
            }
        }
    }
    
    Helper::g_cpuSerial = serial;
    Helper::addHWID("CPU ID", val);
}

void Checks::collectDiskSerial() {
    std::string psCmd = "powershell -Command \"Get-WmiObject Win32_DiskDrive | ForEach-Object { $_.Model + '|' + $_.SerialNumber }\" 2>nul";
    Helper::logWrite("[PS] Running disk collection...");
    std::FILE* pipe = _popen(psCmd.c_str(), "r");
    if (pipe) {
        char buf[512];
        std::string output;
        while (std::fgets(buf, sizeof(buf), pipe) != NULL) output += buf;
        _pclose(pipe);
        std::stringstream ss(output);
        std::string line;
        while (std::getline(ss, line)) {
            line = Helper::trim(line);
            if (line.empty()) continue;
            size_t sep = line.find('|');
            if (sep != std::string::npos) {
                DiskInfo info;
                info.model = Helper::trim(line.substr(0, sep));
                info.serial = Helper::trim(line.substr(sep + 1));
                if (!info.model.empty()) {
                    Helper::g_disks.push_back(info);
                    Helper::logWrite("[DISK] " + info.model + " | " + info.serial);
                }
            }
        }
    }

    if (Helper::g_disks.empty()) {
        std::string cmd = "wmic diskdrive get Model,SerialNumber /format:csv 2>nul";
        std::FILE* wpipe = _popen(cmd.c_str(), "r");
        if (wpipe) {
            char buf[512];
            std::string output;
            while (std::fgets(buf, sizeof(buf), wpipe) != NULL) output += buf;
            _pclose(wpipe);
            std::stringstream ss(output);
            std::string line;
            bool first = true;
            while (std::getline(ss, line)) {
                if (first) { first = false; continue; }
                if (line.empty()) continue;
                std::stringstream ls(line);
                std::string cell;
                std::vector<std::string> cells;
                while (std::getline(ls, cell, ',')) cells.push_back(Helper::trim(cell));
                if (cells.size() >= 3 && !cells[1].empty() && cells[1] != "Model") {
                    DiskInfo info;
                    info.model = cells[1];
                    info.serial = cells[2];
                    Helper::g_disks.push_back(info);
                }
            }
        }
    }

    if (Helper::g_disks.empty()) {
        DiskInfo fallback;
        fallback.model = "Unknown";
        fallback.serial = "N/A";
        Helper::g_disks.push_back(fallback);
    }

    for (const auto& d : Helper::g_disks)
        Helper::addHWID("Disk: " + d.model, d.serial);
}

void Checks::collectBIOSSerial() {
    std::string val = Helper::runWMIC("bios", "SerialNumber");
    std::string lower;
    lower.resize(val.size());
    std::transform(val.begin(), val.end(), lower.begin(), ::tolower);
    if (lower.find("o.e.m") != std::string::npos || lower.find("oem") != std::string::npos ||
        lower.find("to be filled") != std::string::npos || lower.find("system") != std::string::npos ||
        lower.find("default") != std::string::npos) {
        val.clear();
    }
    Helper::g_biosSerial = val;
    Helper::addHWID("BIOS Serial", val);
}

void Checks::collectMAC() {
    std::string getmacCmd = "getmac /v /fo list 2>nul";
    Helper::logWrite("[PS] Running MAC collection via getmac...");
    std::FILE* pipe = _popen(getmacCmd.c_str(), "r");
    if (pipe) {
        char buf[512];
        std::string output;
        while (std::fgets(buf, sizeof(buf), pipe) != NULL) output += buf;
        _pclose(pipe);
        std::stringstream ss(output);
        std::string line;
        std::string curAddr;
        while (std::getline(ss, line)) {
            line = Helper::trim(line);
            if (line.empty()) continue;
            if (line.find("Physical Address") != std::string::npos) {
                size_t colon = line.find(':');
                if (colon != std::string::npos)
                    curAddr = Helper::trim(line.substr(colon + 1));
            } else if (line.find("Transport Name") != std::string::npos) {
                size_t colon = line.find(':');
                if (colon != std::string::npos && !curAddr.empty()) {
                    MACInfo info;
                    info.address = curAddr;
                    info.transport = Helper::trim(line.substr(colon + 1));
                    Helper::g_macs.push_back(info);
                    Helper::logWrite("[MAC] " + info.address + " | " + info.transport);
                    curAddr.clear();
                }
            }
        }
    }

    if (Helper::g_macs.empty()) {
        std::string psCmd = "powershell -Command \"Get-NetAdapter | ForEach-Object { $_.MacAddress + '|' + $_.InterfaceDescription }\" 2>nul";
        std::FILE* ppipe = _popen(psCmd.c_str(), "r");
        if (ppipe) {
            char buf[512];
            std::string output;
            while (std::fgets(buf, sizeof(buf), ppipe) != NULL) output += buf;
            _pclose(ppipe);
            std::stringstream ss(output);
            std::string line;
            while (std::getline(ss, line)) {
                line = Helper::trim(line);
                if (line.empty()) continue;
                size_t sep = line.find('|');
                if (sep != std::string::npos) {
                    MACInfo info;
                    info.address = Helper::trim(line.substr(0, sep));
                    info.transport = Helper::trim(line.substr(sep + 1));
                    if (!info.address.empty())
                        Helper::g_macs.push_back(info);
                }
            }
        }
    }

    if (Helper::g_macs.empty()) {
        MACInfo fallback;
        fallback.address = "N/A";
        fallback.transport = "N/A";
        Helper::g_macs.push_back(fallback);
    }

    for (const auto& m : Helper::g_macs)
        Helper::addHWID("MAC: " + m.address, m.transport);
}

void Checks::collectUUID() {
    std::string val = Helper::runWMIC("csproduct", "UUID");
    Helper::g_uuid = val;
    Helper::addHWID("System UUID", val);
}

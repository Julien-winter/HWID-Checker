#include "Functions.h"

namespace Helper {
    std::mutex consoleMutex;
    CLIConfig cliConfig;
    std::vector<std::pair<std::string, std::string>> g_hwids;
    std::string g_appName = "HWID Checker";
    std::string g_appVersion = "1.0.0";
    std::string g_repoUrl = "Julien-winter/HWID-Checker";
}

std::string Helper::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
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

std::string Helper::runWMIC(const std::string& alias, const std::string& property) {
    std::string cmd = "wmic " + alias + " get " + property + " /format:csv 2>nul";
    std::FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return "";
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

void Helper::printHWID(const std::string& name, const std::string& value) {
    g_hwids.push_back({name, value.empty() ? "N/A" : value});
}

void Helper::displayResults() {
    if (cliConfig.quiet) return;
    Color::setForegroundColor(Color::Cyan);
    std::cout << "\n====================== HWID Checker ======================\n";
    for (const auto& h : g_hwids) {
        Color::setForegroundColor(Color::Yellow);
        std::cout << h.first << ": ";
        Color::setForegroundColor(h.second == "N/A" ? Color::Red : Color::Green);
        std::cout << h.second << "\n";
    }
    Color::setForegroundColor(Color::Cyan);
    std::cout << "========================================================\n";
    Color::setForegroundColor(Color::LightGray);
}

void Helper::exportResultsJSON() {
    if (cliConfig.exportPath.empty()) return;
    std::ofstream f(cliConfig.exportPath);
    if (!f.is_open()) return;
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
}

CLIConfig Helper::parseCLI(int argc, char* argv[]) {
    CLIConfig config;
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--help") { config.showHelp = true; return config; }
        else if (arg == "--quiet") config.quiet = true;
        else if (arg == "--no-update") config.noUpdate = true;
        else if (arg == "--export" && i + 1 < argc) config.exportPath = argv[++i];
    }
    return config;
}

std::string Helper::fetchURL(const std::wstring& url) {
    std::string result;
    HINTERNET hSession = WinHttpOpen(L"HWID-Checker/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    URL_COMPONENTS urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t host[256] = {0}, path[1024] = {0};
    urlComp.lpszHostName = host;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = path;
    urlComp.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &urlComp)) {
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host, urlComp.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

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
    return result;
}

void Checks::checkForUpdate() {
    std::string json = Helper::fetchURL(L"https://api.github.com/repos/" +
        std::wstring(Helper::g_repoUrl.begin(), Helper::g_repoUrl.end()) + L"/releases/latest");

    if (json.empty()) return;

    std::string tag;
    auto tagPos = json.find("\"tag_name\":\"");
    if (tagPos != std::string::npos) {
        tagPos += 12;
        auto endPos = json.find("\"", tagPos);
        if (endPos != std::string::npos)
            tag = json.substr(tagPos, endPos - tagPos);
    }

    if (tag.empty()) return;

    if (tag != "v" + Helper::g_appVersion && tag != Helper::g_appVersion) {
        Color::setForegroundColor(Color::Yellow);
        std::cout << "[!] New version available: " << tag << "\n";
        Color::setForegroundColor(Color::LightGray);
    }
}

void Helper::showHelp() {
    std::cout << g_appName << " v" << g_appVersion << " - Hardware ID Collector\n\n";
    std::cout << "Usage: HWIDChecker.exe [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --help              Show this help message\n";
    std::cout << "  --quiet             Only output HWID values\n";
    std::cout << "  --export FILE       Export as JSON to FILE\n";
    std::cout << "  --no-update         Skip auto-update check\n";
}

void Color::setForegroundColor(const RGBColor& aColor) {
    printf("\x1b[38;2;%d;%d;%dm", aColor.r, aColor.g, aColor.b);
}

void Checks::collectMotherboardSerial() {
    std::string val = Helper::runWMIC("baseboard", "SerialNumber");
    Helper::printHWID("Motherboard Serial", val);
}

void Checks::collectCPUId() {
    std::string val = Helper::runWMIC("cpu", "ProcessorId");
    Helper::printHWID("CPU ID", val);
}

void Checks::collectDiskSerial() {
    std::string val = Helper::runWMIC("diskdrive", "SerialNumber");
    Helper::printHWID("Disk Serial", val);
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
    Helper::printHWID("BIOS Serial", val);
}

void Checks::collectMAC() {
    std::string val = Helper::runWMIC("nic where NetEnabled=true", "MACAddress");
    Helper::printHWID("MAC Address", val);
}

void Checks::collectUUID() {
    std::string val = Helper::runWMIC("csproduct", "UUID");
    Helper::printHWID("System UUID", val);
}

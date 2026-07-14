#include "Includes.h"

int main(int argc, char* argv[]) {
    Helper::setupConsole();
    Helper::cliConfig = Helper::parseCLI(argc, argv);

    if (Helper::cliConfig.showHelp) {
        Helper::showHelp();
        return 0;
    }

    if (Helper::cliConfig.showVersion) {
        Helper::showVersion();
        return 0;
    }

    if (!Helper::isAdmin()) {
        Color::setForegroundColor(Color::Yellow);
        std::cout << "[!] Administrator privileges required. Restarting...\n";
        Helper::autoElevate();
        return 0;
    }

    Helper::initLogging();

    bool firstRun = true;
    while (true) {
        if (!firstRun) {
            Helper::g_hwids.clear();
            Helper::g_disks.clear();
            Helper::g_macs.clear();
            Helper::g_cpuSerial.clear();
            Helper::g_biosSerial.clear();
            Helper::g_moboSerial.clear();
            Helper::g_uuid.clear();
            system("cls");
        }

        Checks::collectMotherboardSerial();
        Checks::collectCPUId();
        Checks::collectDiskSerial();
        Checks::collectBIOSSerial();
        Checks::collectMAC();
        Checks::collectUUID();

        Helper::displayResults();
        Helper::copyToClipboard();

        if (firstRun && !Helper::cliConfig.exportPath.empty()) {
            std::string ext = Helper::cliConfig.exportPath;
            size_t dot = ext.rfind('.');
            if (dot != std::string::npos) {
                std::string extLower = ext.substr(dot + 1);
                std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
                if (extLower == "csv")
                    Helper::exportResultsCSV();
                else
                    Helper::exportResultsJSON();
            } else {
                Helper::exportResultsJSON();
            }
        }

        if (firstRun && !Helper::cliConfig.noUpdate)
            Checks::checkForUpdate();

        firstRun = false;

        if (Helper::cliConfig.headless)
            break;

        std::cout << "\nPress any key to get your hardware serials again...";
        std::cin.get();
    }

    Helper::closeLogging();

    return 0;
}

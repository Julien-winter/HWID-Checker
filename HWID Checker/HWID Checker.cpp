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

    Checks::collectMotherboardSerial();
    Checks::collectCPUId();
    Checks::collectDiskSerial();
    Checks::collectBIOSSerial();
    Checks::collectMAC();
    Checks::collectUUID();

    Helper::displayResults();
    Helper::copyToClipboard();

    if (!Helper::cliConfig.exportPath.empty()) {
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

    if (!Helper::cliConfig.noUpdate)
        Checks::checkForUpdate();

    Helper::closeLogging();

    if (!Helper::cliConfig.headless) {
        std::cout << "\nPress any key to exit...";
        std::cin.get();
    }

    return 0;
}

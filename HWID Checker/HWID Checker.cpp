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

    Helper::generateFingerprint();
    Helper::displayResults();

    if (!Helper::cliConfig.exportPath.empty())
        Helper::exportResultsJSON();

    if (!Helper::cliConfig.noUpdate)
        Checks::checkForUpdate();

    Helper::closeLogging();

    if (!Helper::cliConfig.headless) {
        std::cout << "\nPress any key to exit...";
        std::cin.get();
    }

    return 0;
}

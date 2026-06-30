#include "Includes.h"

int main(int argc, char* argv[]) {
    Helper::setupConsole();
    Helper::cliConfig = Helper::parseCLI(argc, argv);

    if (Helper::cliConfig.showHelp) {
        Helper::showHelp();
        return 0;
    }

    if (!Helper::isAdmin()) {
        Color::setForegroundColor(Color::Red);
        std::cout << "[!] Administrator privileges required.\n";
        return 1;
    }

    Checks::collectMotherboardSerial();
    Checks::collectCPUId();
    Checks::collectDiskSerial();
    Checks::collectBIOSSerial();
    Checks::collectMAC();
    Checks::collectUUID();

    Helper::displayResults();

    if (!Helper::cliConfig.exportPath.empty())
        Helper::exportResultsJSON();

    if (!Helper::cliConfig.noUpdate)
        Checks::checkForUpdate();

    Color::setForegroundColor(Color::LightGray);
    std::cout << "\nPress any key to exit...";
    std::cin.get();
    return 0;
}

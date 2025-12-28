// src/main.cpp
#include <iostream>
#include <string>
#include "BackupEngine.h"

void printUsage() {
    std::cout << "Usage:\n"
              << "  minibackup backup <src_dir> <backup_dir>\n"
              << "  minibackup restore <backup_dir> <target_dir>\n"
              << "  minibackup verify <backup_dir>\n";
}

int main(const int argc, char* argv[]) {
    // 程序名 命令 参数1
    if (argc < 3) {
        printUsage();
        return 1;
    }

    const std::string command = argv[1];

    try {
        if (command == "backup") {
            if (argc < 4) {
                std::cerr << "[Error] Missing arguments for backup.\n";
                printUsage();
                return 1;
            }
            const std::string src = argv[2];
            const std::string dest = argv[3];

            BackupEngine::backup(src, dest);

        } else if (command == "restore") {
            if (argc < 4) {
                std::cerr << "[Error] Missing arguments for restore.\n";
                printUsage();
                return 1;
            }
            const std::string src = argv[2];
            const std::string dest = argv[3];

            BackupEngine::restore(src, dest);

        } else if (command == "verify") {

            if (const std::string dest = argv[2]; BackupEngine::verify(dest)) {
                std::cout << "Verification Passed: All files match the index." << std::endl;
                return 0;
            }
            std::cerr << "Verification Failed: Backup corrupted or incomplete." << std::endl;
            return -1;
        } else {
            std::cerr << "[Error] Unknown command: " << command << "\n";
            printUsage();
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
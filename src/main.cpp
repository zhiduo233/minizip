// src/main.cpp
#include <iostream>
#include <string>
#include "BackupEngine.h"

void printUsage() {
    std::cout << "Usage:\n"
              << "  minibackup backup <src> <dest>\n"
              << "  minibackup restore <src> <dest>\n"
              << "  minibackup verify <src> <dest>\n";
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printUsage();
        return 1;
    }

    std::string command = argv[1];
    std::string src = argv[2];
    std::string dest = argv[3];

    try {
        if (command == "backup") {
            std::cout << "Starting Backup..." << std::endl;
            BackupEngine::copyDirectory(src, dest);
        } else if (command == "restore") {
            std::cout << "Starting Restore..." << std::endl;
            // 还原逻辑和备份是一样的，只是在语义上区分
            BackupEngine::copyDirectory(src, dest);
        } else if (command == "verify") {
            std::cout << "Verifying..." << std::endl;
            bool result = BackupEngine::verifyBackup(src, dest);
            return result ? 0 : -1; // 返回 0 表示成功，非 0 失败（给脚本判断用）
        } else {
            printUsage();
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Operation failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
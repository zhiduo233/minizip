// src/main.cpp
#include <iostream>
#include <string>
#include "BackupEngine.h"

void printUsage() {
    std::cout << "Usage:\n"
              << "  minibackup backup <src_dir> <backup_dir>\n"
              << "  minibackup restore <backup_dir> <target_dir>\n"
              << "  minibackup verify <backup_dir>\n"
              << "  minibackup pack <src_dir> <output_file.pck>\n"   // [新增]
              << "  minibackup unpack <input_file.pck> <dest_dir>\n"; // [新增]
}

int main(const int argc, char* argv[]) {
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
            BackupEngine::backup(argv[2], argv[3]);

        } else if (command == "restore") {
            if (argc < 4) {
                std::cerr << "[Error] Missing arguments for restore.\n";
                printUsage();
                return 1;
            }
            BackupEngine::restore(argv[2], argv[3]);

        } else if (command == "verify") {
            // verify 只需要 1 个路径参数
            if (BackupEngine::verify(argv[2])) {
                std::cout << "Verification Passed: All files match the index." << std::endl;
                return 0;
            } else {
                std::cerr << "Verification Failed: Backup corrupted or incomplete." << std::endl;
                return -1;
            }

        } else if (command == "pack") {
            // [新增] 打包命令
            if (argc < 4) {
                std::cerr << "[Error] Missing arguments for pack.\n";
                printUsage();
                return 1;
            }
            // 参数2是源目录，参数3是目标文件
            BackupEngine::pack(argv[2], argv[3]);

        } else if (command == "unpack") {
            // [新增] 解包命令
            if (argc < 4) {
                std::cerr << "[Error] Missing arguments for unpack.\n";
                printUsage();
                return 1;
            }
            // 参数2是包文件，参数3是解压目录
            BackupEngine::unpack(argv[2], argv[3]);

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
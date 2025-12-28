// src/BackupEngine.cpp

#include "BackupEngine.h"
#include "CRC32.h"
#include <iostream>
#include <fstream>
#include <sstream>

// === 1. 备份：增加容错机制，跳过坏文件 ===
void BackupEngine::backup(const std::string& srcPath, const std::string& destPath) {
    fs::path source(srcPath);
    fs::path destination(destPath);

    if (!fs::exists(source)) throw std::runtime_error("Source not found");
    if (!fs::exists(destination)) fs::create_directories(destination);

    std::ofstream indexFile(destination / "index.txt");
    if (!indexFile.is_open()) throw std::runtime_error("Cannot create index file");

    std::cout << "Scanning and backing up..." << std::endl;

    int successCount = 0;
    int failCount = 0;

    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        try {
            // 计算相对路径
            fs::path relativePath = fs::relative(entry.path(), source);
            fs::path targetPath = destination / relativePath;

            if (fs::is_directory(entry.path())) {
                fs::create_directories(targetPath);
            } else {
                // A. 物理复制
                fs::copy_file(entry.path(), targetPath, fs::copy_options::overwrite_existing);

                // B. 计算 CRC
                std::string checksum = CRC32::getFileCRC(entry.path().string());

                // C. 写入清单
                indexFile << relativePath.string() << "|" << checksum << "\n";

                std::cout << "  [OK] " << relativePath.string() << std::endl;
                successCount++;
            }
        } catch (const std::exception& e) {
            // 捕获单个文件的错误，不中断整个循环
            std::cerr << "  [SKIP] Failed to backup: " << entry.path().string()
                      << "\n         Reason: " << e.what() << std::endl;
            failCount++;
        }
    }

    indexFile.close();
    std::cout << "[Backup] Complete. Success: " << successCount << ", Failed: " << failCount << std::endl;
}

// === 2. 验证：发现错误不退出，检查完所有文件 ===
bool BackupEngine::verify(const std::string& destPath) {
    fs::path destination(destPath);
    fs::path indexFilePath = destination / "index.txt";

    if (!fs::exists(indexFilePath)) {
        std::cerr << "[Error] Index file missing. Cannot verify." << std::endl;
        return false;
    }

    std::ifstream indexFile(indexFilePath);
    std::string line;

    int errorCount = 0;
    int checkedCount = 0;

    std::cout << "Verifying backup integrity..." << std::endl;

    while (std::getline(indexFile, line)) {
        if (line.empty()) continue;

        // 解析格式
        size_t delimiterPos = line.find('|');
        if (delimiterPos == std::string::npos) continue;

        std::string relPath = line.substr(0, delimiterPos);
        std::string expectedCRC = line.substr(delimiterPos + 1);
        fs::path currentFile = destination / relPath;

        checkedCount++;

        try {
            // 检查1: 文件是否存在
            if (!fs::exists(currentFile)) {
                std::cerr << "[MISSING] " << relPath << std::endl;
                errorCount++;
                continue; // 继续下一个循环
            }

            // 检查2: CRC 是否一致
            if (std::string currentCRC = CRC32::getFileCRC(currentFile.string()); currentCRC != expectedCRC) {
                std::cerr << "[CORRUPT] " << relPath
                          << " (Expected: " << expectedCRC << ", Actual: " << currentCRC << ")" << std::endl;
                errorCount++;
            }
            // 如果正常，什么都不打印，保持安静

        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Cannot read file: " << relPath << " (" << e.what() << ")" << std::endl;
            errorCount++;
        }
    }

    if (errorCount == 0) {
        std::cout << "[Verify] Passed. " << checkedCount << " files checked." << std::endl;
        return true;
    }
    std::cerr << "[Verify] FAILED. Found " << errorCount << " errors." << std::endl;
    return false;
}

// === 3. 还原：同样增加容错 ===
void BackupEngine::restore(const std::string& srcPath, const std::string& destPath) {
    const fs::path backupDir(srcPath);
    const fs::path targetDir(destPath);

    if (!fs::exists(targetDir)) fs::create_directories(targetDir);

    int failCount = 0;

    for (const auto& entry : fs::recursive_directory_iterator(backupDir)) {
        try {
            fs::path relativePath = fs::relative(entry.path(), backupDir);
            fs::path targetPath = targetDir / relativePath;

            if (relativePath.filename() == "index.txt") continue;

            if (fs::is_directory(entry.path())) {
                fs::create_directories(targetPath);
            } else {
                fs::copy_file(entry.path(), targetPath, fs::copy_options::overwrite_existing);
            }
        } catch (const std::exception& e) {
            std::cerr << "  [Restore Error] " << entry.path().string() << ": " << e.what() << std::endl;
            failCount++;
        }
    }

    if (failCount > 0) {
        std::cout << "[Restore] Completed with " << failCount << " errors." << std::endl;
    } else {
        std::cout << "[Restore] Complete successfully." << std::endl;
    }
}
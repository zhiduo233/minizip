//
// Created by 27450 on 2025/12/28.
//

// src/BackupEngine.cpp
#include "BackupEngine.h"
#include <iostream>
#include <fstream>
#include <exception>

// 实现复制目录（备份/还原通用）
void BackupEngine::copyDirectory(const std::string& srcPath, const std::string& destPath) {
    try {
        fs::path source(srcPath);
        fs::path destination(destPath);

        // 检查源是否存在
        if (!fs::exists(source)) {
            throw std::runtime_error("Source directory does not exist: " + srcPath);
        }

        // 也就是 cp -r 的逻辑
        // copy_options::recursive: 递归复制
        // copy_options::overwrite_existing: 覆盖已存在文件（适合全量备份/还原）
        fs::copy(source, destination,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);

        std::cout << "[Success] Copied from " << srcPath << " to " << destPath << std::endl;

    } catch (fs::filesystem_error& e) {
        std::cerr << "[Error] Filesystem error: " << e.what() << std::endl;
        throw; // 抛出异常供上层处理
    } catch (std::exception& e) {
        std::cerr << "[Error] " << e.what() << std::endl;
        throw;
    }
}

// 实现文件内容比对
bool BackupEngine::compareFiles(const fs::path& p1, const fs::path& p2) {
    std::ifstream f1(p1, std::ifstream::binary | std::ifstream::ate);
    std::ifstream f2(p2, std::ifstream::binary | std::ifstream::ate);

    if (f1.fail() || f2.fail()) return false;

    // 1. 先比大小，速度最快
    if (f1.tellg() != f2.tellg()) return false;

    // 2. 大小一样，则回滚到文件头，逐字节比对 (或者使用缓冲区比对)
    f1.seekg(0, std::ifstream::beg);
    f2.seekg(0, std::ifstream::beg);

    // 使用迭代器进行流比较
    return std::equal(std::istreambuf_iterator<char>(f1.rdbuf()),
                      std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(f2.rdbuf()));
}

// 实现验证逻辑
bool BackupEngine::verifyBackup(const std::string& srcPath, const std::string& destPath) {
    fs::path source(srcPath);
    fs::path destination(destPath);

    if (!fs::exists(source) || !fs::exists(destination)) {
        std::cerr << "[Verify Fail] Path not found." << std::endl;
        return false;
    }

    // 遍历源目录的所有文件
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        // 计算该文件在目标目录中的对应路径
        // 例如：源是 /data/a.txt, 目标是 /backup, 相对路径是 a.txt, 目标全路径 /backup/a.txt
        fs::path relativePath = fs::relative(entry.path(), source);
        fs::path targetPath = destination / relativePath;

        if (fs::is_directory(entry.path())) {
             if (!fs::exists(targetPath)) return false;
        } else {
            // 如果是文件，必须存在且内容一致
            if (!fs::exists(targetPath)) {
                std::cerr << "[Verify Fail] Missing file: " << targetPath << std::endl;
                return false;
            }
            if (!compareFiles(entry.path(), targetPath)) {
                std::cerr << "[Verify Fail] Content mismatch: " << targetPath << std::endl;
                return false;
            }
        }
    }
    std::cout << "[Success] Verification passed!" << std::endl;
    return true;
}
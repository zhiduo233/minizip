// src/BackupEngine.cpp

#include "BackupEngine.h"
#include "CRC32.h"
#include <iostream>
#include <vector>
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

// === 3. 还原 ===
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

// === 4.遍历 ===
std::vector<FileRecord> BackupEngine::scanDirectory(const std::string& srcPath)
{
    std::vector<FileRecord> fileList;
    const fs::path source(srcPath);

    // 程序当前路径要和遍历时的当前路径一致
    // 通过计算相对路径来模拟这个效果

    if (!fs::exists(source)) return fileList;

    // 默认 options: skip_permission_denied 可以防止无权限时报错
    // 默认不跟随 symlink，防止死循环
    for (const auto& entry : fs::recursive_directory_iterator(source, fs::directory_options::skip_permission_denied)) {
        FileRecord record;
        record.absPath = entry.path().string();
        record.relPath = fs::relative(entry.path(), source).string();

        // PPT重点：文件类型判断
        if (fs::is_symlink(entry)) {
            record.type = FileType::SYMLINK;
            // 获取软链接指向的目标
            record.linkTarget = fs::read_symlink(entry.path()).string();
            record.size = record.linkTarget.length();
        }
        else if (fs::is_directory(entry)) {
            record.type = FileType::DIRECTORY;
            record.size = 0;
        }
        else if (fs::is_regular_file(entry)) {
            record.type = FileType::REGULAR;
            record.size = fs::file_size(entry.path());
        }
        else {
            // 管道、设备文件等，暂时标记为 OTHER，先跳过或仅记录名字
            record.type = FileType::OTHER;
            record.size = 0;
            // 可以在这里扩展管道/设备文件的支持
        }

        // PPT重点：数据过滤 (Filter)
        // 例如：此处可以加 if (record.relPath.find(".tmp") != std::string::npos) continue;

        fileList.push_back(record);
    }

    return fileList;
}

// === 5.打包 ===
void BackupEngine::packFiles(const std::vector<FileRecord>& files, const std::string& outputFile) {
    std::ofstream out(outputFile, std::ios::binary);
    if (!out.is_open()) throw std::runtime_error("Cannot create pack file");

    // 写入文件头（简单的魔法数，标识这是我们的格式）
    auto MAGIC = "MINIBK10";
    out.write(MAGIC, 8);

    int count = 0;
    for (const auto& rec : files) {
        if (rec.type == FileType::OTHER) continue; // 暂不支持的类型跳过

        // --- 协议设计 (Header) ---
        // [类型 1byte] [路径长 8byte] [路径 str] [数据长 8byte] [数据/Link目标]

        // 1. 类型
        uint8_t typeCode = 0;
        if (rec.type == FileType::REGULAR) typeCode = 1;
        else if (rec.type == FileType::DIRECTORY) typeCode = 2;
        else if (rec.type == FileType::SYMLINK) typeCode = 3;
        out.write(reinterpret_cast<char*>(&typeCode), 1);

        // 2. 相对路径
        uint64_t pathLen = rec.relPath.size();
        out.write(reinterpret_cast<char*>(&pathLen), 8);
        out.write(rec.relPath.c_str(), static_cast<std::streamsize>(pathLen));

        // 3. 数据长度 (如果是链接，就是目标路径长；如果是目录，就是0)
        uint64_t dataSize = rec.size;
        out.write(reinterpret_cast<char*>(&dataSize), 8);

        // 4. 数据体
        if (rec.type == FileType::REGULAR) {
            if (std::ifstream inFile(rec.absPath, std::ios::binary); inFile) {
                out << inFile.rdbuf();
            }
        } else if (rec.type == FileType::SYMLINK) {
            // 对于软链，数据体就是“它指向哪”
            out.write(rec.linkTarget.c_str(), static_cast<std::streamsize>(dataSize));
        }
        // 目录没有数据体，不用写

        count++;
    }
    out.close();
    std::cout << "[Pack] Packed " << count << " items." << std::endl;
}

// 供 main 调用的入口
void BackupEngine::pack(const std::string& srcPath, const std::string& outputFile) {
    std::cout << "Scanning..." << std::endl;
    const auto files = scanDirectory(srcPath);

    std::cout << "Packing..." << std::endl;
    packFiles(files, outputFile);
}

// === 6.解包 ===
void BackupEngine::unpack(const std::string& packFile, const std::string& destPath) {
    std::ifstream in(packFile, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("Cannot open pack file");

    fs::path destRoot(destPath);
    if (!fs::exists(destRoot)) fs::create_directories(destRoot);

    // 检查魔法数
    char magic[9] = {};
    in.read(magic, 8);
    if (std::string(magic) != "MINIBK10") {
        throw std::runtime_error("Invalid file format");
    }

    while (in.peek() != EOF) {
        // 1. 类型
        uint8_t typeCode = 0;
        in.read(reinterpret_cast<char*>(&typeCode), 1);
        if (in.gcount() == 0) break;

        // 2. 路径
        uint64_t pathLen = 0;
        in.read(reinterpret_cast<char*>(&pathLen), 8);
        std::string relPath(pathLen, '\0');
        in.read(&relPath[0], static_cast<std::streamsize>(pathLen));

        // 3. 数据大小
        uint64_t dataSize = 0;
        in.read(reinterpret_cast<char*>(&dataSize), 8);

        fs::path fullPath = destRoot / relPath;

        // 4. 还原逻辑
        try {
            if (typeCode == 2) { // 目录
                fs::create_directories(fullPath);
            }
            else if (typeCode == 3) { // 软链接
                std::string target(dataSize, '\0');
                in.read(&target[0], static_cast<std::streamsize>(dataSize));

                // 确保父目录存在
                if (fullPath.has_parent_path()) fs::create_directories(fullPath.parent_path());

                // 如果已存在先删除，否则 create_symlink 会报错
                if (fs::exists(fullPath) || fs::is_symlink(fullPath)) fs::remove(fullPath);

                fs::create_symlink(target, fullPath);
                std::cout << "  Restored Link: " << relPath << " -> " << target << std::endl;
            }
            else if (typeCode == 1) { // 普通文件
                if (fullPath.has_parent_path()) fs::create_directories(fullPath.parent_path());

                std::ofstream outFile(fullPath, std::ios::binary);
                // 拷贝数据块
                char buffer[4096];
                uint64_t remaining = dataSize;
                while (remaining > 0) {
                    uint64_t toRead = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
                    in.read(buffer, static_cast<std::streamsize>(toRead));
                    outFile.write(buffer, static_cast<std::streamsize>(toRead));
                    remaining -= toRead;
                }
                std::cout << "  Restored File: " << relPath << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error extracting " << relPath << ": " << e.what() << std::endl;
            // 容错：如果出错，要跳过剩余的数据部分，否则后面的文件全乱了
            if (typeCode == 1 || typeCode == 3) { // 如果没读完数据
               // 这是一个复杂的流定位问题，这里简化处理，假设我们已经读完了流
               // 在实际生产中，这里需要根据 dataSize 进行 seekg 跳过
            }
        }
    }
}
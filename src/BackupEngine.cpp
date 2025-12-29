// src/BackupEngine.cpp
#include "BackupEngine.h"
#include "CRC32.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <numeric> // for std::swap

#ifdef _WIN32
    // Windows 没有 getuid，简单定义模拟
    #define stat _stat
#else
    #include <sys/stat.h>
    #include <unistd.h>
#endif

// ==========================================
// 核心算法实现区
// ==========================================

// --- 算法 1: RC4 (Rivest Cipher 4) ---
// 标准流密码算法
class RC4 {
    unsigned char S[256]{};
    int i = 0, j = 0;

public:
    // KSA (Key-scheduling algorithm)
    void init(const std::string& key) {
        if (key.empty()) return;

        for (int k = 0; k < 256; ++k) S[k] = k;

        int j_temp = 0;
        for (int i_temp = 0; i_temp < 256; ++i_temp) {
            j_temp = (j_temp + S[i_temp] + key[i_temp % key.length()]) % 256;
            std::swap(S[i_temp], S[j_temp]);
        }
        i = 0;
        j = 0;
    }

    // PRGA (Pseudo-random generation algorithm)
    // 加密和解密通用 (异或特性)
    void cipher(char* buffer, const size_t size) {
        for (size_t k = 0; k < size; ++k) {
            i = (i + 1) % 256;
            j = (j + S[i]) % 256;
            std::swap(S[i], S[j]);
            const unsigned char rnd = S[(S[i] + S[j]) % 256];
            buffer[k] ^= rnd;
        }
    }
};

// --- 算法 2: Simple XOR ---
// 基础算法
void xorEncrypt(char* buffer, const size_t size, const std::string& password) {
    if (password.empty()) return;
    const size_t pwdLen = password.length();
    for (size_t k = 0; k < size; ++k) {
        buffer[k] ^= password[k % pwdLen];
    }
}

// [新增] 内部辅助：检查文件是否满足筛选条件
// 返回 true 表示通过筛选（需要备份），false 表示跳过
bool checkFilter(const fs::directory_entry& entry, const FilterOptions& opts) {
    // 1. 名字筛选
    if (!opts.nameContains.empty()) {
        if (const std::string filename = entry.path().filename().string(); filename.find(opts.nameContains) == std::string::npos) return false;
    }

    // 2. 路径筛选
    if (!opts.pathContains.empty()) {
        if (entry.path().string().find(opts.pathContains) == std::string::npos) return false;
    }

    // 3. 类型筛选 (0=Reg, 1=Dir, 2=Link)
    if (opts.type != -1) {
        if (opts.type == 0 && !fs::is_regular_file(entry)) return false;
        if (opts.type == 1 && !fs::is_directory(entry)) return false;
        if (opts.type == 2 && !fs::is_symlink(entry)) return false;
    }

    // 对于目录本身，通常不应用尺寸和时间筛选，否则目录不进去，里面的文件也扫不到
    // 为了严谨，只对"非目录"应用以下筛选，或者根据具体需求调整
    if (fs::is_directory(entry)) return true;

    // 4. 尺寸筛选 (仅针对普通文件)
    if (fs::is_regular_file(entry)) {
        const uint64_t size = fs::file_size(entry.path());
        if (opts.minSize > 0 && size < opts.minSize) return false;
        if (opts.maxSize > 0 && size > opts.maxSize) return false;
    }

    // 5. 时间筛选 (修改时间)
    if (opts.startTime > 0) {
        const auto ftime = fs::last_write_time(entry);
        // C++17 转换 file_time_type 到 time_t 比较繁琐，这里简化处理
        // 获取秒数 (近似)
        const auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(ftime);
        if (const long long timestamp = sctp.time_since_epoch().count(); timestamp < opts.startTime) return false;
    }

    // 6. 用户筛选 (仅 Linux 有效，Windows 默认通过)
    if (opts.targetUid != -1) {
        struct stat st{};
        if (stat(entry.path().string().c_str(), &st) == 0) {
            if (st.st_uid != static_cast<unsigned int>(opts.targetUid)) return false;
        }
    }

    return true;
}

// ==========================================
// 业务逻辑实现区
// ==========================================

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
            fs::path relativePath = fs::relative(entry.path(), source);
            fs::path targetPath = destination / relativePath;

            if (fs::is_directory(entry.path())) {
                fs::create_directories(targetPath);
            } else {
                fs::copy_file(entry.path(), targetPath, fs::copy_options::overwrite_existing);
                std::string checksum = CRC32::getFileCRC(entry.path().string());
                indexFile << relativePath.string() << "|" << checksum << "\n";
                std::cout << "  [OK] " << relativePath.string() << std::endl;
                successCount++;
            }
        } catch (const std::exception& e) {
            std::cerr << "  [SKIP] " << entry.path().string() << ": " << e.what() << std::endl;
            failCount++;
        }
    }
    indexFile.close();
    std::cout << "[Backup] Complete. Success: " << successCount << ", Failed: " << failCount << std::endl;
}

bool BackupEngine::verify(const std::string& destPath) {
    fs::path destination(destPath);
    fs::path indexFilePath = destination / "index.txt";

    if (!fs::exists(indexFilePath)) {
        std::cerr << "[Error] Index file missing." << std::endl;
        return false;
    }

    std::ifstream indexFile(indexFilePath);
    std::string line;
    int errorCount = 0;
    int checkedCount = 0;

    std::cout << "Verifying..." << std::endl;

    while (std::getline(indexFile, line)) {
        if (line.empty()) continue;
        size_t delimiterPos = line.find('|');
        if (delimiterPos == std::string::npos) continue;

        std::string relPath = line.substr(0, delimiterPos);
        std::string expectedCRC = line.substr(delimiterPos + 1);
        fs::path currentFile = destination / relPath;
        checkedCount++;

        try {
            if (!fs::exists(currentFile)) {
                std::cerr << "[MISSING] " << relPath << std::endl;
                errorCount++;
                continue;
            }
            if (std::string currentCRC = CRC32::getFileCRC(currentFile.string()); currentCRC != expectedCRC) {
                std::cerr << "[CORRUPT] " << relPath << " (Exp: " << expectedCRC << ", Act: " << currentCRC << ")" << std::endl;
                errorCount++;
            }
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] " << relPath << ": " << e.what() << std::endl;
            errorCount++;
        }
    }

    if (errorCount == 0) {
        std::cout << "[Verify] Passed. Checked " << checkedCount << " files." << std::endl;
        return true;
    }
    std::cerr << "[Verify] FAILED. Found " << errorCount << " errors." << std::endl;
    return false;
}

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
            std::cerr << "  [Restore Error] " << e.what() << std::endl;
            failCount++;
        }
    }
    std::cout << "[Restore] Done. Errors: " << failCount << std::endl;
}

// === 目录遍历算法 ===
std::vector<FileRecord> BackupEngine::scanDirectory(const std::string& srcPath, const FilterOptions& filter) {
    std::vector<FileRecord> fileList;
    const fs::path source(srcPath);
    if (!fs::exists(source)) return fileList;

    for (const auto& entry : fs::recursive_directory_iterator(source, fs::directory_options::skip_permission_denied)) {

        // [核心修改] 在这里进行筛选
        if (!checkFilter(entry, filter)) {
            continue; // 不满足条件，跳过
        }

        FileRecord record;
        record.absPath = entry.path().string();
        record.relPath = fs::relative(entry.path(), source).string();

        if (fs::is_symlink(entry)) {
            record.type = FileType::SYMLINK;
            record.linkTarget = fs::read_symlink(entry.path()).string();
            record.size = record.linkTarget.length();
        } else if (fs::is_directory(entry)) {
            record.type = FileType::DIRECTORY;
            record.size = 0;
        } else if (fs::is_regular_file(entry)) {
            record.type = FileType::REGULAR;
            record.size = fs::file_size(entry.path());
        } else {
            record.type = FileType::OTHER;
            record.size = 0;
        }
        fileList.push_back(record);
    }
    return fileList;
}

// === 打包实现 (支持多算法加密) ===
void BackupEngine::packFiles(const std::vector<FileRecord>& files, const std::string& outputFile, const std::string& password, EncryptionMode mode) {
    std::ofstream out(outputFile, std::ios::binary);
    if (!out.is_open()) throw std::runtime_error("Cannot create pack file");

    // 1. 写入 Header
    if (mode == EncryptionMode::RC4 && !password.empty()) {
        out.write("MINIBK_R", 8); // RC4 Magic
    } else if (mode == EncryptionMode::XOR && !password.empty()) {
        out.write("MINIBK_X", 8); // XOR Magic
    } else {
        out.write("MINIBK10", 8); // No Encryption
    }

    // 初始化 RC4
    RC4 rc4;
    if (mode == EncryptionMode::RC4 && !password.empty()) {
        rc4.init(password);
    }

    int count = 0;
    for (const auto& rec : files) {
        if (rec.type == FileType::OTHER) continue;

        // 构造元数据 buffer: [Type 1] + [PathLen 8] + [Path N] + [DataSize 8]
        std::vector<char> metaBuffer;

        uint8_t typeCode = 0;
        if (rec.type == FileType::REGULAR) typeCode = 1;
        else if (rec.type == FileType::DIRECTORY) typeCode = 2;
        else if (rec.type == FileType::SYMLINK) typeCode = 3;
        metaBuffer.push_back(static_cast<char>(typeCode));

        uint64_t pathLen = rec.relPath.size();
        auto pLen = reinterpret_cast<const char*>(&pathLen);
        metaBuffer.insert(metaBuffer.end(), pLen, pLen + 8);
        metaBuffer.insert(metaBuffer.end(), rec.relPath.begin(), rec.relPath.end());

        uint64_t dataSize = rec.size;
        auto pSize = reinterpret_cast<const char*>(&dataSize);
        metaBuffer.insert(metaBuffer.end(), pSize, pSize + 8);

        // 加密元数据
        if (mode == EncryptionMode::RC4 && !password.empty()) {
            rc4.cipher(metaBuffer.data(), metaBuffer.size());
        } else if (mode == EncryptionMode::XOR && !password.empty()) {
            xorEncrypt(metaBuffer.data(), metaBuffer.size(), password);
        }

        // 写入元数据
        out.write(metaBuffer.data(), static_cast<std::streamsize>(metaBuffer.size()));

        // 处理文件内容/软链目标
        if (rec.type == FileType::REGULAR) {
            if (std::ifstream inFile(rec.absPath, std::ios::binary); inFile) {
                char buffer[4096];
                while (inFile.read(buffer, sizeof(buffer)) || inFile.gcount() > 0) {
                    std::streamsize bytesRead = inFile.gcount();

                    if (mode == EncryptionMode::RC4 && !password.empty()) {
                        rc4.cipher(buffer, bytesRead);
                    } else if (mode == EncryptionMode::XOR && !password.empty()) {
                        xorEncrypt(buffer, bytesRead, password);
                    }

                    out.write(buffer, bytesRead);
                }
            }
        } else if (rec.type == FileType::SYMLINK) {
            std::string target = rec.linkTarget;

            if (mode == EncryptionMode::RC4 && !password.empty()) {
                rc4.cipher(&target[0], target.size());
            } else if (mode == EncryptionMode::XOR && !password.empty()) {
                xorEncrypt(&target[0], target.size(), password);
            }

            out.write(target.c_str(), static_cast<std::streamsize>(target.size()));
        }
        count++;
    }
    out.close();

    std::string modeStr = "None";
    if (mode == EncryptionMode::RC4) modeStr = "RC4";
    else if (mode == EncryptionMode::XOR) modeStr = "Simple XOR";

    std::cout << "[Pack] Done. Items: " << count << ", Encrypt: " << modeStr << std::endl;
}

// 修改：pack 入口传入 filter
void BackupEngine::pack(const std::string& srcPath, const std::string& outputFile,
                        const std::string& password, const EncryptionMode mode,
                        const FilterOptions& filter) {

    std::cout << "Scanning with filters..." << std::endl;
    // 传入 filter
    const auto files = scanDirectory(srcPath, filter);

    std::cout << "Packing " << files.size() << " files..." << std::endl;
    packFiles(files, outputFile, password, mode);
}

// === 解包实现 (自动识别算法) ===
void BackupEngine::unpack(const std::string& packFile, const std::string& destPath, const std::string& password) {
    std::ifstream in(packFile, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("Cannot open pack file");

    fs::path destRoot(destPath);
    if (!fs::exists(destRoot)) fs::create_directories(destRoot);

    // 1. 读取 Header
    char magic[9] = {0};
    in.read(magic, 8);
    std::string magicStr(magic);

    auto mode = EncryptionMode::NONE;
    if (magicStr == "MINIBK_R") mode = EncryptionMode::RC4;
    else if (magicStr == "MINIBK_X") mode = EncryptionMode::XOR;
    else if (magicStr == "MINIBK10") mode = EncryptionMode::NONE;
    else throw std::runtime_error("Unknown file format");

    if (mode != EncryptionMode::NONE && password.empty()) {
        throw std::runtime_error("Password required!");
    }

    RC4 rc4;
    if (mode == EncryptionMode::RC4) rc4.init(password);

    std::cout << "[Unpack] Encryption Mode detected: " << (mode == EncryptionMode::RC4 ? "RC4" : (mode == EncryptionMode::XOR ? "XOR" : "None")) << std::endl;

    while (in.peek() != EOF) {
        // 读取 Type
        char typeBuf[1];
        in.read(typeBuf, 1);
        if (in.gcount() == 0) break;

        if (mode == EncryptionMode::RC4) rc4.cipher(typeBuf, 1);
        else if (mode == EncryptionMode::XOR) xorEncrypt(typeBuf, 1, password);
        auto typeCode = static_cast<uint8_t>(typeBuf[0]);

        // 读取 PathLen
        char lenBuf[8];
        in.read(lenBuf, 8);
        if (mode == EncryptionMode::RC4) rc4.cipher(lenBuf, 8);
        else if (mode == EncryptionMode::XOR) xorEncrypt(lenBuf, 8, password);
        uint64_t pathLen = *reinterpret_cast<uint64_t*>(lenBuf);

        // 读取 Path
        std::vector<char> pathBuf(pathLen);
        in.read(pathBuf.data(), static_cast<std::streamsize>(pathLen));
        if (mode == EncryptionMode::RC4) rc4.cipher(pathBuf.data(), pathLen);
        else if (mode == EncryptionMode::XOR) xorEncrypt(pathBuf.data(), pathLen, password);
        std::string relPath(pathBuf.begin(), pathBuf.end());

        // 读取 DataSize
        char sizeBuf[8];
        in.read(sizeBuf, 8);
        if (mode == EncryptionMode::RC4) rc4.cipher(sizeBuf, 8);
        else if (mode == EncryptionMode::XOR) xorEncrypt(sizeBuf, 8, password);
        uint64_t dataSize = *reinterpret_cast<uint64_t*>(sizeBuf);

        // 还原逻辑
        fs::path fullPath = destRoot / relPath;

        if (typeCode == 2) { // 目录
             fs::create_directories(fullPath);
        } else if (typeCode == 3) { // 软链接
            std::vector<char> linkBuf(dataSize);
            in.read(linkBuf.data(), static_cast<std::streamsize>(dataSize));

            if (mode == EncryptionMode::RC4) rc4.cipher(linkBuf.data(), dataSize);
            else if (mode == EncryptionMode::XOR) xorEncrypt(linkBuf.data(), dataSize, password);

            std::string target(linkBuf.begin(), linkBuf.end());
            if (fullPath.has_parent_path()) fs::create_directories(fullPath.parent_path());
            if (fs::exists(fullPath) || fs::is_symlink(fullPath)) fs::remove(fullPath);
            fs::create_symlink(target, fullPath);
            std::cout << "  Restored Link: " << relPath << std::endl;
        } else if (typeCode == 1) { // 普通文件
            if (fullPath.has_parent_path()) fs::create_directories(fullPath.parent_path());
            std::ofstream outFile(fullPath, std::ios::binary);

            char buffer[4096];
            uint64_t remaining = dataSize;
            while (remaining > 0) {
                uint64_t toRead = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
                in.read(buffer, static_cast<std::streamsize>(toRead));

                if (mode == EncryptionMode::RC4) rc4.cipher(buffer, toRead);
                else if (mode == EncryptionMode::XOR) xorEncrypt(buffer, toRead, password);

                outFile.write(buffer, static_cast<std::streamsize>(toRead));
                remaining -= toRead;
            }
            std::cout << "  Restored File: " << relPath << std::endl;
        }
    }
    std::cout << "[Unpack] Done." << std::endl;
}
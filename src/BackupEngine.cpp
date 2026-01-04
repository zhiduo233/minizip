// src/BackupEngine.cpp
#include "BackupEngine.h"
#include "CRC32.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <numeric> // for std::swap

#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
    // Windows 兼容性空实现 (防止报错)
    #define chmod(path, mode) 0
    #define chown(path, uid, gid) 0
    #define utime(path, buf) 0
    struct utimbuf { long actime; long modtime; };
#else
#include <unistd.h>
#include <utime.h> // 用于恢复时间
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

// // --- 算法 3: 筛选器 ---
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

// --- 算法 4: RLE压缩算法 ---
void rleCompress(const std::vector<char>& input, std::vector<char>& output) {
    if (input.empty()) return;
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char count = 1;
        // 查找连续相同的字符，最大 255 (因为用1个字节存count)
        while (i + 1 < input.size() && input[i] == input[i+1] && count < 255) {
            count++;
            i++;
        }
        output.push_back(static_cast<char>(count));
        output.push_back(input[i]);
    }
}

// --- 算法 5: RLE结业算法 ---
void rleDecompress(const std::vector<char>& input, std::vector<char>& output) {
    if (input.empty()) return;
    for (size_t i = 0; i < input.size(); i += 2) {
        if (i + 1 >= input.size()) break;
        const auto count = static_cast<unsigned char>(input[i]);
        char value = input[i+1];
        for (int k = 0; k < count; ++k) {
            output.push_back(value);
        }
    }
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

        struct stat st{};
        if (stat(record.absPath.c_str(), &st) == 0) {
            record.mode = st.st_mode;
            record.mtime = st.st_mtime;
            record.uid = st.st_uid;
            record.gid = st.st_gid;
        }

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
void BackupEngine::packFiles(const std::vector<FileRecord>& files, const std::string& outputFile,
                             const std::string& password, EncryptionMode encMode,
                             CompressionMode compMode) {

    std::ofstream out(outputFile, std::ios::binary);
    if (!out.is_open()) throw std::runtime_error("Cannot create pack file");

    // 1. 写入 Header Magic (8 bytes) - 标识加密模式
    if (encMode == EncryptionMode::RC4) out.write("MINIBK_R", 8);
    else if (encMode == EncryptionMode::XOR) out.write("MINIBK_X", 8);
    else out.write("MINIBK10", 8);

    // 2. [新增] 写入压缩标记 (1 byte) - 标识是否压缩
    // 0 = 无压缩, 1 = RLE
    char compFlag = (compMode == CompressionMode::RLE) ? 1 : 0;
    out.write(&compFlag, 1);

    // 初始化加密器
    RC4 rc4;
    if (encMode == EncryptionMode::RC4 && !password.empty()) rc4.init(password);

    int count = 0;
    for (const auto& rec : files) {
        if (rec.type == FileType::OTHER) continue;

        // 准备文件数据
        std::vector<char> fileData;

        // 读取内容
        if (rec.type == FileType::REGULAR) {
            if (std::ifstream inFile(rec.absPath, std::ios::binary); inFile) {
                fileData.assign(std::istreambuf_iterator<char>(inFile), std::istreambuf_iterator<char>());
            }
        } else if (rec.type == FileType::SYMLINK) {
            fileData.assign(rec.linkTarget.begin(), rec.linkTarget.end());
        }

        // === 步骤 A: 压缩 ===
        if (compMode == CompressionMode::RLE) {
            std::vector<char> compressed;
            rleCompress(fileData, compressed);
            fileData = compressed; // 替换为压缩后的数据
        }

        // === 步骤 B: 加密 (对压缩后的数据加密) ===
        if (encMode == EncryptionMode::RC4 && !password.empty()) {
            rc4.cipher(fileData.data(), fileData.size());
        } else if (encMode == EncryptionMode::XOR && !password.empty()) {
            xorEncrypt(fileData.data(), fileData.size(), password);
        }

        // === 步骤 C: 准备 Meta 数据 ===
        // 注意：这里的 DataSize 必须是【处理后】的大小
        std::vector<char> metaBuffer;

        uint8_t typeCode = (rec.type == FileType::REGULAR ? 1 : (rec.type == FileType::DIRECTORY ? 2 : 3));
        metaBuffer.push_back(static_cast<char>(typeCode));

        uint64_t pathLen = rec.relPath.size();
        auto pLen = reinterpret_cast<const char*>(&pathLen);
        metaBuffer.insert(metaBuffer.end(), pLen, pLen + 8);
        metaBuffer.insert(metaBuffer.end(), rec.relPath.begin(), rec.relPath.end());

        uint64_t finalSize = fileData.size(); // 处理后的大小
        auto pSize = reinterpret_cast<const char*>(&finalSize);
        metaBuffer.insert(metaBuffer.end(), pSize, pSize + 8);

        auto pMode = reinterpret_cast<const char*>(&rec.mode);
        metaBuffer.insert(metaBuffer.end(), pMode, pMode + 4); // mode 是 32位 (4字节)

        auto pUid = reinterpret_cast<const char*>(&rec.uid);
        metaBuffer.insert(metaBuffer.end(), pUid, pUid + 4);

        auto pGid = reinterpret_cast<const char*>(&rec.gid);
        metaBuffer.insert(metaBuffer.end(), pGid, pGid + 4);

        auto pTime = reinterpret_cast<const char*>(&rec.mtime);
        metaBuffer.insert(metaBuffer.end(), pTime, pTime + 8); // mtime 是 64位 (8字节)

        // 加密 Meta
        if (encMode == EncryptionMode::RC4 && !password.empty()) rc4.cipher(metaBuffer.data(), metaBuffer.size());
        else if (encMode == EncryptionMode::XOR && !password.empty()) xorEncrypt(metaBuffer.data(), metaBuffer.size(), password);

        // 写入 Meta
        out.write(metaBuffer.data(), metaBuffer.size());

        // 写入数据主体
        if (!fileData.empty()) {
            out.write(fileData.data(), fileData.size());
        }
        count++;
    }
    out.close();
    std::cout << "[Pack] Done. Items: " << count
              << ", Enc: " << static_cast<int>(encMode)
              << ", Comp: " << static_cast<int>(compMode) << std::endl;
}

// 修改：pack 入口传入 filter
void BackupEngine::pack(const std::string& srcPath, const std::string& outputFile,
                        const std::string& password, const EncryptionMode encMode,
                        const FilterOptions& filter, const CompressionMode compMode) {

    std::cout << "Scanning with filters..." << std::endl;
    // 传入 filter
    const auto files = scanDirectory(srcPath, filter);

    std::cout << "Packing " << files.size() << " files..." << std::endl;
    packFiles(files, outputFile, password, encMode, compMode);
}

// === 解包实现 (自动识别算法) ===
void BackupEngine::unpack(const std::string& packFile, const std::string& destPath, const std::string& password) {
    std::ifstream in(packFile, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("Cannot open pack file");

    fs::path destRoot(destPath);
    if (!fs::exists(destRoot)) fs::create_directories(destRoot);

    // 1. 读取 Header Magic (8 bytes)
    char magic[9] = {0};
    in.read(magic, 8);
    std::string magicStr(magic);

    auto encMode = EncryptionMode::NONE;
    if (magicStr == "MINIBK_R") encMode = EncryptionMode::RC4;
    else if (magicStr == "MINIBK_X") encMode = EncryptionMode::XOR;
    else if (magicStr != "MINIBK10") throw std::runtime_error("Unknown file format");

    // 2. [新增] 读取 Compression Flag (1 byte)
    char compFlag = 0;
    in.read(&compFlag, 1);
    bool isRLE = (compFlag == 1);

    if (encMode != EncryptionMode::NONE && password.empty()) {
        throw std::runtime_error("Password required!");
    }

    RC4 rc4;
    if (encMode == EncryptionMode::RC4) rc4.init(password);

    std::cout << "[Unpack] Enc: " << static_cast<int>(encMode) << ", Comp: " << (isRLE ? "RLE" : "None") << std::endl;

    while (in.peek() != EOF) {
        // --- 读取 Meta (Type, PathLen, Path, DataSize) ---
        // 逻辑：读取 -> 解密 -> 解析

        // 1. Type
        char typeBuf[1];
        in.read(typeBuf, 1);
        if (in.gcount() == 0) break;
        if (encMode == EncryptionMode::RC4) rc4.cipher(typeBuf, 1);
        else if (encMode == EncryptionMode::XOR) xorEncrypt(typeBuf, 1, password);
        auto typeCode = static_cast<uint8_t>(typeBuf[0]);

        // 2. PathLen
        char lenBuf[8];
        in.read(lenBuf, 8);
        if (encMode == EncryptionMode::RC4) rc4.cipher(lenBuf, 8);
        else if (encMode == EncryptionMode::XOR) xorEncrypt(lenBuf, 8, password);
        uint64_t pathLen = *reinterpret_cast<uint64_t*>(lenBuf);

        // 3. Path
        std::vector<char> pathBuf(pathLen);
        in.read(pathBuf.data(), pathLen);
        if (encMode == EncryptionMode::RC4) rc4.cipher(pathBuf.data(), pathLen);
        else if (encMode == EncryptionMode::XOR) xorEncrypt(pathBuf.data(), pathLen, password);
        std::string relPath(pathBuf.begin(), pathBuf.end());

        // 4. DataSize (这是存储在包里的大小)
        char sizeBuf[8];
        in.read(sizeBuf, 8);
        if (encMode == EncryptionMode::RC4) rc4.cipher(sizeBuf, 8);
        else if (encMode == EncryptionMode::XOR) xorEncrypt(sizeBuf, 8, password);
        uint64_t dataSize = *reinterpret_cast<uint64_t*>(sizeBuf);

        // 5. 读取元数据
        // Mode (4)
        char modeBuf[4]; in.read(modeBuf, 4);
        if (encMode == EncryptionMode::RC4) rc4.cipher(modeBuf, 4);
        else if (encMode == EncryptionMode::XOR) xorEncrypt(modeBuf, 4, password);
        uint32_t f_mode = *reinterpret_cast<uint32_t*>(modeBuf);

        // Uid (4)
        char uidBuf[4]; in.read(uidBuf, 4);
        if (encMode == EncryptionMode::RC4) rc4.cipher(uidBuf, 4);
        else if (encMode == EncryptionMode::XOR) xorEncrypt(uidBuf, 4, password);
        uint32_t f_uid = *reinterpret_cast<uint32_t*>(uidBuf);

        // Gid (4)
        char gidBuf[4]; in.read(gidBuf, 4);
        if (encMode == EncryptionMode::RC4) rc4.cipher(gidBuf, 4);
        else if (encMode == EncryptionMode::XOR) xorEncrypt(gidBuf, 4, password);
        uint32_t f_gid = *reinterpret_cast<uint32_t*>(gidBuf);

        // Mtime (8)
        char timeBuf[8]; in.read(timeBuf, 8);
        if (encMode == EncryptionMode::RC4) rc4.cipher(timeBuf, 8);
        else if (encMode == EncryptionMode::XOR) xorEncrypt(timeBuf, 8, password);
        int64_t f_mtime = *reinterpret_cast<int64_t*>(timeBuf);

        // --- 读取并处理数据 ---
        fs::path fullPath = destRoot / relPath;

        std::vector<char> fileData(dataSize);
        if (dataSize > 0) {
            in.read(fileData.data(), dataSize);

            // 步骤 A: 先解密
            if (encMode == EncryptionMode::RC4) rc4.cipher(fileData.data(), dataSize);
            else if (encMode == EncryptionMode::XOR) xorEncrypt(fileData.data(), dataSize, password);

            // 步骤 B: 再解压 (如果是 RLE)
            if (isRLE) {
                std::vector<char> decompressed;
                rleDecompress(fileData, decompressed);
                fileData = decompressed; // 还原为原始数据
            }
        }

        // --- 写入磁盘 ---
        if (typeCode == 2) { // 目录
             fs::create_directories(fullPath);
        } else if (typeCode == 3) { // 软链接
            std::string target(fileData.begin(), fileData.end());
            if (fullPath.has_parent_path()) fs::create_directories(fullPath.parent_path());
            if (fs::exists(fullPath) || fs::is_symlink(fullPath)) fs::remove(fullPath);
            fs::create_symlink(target, fullPath);
        } else if (typeCode == 1) { // 普通文件
            if (fullPath.has_parent_path()) fs::create_directories(fullPath.parent_path());
            std::ofstream outFile(fullPath, std::ios::binary);
            outFile.write(fileData.data(), fileData.size());
        }
        try {
            // 1. 恢复权限
            chmod(fullPath.string().c_str(), f_mode);

            // 2. 恢复所有者 (需要 root 权限，Docker 里通常是 root)
            chown(fullPath.string().c_str(), f_uid, f_gid);

            // 3. 恢复修改时间
            struct utimbuf new_times{};
            new_times.actime = f_mtime;  // 访问时间也设为修改时间
            new_times.modtime = f_mtime; // 修改时间
            utime(fullPath.string().c_str(), &new_times);

        } catch (...) {
            // 某些系统可能不支持，忽略错误
        }
    }
}
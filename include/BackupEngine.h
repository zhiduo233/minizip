//
// Created by 27450 on 2025/12/28.
//

// include/BackupEngine.h
#ifndef MINIBACKUP_BACKUPENGINE_H
#define MINIBACKUP_BACKUPENGINE_H

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

enum class FileType {
    REGULAR,    // 普通文件
    DIRECTORY,  // 目录
    SYMLINK,    // 软链接
    OTHER       // 管道/设备等
};

struct FileRecord {
    std::string relPath;    // 相对路径 (用于存到包里)
    std::string absPath;    // 绝对路径 (用于读取内容)
    FileType type;          // 文件类型
    uint64_t size;          // 文件大小 (如果是链接，则是目标路径长度)
    std::string linkTarget; // 如果是软链接，这里存指向的目标
    // uint32_t permissions; // (扩展项：元数据-权限，先预留)
};

class BackupEngine {
public:
    // 备份
    static void backup(const std::string& srcPath, const std::string& destPath);

    // 验证
    static bool verify(const std::string& destPath);

    // 复原
    static void restore(const std::string& srcPath, const std::string& destPath);

    // 遍历算法
    static std::vector<FileRecord> scanDirectory(const std::string& srcPath);

    // 打包
    static void packFiles(const std::vector<FileRecord>& files, const std::string& outputFile);
    static void pack(const std::string& srcPath, const std::string& outputFile);

    // 解包
    static void unpack(const std::string& packFile, const std::string& destPath);

};

#endif //MINIBACKUP_BACKUPENGINE_H
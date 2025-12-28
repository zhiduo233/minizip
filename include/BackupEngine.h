//
// Created by 27450 on 2025/12/28.
//

#ifndef MINIBACKUP_BACKUPENGINE_H
#define MINIBACKUP_BACKUPENGINE_H

#include <string>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

class BackupEngine {
public:
    // 基础功能 1: 备份 (source -> destination)
    // 基础功能 2: 还原 (反向备份，逻辑复用)
    static void copyDirectory(const std::string& srcPath, const std::string& destPath);

    // 基础功能 3: 验证 (比较 source 和 destination 是否一致)
    static bool verifyBackup(const std::string& srcPath, const std::string& destPath);

private:
    // 辅助函数：比较两个文件内容是否完全一致
    static bool compareFiles(const fs::path& p1, const fs::path& p2);
};

#endif //MINIBACKUP_BACKUPENGINE_H
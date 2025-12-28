//
// Created by 27450 on 2025/12/28.
//

// include/BackupEngine.h
#ifndef MINIBACKUP_BACKUPENGINE_H
#define MINIBACKUP_BACKUPENGINE_H

#include <string>
#include <filesystem>

namespace fs = std::filesystem;

class BackupEngine {
public:
    static void backup(const std::string& srcPath, const std::string& destPath);
    static bool verify(const std::string& destPath);
    static void restore(const std::string& srcPath, const std::string& destPath);
};

#endif //MINIBACKUP_BACKUPENGINE_H
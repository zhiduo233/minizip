// src/Bridge.cpp
#include "BackupEngine.h"
#include <cstring>

// === 跨平台导出宏定义 ===
// 如果是 Windows，需要声明 "这是要导出给别人用的函数" (dllexport)
// 如果是 Linux/Mac，默认就是导出的，这里留空即可
#ifdef _WIN32
    #define LIBRARY_API __declspec(dllexport)
#else
    #define LIBRARY_API
#endif

// 定义一个纯 C 的结构体，与 Python 里的 ctypes.Structure 对应
struct CFilter {
    const char* nameContains;
    const char* pathContains;
    int type;
    unsigned long long minSize;
    unsigned long long maxSize;
    long long startTime;
    int targetUid;
};

extern "C" {

    // 1. 备份接口
    // 在每个函数前加上 LIBRARY_API
    LIBRARY_API int C_Backup(const char* src, const char* dest) {
        try {
            BackupEngine::backup(src, dest);
            return 1;
        } catch (...) {
            return 0;
        }
    }

    // 2. 还原接口
    LIBRARY_API int C_Restore(const char* src, const char* dest) {
        try {
            BackupEngine::restore(src, dest);
            return 1;
        } catch (...) {
            return 0;
        }
    }

    // 3. 验证接口
    LIBRARY_API int C_Verify(const char* backupDir) {
        try {
            return BackupEngine::verify(backupDir) ? 1 : 0;
        } catch (...) {
            return 0;
        }
    }

    // 4. 打包接口
    LIBRARY_API int C_PackWithFilter(const char* src, const char* pckFile,
                                     const char* pwd, const int encMode,
                                     const CFilter* c_filter,
                                     int compMode) {
        try {
            // 加密模式转换
            auto cppEnc = EncryptionMode::NONE;
            if (encMode == 1) cppEnc = EncryptionMode::XOR;
            else if (encMode == 2) cppEnc = EncryptionMode::RC4;

            // [新增] 压缩模式转换 (0=None, 1=RLE)
            auto cppComp = CompressionMode::NONE;
            if (compMode == 1) cppComp = CompressionMode::RLE;

            FilterOptions opts;
            if (c_filter) {
                if (c_filter->nameContains) opts.nameContains = c_filter->nameContains;
                if (c_filter->pathContains) opts.pathContains = c_filter->pathContains;
                opts.type = c_filter->type;
                opts.minSize = c_filter->minSize;
                opts.maxSize = c_filter->maxSize;
                opts.startTime = c_filter->startTime;
                opts.targetUid = c_filter->targetUid;
            }

            // 调用核心
            BackupEngine::pack(src, pckFile, pwd, cppEnc, opts, cppComp);
            return 1;
        } catch (...) {
            return 0;
        }
    }

    // 保留原来的 C_Pack，只是内部使用默认 filter
    LIBRARY_API int C_Pack(const char* src, const char* pckFile, const char* pwd, const int mode) {
        try {
            auto cppMode = EncryptionMode::NONE;
            if (mode == 1) cppMode = EncryptionMode::XOR;
            else if (mode == 2) cppMode = EncryptionMode::RC4;

            BackupEngine::pack(src, pckFile, pwd, cppMode);
            return 1;
        } catch (...) {
            return 0;
        }
    }

    // 5. 解包接口
    LIBRARY_API int C_Unpack(const char* pckFile, const char* dest, const char* pwd) {
        try {
            BackupEngine::unpack(pckFile, dest, pwd);
            return 1;
        } catch (...) {
            return 0;
        }
    }
}
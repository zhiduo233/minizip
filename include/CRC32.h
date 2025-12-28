//
// Created by 27450 on 2025/12/28.
//

// include/CRC32.h

#ifndef MINIBACKUP_CRC32_H
#define MINIBACKUP_CRC32_H

#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <iomanip>
#include <sstream>

class CRC32 {
public:
    // 计算文件的 CRC32 值，返回 8 位十六进制字符串 (例如 "A1B2C3D4")
    static std::string getFileCRC(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) return "00000000";

        // 标准 CRC32 多项式
        uint32_t crc = 0xFFFFFFFF;

        char buffer[4096];
        while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
            for (std::streamsize i = 0; i < file.gcount(); ++i) {
                auto byte = static_cast<uint8_t>(buffer[i]);
                crc ^= byte;
                for (int j = 0; j < 8; ++j) {
                    constexpr uint32_t polynomial = 0xEDB88320;
                    uint32_t mask = -static_cast<int>(crc & 1);
                    crc = crc >> 1 ^ polynomial & mask;
                }
            }
        }

        // 取反得到最终值
        crc = ~crc;

        // 转为 HEX 字符串
        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << crc;
        return ss.str();
    }
};

#endif //MINIBACKUP_CRC32_H
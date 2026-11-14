#pragma once

#include <filesystem>
#include <vector>
#include <cstdint>
#include <memory>

class DirectStorageReader {
public:
    DirectStorageReader();
    ~DirectStorageReader();
    std::vector<uint8_t> Read(const std::filesystem::path& path);

private:
    struct Queue;
    std::unique_ptr<Queue> mQueue;
};

#pragma once
// The file layer and the CRC-32 modzip.cpp (TwinsanityEvolved's zip reader) reads through, in place of its engine's io.h and
// hashes.h: a file opened for reading by its UTF-8 path with 64-bit offsets, and the zip's CRC-32 (ISO-HDLC, reflected 0xEDB88320:
// Crc32Update(0, ...) starts a value, the result final after each call)
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace te::modimport
{
uint32_t Crc32Update(uint32_t crc, const void* data, size_t len);

namespace io
{
class File
{
public:
    File() = default;
    ~File() { Close(); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    bool OpenRead(const std::string& path);
    bool IsOpen() const { return f_ != nullptr; }
    bool Close();
    uint64_t Size() const { return size_; }
    bool ReadAt(uint64_t offset, void* dst, size_t n);

private:
    std::FILE* f_ = nullptr;
    uint64_t size_ = 0;
};
}
}

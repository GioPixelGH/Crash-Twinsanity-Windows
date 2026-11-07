// modzipio.h: the zip reader's file layer and CRC-32
#include "modzipio.h"

namespace te::modimport
{
uint32_t Crc32Update(uint32_t crc, const void* data, size_t len)
{
    static uint32_t table[256];
    static const bool made = [] {
        for (uint32_t i = 0; i < 256; i++)
        {
            uint32_t value = i;
            for (int bit = 0; bit < 8; bit++)
            {
                value = (value & 1) != 0 ? 0xEDB88320u ^ (value >> 1) : value >> 1;
            }

            table[i] = value;
        }

        return true;
    }();
    (void)made;
    const auto* bytes = static_cast<const uint8_t*>(data);
    crc = ~crc;
    for (size_t i = 0; i < len; i++)
    {
        crc = table[(crc ^ bytes[i]) & 0xff] ^ (crc >> 8);
    }

    return ~crc;
}

bool io::File::OpenRead(const std::string& path)
{
    Close();
    const std::filesystem::path name(std::u8string(path.begin(), path.end()));
#if defined(_WIN32)
    f_ = _wfopen(name.wstring().c_str(), L"rb");
#else
    f_ = std::fopen(name.string().c_str(), "rb");
#endif
    if (f_ == nullptr)
    {
        return false;
    }

    std::error_code error;
    size_ = std::filesystem::file_size(name, error);
    if (error)
    {
        Close();
        return false;
    }

    return true;
}

bool io::File::Close()
{
    if (f_ != nullptr)
    {
        std::fclose(f_);
        f_ = nullptr;
    }

    size_ = 0;
    return true;
}

bool io::File::ReadAt(uint64_t offset, void* dst, size_t n)
{
    if (f_ == nullptr)
    {
        return false;
    }

#if defined(_WIN32)
    if (_fseeki64(f_, static_cast<long long>(offset), SEEK_SET) != 0)
#else
    if (fseeko(f_, static_cast<off_t>(offset), SEEK_SET) != 0)
#endif
    {
        return false;
    }

    return std::fread(dst, 1, n, f_) == n;
}
}

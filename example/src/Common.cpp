#include "Common.h"

std::vector<uint32_t> readFromFile(const std::string &location)
{
    std::ifstream file(location, std::ios::binary);
    std::vector<uint32_t> buffer;

    if(file.is_open())
    {
        file.seekg (0, file.end);
        uint32_t length = file.tellg();
        file.seekg (0, file.beg);

        buffer.resize(length);
        file.read((char*)buffer.data(), length);

        file.close();

        return buffer;
    }
    else
    {
        throw std::runtime_error("Couldn't open file " + location);
    }
}
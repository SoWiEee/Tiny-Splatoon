#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace restoration::environment_detail {
struct TextureDescriptor {
    std::uint32_t width=0,height=0;
    unsigned bits=0;
};
using TextureCatalog=std::map<std::string,TextureDescriptor,std::less<>>;

TextureCatalog parseTextureCatalog(std::string_view json);
TextureCatalog readTextureCatalog(const std::filesystem::path& directory);
std::vector<unsigned char> decodeTextureBlob(const std::vector<unsigned char>& compressed,const TextureDescriptor& descriptor,const std::string& label);
std::vector<unsigned char> readTextureBlob(const std::filesystem::path& path,const TextureDescriptor& descriptor);
}

// Response cache for GETs: RAM (LRU) + optional SD card, validated against the server with ETag / If-None-Match.
// Stores the already-filtered JSON (what the UI uses), never the API key. UI thread only (the SD shares the LCD's SPI bus).
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace api { struct Request; }

namespace cache {
struct Entry {
    std::string etag, json;
    uint32_t used = 0;
};
std::string keyFor(const api::Request& r);                                  // "" = not cacheable
bool lookup(const std::string& key, Entry& out);                            // RAM, then SD
void store(const std::string& key, const std::string& etag, std::string&& json);
void clear();                                                               // RAM + SD
void stats(size_t& ramEntries, size_t& ramBytes, size_t& sdFiles);
}  // namespace cache

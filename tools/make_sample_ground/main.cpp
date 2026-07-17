/**
 * Writes a simple tileable crystal ground PNG (truecolor) for Grok pipeline testing.
 * No external deps — minimal PNG writer.
 */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

// CRC32 for PNG
static uint32_t crc_table[256];
static void crc_init() {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
    crc_table[n] = c;
  }
}
static uint32_t crc32(const uint8_t* buf, size_t len) {
  uint32_t c = 0xffffffffu;
  for (size_t n = 0; n < len; n++) c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
  return c ^ 0xffffffffu;
}

static void write_u32be(std::ostream& o, uint32_t v) {
  o.put(char((v >> 24) & 0xff));
  o.put(char((v >> 16) & 0xff));
  o.put(char((v >> 8) & 0xff));
  o.put(char(v & 0xff));
}

static void write_chunk(std::ostream& o, const char* type, const std::vector<uint8_t>& data) {
  write_u32be(o, static_cast<uint32_t>(data.size()));
  o.write(type, 4);
  o.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  std::vector<uint8_t> forcrc(4 + data.size());
  for (int i = 0; i < 4; i++) forcrc[i] = static_cast<uint8_t>(type[i]);
  for (size_t i = 0; i < data.size(); i++) forcrc[4 + i] = data[i];
  write_u32be(o, crc32(forcrc.data(), forcrc.size()));
}

// Minimal zlib store (uncompressed) for small images
static std::vector<uint8_t> zlib_store(const std::vector<uint8_t>& raw) {
  std::vector<uint8_t> out;
  out.push_back(0x78);
  out.push_back(0x01);
  size_t pos = 0;
  while (pos < raw.size()) {
    size_t block = std::min<size_t>(65535, raw.size() - pos);
    bool last = pos + block >= raw.size();
    out.push_back(last ? 0x01 : 0x00);
    out.push_back(static_cast<uint8_t>(block & 0xff));
    out.push_back(static_cast<uint8_t>((block >> 8) & 0xff));
    uint16_t nlen = static_cast<uint16_t>(~block);
    out.push_back(static_cast<uint8_t>(nlen & 0xff));
    out.push_back(static_cast<uint8_t>((nlen >> 8) & 0xff));
    out.insert(out.end(), raw.begin() + static_cast<long>(pos),
               raw.begin() + static_cast<long>(pos + block));
    pos += block;
  }
  // Adler32
  uint32_t s1 = 1, s2 = 0;
  for (uint8_t b : raw) {
    s1 = (s1 + b) % 65521;
    s2 = (s2 + s1) % 65521;
  }
  uint32_t adler = (s2 << 16) | s1;
  out.push_back(static_cast<uint8_t>((adler >> 24) & 0xff));
  out.push_back(static_cast<uint8_t>((adler >> 16) & 0xff));
  out.push_back(static_cast<uint8_t>((adler >> 8) & 0xff));
  out.push_back(static_cast<uint8_t>(adler & 0xff));
  return out;
}

int main(int argc, char** argv) {
  crc_init();
  namespace fs = std::filesystem;
  fs::path out = argc > 1 ? argv[1] : "assets/grok_inbox/crystal_ground_sample.png";
  fs::create_directories(out.parent_path());

  const int W = 128, H = 128;
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>((W * 3 + 1) * H));
  for (int y = 0; y < H; ++y) {
    raw.push_back(0); // filter none
    for (int x = 0; x < W; ++x) {
      float u = x / float(W), v = y / float(H);
      float n = std::sin(u * 18.f) * std::cos(v * 14.f) * 0.5f + 0.5f;
      float n2 = std::sin((u + v) * 40.f) * 0.5f + 0.5f;
      uint8_t r = static_cast<uint8_t>(30 + n * 40 + n2 * 20);
      uint8_t g = static_cast<uint8_t>(80 + n * 100 + n2 * 30);
      uint8_t b = static_cast<uint8_t>(100 + n * 120);
      raw.push_back(r);
      raw.push_back(g);
      raw.push_back(b);
    }
  }

  std::ofstream f(out, std::ios::binary);
  const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
  f.write(reinterpret_cast<const char*>(sig), 8);

  std::vector<uint8_t> ihdr(13);
  auto put32 = [](uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;
    p[3] = v & 0xff;
  };
  put32(ihdr.data(), W);
  put32(ihdr.data() + 4, H);
  ihdr[8] = 8;  // bit depth
  ihdr[9] = 2;  // RGB
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;
  write_chunk(f, "IHDR", ihdr);

  auto idat = zlib_store(raw);
  write_chunk(f, "IDAT", idat);
  write_chunk(f, "IEND", {});

  std::printf("Wrote %s\n", out.string().c_str());
  return 0;
}

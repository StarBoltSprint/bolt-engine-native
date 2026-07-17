/**
 * Seamless organic crystal ground tile (512²) — multi-octave noise, not grid waves.
 */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
#include <filesystem>
#include <algorithm>

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
  for (int i = 0; i < 4; i++) forcrc[static_cast<size_t>(i)] = static_cast<uint8_t>(type[i]);
  for (size_t i = 0; i < data.size(); i++) forcrc[4 + i] = data[i];
  write_u32be(o, crc32(forcrc.data(), forcrc.size()));
}
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

static float hash2(int x, int z) {
  float s = std::sin(x * 127.1f + z * 311.7f) * 43758.5453f;
  return s - std::floor(s);
}
static float noise(float x, float z) {
  int x0 = (int)std::floor(x), z0 = (int)std::floor(z);
  float fx = x - x0, fz = z - z0;
  float u = fx * fx * (3 - 2 * fx);
  float v = fz * fz * (3 - 2 * fz);
  float a = hash2(x0, z0), b = hash2(x0 + 1, z0), c = hash2(x0, z0 + 1), d = hash2(x0 + 1, z0 + 1);
  return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
static float fbm(float x, float z) {
  float v = 0, a = 0.5f, f = 1.f;
  for (int i = 0; i < 6; ++i) {
    v += a * noise(x * f, z * f);
    a *= 0.5f;
    f *= 2.02f;
  }
  return v;
}

int main(int argc, char** argv) {
  crc_init();
  namespace fs = std::filesystem;
  fs::path out = argc > 1 ? argv[1] : "assets/grok_inbox/crystal_ground_sample.png";
  fs::create_directories(out.parent_path());

  const int W = 512, H = 512;
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>((W * 3 + 1) * H));
  for (int y = 0; y < H; ++y) {
    raw.push_back(0);
    for (int x = 0; x < W; ++x) {
      // Seamless domain: tile in [0,8)
      float u = x / float(W) * 8.f;
      float v = y / float(H) * 8.f;
      float n = fbm(u, v);
      float n2 = fbm(u * 2.3f + 17.f, v * 2.3f - 9.f);
      float ridge = 1.f - std::abs(2.f * n - 1.f);
      ridge *= ridge;
      float mixv = n * 0.55f + n2 * 0.25f + ridge * 0.35f;
      mixv = std::clamp(mixv, 0.f, 1.f);
      // Crystal teal / cyan palette
      uint8_t r = static_cast<uint8_t>(18 + mixv * 55 + ridge * 30);
      uint8_t g = static_cast<uint8_t>(70 + mixv * 120 + n2 * 40);
      uint8_t b = static_cast<uint8_t>(95 + mixv * 140 + ridge * 50);
      // Speckle quartz
      if (hash2(x / 3, y / 3) > 0.92f) {
        r = 200;
        g = 240;
        b = 255;
      }
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
  ihdr[8] = 8;
  ihdr[9] = 2;
  ihdr[10] = ihdr[11] = ihdr[12] = 0;
  write_chunk(f, "IHDR", ihdr);
  write_chunk(f, "IDAT", zlib_store(raw));
  write_chunk(f, "IEND", {});
  std::printf("Wrote organic crystal tile %dx%d -> %s\n", W, H, out.string().c_str());
  return 0;
}

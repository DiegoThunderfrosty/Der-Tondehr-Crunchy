#include "preset/CrunchyPresetFormat.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

#define CHECK(condition) do { \
  if (!(condition)) { \
    std::cerr << "Check failed at line " << __LINE__ << ": " #condition "\n"; \
    return 1; \
  } \
} while (false)

static std::string Hex(const std::array<std::uint8_t, 32>& d) {
  static const char* h = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (auto b : d) { out.push_back(h[b >> 4]); out.push_back(h[b & 15]); }
  return out;
}

int main(int argc, char** argv) {
  const char* abc = "abc";
  CHECK(Hex(crunchy::preset::SHA256ForTesting(abc, 3)) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  const std::string thousandA(1000, 'a');
  CHECK(Hex(crunchy::preset::SHA256ForTesting(thousandA.data(), thousandA.size())) ==
        "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");

  using crunchy::preset::Entry;
  using crunchy::preset::ValueType;
  std::vector<Entry> source {
    {1u, ValueType::Continuous, 5.0},
    {2u, ValueType::Toggle, 1.0},
    {3u, ValueType::Enumeration, 2.0}
  };
  std::vector<std::uint8_t> bytes;
  const bool built = crunchy::preset::BuildFile(source, bytes);
  CHECK(built);
  CHECK(bytes.size() == crunchy::preset::kHeaderBytes + source.size() * crunchy::preset::kEntryBytes);

  std::vector<Entry> decoded;
  crunchy::preset::ParseError error {};
  const bool parsed = crunchy::preset::ParseFile(bytes, decoded, error);
  CHECK(parsed);
  CHECK(error == crunchy::preset::ParseError::None);
  CHECK(decoded.size() == source.size());
  for (std::size_t i = 0; i < source.size(); ++i) {
    CHECK(decoded[i].id == source[i].id);
    CHECK(decoded[i].type == source[i].type);
    CHECK(decoded[i].value == source[i].value);
  }

  auto tampered = bytes;
  tampered.back() ^= 0x01;
  const bool parsedTampered = crunchy::preset::ParseFile(tampered, decoded, error);
  CHECK(!parsedTampered);
  CHECK(error == crunchy::preset::ParseError::BadDigest);

  auto wrongMagic = bytes;
  wrongMagic[0] = 'X';
  const bool parsedWrongMagic = crunchy::preset::ParseFile(wrongMagic, decoded, error);
  CHECK(!parsedWrongMagic);
  CHECK(error == crunchy::preset::ParseError::BadMagic);

  auto wrongProduct = bytes;
  wrongProduct[8] = 'X';
  const bool parsedWrongProduct = crunchy::preset::ParseFile(wrongProduct, decoded, error);
  CHECK(!parsedWrongProduct);
  CHECK(error == crunchy::preset::ParseError::WrongProduct);

  auto wrongHash = bytes;
  // hash algorithm field begins at byte 28: magic 8 + product 8 + versions/count 12.
  wrongHash[28] = 99;
  const bool parsedWrongHash = crunchy::preset::ParseFile(wrongHash, decoded, error);
  CHECK(!parsedWrongHash);
  CHECK(error == crunchy::preset::ParseError::UnsupportedHashAlgorithm);

  CHECK(argc == 3);
  for (int i = 1; i < argc; ++i) {
    std::ifstream file(argv[i], std::ios::binary);
    CHECK(file.good());
    const std::vector<std::uint8_t> presetBytes(
      (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(crunchy::preset::ParseFile(presetBytes, decoded, error));
    CHECK(error == crunchy::preset::ParseError::None);
    CHECK(!decoded.empty());
  }

  std::cout << "Crunchy preset format tests passed\n";
  return 0;
}

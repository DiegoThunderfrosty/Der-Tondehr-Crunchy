#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace crunchy::preset {

static constexpr const char* kExtension = ".dtcpreset";
static constexpr std::uint32_t kContainerVersion = 1u;
static constexpr std::uint32_t kPayloadVersion = 1u;
static constexpr std::uint32_t kHashAlgorithmSHA256 = 1u;
static constexpr std::size_t kHeaderBytes = 80u;
static constexpr std::size_t kEntryBytes = 16u;
static constexpr std::size_t kMaxFileBytes = 256u * 1024u;
static constexpr std::uint32_t kMaxEntries = 128u;

static constexpr std::array<std::uint8_t, 8> kMagic {{
  'D','T','C','P','R','S','T','1'
}};
static constexpr std::array<std::uint8_t, 8> kProduct {{
  'C','R','U','N','C','H','Y','\0'
}};

// The on-disk type is deliberately independent from iPlug2 parameter types.
// It makes malformed/cross-plugin files structurally rejectable before any
// parameter is changed.
enum class ValueType : std::uint32_t {
  Continuous = 1u,
  Toggle = 2u,
  Enumeration = 3u
};

struct Entry {
  std::uint32_t id = 0u;       // stable Crunchy preset ID, not an enum index
  ValueType type = ValueType::Continuous;
  double value = 0.0;
};

enum class ParseError {
  None,
  TooSmall,
  TooLarge,
  BadMagic,
  WrongProduct,
  UnsupportedContainerVersion,
  UnsupportedPayloadVersion,
  UnsupportedHashAlgorithm,
  BadEntryCount,
  BadPayloadSize,
  BadDigest,
  DuplicateId,
  InvalidValueType,
  NonFiniteValue
};

inline const char* ParseErrorText(ParseError error) {
  switch (error) {
    case ParseError::None: return "OK";
    case ParseError::TooSmall: return "Preset file is truncated";
    case ParseError::TooLarge: return "Preset file is too large";
    case ParseError::BadMagic: return "File is not a Der Tondehr Crunchy preset";
    case ParseError::WrongProduct: return "Preset belongs to a different product";
    case ParseError::UnsupportedContainerVersion: return "Unsupported preset container version";
    case ParseError::UnsupportedPayloadVersion: return "Preset was created by a newer Crunchy preset format";
    case ParseError::UnsupportedHashAlgorithm: return "Unsupported preset integrity algorithm";
    case ParseError::BadEntryCount: return "Preset contains an invalid control count";
    case ParseError::BadPayloadSize: return "Preset payload size is invalid";
    case ParseError::BadDigest: return "Preset integrity check failed";
    case ParseError::DuplicateId: return "Preset contains duplicate control IDs";
    case ParseError::InvalidValueType: return "Preset contains an invalid control type";
    case ParseError::NonFiniteValue: return "Preset contains an invalid control value";
  }
  return "Invalid preset";
}

namespace detail {

inline std::uint32_t RotR(std::uint32_t x, std::uint32_t n) {
  return (x >> n) | (x << (32u - n));
}

class SHA256 {
public:
  SHA256() { Reset(); }

  void Reset() {
    mState = {{
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    }};
    mBitCount = 0u;
    mBufferSize = 0u;
  }

  void Update(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    if (!bytes || size == 0u) return;
    mBitCount += static_cast<std::uint64_t>(size) * 8u;
    while (size > 0u) {
      const std::size_t available = 64u - mBufferSize;
      const std::size_t take = std::min(available, size);
      std::memcpy(mBuffer.data() + mBufferSize, bytes, take);
      mBufferSize += take;
      bytes += take;
      size -= take;
      if (mBufferSize == 64u) {
        Transform(mBuffer.data());
        mBufferSize = 0u;
      }
    }
  }

  std::array<std::uint8_t, 32> Final() {
    const std::uint64_t originalBits = mBitCount;
    const std::uint8_t one = 0x80u;
    UpdateWithoutCount(&one, 1u);
    const std::uint8_t zero = 0u;
    while (mBufferSize != 56u)
      UpdateWithoutCount(&zero, 1u);

    std::uint8_t lengthBytes[8] {};
    for (int i = 0; i < 8; ++i)
      lengthBytes[7 - i] = static_cast<std::uint8_t>((originalBits >> (i * 8)) & 0xffu);
    UpdateWithoutCount(lengthBytes, sizeof(lengthBytes));

    std::array<std::uint8_t, 32> digest {};
    for (std::size_t i = 0; i < mState.size(); ++i) {
      digest[i * 4 + 0] = static_cast<std::uint8_t>((mState[i] >> 24) & 0xffu);
      digest[i * 4 + 1] = static_cast<std::uint8_t>((mState[i] >> 16) & 0xffu);
      digest[i * 4 + 2] = static_cast<std::uint8_t>((mState[i] >> 8) & 0xffu);
      digest[i * 4 + 3] = static_cast<std::uint8_t>(mState[i] & 0xffu);
    }
    return digest;
  }

private:
  void UpdateWithoutCount(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    while (size > 0u) {
      const std::size_t available = 64u - mBufferSize;
      const std::size_t take = std::min(available, size);
      std::memcpy(mBuffer.data() + mBufferSize, bytes, take);
      mBufferSize += take;
      bytes += take;
      size -= take;
      if (mBufferSize == 64u) {
        Transform(mBuffer.data());
        mBufferSize = 0u;
      }
    }
  }

  void Transform(const std::uint8_t* block) {
    static constexpr std::uint32_t k[64] = {
      0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
      0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
      0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
      0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
      0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
      0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
      0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
      0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };

    std::uint32_t w[64] {};
    for (int i = 0; i < 16; ++i) {
      const int o = i * 4;
      w[i] = (static_cast<std::uint32_t>(block[o]) << 24)
           | (static_cast<std::uint32_t>(block[o + 1]) << 16)
           | (static_cast<std::uint32_t>(block[o + 2]) << 8)
           | static_cast<std::uint32_t>(block[o + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a=mState[0], b=mState[1], c=mState[2], d=mState[3];
    std::uint32_t e=mState[4], f=mState[5], g=mState[6], h=mState[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t s1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
      const std::uint32_t ch = (e & f) ^ ((~e) & g);
      const std::uint32_t temp1 = h + s1 + ch + k[i] + w[i];
      const std::uint32_t s0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = s0 + maj;
      h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    mState[0]+=a; mState[1]+=b; mState[2]+=c; mState[3]+=d;
    mState[4]+=e; mState[5]+=f; mState[6]+=g; mState[7]+=h;
  }

  std::array<std::uint32_t, 8> mState {};
  std::array<std::uint8_t, 64> mBuffer {};
  std::uint64_t mBitCount = 0u;
  std::size_t mBufferSize = 0u;
};

inline void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu));
}

inline void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i)
    out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu));
}

inline void AppendDouble(std::vector<std::uint8_t>& out, double value) {
  static_assert(sizeof(double) == sizeof(std::uint64_t), "Crunchy presets require IEEE-754 64-bit double");
  std::uint64_t bits = 0u;
  std::memcpy(&bits, &value, sizeof(bits));
  AppendU64(out, bits);
}

inline bool ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t& pos, std::uint32_t& value) {
  if (pos + 4u > bytes.size()) return false;
  value = 0u;
  for (int i = 0; i < 4; ++i)
    value |= static_cast<std::uint32_t>(bytes[pos++]) << (i * 8);
  return true;
}

inline bool ReadU64(const std::vector<std::uint8_t>& bytes, std::size_t& pos, std::uint64_t& value) {
  if (pos + 8u > bytes.size()) return false;
  value = 0u;
  for (int i = 0; i < 8; ++i)
    value |= static_cast<std::uint64_t>(bytes[pos++]) << (i * 8);
  return true;
}

inline bool ReadDouble(const std::vector<std::uint8_t>& bytes, std::size_t& pos, double& value) {
  std::uint64_t bits = 0u;
  if (!ReadU64(bytes, pos, bits)) return false;
  std::memcpy(&value, &bits, sizeof(value));
  return true;
}

inline void FeedU32(SHA256& hash, std::uint32_t value) {
  std::uint8_t bytes[4] {};
  for (int i = 0; i < 4; ++i)
    bytes[i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu);
  hash.Update(bytes, sizeof(bytes));
}

inline void FeedU64(SHA256& hash, std::uint64_t value) {
  std::uint8_t bytes[8] {};
  for (int i = 0; i < 8; ++i)
    bytes[i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu);
  hash.Update(bytes, sizeof(bytes));
}

inline std::array<std::uint8_t, 32> ComputeDigest(std::uint32_t containerVersion,
                                                  std::uint32_t payloadVersion,
                                                  std::uint32_t entryCount,
                                                  std::uint32_t hashAlgorithm,
                                                  std::uint64_t payloadSize,
                                                  const std::uint8_t* payload,
                                                  std::size_t payloadBytes) {
  static constexpr std::uint8_t domain[] = {
    'D','e','r','T','o','n','d','e','h','r','C','r','u','n','c','h','y','P','r','e','s','e','t','\0'
  };
  SHA256 hash;
  hash.Update(domain, sizeof(domain));
  hash.Update(kMagic.data(), kMagic.size());
  hash.Update(kProduct.data(), kProduct.size());
  FeedU32(hash, containerVersion);
  FeedU32(hash, payloadVersion);
  FeedU32(hash, entryCount);
  FeedU32(hash, hashAlgorithm);
  FeedU64(hash, payloadSize);
  if (payload && payloadBytes)
    hash.Update(payload, payloadBytes);
  return hash.Final();
}

} // namespace detail

inline std::array<std::uint8_t, 32> SHA256ForTesting(const void* data, std::size_t size) {
  detail::SHA256 hash;
  hash.Update(data, size);
  return hash.Final();
}

inline bool BuildFile(const std::vector<Entry>& entries, std::vector<std::uint8_t>& bytes) {
  bytes.clear();
  if (entries.empty() || entries.size() > kMaxEntries)
    return false;

  std::unordered_set<std::uint32_t> ids;
  std::vector<std::uint8_t> payload;
  payload.reserve(entries.size() * kEntryBytes);
  for (const Entry& entry : entries) {
    if (entry.id == 0u || !ids.insert(entry.id).second || !std::isfinite(entry.value))
      return false;
    const std::uint32_t type = static_cast<std::uint32_t>(entry.type);
    if (type < static_cast<std::uint32_t>(ValueType::Continuous)
        || type > static_cast<std::uint32_t>(ValueType::Enumeration))
      return false;
    detail::AppendU32(payload, entry.id);
    detail::AppendU32(payload, type);
    detail::AppendDouble(payload, entry.value);
  }

  const std::uint64_t payloadSize = static_cast<std::uint64_t>(payload.size());
  if (kHeaderBytes + payload.size() > kMaxFileBytes)
    return false;
  const auto digest = detail::ComputeDigest(kContainerVersion, kPayloadVersion,
                                             static_cast<std::uint32_t>(entries.size()),
                                             kHashAlgorithmSHA256, payloadSize,
                                             payload.data(), payload.size());

  bytes.reserve(kHeaderBytes + payload.size());
  bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
  bytes.insert(bytes.end(), kProduct.begin(), kProduct.end());
  detail::AppendU32(bytes, kContainerVersion);
  detail::AppendU32(bytes, kPayloadVersion);
  detail::AppendU32(bytes, static_cast<std::uint32_t>(entries.size()));
  detail::AppendU32(bytes, kHashAlgorithmSHA256);
  detail::AppendU64(bytes, payloadSize);
  bytes.insert(bytes.end(), digest.begin(), digest.end());
  detail::AppendU64(bytes, 0u); // reserved for a future authenticated format
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes.size() == kHeaderBytes + payload.size();
}

inline bool ParseFile(const std::vector<std::uint8_t>& bytes,
                      std::vector<Entry>& entries,
                      ParseError& error) {
  entries.clear();
  error = ParseError::None;
  if (bytes.size() < kHeaderBytes) { error = ParseError::TooSmall; return false; }
  if (bytes.size() > kMaxFileBytes) { error = ParseError::TooLarge; return false; }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    error = ParseError::BadMagic; return false;
  }
  if (!std::equal(kProduct.begin(), kProduct.end(), bytes.begin() + 8)) {
    error = ParseError::WrongProduct; return false;
  }

  std::size_t pos = 16u;
  std::uint32_t containerVersion = 0u, payloadVersion = 0u, entryCount = 0u, hashAlgorithm = 0u;
  std::uint64_t payloadSize = 0u;
  if (!detail::ReadU32(bytes, pos, containerVersion)
      || !detail::ReadU32(bytes, pos, payloadVersion)
      || !detail::ReadU32(bytes, pos, entryCount)
      || !detail::ReadU32(bytes, pos, hashAlgorithm)
      || !detail::ReadU64(bytes, pos, payloadSize)) {
    error = ParseError::TooSmall; return false;
  }
  if (containerVersion != kContainerVersion) {
    error = ParseError::UnsupportedContainerVersion; return false;
  }
  if (payloadVersion > kPayloadVersion || payloadVersion == 0u) {
    error = ParseError::UnsupportedPayloadVersion; return false;
  }
  if (hashAlgorithm != kHashAlgorithmSHA256) {
    error = ParseError::UnsupportedHashAlgorithm; return false;
  }
  if (entryCount == 0u || entryCount > kMaxEntries) {
    error = ParseError::BadEntryCount; return false;
  }
  if (payloadSize != static_cast<std::uint64_t>(entryCount) * kEntryBytes
      || payloadSize != static_cast<std::uint64_t>(bytes.size() - kHeaderBytes)) {
    error = ParseError::BadPayloadSize; return false;
  }

  std::array<std::uint8_t, 32> storedDigest {};
  std::memcpy(storedDigest.data(), bytes.data() + pos, storedDigest.size());
  pos += storedDigest.size();
  std::uint64_t reserved = 0u;
  if (!detail::ReadU64(bytes, pos, reserved) || pos != kHeaderBytes) {
    error = ParseError::TooSmall; return false;
  }
  if (reserved != 0u) {
    error = ParseError::UnsupportedContainerVersion; return false;
  }

  const auto computed = detail::ComputeDigest(containerVersion, payloadVersion, entryCount,
                                               hashAlgorithm, payloadSize,
                                               bytes.data() + kHeaderBytes,
                                               static_cast<std::size_t>(payloadSize));
  if (computed != storedDigest) {
    error = ParseError::BadDigest; return false;
  }

  entries.reserve(entryCount);
  std::unordered_set<std::uint32_t> ids;
  pos = kHeaderBytes;
  for (std::uint32_t i = 0; i < entryCount; ++i) {
    std::uint32_t id = 0u, typeRaw = 0u;
    double value = 0.0;
    if (!detail::ReadU32(bytes, pos, id) || !detail::ReadU32(bytes, pos, typeRaw)
        || !detail::ReadDouble(bytes, pos, value)) {
      error = ParseError::BadPayloadSize; return false;
    }
    if (id == 0u || !ids.insert(id).second) {
      error = ParseError::DuplicateId; return false;
    }
    if (typeRaw < static_cast<std::uint32_t>(ValueType::Continuous)
        || typeRaw > static_cast<std::uint32_t>(ValueType::Enumeration)) {
      error = ParseError::InvalidValueType; return false;
    }
    if (!std::isfinite(value)) {
      error = ParseError::NonFiniteValue; return false;
    }
    entries.push_back({id, static_cast<ValueType>(typeRaw), value});
  }
  return pos == bytes.size();
}

} // namespace crunchy::preset

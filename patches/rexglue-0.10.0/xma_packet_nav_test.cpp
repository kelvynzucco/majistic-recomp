#include "xma_packet_nav.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace {
void WriteHeader(std::vector<uint8_t>& buf, uint32_t pkt_idx, uint8_t b0, uint8_t b1, uint8_t b2,
                 uint8_t b3) {
  const size_t off = static_cast<size_t>(pkt_idx) * 2048;
  buf[off + 0] = b0;
  buf[off + 1] = b1;
  buf[off + 2] = b2;
  buf[off + 3] = b3;
}
}  // namespace

int main() {
  using rex::audio::xma::FindNextPacketReadOffset;
  constexpr uint32_t kBitsPerPacket = 2048 * 8;

  // 1. Single-stream (skip=0): continuation packet 03 FF F8 00 advances to pkt + 1.
  {
    std::vector<uint8_t> buf(4 * 2048, 0);
    WriteHeader(buf, 1, 0x03, 0xFF, 0xF8, 0x00);  // no new frame, skip=0 -> pkt 2
    WriteHeader(buf, 2, 0x0C, 0x00, 0x01, 0x00);  // valid frame at offset 32
    auto r = FindNextPacketReadOffset(buf.data(), 1, 4);
    assert(!r.reached_buffer_end);
    assert(r.read_offset == 2 * kBitsPerPacket + 32);
  }

  // 2. Multi-stream 6ch (eb55c234.gtd chunk 0): pkt 31 (03 FF F8 02) skips to pkt 34 (Stream 2),
  //    NOT pkt 32 (Stream 0).
  {
    std::vector<uint8_t> buf(64 * 2048, 0);
    WriteHeader(buf, 30, 0x03, 0xFF, 0xF8, 0x01);  // Stream 0 cont -> pkt 32
    WriteHeader(buf, 31, 0x03, 0xFF, 0xF8, 0x02);  // Stream 2 cont -> pkt 34
    WriteHeader(buf, 32, 0x0C, 0x00, 0x01, 0x02);  // Stream 0 start
    WriteHeader(buf, 33, 0x0C, 0x00, 0x01, 0x02);  // Stream 1 start
    WriteHeader(buf, 34, 0x0C, 0x00, 0x01, 0x02);  // Stream 2 start

    auto r0 = FindNextPacketReadOffset(buf.data(), 30, 64);
    assert(!r0.reached_buffer_end);
    assert(r0.read_offset == 32 * kBitsPerPacket + 32);

    auto r2 = FindNextPacketReadOffset(buf.data(), 31, 64);
    assert(!r2.reached_buffer_end);
    assert(r2.read_offset == 34 * kBitsPerPacket + 32);
  }

  // 3. Multi-stream with ALL_ZERO tail packet (eb55c234.gtd chunk 8):
  //    pkt 30 (03 FF F8 02) must skip over ALL_ZERO pkt 31 directly to pkt 33 (Stream 1).
  {
    std::vector<uint8_t> buf(64 * 2048, 0);
    WriteHeader(buf, 30, 0x03, 0xFF, 0xF8, 0x02);  // Stream 1 cont -> pkt 33
    // pkt 31 is ALL_ZERO (00 00 00 00)
    WriteHeader(buf, 32, 0x0C, 0x00, 0x01, 0x02);  // Stream 0
    WriteHeader(buf, 33, 0x0C, 0x00, 0x01, 0x02);  // Stream 1
    auto r = FindNextPacketReadOffset(buf.data(), 30, 64);
    assert(!r.reached_buffer_end);
    assert(r.read_offset == 33 * kBitsPerPacket + 32);
  }

  // 4. Multi-stream buffer-end deadlock case (eb55c234.gtd chunk 11):
  //    pkt 62 (03 FF F8 03) followed by ALL_ZERO pkt 63 must terminate buffer with overflow=2,
  //    NOT land on ALL_ZERO pkt 63.
  {
    std::vector<uint8_t> buf(64 * 2048, 0);
    WriteHeader(buf, 62, 0x03, 0xFF, 0xF8, 0x03);  // Stream 2 cont -> pkt 66 (overflow=2)
    // pkt 63 is ALL_ZERO
    auto r = FindNextPacketReadOffset(buf.data(), 62, 64);
    assert(r.reached_buffer_end);
    assert(r.read_offset == 32);
    assert(r.next_buffer_packet_index == 2);
  }

  // 5. Full packet skip (0xFF) terminates buffer with overflow=0.
  {
    std::vector<uint8_t> buf(8 * 2048, 0);
    WriteHeader(buf, 2, 0x03, 0xFF, 0xF8, 0xFF);
    auto r = FindNextPacketReadOffset(buf.data(), 2, 8);
    assert(r.reached_buffer_end);
    assert(r.read_offset == 32);
    assert(r.next_buffer_packet_index == 0);
  }

  // 6. Chained continuation packets across multiple hops.
  {
    std::vector<uint8_t> buf(12 * 2048, 0);
    WriteHeader(buf, 1, 0x03, 0xFF, 0xF8, 0x02);  // -> pkt 4
    WriteHeader(buf, 4, 0x03, 0xFF, 0xF8, 0x03);  // -> pkt 8
    WriteHeader(buf, 8, 0x08, 0x10, 0x01, 0x02);  // frame offset = (0x10 << 5) + 32 = 544
    auto r = FindNextPacketReadOffset(buf.data(), 1, 12);
    assert(!r.reached_buffer_end);
    assert(r.read_offset == 8 * kBitsPerPacket + 544);
  }

  return 0;
}

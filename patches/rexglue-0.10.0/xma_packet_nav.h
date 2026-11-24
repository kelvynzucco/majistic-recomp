#pragma once

#include <cstdint>

namespace rex::audio::xma {

struct NextPacketOffsetResult {
  // Bit offset within the buffer when a packet with a frame start is found;
  // equals kBitsPerPacketHeader (32) when no further packet exists in this buffer.
  uint32_t read_offset = 32;
  // Packet index overflow past packet_count when crossing into the next input buffer
  // (for multi-stream interleaved XMA2), or 0 if terminated by skip_count == 0xFF.
  uint32_t next_buffer_packet_index = 0;
  bool reached_buffer_end = true;
};

// Advance across packets in an XMA2 input buffer until a packet containing a frame
// start (packet_frame_offset <= kMaxFrameSizeinBits) is found or the buffer ends.
// Continuation packets with no frame start (e.g. header 03 FF F8 <skip>,
// packet_frame_offset == 32799) advance using their own skip_count so interleaved
// multi-stream XMA2 streams never step into sibling streams or zero-padded tail packets.
inline NextPacketOffsetResult FindNextPacketReadOffset(const uint8_t* buffer,
                                                       uint32_t next_packet_index,
                                                       uint32_t packet_count) {
  constexpr uint32_t kBytesPerPacket = 2048;
  constexpr uint32_t kBitsPerPacket = kBytesPerPacket * 8;
  constexpr uint32_t kBitsPerPacketHeader = 32;
  constexpr uint32_t kMaxFrameSizeinBits = 0x4000 - kBitsPerPacketHeader;

  while (next_packet_index < packet_count) {
    const uint8_t* pkt = buffer + (static_cast<size_t>(next_packet_index) * kBytesPerPacket);
    const uint32_t frame_offset =
        static_cast<uint16_t>(((pkt[0] & 0x3) << 13) | (pkt[1] << 5) | (pkt[2] >> 3)) +
        kBitsPerPacketHeader;

    if (frame_offset <= kMaxFrameSizeinBits) {
      return {next_packet_index * kBitsPerPacket + frame_offset, 0, false};
    }

    const uint8_t skip_count = pkt[3];
    if (skip_count == 0xFF) {
      return {kBitsPerPacketHeader, 0, true};
    }
    next_packet_index += static_cast<uint32_t>(skip_count) + 1;
  }

  return {kBitsPerPacketHeader,
          next_packet_index >= packet_count ? (next_packet_index - packet_count) : 0, true};
}

}  // namespace rex::audio::xma

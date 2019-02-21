// Copyright 2013-2019 Stanford University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef STOKE_SRC_STATE_MEMORY_H
#define STOKE_SRC_STATE_MEMORY_H

#include <cassert>
#include <iostream>
#include <stdint.h>

#include "src/ext/cpputil/include/container/bit_vector.h"
#include "src/ext/cpputil/include/io/fail.h"

#include "src/state/addr_iterator.h"

namespace stoke {

class Memory {
public:
  /** Creates an empty memory. */
  Memory() {
    resize(0, 0);
  }

  /** Sets the virtual address base and size. Rounds down to 256-bit align. Pads with headroom. */
  Memory& resize(uint64_t base, size_t size) {
    // Round down to 256-bit (32-byte) align.
    base_ = base & 0xffffffffffffffe0;
    // Pad size with amount rounded down and 256-bits (32-bytes) of headroom.
    size += (base % 32) + 32;
    // Round size up to align to an 8 byte boundary.
    size = (size + 7) & 0xfffffffffffffff8;

    // Make sure that we don't overflow past the 64-bit address space (noting
    // that 32 bytes of headroom could "wrap around" into the beginning of the
    // address space.)
    assert(base_ + size > base_ || base_ + size <= 0x20);

    contents_.resize_for_fixed_bytes(size);
    valid_.resize_for_bits(size);

    return *this;
  }

  /** Zeros memory and resets valid bits. */
  void clear() {
    contents_.reset();
    valid_.reset();
  }
  /** Copy state from another memory. */
  void copy(const Memory& rhs) {
    assert(base_ == rhs.base_);

    assert(contents_.num_fixed_bytes() == rhs.contents_.num_fixed_bytes());
    contents_.copy(rhs.contents_);

    assert(valid_.num_fixed_bytes() == rhs.valid_.num_fixed_bytes());
    valid_.copy(rhs.valid_);
  }

  /** Logical memory size; doesn't include headroom. */
  size_t size() const {
    return contents_.num_fixed_bytes() - 32;
  }
  /** Lower bound on valid addresses; returns aligned value. */
  uint64_t lower_bound() const {
    return base_;
  }

  /** Upper bound on valid addresses; doesn't include headroom. */
  // WARNING IMPORTANT: uint64_t overflow risk when, e.g. addr=0xffffffffffffffe0, size=0x20
  // do not compare to upper_bound()!!
  uint64_t upper_bound() const {
    return base_ + size();
  }
  /** Returns true if a virtual address is contained in this memory; does not include headroom. */
  bool in_range(uint64_t addr) const {
    return addr >= lower_bound() && addr - base_ < size();
  }

  /** Element access; undefined for invalid bytes */
  uint8_t& operator[](size_t addr) {
    assert(is_valid(addr));
    return contents_.get_fixed_byte(addr - base_);
  }
  /** Element access; undefined for invalid bytes */
  uint8_t operator[](size_t addr) const {
    assert(is_valid(addr));
    return contents_.get_fixed_byte(addr - base_);
  }
  /** Element access; undefined for invalid quads */
  uint64_t get_quad(size_t addr) const {
    assert(is_valid_quad(addr));
    assert((addr - base_) % 8 == 0);
    return contents_.get_fixed_quad((addr - base_)/8);
  }

  /** Pointer to underlying data. */
  void* data() {
    return contents_.data();
  }
  /** Pointer to the valid bit mask. */
  void* valid_mask() {
    return valid_.data();
  }

  /** Returns true if a byte is valid. */
  bool is_valid(uint64_t addr) const {
    assert(in_range(addr));
    return valid_[addr - base_];
  }
  /** Sets this byte as valid. */
  Memory& set_valid(uint64_t addr, bool v) {
    assert(in_range(addr));
    valid_[addr - base_] = v;
    return *this;
  }
  /** Returns true if a quad is valid. */
  bool is_valid_quad(uint64_t addr) const {
    assert(in_range(addr) && in_range(addr+7));
    assert((addr - base_) % 8 == 0);

    return valid_.get_fixed_byte((addr-base_)/8) == 0xff;
  }
  /** Returns a pointer to the beginning of the valid byte addrs in this memory. */
  addr_iterator valid_begin() const {
    return addr_iterator(valid_.set_bit_index_begin(), base_);
  }
  /** Returns a pointer to the end of the valid byte addrs in this memory. */
  addr_iterator valid_end() const {
    return addr_iterator(valid_.set_bit_index_end(), base_);
  }

  /** Bit-wise xor; ignores shadows. */
  Memory& operator^=(const Memory& rhs) {
    contents_ ^= rhs.contents_;
    return *this;
  }
  /** Bit-wise xor; ignores shadows. */
  Memory operator^(const Memory& rhs) {
    auto ret = *this;
    return ret ^= rhs;
  }

  /** Comparison based on components; ignores shadows. */
  bool operator==(const Memory& rhs) const {
    return base_ == rhs.base_ && contents_ == rhs.contents_;
  }
  /** Comparison based on components; ignores shadows. */
  bool operator!=(const Memory& rhs) const {
    return base_ != rhs.base_ || contents_ != rhs.contents_;
  }

  /** Write text. */
  std::ostream& write_text(std::ostream& os) const;
  /** Read text. */
  std::istream& read_text(std::istream& is);

private:
  /** Virtual base address. */
  uint64_t base_;
  /** Virtual memory sandbox. */
  cpputil::BitVector contents_;
  /** Shadow bit vector for tracking valid bytes. */
  cpputil::BitVector valid_;

  /** Write a text summary of memory. */
  void write_text_summary(std::ostream& os) const;
  /** Write a text row of memory. */
  void write_text_row(std::ostream& os, uint64_t addr) const;
  /** Write all text rows from memory. */
  void write_text_contents(std::ostream& os) const;

  /** Read a text summary of memory. */
  void read_text_summary(std::istream& is);
  /** Read a text row of memory. */
  void read_text_row(std::istream& is);
  /** Read all text rows of memory. */
  void read_text_contents(std::istream& is);

  /** Does this row contain at least one valid address? */
  bool valid_row(uint64_t addr) const;
  /** How many of the rows in this memory are valid? */
  size_t valid_count() const;
};

} // namespace stoke

#endif

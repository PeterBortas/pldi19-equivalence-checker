// Copyright 2013-2019 Stanford University
//
// Licensed under the Apache License, Version 2.0 (the License);
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an AS IS BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#ifndef STOKE_SRC_SYMSTATE_MEMORY_TRIVIAL_H
#define STOKE_SRC_SYMSTATE_MEMORY_TRIVIAL_H

#include "src/symstate/bitvector.h"
#include "src/symstate/memory.h"

namespace stoke {

/** Memory models all reads as seeing a new unconstrained value.  Records
  the address/value of all reads/writes.  Could, theoretically, be used to
  build a complete memory model by adding constraints to the read values. */
class TrivialMemory : public SymMemory {

public:

  struct SymbolicAccess {
    DereferenceInfo info;          // line number?
    size_t size;          // how many bytes are we accessing?
    SymBitVector address; // symbolic address
    SymBitVector value;   // symbolic value
    bool is_read;         // read or write?
    bool is_rewrite;      // for the client to use, if they like
  };

  /** Updates the memory with a write.
   *  Returns condition for segmentation fault */
  SymBool write(SymBitVector address, SymBitVector value, uint16_t size, DereferenceInfo info) {

    SymbolicAccess sa;
    sa.info = info;
    sa.size = size/8;
    sa.address = address;
    sa.value = value;
    sa.is_read = false;
    write_list_.push_back(sa);
    all_list_.push_back(sa);

    return SymBool::_false();
  }

  /** Reads from the memory.  Returns value and segv condition. */
  std::pair<SymBitVector,SymBool> read(SymBitVector address, uint16_t size, DereferenceInfo info) {

    auto value = SymBitVector::tmp_var(size);

    SymbolicAccess sa;
    sa.info = info;
    sa.size = size/8;
    sa.address = address;
    sa.value = value;
    sa.is_read = true;
    read_list_.push_back(sa);
    all_list_.push_back(sa);

    return std::pair<SymBitVector, SymBool>(value, SymBool::_false());
  }

  /** Get all the reads */
  std::vector<SymbolicAccess> get_reads() {
    return read_list_;
  }

  /** Get all the writes */
  std::vector<SymbolicAccess> get_writes() {
    return write_list_;
  }

  /** Get all the accesses */
  std::vector<SymbolicAccess> get_all() {
    return all_list_;
  }

  /** Get list of accesses accessed (via read or write).  This is needed for
   * marking relevant cells valid in the counterexample. */
  std::map<const SymBitVectorAbstract*, uint64_t> get_access_list() {
    std::map<const SymBitVectorAbstract*, uint64_t> m;
    for (auto it : all_list_) {
      m[it.address.ptr] = it.size;
    }
    return m;
  }

  std::string get_type() const {
    return "trivial";
  }

private:

  /** List of all reads */
  std::vector<SymbolicAccess> read_list_;
  /** List of all writes */
  std::vector<SymbolicAccess> write_list_;
  /** List of all accesses */
  std::vector<SymbolicAccess> all_list_;

};

};

#endif

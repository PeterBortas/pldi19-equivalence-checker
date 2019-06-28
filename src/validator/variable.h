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

#include <string>

#include "src/ext/x64asm/include/x64asm.h"
#include "src/state/cpu_state.h"
#include "src/symstate/state.h"

#ifndef STOKE_SRC_VALIDATOR_VARIABLE_H
#define STOKE_SRC_VALIDATOR_VARIABLE_H

namespace stoke {

struct Variable {

  // For all
  bool is_rewrite;
  size_t size;         // the number of bytes this variable corresponds to
  int32_t offset;      // where to find the value in the whole operand
  long coefficient;    // the multiplicative coefficient used for invariants

  // For registers and memory
  x64asm::Operand operand;
  bool is_lea;        // variable refers to address, not actual value.

  // For ghosts
  bool is_ghost;
  std::string name;

  /** From an abstract state, find the abstract value of this term. */
  SymBitVector from_state(SymState& target, SymState& rewrite) const;

  /** From a concrete state, find the value of this term. */
  uint64_t from_state(const CpuState& target, const CpuState& rewrite) const;
  /** From a concrete state, find the value of this term. */
  cpputil::BitVector from_state_vector(const CpuState& target, const CpuState& rewrite) const;


  /** Is this variable safe to use?  i.e. does it not derference bad memory? */
  bool is_valid(const CpuState& target, const CpuState& rewrite) const;
  bool is_valid(const std::vector<CpuState>& target, const std::vector<CpuState>& rewrite) const;

  /** Does this have a memory dereference? */
  bool is_dereference() const;
  bool is_stack() const {
    assert(is_dereference());
    auto mem = static_cast<const x64asm::Mem*>(&operand);
    return mem->get_base() == x64asm::rsp || mem->get_base() == x64asm::rbp;
  }
  bool is_related(const Variable& v) const {
    //std::cout << "checking if from same program..." << std::endl;
    if (is_rewrite != v.is_rewrite)
      return false;
    //std::cout << "checking if ghost flag differs ..." << std::endl;
    if (is_ghost != v.is_ghost)
      return false;
    //std::cout << "checking if ghosts name matches ..." << std::endl;
    if (is_ghost)
      return name == v.name;

    //std::cout << "checking if memory status differs ..." << std::endl;
    if (operand.is_typical_memory() != v.operand.is_typical_memory())
      return false;

    if (operand.is_typical_memory()) {
      //std::cout << "comparing memory..." << std::endl;
      auto m1 = *static_cast<const x64asm::Mem*>(&operand);
      auto m2 = *static_cast<const x64asm::Mem*>(&v.operand);
      if (m1.contains_base() != m2.contains_base())
        return false;
      if (m1.contains_index() != m2.contains_index())
        return false;
      if (m1.contains_base() && m1.get_base() != m2.get_base())
        return false;
      if (m1.contains_index() && m1.get_index() != m2.get_index())
        return false;
      return true;
    } else {
      //std::cout << "comparing operand" << std::endl;
      return operand == v.operand;
    }
  }
  /** From a concrete state, get the address of the memory dereference. */
  uint64_t get_addr(const CpuState& target, const CpuState& rewrite) const;
  SymBitVector get_addr(const SymState& target, const SymState& rewrite) const;

  /** Make basic block ghost variable. */
  static Variable bb_ghost(size_t n, bool is_rewrite);

  /** Get basic block from ghost variable. */
  size_t get_ghost_bb();

  static Variable lea_variable(x64asm::Mem m, bool is_rewrite);

  Variable(x64asm::Operand op, bool rewrite) : is_rewrite(rewrite), size(op.size()/8), offset(0),
    coefficient(1), operand(op), is_lea(false), is_ghost(false), name("") { }

  Variable(x64asm::Operand op, bool rewrite, size_t sz, int32_t off) : is_rewrite(rewrite), size(sz),
    offset(off), coefficient(1), operand(op), is_lea(false), is_ghost(false), name("") { }

  Variable(std::string var, bool rewrite, size_t sz=8) : is_rewrite(rewrite), size(sz),
    offset(0), coefficient(1), operand(x64asm::rax), is_lea(false), is_ghost(true), name(var) { }

  Variable(std::istream& is) : operand(x64asm::rax), is_lea(false) {
    deserialize(is);
  }

  bool operator<(const Variable& other) const {
    if (is_rewrite != other.is_rewrite)
      return is_rewrite < other.is_rewrite;
    if (size != other.size)
      return size < other.size;
    if (coefficient != other.coefficient)
      return coefficient < other.coefficient;
    if (operand != other.operand)
      return operand < other.operand;
    if (name != other.name)
      return name < other.name;
    if (is_ghost != other.is_ghost)
      return is_ghost < other.is_ghost;
    if (offset != other.offset)
      return offset < other.offset;
    return false;
  }

  bool operator==(const Variable& other) const {
    if (is_rewrite != other.is_rewrite)
      return false;
    if (size != other.size)
      return false;
    if (coefficient != other.coefficient)
      return false;
    if (operand != other.operand)
      return false;
    if (name != other.name)
      return false;
    if (is_ghost != other.is_ghost)
      return false;
    if (offset != other.offset)
      return false;
    return true;
  }

  bool operator!=(const Variable& other) const {
    return !(*this == other);
  }

  std::ostream& serialize(std::ostream& out) const;
  std::istream& deserialize(std::istream& is);
};

}

namespace std {
std::ostream& operator<<(std::ostream&, const stoke::Variable&);
}

#endif

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

#ifndef STOKE_SRC_VALIDATOR_INVARIANT_MEMORY_NULL_H
#define STOKE_SRC_VALIDATOR_INVARIANT_MEMORY_NULL_H

#include "src/validator/invariant.h"

namespace stoke {

class MemoryNullInvariant : public Invariant {

public:
  using Invariant::check;

  // TODO: rewrite to use Variable class
  MemoryNullInvariant(x64asm::Mem m, bool is_rewrite, bool is_null)
    : m_(m), is_rewrite_(is_rewrite), is_null_(is_null) {
    is_rewrite_ = is_rewrite;
  }

  SymBool operator()(SymState& target, SymState& rewrite, size_t& number) {

    set_di(target, number, false);
    set_di(rewrite, number, true);

    //std::cout << "Visiting ";
    write(std::cout);
    //std::cout << " with no " << number << std::endl;

    auto& state = is_rewrite_ ? rewrite : target;

    auto result = state.lookup(m_) == SymBitVector::constant(m_.size(), 0);

    if (is_null_)
      return result;
    else
      return !result;
  }

  bool check(const CpuState& target, const CpuState& rewrite) const {

    auto& state = is_rewrite_ ? rewrite : target;

    if (!state.in_range(m_) || !state.is_valid(m_)) {
      /*
      std::cout << state << std::endl;
      std::cout << " * " << m_ << std::endl;
      std::cout << " * state not valid" << std::endl;
      */
      return false;
    }

    auto mem_val = state[m_];

    bool zero = true;
    for (size_t i = 0; i < m_.size()/8; ++i) {
      if (mem_val.get_fixed_byte(i) != 0) {
        zero = false;
        //std::cout << " * found non-zero byte" << std::endl;
        break;
      }
    }

    if (is_null_)
      return zero;
    else
      return !zero;
  }

  virtual std::vector<Variable> get_variables() const {
    std::vector<Variable> result;
    return result;
  }

  std::ostream& write(std::ostream& os) const {
    os << m_;
    if (is_rewrite_)
      os << "'";
    if (is_null_)
      os << " == 0";
    else
      os << " != 0";

    return os;
  }

  virtual std::ostream& serialize(std::ostream& out) const {
    out << "MemoryNullInvariant" << std::endl;
    const x64asm::Operand* op = static_cast<const x64asm::Operand*>(&m_);
    out << *op << " " << is_rewrite_ << " " << is_null_ << std::endl;
    return out;
  }

  MemoryNullInvariant(std::istream& is) : m_(x64asm::M8(x64asm::Imm32(0))) {
    x64asm::M8 m8(x64asm::Imm32(0));
    is >> std::ws >> m8 >> is_rewrite_ >> is_null_;
    CHECK_STREAM(is);
    m_ = *static_cast<x64asm::Mem*>(&m8);
  }

  std::shared_ptr<Invariant> clone() const override {
    return std::make_shared<MemoryNullInvariant>(m_, is_rewrite_, is_null_);
  }

private:

  x64asm::Mem m_;
  bool is_rewrite_;
  bool is_null_;

};

} // namespace stoke



#endif

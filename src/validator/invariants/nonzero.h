// Copyright 2013-2016 Stanford University
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

#ifndef STOKE_SRC_VALIDATOR_INVARIANT_NONZERO_H
#define STOKE_SRC_VALIDATOR_INVARIANT_NONZERO_H

#include "src/validator/invariant.h"

namespace stoke {

class NonzeroInvariant : public Invariant {

public:
  using Invariant::check;

  // negate false -> check if nonzero
  NonzeroInvariant(Variable v, bool negate = false) : variable_(v), negate_(negate) {
  }

  SymBool operator()(SymState& target, SymState& rewrite, size_t& number) {

    set_di(target, number, false);
    set_di(rewrite, number, true);

    /*
    std::cout << "NonzeroInvariant variable_.size = " << variable_.size << std::endl;
    std::cout << "variable_.from_state(target,rewrite).width() = "
         << variable_.from_state(target, rewrite).width() << std::endl; */

    if (!negate_)
      return variable_.from_state(target, rewrite) != SymBitVector::constant(variable_.size*8, 0);
    else
      return variable_.from_state(target, rewrite) == SymBitVector::constant(variable_.size*8, 0);
  }

  bool check(const CpuState& target, const CpuState& rewrite) const {
    if (!negate_)
      return !(variable_.from_state(target,rewrite) == 0);
    else
      return (variable_.from_state(target,rewrite) == 0);
  }

  virtual std::vector<Variable> get_variables() const {
    std::vector<Variable> result;
    result.push_back(variable_);
    return result;
  }

  std::ostream& write(std::ostream& os) const {
    if (!negate_)
      os << variable_ << " != 0";
    else
      os << variable_ << " == 0";
    return os;
  }

  virtual std::ostream& serialize(std::ostream& out) const {
    out << "NonzeroInvariant" << std::endl;
    variable_.serialize(out);
    out << negate_ << std::endl;
    return out;
  }

  NonzeroInvariant(std::istream& is) : variable_(is) {
    is >> negate_;
    CHECK_STREAM(is);
  }

  std::shared_ptr<Invariant> clone() const override {
    return std::make_shared<NonzeroInvariant>(variable_, negate_);
  }

  virtual bool does_not_imply(std::shared_ptr<Invariant> inv) const override {
    auto casted = std::dynamic_pointer_cast<NonzeroInvariant>(inv);
    if (casted) {
      return !variable_.is_related(casted->variable_);
    } else {
      return false;
    }
  }

private:

  Variable variable_;
  bool negate_;

};

} // namespace stoke



#endif

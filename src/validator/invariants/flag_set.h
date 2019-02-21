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

#ifndef STOKE_SRC_VALIDATOR_INVARIANT_FLAG_SET_H
#define STOKE_SRC_VALIDATOR_INVARIANT_FLAG_SET_H

#include "src/validator/handler.h"
#include "src/validator/handlers/conditional_handler.h"
#include "src/validator/invariant.h"


namespace stoke {

class FlagSetInvariant : public Invariant {

public:
  using Invariant::check;

  FlagSetInvariant(x64asm::Eflags ef, bool is_rewrite, bool invert = false) : flag_(ef) {
    is_rewrite_ = is_rewrite;
    invert_ = invert;
  }

  SymBool operator()(SymState& left, SymState& right, size_t& number) {
    auto& state = is_rewrite_ ? right : left;

    if (invert_)
      return !state[flag_];
    else
      return state[flag_];
  }

  bool check(const CpuState& target, const CpuState& rewrite) const {
    auto& state = is_rewrite_ ? rewrite : target;

    if (invert_)
      return !state[flag_];
    else
      return state[flag_];
  }

  std::ostream& write(std::ostream& os) const {
    if (invert_)
      os << "!";

    if (is_rewrite_) {
      os << flag_ << "'";
    } else {
      os << flag_;
    }

    return os;
  }

  virtual std::ostream& serialize(std::ostream& out) const {
    out << "FlagSetInvariant" << std::endl;
    out << flag_ << " " << is_rewrite_ << " " << invert_ << std::endl;
    return out;
  }

  FlagSetInvariant(std::istream& is) : flag_(x64asm::eflags_cf) {
    is >> flag_ >> std::ws >> is_rewrite_ >> std::ws >> invert_ >> std::ws;
    CHECK_STREAM(is);
  }

  std::shared_ptr<Invariant> clone() const override {
    return std::make_shared<FlagSetInvariant>(flag_, is_rewrite_, invert_);
  }


private:

  x64asm::Eflags flag_;
  bool is_rewrite_;
  bool invert_;

};

} // namespace stoke



#endif

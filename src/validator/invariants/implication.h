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

#ifndef STOKE_SRC_VALIDATOR_INVARIANT_IMPLICATION_H
#define STOKE_SRC_VALIDATOR_INVARIANT_IMPLICATION_H

#include "src/validator/invariant.h"

namespace stoke {

class ImplicationInvariant : public Invariant {

public:
  using Invariant::check;

  ImplicationInvariant(std::shared_ptr<Invariant> a, std::shared_ptr<Invariant> b) : a_(a), b_(b) { }

  SymBool operator()(SymState& left, SymState& right, size_t& number) {

    auto a = (*a_)(left, right, number);
    number++;
    auto b = (*b_)(left, right, number);

    return !a | b;
  }

  void get_dereference_map(DereferenceMap& deref_map, const CpuState& target, const CpuState& rewrite, size_t& number) {
    a_->get_dereference_map(deref_map, target, rewrite, number);
    number++;
    b_->get_dereference_map(deref_map, target, rewrite, number);
  };


  bool check (const CpuState& target, const CpuState& rewrite) const {

    auto a = a_->check(target, rewrite);
    auto b = b_->check(target, rewrite);

    return !a | b;
  }

  std::ostream& write(std::ostream& os) const {

    os << "( ";
    a_->write(os);
    os << " => ";
    b_->write(os);
    os << " )";

    return os;
  }

  virtual std::vector<Variable> get_variables() const {
    std::vector<Variable> result;
    auto a = a_->get_variables();
    auto b = b_->get_variables();
    result.insert(result.begin(), a.begin(), a.end());
    result.insert(result.begin(), b.begin(), b.end());
    return result;
  }

  virtual std::ostream& serialize(std::ostream& out) const override {
    out << "ImplicationInvariant" << std::endl;
    a_->serialize(out);
    b_->serialize(out);
    return out;
  }

  ImplicationInvariant(std::istream& is) {
    a_ = Invariant::deserialize(is);
    CHECK_STREAM(is);
    b_ = Invariant::deserialize(is);
    CHECK_STREAM(is);
  }

  std::shared_ptr<Invariant> clone() const override {
    return std::make_shared<ImplicationInvariant>(a_->clone(), b_->clone());
  }

  virtual bool is_critical() override {
    return b_->is_critical();
  }

  virtual bool is_postponable() const override {
    return b_->is_postponable();
  }


private:

  std::shared_ptr<Invariant> a_;
  std::shared_ptr<Invariant> b_;

};

} // namespace stoke



#endif

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

// Copyrewrite 2013-2015 Stanford University
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

#ifndef STOKE_SRC_VALIDATOR_INVARIANT_EXPR_H
#define STOKE_SRC_VALIDATOR_INVARIANT_EXPR_H

#include "src/expr/expr.h"
#include "src/expr/expr_parser.h"
#include "src/validator/invariant.h"

namespace stoke {

class ExprInvariant : public Invariant {

public:
  using Invariant::check;

  ExprInvariant(Expr<uint64_t>* e, std::string orig_str) : expr_(e), original_(orig_str) {

  }

  /** Check if this invariant holds over an abstract state */
  SymBool operator()(SymState& target, SymState& rewrite, size_t& number) {

    set_di(target, number, false);
    set_di(rewrite, number, true);

    std::function<SymBitVector (const std::string& s)> env = [&](const std::string& s) -> SymBitVector {
      auto variable = parse_variable(s);
      return variable.from_state(target, rewrite);
    };

    Expr<SymBitVector>* bv_expr = expr_->map<SymBitVector>();
    auto value = (*bv_expr)(env);
    auto result = SymBool(value[0]);

    return result;
  }

  /** Check if this invariant holds over a concrete state. */
  bool check(const CpuState& target, const CpuState& rewrite) const {

    std::function<uint64_t (const std::string& s)> env = [&](const std::string& s) -> uint64_t {
      auto variable = parse_variable(s);
      return variable.from_state(target, rewrite);
    };

    auto value = (*expr_)(env);

    return (bool)value;
  }

  std::ostream& write(std::ostream& os) const {
    os << "(" << original_ << ")";
    return os;
  }

  virtual std::vector<Variable> get_variables() const {
    return {};
  }

  virtual std::ostream& serialize(std::ostream& out) const {
    out << "ExprInvariant" << std::endl;
    out << original_ << std::endl;
    return out;
  }

  ExprInvariant(std::istream& is) : expr_(NULL) {
    is >> std::ws;
    CHECK_STREAM(is);
    getline(is, original_);
    CHECK_STREAM(is);
    //std::cout << "Got line: " << original_ << std::endl;
    expr_ = parse(original_);
  }

  static Expr<uint64_t>* parse(std::string s)  {
    //std::cout << "Attempting parse of " << s << std::endl;
    ExprParser<uint64_t> parser(s, [] (const std::string& s) -> bool {
      //std::cout << "Got variable " << s << std::endl;
      try {
        parse_variable(s);
        //std::cout << "Parse success" << std::endl;
        return true;
      } catch (std::string s) {
        std::cerr << s << std::endl;
        return false;
      }
      std::cout << "Shouldn't be here" << std::endl;
      return false;
    });
    if (!parser.has_error()) {
      auto expr = parser.get();
      //expr->print_dot();
      return expr;
    }
    else {
      std::cout << __FILE__ << ":" << __LINE__
                << ": parser got error: " << parser.get_error() << std::endl;
      return NULL;
    }
  }

  std::shared_ptr<Invariant> clone() const override {
    return std::make_shared<ExprInvariant>(expr_, original_);
  }


private:

  static Variable parse_variable(const std::string& s) {
    if (s.size() < 3 || s[1] != '_' || (s[0] != 't' && s[0] != 'r')) {
      std::stringstream ss;
      ss << __FILE__ << ":" << __LINE__
         << ": Invalid environment variable. " << s << std::endl;
      throw ss.str();
    }

    bool is_rewrite = s[0] == 'r';

    if (s[2] == '%' || s[2] == '(' || s[2] == '0') {
      std::stringstream ss;
      x64asm::Operand r(x64asm::rax);
      std::string op_str = s.substr(2);
      ss << op_str;
      ss >> r;
      if (ss.bad() || ss.fail()) {
        std::stringstream ss;
        ss << __FILE__ << ":" << __LINE__
           << ": Could not parse operand " << op_str << std::endl;
        throw ss.str();
      }

      if (r.is_typical_memory()) {
        x64asm::M64 m(*static_cast<x64asm::M8*>(&r));
        Variable v(m, is_rewrite);
        return v;
      } else {
        Variable v(r, is_rewrite);
        return v;
      }
    } else {
      std::string ghost_name = s.substr(2);
      Variable v(ghost_name, is_rewrite);
      return v;
    }
  }


  Expr<uint64_t>* expr_;
  std::string original_;

};

} // namespace stoke



#endif

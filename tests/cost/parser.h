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


#include <set>
#include <sstream>

#include "src/cfg/cfg.h"
#include "src/cost/cost_function.h"
#include "src/cost/cost_parser.h"
#include "src/cost/expr.h"

namespace stoke {

class CostParserTest : public ::testing::Test {

public:
  CostParserTest() : a_(2), b_(3), c_(7) {}

protected:

  /** Parses the string s and returns the cost function */
  ExprCost* parse(std::string s) {
    Cfg empty({}, x64asm::RegSet::empty(), x64asm::RegSet::empty());;
    CostParser cp(s, table_);

    return cp.run();
  }

  /** Parses the string s and tests the cost. */
  Cost check(std::string s) {
    Cfg empty({}, x64asm::RegSet::empty(), x64asm::RegSet::empty());;
    CostParser cp(s, table_);

    auto cf = cp.run();
    EXPECT_TRUE(cf);
    EXPECT_EQ("", cp.get_error());

    if (cf) {
      auto result = (*cf)(empty).second;
      return result;
    } else {
      return 0ul;
    }
  }

  /** Parses the string s and checks for an error. */
  std::string check_err(std::string s) {
    Cfg empty({}, x64asm::RegSet::empty(), x64asm::RegSet::empty());;
    CostParser cp(s, table_);

    auto cf = cp.run();
    EXPECT_FALSE(cf) << "'" << s << "' parsed ok";
    auto result = cp.get_error();
    return result;
  }

  /** Leaf cost functions that can be used. */
  ExprCost a_; // 2
  ExprCost b_; // 3
  ExprCost c_; // 7

private:

  CostParser::SymbolTable table_;

  void SetUp() {
    table_["a"] = &a_;
    table_["bb"] = &b_;
    table_["ccc"] = &c_;
  }
};

TEST_F(CostParserTest, Trivial) {
  EXPECT_EQ(1ul, check("1"));
}

TEST_F(CostParserTest, Addition) {
  EXPECT_EQ(5ul, check("a+bb"));
}

TEST_F(CostParserTest, Subtraction) {
  EXPECT_EQ(1ul, check("bb-a"));
}

TEST_F(CostParserTest, SpacesWork) {
  EXPECT_EQ(1ul, check("bb -a   "));
}

TEST_F(CostParserTest, Multiplication) {
  EXPECT_EQ(6ul, check("bb*a"));
}

TEST_F(CostParserTest, Division) {
  EXPECT_EQ(3ul, check("ccc/a"));
}

TEST_F(CostParserTest, Modulus) {
  EXPECT_EQ(1ul, check("ccc % bb"));
  EXPECT_EQ(1ul, check("ccc % a"));
  EXPECT_EQ(0ul, check("(ccc+1) % a"));
  EXPECT_EQ(2ul, check("(ccc+1) % bb"));
}

TEST_F(CostParserTest, And) {
  EXPECT_EQ(3ul, check("ccc & bb"));
  EXPECT_EQ(2ul, check("ccc & a"));
  EXPECT_EQ(0ul, check("(ccc+1) & a"));
  EXPECT_EQ(0ul, check("(ccc+1) & bb"));
}

TEST_F(CostParserTest, Or) {
  EXPECT_EQ(7ul,  check("ccc | bb"));
  EXPECT_EQ(7ul,  check("ccc | a"));
  EXPECT_EQ(10ul, check("(ccc+1) | a"));
  EXPECT_EQ(11ul, check("(ccc+1) | bb"));
}

TEST_F(CostParserTest, Shl) {
  EXPECT_EQ(7ul*8ul,  check("ccc << bb"));
  EXPECT_EQ(28ul,     check("ccc << a"));
  EXPECT_EQ(32ul,     check("(ccc+1) << a"));
  EXPECT_EQ(64ul,     check("(ccc+1) << bb"));
}

TEST_F(CostParserTest, Shr) {
  EXPECT_EQ(0ul,  check("ccc >> bb"));
  EXPECT_EQ(1ul,  check("ccc >> a"));
  EXPECT_EQ(2ul,  check("(ccc+1) >> a"));
  EXPECT_EQ(1ul,  check("(ccc+1) >> bb"));
}

TEST_F(CostParserTest, Lt) {
  EXPECT_EQ(0ul,  check("ccc < bb"));
  EXPECT_EQ(0ul,  check("ccc < a"));
  EXPECT_EQ(1ul,  check("a < ccc"));
  EXPECT_EQ(1ul,  check("bb < ccc"));
  EXPECT_EQ(0ul,  check("ccc < ccc"));
}

TEST_F(CostParserTest, Gt) {
  EXPECT_EQ(1ul,  check("ccc > bb"));
  EXPECT_EQ(1ul,  check("ccc > a"));
  EXPECT_EQ(0ul,  check("a > ccc"));
  EXPECT_EQ(0ul,  check("bb > ccc"));
  EXPECT_EQ(0ul,  check("ccc > ccc"));
}

TEST_F(CostParserTest, Lte) {
  EXPECT_EQ(0ul,  check("ccc <= bb"));
  EXPECT_EQ(0ul,  check("ccc <= a"));
  EXPECT_EQ(1ul,  check("a   <= ccc"));
  EXPECT_EQ(1ul,  check("bb  <= ccc"));
  EXPECT_EQ(1ul,  check("ccc <= ccc"));
}

TEST_F(CostParserTest, Gte) {
  EXPECT_EQ(1ul,  check("ccc >= bb"));
  EXPECT_EQ(1ul,  check("ccc >= a"));
  EXPECT_EQ(0ul,  check("a   >= ccc"));
  EXPECT_EQ(0ul,  check("bb  >= ccc"));
  EXPECT_EQ(1ul,  check("ccc >= ccc"));
}

TEST_F(CostParserTest, Eq) {
  EXPECT_EQ(0ul,  check("ccc == bb"));
  EXPECT_EQ(0ul,  check("ccc == a"));
  EXPECT_EQ(0ul,  check("a   == ccc"));
  EXPECT_EQ(0ul,  check("bb  == ccc"));
  EXPECT_EQ(1ul,  check("ccc == ccc"));
}

TEST_F(CostParserTest, TimesBeforePlus) {
  EXPECT_EQ(13ul,  check("7+3*2"));
  EXPECT_EQ(20ul,  check("(7+3)*2"));
  EXPECT_EQ(23ul,  check("ccc*bb+a"));
}

TEST_F(CostParserTest, VariableNotFound) {
  check_err("aa");
}

TEST_F(CostParserTest, MiscelaneousErrors) {
  check_err("1+");
  check_err("1+()");
  check_err("(1,2)");
  check_err("a++bb");
  check_err("1+1b");
  check_err("b1+1");
  check_err("(1+3)(2+4)");
  check_err("+a-3");
}

TEST_F(CostParserTest, DoLogic) {
  EXPECT_EQ(1ul, check("(3 > 2) & (3 >= 3)"));
  EXPECT_EQ(0ul, check("(2 > 2) & (3 >= 3)"));
  EXPECT_EQ(1ul, check("(2 > 2) | (3 >= 3)"));
  EXPECT_EQ(0ul, check("(2 > 2) | (3 > 3)"));
}

TEST_F(CostParserTest, LeafFunctions) {

  auto cf = parse("1 + a");
  std::set<CostFunction*> a;
  a.insert(static_cast<CostFunction*>(&a_));

  EXPECT_EQ(a, cf->leaf_functions());

}

TEST_F(CostParserTest, NoLeafFunctions) {
  auto cf = parse("1 + 3*4");
  std::set<CostFunction*> a;
  EXPECT_EQ(a, cf->leaf_functions());
}

TEST_F(CostParserTest, TwoLeafFunctions) {
  auto cf = parse("1 + 3*(a - bb)");
  std::set<CostFunction*> a;
  a.insert(static_cast<CostFunction*>(&a_));
  a.insert(static_cast<CostFunction*>(&b_));
  EXPECT_EQ(a, cf->leaf_functions());
}

}//namespace stoke

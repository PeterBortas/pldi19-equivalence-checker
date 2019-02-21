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

#include "src/ext/x64asm/include/x64asm.h"
#include "src/cfg/cfg.h"
#include "src/sandbox/sandbox.h"
#include "src/stategen/stategen.h"

namespace stoke {

class StateRandomTest : public ::testing::Test {
private:
  void SetUp() {
    Sandbox sb;

    x64asm::Code code{{x64asm::LABEL_DEFN, {x64asm::Label{".foo"}}}, {x64asm::RET, {}}};
    x64asm::RegSet rs = x64asm::RegSet::empty();
    Cfg cfg(code, rs, rs);

    StateGen sg(&sb);
    sg.get(state_, cfg);

    size_t shadows = (rand() % 8);
    for (size_t i = 0; i < shadows; ++i) {
      stringstream ss;
      ss << "var" << i;
      state_.shadow[ss.str()] = rand();
    }
  }

protected:
  CpuState state_;
};

// Checks whether write_text and read_text are inverses
TEST_F(StateRandomTest, Issue55Text) {
  std::stringstream ss;
  state_.write_text(ss);

  CpuState result;
  result.read_text(ss);

  ASSERT_EQ(state_, result);
}

TEST_F(StateRandomTest, GetAddrExplicit) {

  // Code for sandbox
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "leaq (%rax, %rdx, 2), %rax" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  Sandbox sb;
  Cfg cfg(c, x64asm::RegSet::empty(), x64asm::RegSet::empty());
  sb.insert_input(state_);
  sb.insert_function(cfg);
  sb.set_entrypoint(cfg.get_code()[0].get_operand<x64asm::Label>(0));
  sb.run(0);

  auto sb_output = sb.output_begin()->gp[x64asm::rax].get_fixed_quad(0);

  // Code for instruction
  auto mem = c[1].get_operand<x64asm::M8>(c[1].mem_index());

  EXPECT_EQ(sb_output, state_.get_addr(mem));
}

} //namespace stoke

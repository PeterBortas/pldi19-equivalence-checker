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

#include <iostream>
#include <limits>
#include <vector>

#include "src/ext/cpputil/include/command_line/command_line.h"
#include "src/ext/cpputil/include/io/column.h"
#include "src/ext/cpputil/include/io/console.h"
#include "src/ext/cpputil/include/io/filterstream.h"
#include "src/ext/cpputil/include/signal/debug_handler.h"
#include "src/ext/x64asm/include/x64asm.h"

#include "src/state/cpu_states.h"
#include "src/tunit/tunit.h"
#include "src/validator/learner.h"

#include "tools/io/cpu_states.h"
#include "tools/io/reg_set.h"
#include "tools/io/tunit.h"

#include "tools/gadgets/functions.h"
#include "tools/gadgets/learner.h"
#include "tools/gadgets/rewrite.h"
#include "tools/gadgets/seed.h"
#include "tools/gadgets/target.h"


using namespace cpputil;
using namespace std;
using namespace stoke;
using namespace x64asm;

cpputil::Heading& register_heading =
  cpputil::Heading::create("Register Selection:");

cpputil::ValueArg<x64asm::RegSet, RegSetReader, RegSetWriter>& target_regs_arg =
  cpputil::ValueArg<x64asm::RegSet, RegSetReader, RegSetWriter>::create("target_regs")
  .alternate("tr")
  .usage("{ %rax %rsp ... }")
  .description("Registers defined on entry")
  .default_val(x64asm::RegSet::all_gps());

cpputil::ValueArg<x64asm::RegSet, RegSetReader, RegSetWriter>& rewrite_regs_arg =
  cpputil::ValueArg<x64asm::RegSet, RegSetReader, RegSetWriter>::create("rewrite_regs")
  .alternate("rr")
  .usage("{ %rax %rsp ... }")
  .description("Registers defined on entry")
  .default_val(x64asm::RegSet::all_gps());

cpputil::Heading& testcase_heading =
  cpputil::Heading::create("Testcase Selection:");

cpputil::FileArg<CpuStates, CpuStatesReader, CpuStatesWriter>& target_testcases_arg =
  cpputil::FileArg<CpuStates, CpuStatesReader, CpuStatesWriter>::create("target_testcases")
  .alternate("tt")
  .usage("<path/to/file>")
  .description("Testcases for Target");

cpputil::FileArg<CpuStates, CpuStatesReader, CpuStatesWriter>& rewrite_testcases_arg =
  cpputil::FileArg<CpuStates, CpuStatesReader, CpuStatesWriter>::create("rewrite_testcases")
  .alternate("rt")
  .usage("<path/to/file>")
  .description("Testcases for Rewrite");


cpputil::Heading& condition_heading =
  cpputil::Heading::create("Conditional Flags Selection:");

cpputil::ValueArg<string>& target_flag_arg =
  cpputil::ValueArg<string>::create("target_flag")
  .alternate("tf")
  .usage("<string>")
  .default_val("")
  .description("Flag (e.g. 'ne') to split test cases on");

cpputil::ValueArg<string>& rewrite_flag_arg =
  cpputil::ValueArg<string>::create("rewrite_flag")
  .alternate("rf")
  .usage("<string>")
  .default_val("")
  .description("Flag (e.g. 'ne') to split test cases on");



int main(int argc, char** argv) {
  CommandLineConfig::strict_with_convenience(argc, argv);
  DebugHandler::install_sigsegv();
  DebugHandler::install_sigill();

  TUnit empty;
  Cfg empty_cfg(empty, RegSet::universe(), RegSet::universe());

  SeedGadget seed;
  FunctionsGadget aux_fxns;
  TargetGadget target(aux_fxns, false);
  RewriteGadget rewrite(aux_fxns);
  InvariantLearnerGadget learner(seed, target, rewrite);

  auto target_tcs_orig = target_testcases_arg.value();
  auto rewrite_tcs_orig = rewrite_testcases_arg.value();

  vector<CpuState> target_tcs;
  vector<CpuState> rewrite_tcs;

  /*
  for(size_t i = 0; i < target_tcs_orig.size(); ++i) {
    auto target = target_tcs_orig[i];
    auto rewrite = rewrite_tcs_orig[i];
    // Question 1: is rax + rdi - rdx' constant?
    Variable target_rax(rax, false);
    auto v1 = target[rax];
    auto v2 = target[rdi];
    auto v3 = rewrite[rdx];
    auto v4 = v1 + v2 - v3;
    cout << "rax + rdi - rdx' = " << v4 << endl;
    // Question 2: is rax*40...0 constant?
    cout << "0x4000000000000000*rax = " << 0x4000000000000000*target[rax] << endl;
  }
  */

  target_tcs = target_tcs_orig;
  rewrite_tcs = rewrite_tcs_orig;

  cout << "Analyzing " << target_tcs.size() << " testcases." << endl;


  ImplicationGraph ig(target, rewrite);
  auto invs = learner.learn(target_regs_arg.value(), rewrite_regs_arg.value(),
                            target_tcs, rewrite_tcs, ig,
                            target_flag_arg.value(), rewrite_flag_arg.value());

  for (size_t i = 0; i < invs->size(); ++i) {
    cout << *(*invs)[i] << endl;
  }

  return 0;
}

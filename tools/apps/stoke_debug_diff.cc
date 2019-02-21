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

#include "src/ext/cpputil/include/command_line/command_line.h"
#include "src/ext/cpputil/include/io/console.h"
#include "src/ext/cpputil/include/signal/debug_handler.h"

#include "tools/args/target.inc"
#include "tools/gadgets/functions.h"
#include "tools/gadgets/rewrite.h"
#include "tools/gadgets/sandbox.h"
#include "tools/gadgets/seed.h"
#include "tools/gadgets/target.h"
#include "tools/gadgets/testcases.h"
#include "tools/io/state_diff.h"

using namespace cpputil;
using namespace std;
using namespace stoke;
using namespace x64asm;

auto& dbg = Heading::create("Diff Options:");
auto& show_unchanged = FlagArg::create("show_unchanged")
                       .description("Show unchanged lines");
auto& dont_show_all_registers = FlagArg::create("diff_relevant_registers")
                                .description("Show only changes from live_out and def_in");

int main(int argc, char** argv) {
  CommandLineConfig::strict_with_convenience(argc, argv);
  DebugHandler::install_sigsegv();
  DebugHandler::install_sigill();

  if (testcases_arg.value().empty()) {
    Console::error(1) << "No testcases provided.";
  }

  FunctionsGadget aux_fxns;
  TargetGadget target(aux_fxns, false);
  RewriteGadget rewrite(aux_fxns);
  SeedGadget seed;
  TestcaseGadget tc(seed);
  CpuStates tcs;
  CpuState initial = tc;
  tcs.push_back(tc);
  SandboxGadget sb(tcs, aux_fxns);

  sb.run(target);
  const auto target_result = *(sb.result_begin());
  sb.run(rewrite);
  const auto rewrite_result = *(sb.result_begin());

  Console::msg() << diff_states(target_result, rewrite_result, show_unchanged, !dont_show_all_registers, target.live_outs() | target.def_ins());

  return 0;
}

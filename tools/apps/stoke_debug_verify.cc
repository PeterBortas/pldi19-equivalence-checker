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

#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

#include "src/ext/cpputil/include/command_line/command_line.h"
#include "src/ext/cpputil/include/signal/debug_handler.h"
#include "src/ext/cpputil/include/io/filterstream.h"
#include "src/ext/cpputil/include/io/column.h"
#include "src/ext/cpputil/include/io/console.h"

#include "tools/common/version_info.h"

#include "tools/gadgets/cost_function.h"
#include "tools/gadgets/functions.h"
#include "tools/gadgets/rewrite.h"
#include "tools/gadgets/learner.h"
#include "tools/gadgets/sandbox.h"
#include "tools/gadgets/seed.h"
#include "tools/gadgets/solver.h"
#include "tools/gadgets/target.h"
#include "tools/gadgets/testcases.h"
#include "tools/gadgets/verifier.h"
#include "tools/io/state_diff.h"

using namespace cpputil;
using namespace std;
using namespace stoke;
using namespace x64asm;

auto& dbg = Heading::create("Diff Options:");
auto& show_unchanged = FlagArg::create("show_unchanged")
                       .description("Show unchanged lines");
auto& show_all_registers = FlagArg::create("diff_all_registers")
                           .description("Show changes in all registers, not just the ones from live_out and def_in");

auto& machine_output_arg = ValueArg<string>::create("machine_output")
                           .usage("<path/to/file.s>")
                           .description("Machine-readable output (result and counterexample)");


void print_machine_output(bool verified, string error, string counterexample, bool has_counterexample) {
  ofstream f;
  f.open(machine_output_arg.value());
  f << "{" << endl;
  f << "  \"verified\": " << (verified ? "true" : "false") << "," << endl;
  f << "  \"counter_examples_available\": " << (has_counterexample ? "true" : "false") << "," << endl;
  f << "  \"counterexample\": \"" << counterexample << "\"," << endl;
  f << "  \"error\": \"" << error << "\"" << endl;
  f << "}" << endl;
  f.close();
}


//https://stackoverflow.com/questions/10205543/saving-gmon-out-before-killing-a-process
void sigUsr1Handler(int sig)
{
  fprintf(stderr, "Exiting on SIGUSR1\n");
  void (*_mcleanup)(void);
  _mcleanup = (void (*)(void))dlsym(RTLD_DEFAULT, "_mcleanup");
  if (_mcleanup == NULL)
    fprintf(stderr, "Unable to find gprof exit hook\n");
  else _mcleanup();
  _exit(0);
}

int main(int argc, char** argv) {

  signal(SIGUSR1, sigUsr1Handler);
  cout << "VERSION: " << version_info << endl;
  CommandLineConfig::strict_with_convenience(argc, argv);
  DebugHandler::install_sigsegv();
  DebugHandler::install_sigill();


  FunctionsGadget aux_fxns;
  TargetGadget target(aux_fxns, false);
  RewriteGadget rewrite(aux_fxns);

  SeedGadget seed;
  TestSetGadget test_set(seed);
  SandboxGadget sb(test_set, aux_fxns);
  CorrectnessCostGadget fxn(target, &sb);
  InvariantLearnerGadget learner(seed, target, rewrite);

  DataCollector data_collector(sb);
  ObligationCheckerGadget obligation_checker;

  VerifierGadget verifier(sb, fxn, learner);

  ofilterstream<Column> os(Console::msg());
  os.filter().padding(3);

  os << "Target" << endl;
  os << endl;
  os << target_arg.value().get_code() << endl;
  os.filter().next();

  os << "Rewrite" << endl;
  os << endl;
  os << rewrite_arg.value().get_code() << endl;
  os.filter().done();

  Console::msg() << endl;

  if (strategy_arg.value() == "none") {
    Console::warn() << "'--strategy none' passed, so no verification is done." << endl;
    return 0;
  }

  int *x = new int[1000];
  bool res;
  try {
    res = verifier.verify(target, rewrite);
  } catch (std::bad_alloc e) {
    delete x;
    cerr << "Out of memory!" << endl;
    exit(1);
  }
  delete x;

  if (verifier.has_error()) {
    Console::msg() << "Encountered error: " << endl;
    Console::msg() << verifier.error() << endl;
    print_machine_output(false, verifier.error(), "", false);
    return 1;
  }

  Console::msg() << "Equivalent: " << (res ? "yes" : "no") << endl;

  if (!res && verifier.counter_examples_available()) {
    Console::msg() << endl << verifier.counter_examples_available() << " Counterexamples." << endl;
    Console::msg() << endl;
    Console::msg() << verifier.get_counter_examples()[0];
    Console::msg() << endl << endl;
    Console::msg() << "Difference of running target and rewrite on the counterexample:";
    Console::msg() << endl << endl;
    CpuStates tcs;
    tcs.push_back(verifier.get_counter_examples()[0]);
    SandboxGadget sb(tcs, aux_fxns);
    sb.run(target);
    const auto target_result = *(sb.result_begin());
    sb.run(rewrite);
    const auto rewrite_result = *(sb.result_begin());
    Console::msg() << diff_states(target_result, rewrite_result, show_unchanged, show_all_registers, target.live_outs() | target.def_ins());
    Console::msg() << endl;
  } else if (!res) {
    Console::msg() << endl << "No counterexample available." << endl;
  }

  // output machine-readable result
  if (machine_output_arg.has_been_provided()) {
    auto cpustate_to_string = [](CpuState cs) {
      stringstream ss;
      ss << cs;
      auto res = regex_replace(ss.str(), regex("\n"), "\\n");
      return res;
    };

    string counterexample = "";
    if (verifier.counter_examples_available()) {
      counterexample = cpustate_to_string(verifier.get_counter_examples()[0]);
    }

    print_machine_output(res, "", counterexample, verifier.counter_examples_available());
  }

  return 0;
}

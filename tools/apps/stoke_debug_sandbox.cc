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

#include "tools/args/target.inc"
#include "tools/gadgets/functions.h"
#include "tools/gadgets/sandbox.h"
#include "tools/gadgets/seed.h"
#include "tools/gadgets/target.h"
#include "tools/gadgets/testcases.h"

using namespace cpputil;
using namespace std;
using namespace stoke;
using namespace x64asm;

auto& dbg = Heading::create("Debug Options:");
auto& debug = FlagArg::create("debug")
              .alternate("d")
              .description("Debug mode, step through instructions one at a time");

auto& verbose = FlagArg::create("verbose")
                .alternate("d")
                .description("Print state following each instruction");

auto& operands = ValueArg<string>::create("operands")
                 .description("Operands to print for each basic block executed")
                 .default_val("");

// The current program stack
vector<pair<TUnit, size_t>> program_stack;
vector<Operand> operand_list;

// Kind of ugly; the callback needs to see this Gadgets, but we
// can't make it global. It'll be initialized correctly in main.
FunctionsGadget* fg = nullptr;

void print_state(const StateCallbackData& data) {
  Console::msg() << "Current State: " << endl;
  Console::msg() << endl;
  Console::msg() << data.state << endl;
  Console::msg() << endl;
}

void print_stack() {
  Console::msg() << "Program Stack: " << endl;
  Console::msg() << endl;

  ofilterstream<Column> ofs(Console::msg());
  ofs.filter().padding(1);

  for (size_t i = 0, ie = program_stack.size(); i < ie; ++i) {
    ofs << "[" << i << "]" << endl;
  }
  ofs.filter().next();
  for (size_t i = 0, ie = program_stack.size(); i < ie; ++i) {
    ofs << program_stack[i].first.get_name() << endl;
  }
  ofs.filter().next();
  for (size_t i = 0, ie = program_stack.size(); i < ie; ++i) {
    ofs << program_stack[i].second << endl;
  }

  ofs.filter().next();
  ofs.filter().done();

  Console::msg() << endl;
}

void print_current(const pair<TUnit, size_t>& frame) {
  const auto& instr = frame.first.get_code()[frame.second];
  Console::msg() << "Current Instruction: " << instr << endl;
  Console::msg() << endl;
}

void print_frame(size_t idx, const pair<TUnit, size_t>& frame) {
  Console::msg() << "[" << idx << "] " << frame.first.get_name() << " " << frame.second << endl;
  Console::msg() << endl;
  for (size_t i = 0, ie = frame.first.get_code().size(); i < ie; ++i) {
    Console::msg() << (i == frame.second ? "-> " : "   ") << frame.first.get_code()[i] << endl;
  }
  Console::msg() << endl;
}

bool user_loop(CpuState& tc) {
  auto key = ' ';
  auto idx = program_stack.size()-1;

  while (true) {
    Console::msg() << "(l)ist, (u)p, (d)own, (s)tep, (c)ontinue, (w)ipe heap or (q)uit: ";
    cin >> key;
    Console::msg() << endl;

    switch (key) {
    case 'w':
      tc.heap ^= tc.heap;
      break;
    case 'u':
      idx = idx == 0 ? idx : idx-1;
      print_frame(idx, program_stack[idx]);
      break;
    case 'd':
      idx = idx == (program_stack.size()-1) ? idx : idx+1;
      print_frame(idx, program_stack[idx]);
      break;
    case 'l':
      print_frame(idx, program_stack[idx]);
      break;
    case 's':
      return true;
    case 'c':
      return false;
    case 'q':
      exit(0);
    default:
      break;
    }
  }
}

void update_stack(const pair<TUnit, size_t>& frame) {
  const auto& instr = frame.first.get_code()[frame.second];
  if (instr.get_opcode() == CALL_LABEL) {
    const auto dest = instr.get_operand<Label>(0);
    assert(target_arg.value().invariant_first_instr_is_label());
    if (dest == target_arg.value().get_leading_label()) {
      program_stack.push_back({target_arg.value(), 0});
    } else {
      for (const auto& fxn : *fg) {
        assert(fxn.invariant_first_instr_is_label());
        if (dest == fxn.get_leading_label()) {
          program_stack.push_back({fxn, 0});
        }
      }
    }
  } else if (instr.is_any_return()) {
    program_stack.pop_back();
  }
}

void callback(const StateCallbackData& data, void* arg) {
  assert(fg != nullptr);
  assert(!program_stack.empty());

  auto& frame = program_stack.back();
  frame.second = data.line;

  if (operand_list.size()) {
    cout << left;
    cout << dec << setw(4) << " " << setw(4) << data.line << setw(4) << " ";
    for (auto op : operand_list) {
      cout << hex << setw(4) << " " << setw(16) << (uint64_t)data.state[op].get_fixed_quad(0);
    }
    cout << endl;
    cout << dec << setw(0);
  } else {
    // Print current execution state if debug was ever specified
    if (debug.value() || verbose.value()) {
      print_state(data);
      print_stack();
      print_current(frame);
    }

    // User interaction loop
    auto stepping = (bool*) arg;
    if (*stepping) {
      *stepping = user_loop(data.state);
    }
  }

  // Update the stack based on the current instruction
  update_stack(frame);
}

int main(int argc, char** argv) {
  // no reason to check def-in/live-out
  def_in_arg.default_val(RegSet::universe()).set_provided();
  live_out_arg.default_val(RegSet::empty()).set_provided();

  CommandLineConfig::strict_with_convenience(argc, argv);
  DebugHandler::install_sigsegv();
  DebugHandler::install_sigill();

  if (testcases_arg.value().empty()) {
    Console::msg() << "No testcases provided." << endl;
    return 0;
  }

  FunctionsGadget aux_fxns;
  TargetGadget target(aux_fxns, false);
  cout << target.get_function() << endl;
  SeedGadget seed;
  TestcaseGadget tc(seed);
  CpuStates tcs;
  tcs.push_back(tc);

  // parse operands
  if (operands.value().size()) {
    stringstream ss(operands.value());
    string token;
    while (std::getline(ss, token, ';')) {
      x64asm::Operand op(rax);
      stringstream tmp_ss(token);
      tmp_ss >> op;
      if (op.is_typical_memory()) {
        M64 m(*static_cast<M8*>(&op));
        operand_list.push_back(m);
      } else {
        operand_list.push_back(op);
      }
    }
  }
  if (operand_list.size())  {
    cout << endl;
    cout << "    line    ";
  }
  for (auto op : operand_list) {
    stringstream ss;
    ss << op;
    while (ss.str().size() < 16)
      ss << " ";
    cout << "    " << ss.str();
  }
  if (operand_list.size())
    cout << endl;



  auto stepping = debug.value();
  fg = &aux_fxns;
  program_stack.push_back({target_arg.value(), 0});
  SandboxGadget sb(tcs, aux_fxns);
  sb.insert_before(callback, &stepping);

  sb.run(target);

  if (operand_list.size() == 0) {
    const auto result = *(sb.result_begin());
    if (result.code != ErrorCode::NORMAL) {
      Console::msg() << "Control returned abnormally with signal " << dec << (int)result.code << " [" << readable_error_code(result.code) << "]" << endl;
    } else {
      Console::msg() << "Control returned normally with state: " << endl;
      Console::msg() << endl;
      Console::msg() << result << endl;
    }
    Console::msg() << endl;
  }

  return 0;
}

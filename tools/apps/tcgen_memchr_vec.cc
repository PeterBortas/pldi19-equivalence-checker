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

#include "src/cfg/cfg.h"
#include "src/cfg/paths.h"
#include "src/state/cpu_state.h"
#include "src/state/cpu_states.h"
#include "src/stategen/stategen.h"
#include "src/validator/handlers/combo_handler.h"
#include "src/validator/filters/default.h"
#include "src/validator/filters/forbidden_dereference.h"
#include "src/validator/obligation_checker.h"
#include "src/validator/invariants/false.h"
#include "src/validator/invariants/true.h"

#include "src/ext/cpputil/include/command_line/command_line.h"
#include "src/ext/cpputil/include/signal/debug_handler.h"
#include "src/ext/cpputil/include/io/filterstream.h"
#include "src/ext/cpputil/include/io/column.h"
#include "src/ext/cpputil/include/io/console.h"

#include "tools/gadgets/seed.h"
#include "tools/io/state_diff.h"

using namespace cpputil;
using namespace std;
using namespace stoke;
using namespace x64asm;

#include <cstdlib>
#include <time.h>

int main(int argc, char** argv) {

  CommandLineConfig::strict_with_convenience(argc, argv);

  CpuStates outputs;

  srand(time(0) ^ (getpid()*0xff));
  Sandbox sb;
  StateGen sg(&sb);
  for (size_t i = 0; i < 60; ++i) {
    for (size_t j = 0; j < 8; ++j) {
      for (size_t k = 0; k <= i; ++k) {
        CpuState tc;
        sg.get(tc);

        uint64_t rdi_v = tc[rdi];
        rdi_v = (rdi_v & 0xfffffffffffffff8) + j;
        tc.update(rdi, rdi_v);
        tc.update(rdx, i);

        tc.heap.resize(rdi_v, i+2);

        /** add 8 bytes after for safety */
        for (uint64_t addr = rdi_v; addr <= rdi_v+i+1; ++addr) {
          tc.heap.set_valid(addr, true);
          do {
            tc.heap[addr] = (rand()%255);
          } while (tc.heap[addr] == tc[sil]);
        }

        /** pick a random place to add the character in question */
        if (k != i) {
          size_t position = k;
          tc.heap[rdi_v + position] = tc[sil];
        }

        outputs.push_back(tc);
      }
    }
  }

  // Print anything we have so far
  outputs.write_text(cout);

  return 0;
}




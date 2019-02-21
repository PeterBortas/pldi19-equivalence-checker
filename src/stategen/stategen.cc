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

#include "src/stategen/stategen.h"

#include <cstdlib>
#include <string>

#include "src/sandbox/sandbox.h"
#include "src/sandbox/state_callback.h"
#include "src/state/regs.h"

#define DEBUG_STATEGEN(X) { if(0) { X } }
using namespace std;
using namespace stoke;
using namespace x64asm;

namespace {

void callback(const StateCallbackData& data, void* arg) {
  size_t& last_line = *((size_t*) arg);
  last_line = data.line;
}

} // namespace

namespace stoke {

bool StateGen::get(CpuState& cs) {
  // Randomize registers
  for (size_t i = 0, ie = cs.gp.size(); i < ie; ++i) {
    auto& r = cs.gp[i];
    auto max = get_max_value(i);
    auto mask = get_bitmask(i);
    for (size_t j = 0, je = r.num_fixed_bytes(); j < je; ++j) {
      auto max_byte = max & 0xff;
      auto mask_byte = mask & 0xff;
      r.get_fixed_byte(j) = (gen_() % (max_byte + 1)) & mask_byte;
      max >>= 8;
      mask >>= 8;
    }
  }
  for (size_t i = 0, ie = cs.sse.size(); i < ie; ++i) {
    auto& s = cs.sse[i];
    for (size_t j = 0, je = s.num_fixed_bytes(); j < je; ++j) {
      s.get_fixed_byte(j) = gen_() % 256;
    }
  }
  for (size_t i = 0, ie = cs.rf.size(); i < ie; ++i) {
    if (!cs.rf.is_fixed(i)) {
      cs.rf.set(i, gen_() % 2);
    }
  }

  // Map rsp to a high address
  cs.gp[rsp].get_fixed_byte(0) = 0x00;
  cs.gp[rsp].get_fixed_byte(7) = (gen_() % 250) + 1;

  // Generate default memory
  cs.stack.resize(cs.gp[rsp].get_fixed_quad(0) - stack_size_, stack_size_);
  cs.heap.resize(0x100000000, 0);
  cs.data.resize(0x000000000, 0);
  randomize_mem(cs.stack);

  return true;
}

void StateGen::cleanup() {
  sb_->clear_callbacks();
  sb_->clear_inputs();
}

bool StateGen::get(CpuState& cs, const Cfg& cfg, bool no_randomize) {
  // Insert callbacks before every instruction and compile
  size_t last_line_index;
  sb_->clear_callbacks();
  sb_->insert_before(callback, (void*)&last_line_index);
  sb_->compile(cfg);

  // Generate a random state if requested
  if (!no_randomize)
    get(cs);

  // now try to patch in the gaps
  tried_to_fix_misalign_ = false;
  for (int i = 0; i < (int)max_attempts_; ++i) {
    // Reset the sandbox state and try executing
    sb_->clear_inputs();
    sb_->insert_input(cs);
    sb_->run_one(0);
    auto last_line = cfg.get_code()[last_line_index];

    // There's a single failure case we have to deal with immediately.
    // If the sandbox couldn't link cfg against its aux functions, it
    // won't ever run and set the value of last_line.
    if (sb_->get_result(0)->code == ErrorCode::SIGBUS_) {
      error_message_ = "Linking failed!";
      cleanup();
      return false;
    }

    // If we didn't segfault, or we did due to misalign and it's allowed,
    // then we're done
    if (is_ok(last_line)) {
      cleanup();
      return true;
    }
    // Otherwise, try allocating away a segfault and retry
    else if (fix(*(sb_->get_result(0)), cs, cfg, last_line_index)) {
      i--;
    }
    // Otherwise, generate a new state and call this attempt failed
    else {
      get(cs);
      tried_to_fix_misalign_ = false;
    }
  }

  error_message_ = "Max attempts exceeded.";
  cleanup();
  return false;
}

bool StateGen::is_ok(const Instruction& line) {
  if (sb_->get_result(0)->code == ErrorCode::NORMAL) {
    return true;
  }

  if (!is_supported_deref(line))
    return false;

  CpuState cs = *(sb_->get_result(0));
  const auto addr = cs.get_addr(line);
  const auto size = get_size(line);

  // If the address is already allocated, there's a segfault,
  // it's misaligned and we allow misaligned, then we're ok.
  if (allow_unaligned_ && is_misaligned(addr, size) &&
      cs.code == ErrorCode::SIGSEGV_ &&
      (already_allocated(cs.stack, addr, size) ||
       already_allocated(cs.heap, addr, size))) {
    return true;
  }

  return false;
}

bool StateGen::is_supported_deref(const Instruction& instr) {
  // Special support for push/pop/ret
  if (instr.is_push() || instr.is_pop() || instr.is_any_return() || instr.is_call()) {
    if (instr.is_explicit_memory_dereference()) {
      error_message_ = "StateGen does not support push/pop with memory argument.";
      return false;
    }  else {
      return true;
    }
  }

  // No support for implicit memory accesses
  if (instr.is_implicit_memory_dereference()) {
    error_message_ = "Implicit memory dereferences not supported.";
    return false;
  }

  if (instr.mem_index() == -1) {
    error_message_ = "Could not find an explicit or implicit memory dereference."
                     "  Bug somewhere (forgot retq?).";
    return false;
  }

  const auto mi = instr.mem_index();
  const auto op = instr.get_operand<M8>(mi);

  // No support for rip-offset form or segment register addressing
  if (op.contains_seg()) {
    error_message_ = "No support for segment addressing";
    return false;
  }

  return true;
}


size_t StateGen::get_size(const Instruction& instr) const {
  // Special handling for implicit dereferences
  if (instr.is_push() || instr.is_pop() || instr.is_any_return() || instr.is_call()) {
    return 8;
  }

  // Otherwise, we can infer width from type
  const auto mi = instr.mem_index();
  return instr.get_operand<M8>(mi).size()/8;
}

bool StateGen::resize_within(Memory& mem, uint64_t addr, size_t size) {
  // This should always be true, otherwise there'd be no work to do
  // * See the previous check against already_allocated() one level up
  assert((addr + size - mem.size()) > mem.lower_bound());

  const auto delta = addr + size - mem.upper_bound();
  if (mem.size() + delta > max_memory_) {
    return false;
  }

  mem.resize(mem.lower_bound(), mem.size() + delta);
  randomize_mem(mem);
  return true;
}

bool StateGen::resize_below(Memory& mem, uint64_t addr, size_t size) {
  size_t new_size = 0;
  if (addr + size - mem.size() > mem.lower_bound()) {
    // i.e. the access is bigger than the entire existing memory region
    new_size = size;
  } else {
    new_size = mem.upper_bound() - addr;
  }

  if (new_size > max_memory_) {
    return false;
  }

  mem.resize(addr, new_size);
  randomize_mem(mem);
  return true;
}

bool StateGen::resize_above(Memory& mem, uint64_t addr, size_t size) {
  const auto delta = addr + size - mem.lower_bound() - mem.size();
  if (mem.size() + delta > max_memory_) {
    return false;
  }

  mem.resize(mem.lower_bound(), mem.size() + delta);
  randomize_mem(mem);
  return true;
}

void StateGen::randomize_mem(Memory& mem) {
  for (uint64_t i = 0; i < mem.size(); ++i) {
    uint64_t addr = mem.lower_bound() + i;
    if (!mem.is_valid(addr)) {
      mem.set_valid(addr, true);
      mem[addr] = gen_() % 256;
    }
  }
}

bool StateGen::resize_mem(Memory& mem, uint64_t addr, size_t size) {
  if (mem.size() == 0) {
    mem.resize(addr, size);
    randomize_mem(mem);
    return true;
  } else if (mem.in_range(addr) && resize_within(mem, addr, size)) {
    return true;
  } else if (addr < mem.lower_bound() && resize_below(mem, addr, size)) {
    return true;
  } else if (mem.upper_bound() && addr >= mem.upper_bound() && resize_above(mem, addr, size)) {
    return true;
  } else {
    return false;
  }
}

bool StateGen::fix_misalignment(const CpuState& cs, CpuState& fixed, const Instruction& instr) {
  const auto mi = instr.mem_index();
  const auto op = instr.get_operand<M8>(mi);

  const auto addr = cs.get_addr(instr);
  const auto mask = 0x1f;
  const auto offset = addr & mask;

  if (op.contains_base()) {
    const auto current = cs.gp[op.get_base()].get_fixed_quad(0);
    if (((current - offset) & mask) && !tried_to_fix_misalign_) {
      const auto new_byte = (current & mask) - offset;
      fixed.gp[op.get_base()].get_fixed_byte(0) = new_byte;
      tried_to_fix_misalign_ = true;
      return true;
    } else {
      error_message_ = "Could not fix misaligned memory reference.";
      tried_to_fix_misalign_ = false;
      return false;
    }
  } else {
    error_message_ = "Could not find misaligned memory reference.";
    tried_to_fix_misalign_ = false;
    return false;
  }

}

bool StateGen::fix(const CpuState& cs, CpuState& fixed, const Cfg& cfg, size_t line) {

  DEBUG_STATEGEN(cout << "[fix] cs = " << endl;)
  DEBUG_STATEGEN(cout << cs << endl;)

  DEBUG_STATEGEN(cout << "[fix] fixed = " << endl;)
  DEBUG_STATEGEN(cout << cs << endl;)



  auto instr = cfg.get_code()[line];
  // Clear the error message unless something bad happens.
  error_message_ = "";

  // Only sigsegv is fixable
  if (cs.code != ErrorCode::SIGSEGV_) {
    error_message_ = "Interrupt was not segfault, but signal " + std::to_string((int)cs.code) + " [" + readable_error_code(cs.code) + "] instead.";
    return false;
  }
  // Only explicit dereferences are fixable
  if (!is_supported_deref(instr)) {
    return false;
  }

  const auto size = get_size(instr);
  auto addr = cs.get_addr(instr);

  DEBUG_STATEGEN(cout << "[fix] called with " << addr << endl;)

  if (instr.mem_index() != -1) {
    auto mem = instr.get_operand<Mem>(instr.mem_index());
    if (mem.rip_offset()) {
      auto& fxn = cfg.get_function();
      uint64_t disp = mem.get_disp();

      if (disp & 0x80000000)
        disp |= 0xffffffff00000000;
      else
        disp &= 0x00000000ffffffff;

      if (linemap_.size() && linemap_.count(line)) {
        DEBUG_STATEGEN(
          cout << "[fix] have rip offset of " << linemap_[line].rip_offset << endl;
          cout << "[fix] (uint64_t)mem.get_disp() = " << (uint64_t)mem.get_disp() << endl;
          cout << "[fix] disp = " << disp << endl;)
        addr = linemap_[line].rip_offset + disp;
      } else {
        addr = disp + fxn.get_rip_offset() + fxn.hex_offset(line) + fxn.hex_size(line);
      }
    }
  }

  DEBUG_STATEGEN(cout << "[fix] FIXING @" << addr << " with size " << size << endl;)
  DEBUG_STATEGEN(cout << "offending instruction: " << instr << " at line " << line << endl;)

  // We can't do anything about misaligned memory or pre-allocated memory
  if (is_misaligned(addr, size) && !allow_unaligned_) {
    DEBUG_STATEGEN(cout << "this is misaligned." << endl;)
    return fix_misalignment(cs, fixed, instr);
  }

  vector<Memory> segments;
  segments.push_back(fixed.stack);
  segments.push_back(fixed.heap);
  segments.insert(segments.begin(), fixed.segments.begin(), fixed.segments.end());

  for (auto& segment : segments) {
    if (already_allocated(segment, addr, size)) {
      tried_to_fix_misalign_ = false;
      error_message_ = "Memory was already allocated in segment.";
      return false;
    }
  }
  DEBUG_STATEGEN(cout << "[fix] Seems not to already be allocated here: " << endl;
  for (auto& segment : segments) {
  segment.write_text(cout);
    cout << endl << endl;
  })

  /** See if we can resize one of the segments. */
  for (auto& segment : segments) {
    if (resize_mem(segment, addr, size)) {
      fixed.stack = segments[0];
      fixed.heap = segments[1];
      segments.erase(segments.begin());
      segments.erase(segments.begin());
      fixed.segments = segments;
      return true;
    }
  }

  /* If not, create a new segment! */
  Memory m;
  bool b = resize_mem(m, addr, size);
  assert(b);
  segments.push_back(m);
  fixed.segments = segments;
  DEBUG_STATEGEN(cout << "[fix] Adding segment" << endl;)
  DEBUG_STATEGEN(m.write_text(cout);)
  DEBUG_STATEGEN(cout << endl;)
  DEBUG_STATEGEN(cout << fixed << endl;)
  return true;
}

} // namespace stoke

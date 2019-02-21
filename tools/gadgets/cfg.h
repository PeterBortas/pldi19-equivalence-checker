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

#ifndef STOKE_TOOLS_GADGETS_CFG_H
#define STOKE_TOOLS_GADGETS_CFG_H

#include <vector>

#include "src/ext/cpputil/include/io/console.h"
#include "src/ext/x64asm/include/x64asm.h"

#include "src/cfg/cfg.h"
#include "src/cfg/cfg_transforms.h"
#include "src/sandbox/sandbox.h"
#include "src/target/cpu_info.h"
#include "src/tunit/tunit.h"

#include "tools/args/target.inc"
#include "tools/args/in_out.inc"

namespace stoke {

class CfgGadget : public Cfg {
public:
  CfgGadget(const TUnit& fxn, const std::vector<TUnit>& aux_fxns, bool is_init_zero)
    : Cfg(fxn, def_in(live_out()), live_out()) {

    // The TUnit constructor and parser should prevent this from ever happening.
    // This is a major bug and should be reported by the user.
    if (!get_function().check_invariants()) {
      cpputil::Console::error(1) << "(" << fxn.get_name() << ") Function bug; please report!" << std::endl;
    }

    // Emit warning if register values were guessed
    reg_warning();

    // Check for unsupported instructions and cpu flags
    if (!live_dangerously_arg.value()) {
      flag_check();
      sandbox_check();
    }

    // Check that this function can link against auxiliary functions
    linker_check(aux_fxns);

    // Add summaries for auxiliary functions
    // @todo At some point, all functions should have summaries for everything else
    summarize_functions(aux_fxns);

    if (!live_dangerously_arg.value()) {
      // Check Cfg invariants
      // These warnings need to be emitted to the user because the Cfg class isn't guaranteed
      // to catch them during construction
      if (!invariant_no_undef_reads()) {
        cpputil::Console::error(1) << "(" << fxn.get_name() << ") Reads from an undefined location: " << which_undef_read() << std::endl;
      } else if (!invariant_no_undef_live_outs() && !is_init_zero) {
        cpputil::Console::error(1) << "(" << fxn.get_name() << ") Leaves a live out undefined. Use --init ZERO if this is an initial rewrite " << which_undef_read() << std::endl;
      }

      // Control shouldn't ever reach here given the checks above.
      // This is a major bug and should be reported by the user
      if (!check_invariants() && !is_init_zero) {
        cpputil::Console::error(1) << "(" << fxn.get_name() << ") Cfg bug; please report!" << std::endl;
      }
    }
  }

private:
  void reg_warning() const {
    // The static guard here to prevent this warning from being emitted more than once
    // once for the target, and once for current, best_cost, best_correct, etc...
    static auto once = false;
    if (!once) {
      once = true;
      if (!live_out_arg.has_been_provided()) {
        cpputil::Console::warn() << "No live out values provided, assuming " << live_out() << std::endl;
      }
      if (!def_in_arg.has_been_provided()) {
        cpputil::Console::warn() << "No def in values provided; assuming " << def_in(live_out()) << std::endl;
      }
    }
  }

  x64asm::RegSet def_in(const x64asm::RegSet& live_out) const {
    // Always prefer user inputs, otherwise solve for live_ins
    auto def_in = def_in_arg.has_been_provided() ?
                  def_in_arg.value() :
                  Cfg(target_arg.value(), x64asm::RegSet::empty(), live_out).live_ins();

    // Add mxcsr[rc] unless otherwise specified
    if (!no_default_mxcsr_arg) {
      def_in += x64asm::mxcsr_rc;
    }

    return def_in;
  }

  x64asm::RegSet live_out() const {
    // Always prefer user inputs
    if (live_out_arg.has_been_provided()) {
      return live_out_arg.value();
    }

    return x64asm::RegSet::linux_call_return() | x64asm::RegSet::linux_call_preserved();
  }

  /** Checks for unsupported cpu flags */
  void flag_check() const {
    const auto cpu_flags = CpuInfo::get_flags();
    auto code_flags = get_function().get_code().required_flags();

    if (!cpu_flags.contains(code_flags)) {
      const auto diff = code_flags - cpu_flags;
      cpputil::Console::error(1) << "Target/rewrite requires unavailable cpu flags: " << diff << std::endl;
    }
  }

  /** Checks for unsupported sandbox instructions */
  void sandbox_check() const {
    for (const auto& instr : get_function().get_code()) {
      if (!Sandbox::is_supported(instr)) {
        cpputil::Console::error(1) << "Target/rewrite contains an unsupported instruction: " << instr << std::endl;
      }
    }
  }

  /** Check whether linking is possible for these code sequences */
  void linker_check(const std::vector<TUnit>& aux_fxns) const {
    x64asm::Assembler assm;

    // We can't let this hex go out of scope before linking
    std::vector<x64asm::Function> hex;
    auto result = assm.assemble(get_code());

    if (!result.first) {
      cpputil::Console::error(1) << "Target/rewrite has jump with 8-bit offset but target is too far away." << std::endl;
    }
    hex.push_back(result.second);

    for (const auto& fxn : aux_fxns) {
      auto result = assm.assemble(fxn.get_code());
      if (!result.first) {
        cpputil::Console::error(1) << "Auxiliary function " << fxn.get_leading_label() <<
                                   "has jump with 8-bit offset but target is too far away." << std::endl;
      }
      hex.push_back(result.second);
    }

    x64asm::Linker lnkr;
    lnkr.start();
    for (auto& h : hex) {
      lnkr.link(h);
    }
    lnkr.finish();

    if (lnkr.multiple_def()) {
      cpputil::Console::error(1) << "Target/rewrite and functions contain a multiple definition error (" << lnkr.get_multiple_def() << ")!" << std::endl;
    } else if (lnkr.undef_symbol()) {
      cpputil::Console::error(1) << "Target/rewrite and functions contain an undefined symbol error (" << lnkr.get_undef_symbol() << ")!" << std::endl;
    }
  }

  /** Add dataflow summaries for auxiliary functions */
  void summarize_functions(const std::vector<TUnit>& aux_fxns) {
    for (const auto& fxn : aux_fxns) {
      const auto& code = fxn.get_code();
      const auto& lbl = fxn.get_leading_label();
      TUnit::MayMustSets mms = {
        code.must_read_set(),
        code.must_write_set(),
        code.must_undef_set(),
        code.maybe_read_set(),
        code.maybe_write_set(),
        code.maybe_undef_set()
      };
      mms = fxn.get_may_must_sets(mms);
      // check consistency of dataflow information
      std::string consistency_warning = "Dataflow information is inconsistent for function '" + fxn.get_name() + "'.  The maybe set needs to contain the must set. ";
      if (!mms.maybe_read_set.contains(mms.must_read_set)) {
        cpputil::Console::error(1) << consistency_warning << "maybe-read: " << mms.maybe_read_set << ". must-read: " << mms.must_read_set << std::endl;
      }
      if (!mms.maybe_write_set.contains(mms.must_write_set)) {
        cpputil::Console::error(1) << consistency_warning << "maybe-write: " << mms.maybe_write_set << ". must-write: " << mms.must_write_set << std::endl;
      }
      if (!mms.maybe_undef_set.contains(mms.must_undef_set)) {
        cpputil::Console::error(1) << consistency_warning << "maybe-undef: " << mms.maybe_undef_set << ". must-undef: " << mms.must_undef_set << std::endl;
      }
      add_summary(lbl, mms);
    }
    recompute();
  }
};

} // namespace stoke

#endif


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

#include <chrono>

#include "src/cfg/cfg.h"
#include "src/cfg/paths.h"
#include "src/stategen/stategen.h"
#include "src/symstate/memory/arm.h"
#include "src/symstate/memory/trivial.h"
#include "src/validator/error.h"
#include "src/validator/invariants/conjunction.h"
#include "src/validator/invariants/flag.h"
#include "src/validator/invariants/memory_equality.h"
#include "src/validator/invariants/state_equality.h"
#include "src/validator/invariants/true.h"
#include "src/validator/path_unroller.h"
#include "src/validator/smt_obligation_checker.h"
#include "src/solver/z3solver.h"
#include "src/symstate/memory_manager.h"

#include "tools/io/state_diff.h"
#include "tools/common/version_info.h"

#include <ostream>
#include <iostream>
#include <fstream>

//#define DO_DEBUG 0
#define DO_DEBUG ENABLE_LOCAL_DEBUG

#define OBLIG_DEBUG(X) { if(1) { X } }
#define CONSTRAINT_DEBUG(X) { if(0) { X } }
#define DEBUG_BUILDTC_FROM_ARRAY(X) { if(0) { X } }
#define BUILD_TC_DEBUG(X) { if(0) { X } }
#define DEBUG_ARM(X) { if(0) { X } }
#define DEBUG_CHECK_CEG(X) { if(0) { X } }
#define DEBUG_MAP_TC(X) {}
#define ALIAS_DEBUG(X) {  }
#define ALIAS_CASE_DEBUG(X) {  }
//#define DEBUG_ARMS_RACE(X)  X
#define DEBUG_ARMS_RACE(X)  { }
#define DEBUG_FIXPOINT(X) { }

//#ifdef STOKE_DEBUG_CEG
//#define CEG_DEBUG(X) { X }
//#else
//#define CEG_DEBUG(X) { }
//#endif
#define CEG_DEBUG(X) { if(ENABLE_DEBUG_CEG) { X } }

#define MAX(X,Y) ( (X) > (Y) ? (X) : (Y) )
#define MIN(X,Y) ( (X) < (Y) ? (X) : (Y) )

using namespace cpputil;
using namespace std;
using namespace stoke;
using namespace x64asm;
using namespace std::chrono;

#ifdef DEBUG_CHECKER_PERFORMANCE
uint64_t SmtObligationChecker::number_queries_ = 0;
uint64_t SmtObligationChecker::number_cases_ = 0;
uint64_t SmtObligationChecker::constraint_gen_time_ = 0;
uint64_t SmtObligationChecker::solver_time_ = 0;
uint64_t SmtObligationChecker::aliasing_time_ = 0;
uint64_t SmtObligationChecker::ceg_time_ = 0;
#endif

template <typename K, typename V>
map<K,V> append_maps(vector<map<K,V>> maps) {

  map<K,V> output;

  for (auto m : maps) {
    for (auto p : m) {
      output[p.first] = p.second;
    }
  }

  return output;
}

/** Returns an invariant representing the fact that the last state transition in the path is taken. */
std::shared_ptr<Invariant> SmtObligationChecker::get_jump_inv(const Cfg& cfg, Cfg::id_type end_block, const CfgPath& p, bool is_rewrite) {
  auto jump_type = ObligationChecker::is_jump(cfg, end_block, p, p.size() - 1);

  //cout << "get_jump_inv: end_block=" << end_block << " is_rewrite=" << is_rewrite << " path=" << p << endl;
  //cout << "get_jump_inv: jump type " << jump_type << endl;

  if (jump_type == SmtObligationChecker::JumpType::NONE) {
    return std::make_shared<TrueInvariant>();
  }

  auto last_block = p[p.size()-1];
  auto instr_count = cfg.num_instrs(last_block);
  assert(instr_count > 0);
  auto jump_instr = cfg.get_code()[cfg.get_index(Cfg::loc_type(last_block, instr_count - 1))];

  if (!jump_instr.is_jcc()) {
    //cout << "   get_jump_inv: no cond jump" << endl;
    return std::make_shared<TrueInvariant>();
  }

  bool is_fallthrough = jump_type == SmtObligationChecker::JumpType::FALL_THROUGH;
  auto jump_inv = std::make_shared<FlagInvariant>(jump_instr, is_rewrite, is_fallthrough);
  //cout << "   get_jump_inv: got " << *jump_inv << endl;
  return jump_inv;
}

BitVector SmtObligationChecker::add_to_map(const SymArray& array, unordered_map<uint64_t, BitVector>& mem_map) const {

  BitVector default_value_bv(8);
  if (array.ptr == nullptr) {
    return default_value_bv;
  }

  auto symarray = dynamic_cast<const SymArrayVar* const>(array.ptr);
  assert(symarray != nullptr);
  auto str = symarray->name_;
  //cout << "name is " << str << endl;

  auto map_and_default = solver_.get_model_array(str, 64, 8);
  auto orig_map = map_and_default.first;
  uint64_t default_value = map_and_default.second;

  for (auto pair : orig_map) {
    //TODO: fix so that we add one byte at a time
    auto start_addr = pair.first;
    auto bv = pair.second;
    mem_map[start_addr] = bv;
  }

  default_value_bv.get_fixed_byte(0) = default_value;


  return default_value_bv;
}

bool SmtObligationChecker::build_testcase_from_array(CpuState& ceg, SymArray heap, const vector<SymArray>& stacks, const map<const SymBitVectorAbstract*, uint64_t>& others, uint64_t stack_pointer) const {

  unordered_map<uint64_t, BitVector> mem_map;
  auto default_heap = add_to_map(heap, mem_map);
  BitVector default_stack(8);
  for (auto stack : stacks)
    default_stack = add_to_map(stack, mem_map);

  for (auto p : others) {
    auto abs_var = p.first;
    uint64_t size = p.second/8;

    auto var = dynamic_cast<const SymBitVectorVar*>(abs_var);
    assert(var != NULL);
    auto var_name = var->get_name();
    auto var_size = var->get_size();
    assert(var_size == 64);
    auto address_bv = solver_.get_model_bv(var_name, var_size);
    auto addr = address_bv.get_fixed_quad(0);

    for (uint64_t i = addr; i < addr + size; ++i) {
      if (!mem_map.count(i)) {
        mem_map[i] = default_heap;
      }
    }
  }

  // ensure space on stack is allocated and initialized
  size_t stack_size=128;
  uint64_t rsp_loc = stack_pointer;
  DEBUG_BUILDTC_FROM_ARRAY(cout << "[build_testcase_from_array] rsp_loc = " << rsp << endl;)
  BitVector zero_bv(8);
  if (rsp_loc > stack_size && rsp_loc < (uint64_t)(-stack_size)) {
    for (uint64_t i = rsp_loc+stack_size; i > rsp_loc - stack_size; i--) {
      if (!mem_map.count(i))
        mem_map[i] = default_stack;
    }
  }

  BUILD_TC_DEBUG(
    cout << "[build tc] map:" << endl;
  for (auto it : mem_map) {
  cout << "  " << it.first << " -> " << (uint64_t)it.second.get_fixed_byte(0) << endl;
  }
  );

  return ceg.memory_from_map(mem_map);

}


CpuState SmtObligationChecker::run_sandbox_on_path(const Cfg& cfg, const CfgPath& P, const CpuState& state) {

  // TODO: fixme
  CpuState output;
  return output;

}

bool SmtObligationChecker::check_counterexample(
  const Cfg& target,
  const Cfg& rewrite,
  const Code& target_unroll,
  const Code& rewrite_unroll,
  const CfgPath& P,
  const CfgPath& Q,
  const LineMap& target_linemap,
  const LineMap& rewrite_linemap,
  const std::shared_ptr<Invariant> assume,
  const std::shared_ptr<Invariant> prove,
  const CpuState& ceg_t,
  const CpuState& ceg_r,
  CpuState& ceg_t_expected,
  CpuState& ceg_r_expected,
  bool separate_stack) {

  CpuState target_output;
  CpuState rewrite_output;
  for (size_t k = 0; k < 2; ++k) {
    const CpuState& start = k ? ceg_r : ceg_t;
    CpuState& expected = k ? ceg_r_expected : ceg_t_expected;
    const Cfg& program = k ? rewrite : target;
    const Code& unroll = k ? rewrite_unroll : target_unroll;
    const CfgPath& path = k ? Q : P;
    const LineMap& linemap = k ? rewrite_linemap : target_linemap;
    CpuState& output = k ? rewrite_output : target_output;
    string name = k ? "rewrite" : "target";
    Cfg cfg(unroll, program.def_ins(), program.live_outs());

    DEBUG_CHECK_CEG(
      stringstream ss;
      ss << "ceg-check-debug-";
      if (k == 0)
      ss << "target";
      else
        ss << "rewrite";
        ofstream debug(ss.str()+".cfg");
        debug << cfg.get_function() << endl;
        debug.close();
        ofstream debug2(ss.str()+".cs");
        debug2 << start << endl;
        debug2.close();
        cout << "LINEMAP" << endl;
      for (auto entry : linemap) {
        cout << entry.first << " -> lineno: " << entry.second.line_number << " bb: " << entry.second.block_number << endl;
      }
  )

    /** Setup Sandbox */
    Sandbox sb;
    sb.set_abi_check(false);
    sb.set_stack_check(false);
    sb.set_linemap(linemap);
    sb.insert_input(start);

    /** Run Sandbox */
    DataCollector dc(sb);
    auto traces = dc.get_detailed_traces(cfg, &linemap);
    assert(traces.size() > 0);
    auto trace = traces[0];

    auto last_state = *sb.get_output(0);
    if (last_state.code != ErrorCode::NORMAL) {
      cout << "  (Counterexample fails in sandbox for " << name << ".)" << endl;
      cout << "  START STATE " << endl << start << endl << endl;
      cout << "  EXPECTED STATE " << endl << expected << endl << endl;
      return false;
    }

    /** Get output */
    output = traces[0].back().cs;

    /** Compare */
    DEBUG_CHECK_CEG(
      assert(path.size() > 0);
      auto last_block = path.back();
      RegSet registers;
      if (last_block == program.get_exit())
      registers = program.def_outs();
      else
        registers = program.def_ins(last_block);
        cout << "registers that should be compared: " << registers << endl;)
        if (output != expected) {
          cout << "  (Counterexample execution differs in sandbox for " << name << ".)" << endl;
          cout << "  START STATE " << endl << start << endl << endl;
          cout << "  EXPECTED STATE " << endl << expected << endl << endl;
          cout << "  ACTUAL STATE " << endl << output << endl << endl;
          cout << diff_states(expected, output, false, true, x64asm::RegSet::universe());
          cout << "  CODE " << endl << unroll << endl << endl;
          return false;
        }
  }

  // First, the counterexample has to pass the invariant.
  if (!assume->check(ceg_t, ceg_r)) {
    cout << "  (Counterexample does not meet assumed invariant.)" << endl;
    auto conj = dynamic_pointer_cast<ConjunctionInvariant>(assume);
    for (size_t i = 0; i < conj->size(); ++i) {
      auto inv = (*conj)[i];
      if (!inv->check(ceg_t, ceg_r))
        cout << "     " << *inv << endl;
    }
    return false;
  }

  // Check the sandbox-provided output states to see if they fail the 'prove' invariant
  if (prove->check(target_output, rewrite_output)) {
    cout << "  (Counterexample satisfies desired invariant; it shouldn't)" << endl;
    return false;
  }

  cout << "  (Counterexample verified in sandbox)" << endl;
  return true;
}


void SmtObligationChecker::build_circuit(const Cfg& cfg, Cfg::id_type bb, JumpType jump,
    SymState& state, size_t& line_no, const LineMap& line_info,
    bool ignore_last_line) {

  if (cfg.num_instrs(bb) == 0)
    return;

  size_t start_index = cfg.get_index(std::pair<Cfg::id_type, size_t>(bb, 0));
  size_t end_index = start_index + cfg.num_instrs(bb);

  /** symbolically execute each instruction */
  for (size_t i = start_index; i < end_index; ++i) {
    auto li = line_info.at(line_no);
    line_no++;
    auto instr = cfg.get_code()[i];

    /*cout << "ABOUT TO SYMBOLICALLY EXECUTE " << instr << endl;
    cout << "STATE IS " << endl;
    cout << state << endl << endl;*/

    if (instr.is_jcc()) {
      if (ignore_last_line)
        continue;

      // get the name of the condition
      string name = opcode_write_att(instr.get_opcode());
      string condition = name.substr(1);
      auto constraint = ConditionalHandler::condition_predicate(condition, state);

      // figure out if its this condition (jump case) or negation (fallthrough)
      //cout << "INSTR: " << instr << endl;
      switch (jump) {
      case JumpType::JUMP:
        //cout << "Assuming jump for " << instr << endl;
        state.constraints.push_back(constraint);
        break;
      case JumpType::FALL_THROUGH:
        //cout << "Assuming fall-through for " << instr << endl;
        constraint = !constraint;
        state.constraints.push_back(constraint);
        break;
      case JumpType::NONE:
        break;
      default:
        assert(false);
      }

    } else if (instr.is_label_defn() || instr.is_nop() || instr.is_any_jump()) {
      continue;
    } else if (instr.is_ret()) {
      return;
    } else {
      // Build the handler for the instruction
      state.set_deref(li.deref);
      state.rip = SymBitVector::constant(64, li.rip_offset);

      if (nacl_) {
        // We need to add constraints keeping the index register (if present)
        // away from the edges of the ddress space.
        if (instr.is_explicit_memory_dereference()) {
          auto mem = instr.get_operand<M8>(instr.mem_index());
          if (mem.contains_index()) {
            R64 index = mem.get_index();
            auto address = state[index];
            state.constraints.push_back(address >= SymBitVector::constant(64, 0x10));
            state.constraints.push_back(address <= SymBitVector::constant(64, 0xfffffff0));
          }
        }
      }

      //cout << "LINE=" << line_no-1 << ": " << instr << endl;
      auto constraints = (filter_)(instr, state);
      for (auto constraint : constraints) {
        state.constraints.push_back(constraint);
      }

      if (filter_.has_error()) {
        error_ = filter_.error();
      }
    }
  }
}


void SmtObligationChecker::return_error(Callback& callback, string& s, void* optional, uint64_t smt_duration, uint64_t gen_duration) const {
  ObligationChecker::Result result;
  result.verified = false;
  result.has_ceg = false;
  result.has_error = true;
  result.error_message = s;
  result.source_version = string(version_info);
  result.smt_time_microseconds = smt_duration;
  result.gen_time_microseconds = gen_duration;
  callback(result, optional);
}


bool SmtObligationChecker::generate_arm_testcases(
  const Cfg& target,
  const Cfg& rewrite,
  const Code& target_unroll,
  const Code& rewrite_unroll,
  const LineMap& target_linemap,
  const LineMap& rewrite_linemap,
  bool separate_stack,
  const std::shared_ptr<Invariant> assume,
  std::vector<std::pair<CpuState,CpuState>>& testcases) {

  cout << "uh-oh.  attempting stategen." << endl;

  SymState state_t("1");
  SymState state_r("2");
  FlatMemory target_flat(separate_stack);
  FlatMemory rewrite_flat(separate_stack);
  state_t.memory = &target_flat;
  state_r.memory = &rewrite_flat;
  size_t dummy = 0;
  auto my_assume = assume->clone();
  auto assumption = (*my_assume)(state_t, state_r, dummy);

  CpuState target_tc;
  CpuState rewrite_tc;

  //assumption = simplifier_.simplify(assumption);
  bool assumption_sat = solver_.is_sat({assumption});
  bool ok = true;
  if (solver_.has_error() || !assumption_sat) {
    cout << "couldn't get satisfying assignment for assumption" << endl;
    return false;
  } else {
    cout << "... extracting model" << endl;
    target_tc = state_from_model("_1");
    rewrite_tc = state_from_model("_2");

    vector<map<const SymBitVectorAbstract*, uint64_t>> other_maps;
    other_maps.push_back(target_flat.get_access_list());
    other_maps.push_back(rewrite_flat.get_access_list());
    auto other_map = append_maps(other_maps);
    auto target_rsp = target_tc[rsp];
    auto rewrite_rsp = rewrite_tc[rsp];

    // doesn't really matter if these fail or not...
    build_testcase_from_array(target_tc, target_flat.get_start_variable(),
                              target_flat.get_stack_start_variables(),
                              other_map, target_rsp);
    build_testcase_from_array(rewrite_tc, rewrite_flat.get_start_variable(),
                              rewrite_flat.get_stack_start_variables(),
                              other_map, rewrite_rsp);

    cout << "... running sandbox / statgen for target" << endl;
    Sandbox sb1;
    sb1.set_abi_check(false);
    sb1.set_stack_check(false);
    StateGen sg1(&sb1);
    cout << target_unroll << endl;
    sb1.set_linemap(target_linemap);
    sg1.set_linemap(target_linemap);
    sg1.set_max_attempts(target_unroll.size());
    ok = sg1.get(target_tc, Cfg(target_unroll, target.def_ins(), target.live_outs()), true);
    if (!ok) {
      cout << "SG1 failed: " << sg1.get_error() << endl;
      return false;
    }

    cout << "... running sandbox / statgen for rewrite" << endl;
    Sandbox sb2;
    sb2.set_abi_check(false);
    sb2.set_stack_check(false);
    StateGen sg2(&sb2);
    cout << rewrite_unroll << endl;
    sb2.set_linemap(rewrite_linemap);
    sg2.set_linemap(rewrite_linemap);
    sg2.set_max_attempts(rewrite_unroll.size());
    ok = sg2.get(rewrite_tc, Cfg(rewrite_unroll, rewrite.def_ins(), rewrite.live_outs()), true);

    if (!ok) {
      cout << "SG2 failed: " << sg2.get_error() << endl;
      return false;
    }

    cout << "stategen target tc: " << endl;
    cout << target_tc << endl;
    cout << "stategen rewrite tc: " << endl;
    cout << rewrite_tc << endl;
    cout << "stategen worked!" << endl;
    testcases.push_back(std::pair<CpuState,CpuState>(target_tc, rewrite_tc));
    return true;
  }
  return false;
}

void SmtObligationChecker::check(
  const Cfg& target,
  const Cfg& rewrite,
  Cfg::id_type target_block,
  Cfg::id_type rewrite_block,
  const CfgPath& P,
  const CfgPath& Q,
  std::shared_ptr<Invariant> assume,
  std::shared_ptr<Invariant> prove,
  const vector<pair<CpuState, CpuState>>& given_testcases,
  Callback& callback,
  bool override_separate_stack,
  void* optional) {

  auto start_time = system_clock::now();

  auto testcases = given_testcases;

#ifdef DEBUG_CHECKER_PERFORMANCE
  number_queries_++;
  microseconds perf_start = duration_cast<microseconds>(system_clock::now().time_since_epoch());
#endif

  // TEMPORARY -- for debugging
  /*
  auto assume_conj = static_cast<ConjunctionInvariant*>(&assume);
  vector<size_t> is_postponable;
  for(size_t i = 0 ; i < assume_conj->size(); ++i) {
    cout << " conjunct " << i << " is : " << *(*assume_conj)[i] << endl;
    if((*assume_conj)[i]->is_postponable()) {
      is_postponable.insert(is_postponable.begin(), i);
      cout <<"      - postponable" << endl;
    } else {
      cout <<"      - not postponable" << endl;
    }
  }
  for(auto i : is_postponable) {
    //assume_conj->remove(i);
  }*/

  static mutex print_m;
  OBLIG_DEBUG(print_m.lock();)
  OBLIG_DEBUG(cout << "===========================================" << endl;)
  OBLIG_DEBUG(cout << "Obligation Check. solver_=" << &solver_ << " this=" << this << endl;)
  OBLIG_DEBUG(cout << "Paths P: " << P << " Q: " << Q << endl;)
  OBLIG_DEBUG(cout << "Assuming: ";)
  OBLIG_DEBUG(assume->write_pretty(cout);)
  OBLIG_DEBUG(cout << endl;)
  OBLIG_DEBUG(cout << "Proving: ";)
  OBLIG_DEBUG(prove->write_pretty(cout);)
  OBLIG_DEBUG(cout << endl;)
  OBLIG_DEBUG(cout << "----" << endl;)
  OBLIG_DEBUG(print_m.unlock();)

  // Get a list of all aliasing cases.
  bool flat_model = alias_strategy_ == AliasStrategy::FLAT;
  bool arm_model = alias_strategy_ == AliasStrategy::ARM;
  bool dummy_model = alias_strategy_ == AliasStrategy::DUMMY;
  bool arm_testcases = arm_model && (testcases.size() > 0);

  //OBLIG_DEBUG(cout << "[check_core] arm_testcases = " << arm_testcases << endl;)

#ifdef DEBUG_CHECKER_PERFORMANCE
  microseconds perf_alias = duration_cast<microseconds>(system_clock::now().time_since_epoch());
  aliasing_time_ += (perf_alias - perf_start).count();
#endif

#ifdef DEBUG_CHECKER_PERFORMANCE
  microseconds perf_constr_start = duration_cast<microseconds>(system_clock::now().time_since_epoch());
  number_cases_++;
#endif

  // Step 2: Build circuits
  vector<SymBool> constraints;

  SymState state_t("1_INIT");
  SymState state_r("2_INIT");

  bool separate_stack = separate_stack_ || override_separate_stack;
  //OBLIG_DEBUG(cout << "separate_stack = " << separate_stack << endl;)
  if (flat_model) {
    state_t.memory = new FlatMemory(separate_stack);
    state_r.memory = new FlatMemory(separate_stack);
  } else if (arm_model) {
    state_t.memory = new ArmMemory(separate_stack, solver_);
    state_r.memory = new ArmMemory(separate_stack, solver_);
    oc_sandbox_.reset();
  } else if (dummy_model) {
    state_t.memory = new TrivialMemory();
    state_r.memory = new TrivialMemory();
  }

  // Check for memory equality invariants.  If one has a non-empty set of locations that
  // aren't related, we update the memory representations with some writes to illustrate this.
  auto assume_conj = dynamic_pointer_cast<ConjunctionInvariant>(assume);
  size_t invariant_lineno = 0;
  if (assume_conj) {
    for (size_t i = 0; i < assume_conj->size(); ++i) {
      auto inv = (*assume_conj)[i];
      auto memequ = dynamic_pointer_cast<MemoryEqualityInvariant>(inv);
      if (memequ) {
        auto constraint = (*memequ)(state_t, state_r, invariant_lineno);
        constraints.push_back(constraint);
        //cout << "Adding constraint for memory equality: " << constraint << endl;
        auto excluded_locations = memequ->get_excluded_locations();
        for (auto loc : excluded_locations) {
          DereferenceInfo di;
          di.is_invariant = true;
          di.invariant_number = invariant_lineno;
          di.is_rewrite = loc.is_rewrite;
          di.implicit_dereference = false;
          di.line_number = (size_t)(-1);
          invariant_lineno++;

          auto& state = loc.is_rewrite ? state_r : state_t;
          auto var_addr = loc.get_addr(state_t, state_r);
          auto var_value = SymBitVector::tmp_var(loc.size*8);
          //cout << "Performing write of " << var_addr << " -> " << var_value << endl;
          state.memory->write(var_addr, var_value, loc.size*8, di);
        }
        assume_conj->remove(i);
        break;
      }
    }
  }

  // Add (other) given assumptions
  auto assumption = (*assume)(state_t, state_r, invariant_lineno);
  constraints.push_back(assumption);
  invariant_lineno++;

  // Generate line maps
  LineMap target_linemap;
  LineMap rewrite_linemap;
  Code target_unroll;
  Code rewrite_unroll;
  PathUnroller::generate_linemap(target, P, target_linemap, false, target_unroll);
  PathUnroller::generate_linemap(rewrite, Q, rewrite_linemap, true, rewrite_unroll);



  // Build the circuits
  error_ = "";

  size_t line_no = 0;
  try {
    for (size_t i = 0; i < P.size(); ++i)
      build_circuit(target, P[i], is_jump(target,target_block,P,i), state_t, line_no, target_linemap, i == P.size() - 1);
    line_no = 0;
    for (size_t i = 0; i < Q.size(); ++i)
      build_circuit(rewrite, Q[i], is_jump(rewrite,rewrite_block,Q,i), state_r, line_no, rewrite_linemap, i == Q.size() - 1);
  } catch (validator_error e) {
    stringstream message;
    message << e.get_file() << ":" << e.get_line() << ": " << e.get_message();
    auto str = message.str();
    uint64_t gen_time = duration_cast<microseconds>(system_clock::now() - start_time).count();
    return_error(callback, str, optional, 0, gen_time);
    delete state_t.memory;
    delete state_r.memory;
    return;
  }

  // Get the last jump conditions
  if (P.size() > 0) {
    auto ji = get_jump_inv(target, target_block, P, false);
    size_t tmp_invariant_lineno = invariant_lineno;
    SymBool conj = (*ji)(state_t, state_t, tmp_invariant_lineno);
    //cout << "LAST JUMP COND FOR TARGET: " << conj << endl;
    constraints.push_back(conj);
  }
  if (Q.size() > 0) {
    auto ji = get_jump_inv(rewrite, rewrite_block, Q, true);
    size_t tmp_invariant_lineno = invariant_lineno;
    SymBool conj = (*ji)(state_r, state_r, tmp_invariant_lineno);
    //cout << "LAST JUMP COND FOR REWRITE: " << conj << endl;
    constraints.push_back(conj);
  }


  if (error_ != "") {
    uint64_t gen_time = duration_cast<microseconds>(system_clock::now() - start_time).count();
    return_error(callback, error_, optional, 0, gen_time);
    return;
  }

  constraints.insert(constraints.end(), state_t.constraints.begin(), state_t.constraints.end());
  constraints.insert(constraints.end(), state_r.constraints.begin(), state_r.constraints.end());


  if (testcases.size() == 0) {
    // this is an expensive road, so let's do a sanity check first
    // also it seems unlikely this path is feasible given that nobody gave us
    // a test case for it...
    auto sat_start = system_clock::now();
    //simplifier_.simplify(constraints);
    if (!solver_.is_sat(constraints) && !solver_.has_error()) {
      cout << "We've finished early without modeling memory!" << endl;
      /** we're done, yo. */
      uint64_t smt_duration = duration_cast<microseconds>(system_clock::now() - sat_start).count();
      uint64_t gen_duration = duration_cast<microseconds>(sat_start - start_time).count();

      ObligationChecker::Result result;
      result.solver = solver_.get_enum();
      result.strategy = alias_strategy_;
      result.smt_time_microseconds = smt_duration;
      result.gen_time_microseconds = gen_duration;
      result.source_version = string(version_info);
      result.comments = "No memory short circuit";
      result.verified = true;
      result.has_ceg = false;
      result.has_error = false;
      result.error_message = "";
      callback(result, optional);
      return;
    } else {
      cout << "Couldn't take short-circuit option without memory." << endl;
    }

  }


  auto prove_conj = dynamic_pointer_cast<ConjunctionInvariant>(prove);
  shared_ptr<MemoryEqualityInvariant> prove_memequ;
  if (!prove_conj) {
    prove_conj = make_shared<ConjunctionInvariant>();
    prove_conj->add_invariant(prove);
  }
  for (size_t i = 0; i < prove_conj->size(); ++i) {
    auto inv = (*prove_conj)[i];
    auto memequ = dynamic_pointer_cast<MemoryEqualityInvariant>(inv);
    if (memequ) {
      prove_memequ = memequ;
      prove_conj->remove(i);
      break;
    }
  }

  // Build inequality constraint
  auto prove_part2 = !(*prove_conj)(state_t, state_r, invariant_lineno);
  //cout << "prove constraints part 2 = " << prove_part2 << endl;

  // Try to generate ARM testcase if needed
  if (arm_model && (testcases.size() == 0)) {
    generate_arm_testcases(target, rewrite, target_unroll, rewrite_unroll,
                           target_linemap, rewrite_linemap, separate_stack,
                           assume, testcases);
    arm_testcases = arm_model && (testcases.size() > 0);
  }

  DereferenceMaps deref_maps;
  if (arm_testcases) {
    // Build dereference map
    for (const auto& tc : testcases) {
      DereferenceMap deref_map;
      deref_maps.push_back(deref_map);
      break;
    }

    // Update dereference maps for the assumption if ARM
    for (size_t i = 0; i < deref_maps.size(); ++i) {
      size_t tmp_invariant_lineno = 0;
      auto& deref_map = deref_maps[i];
      const auto& tc_pair = testcases[i];
      DEBUG_ARM(
        cout << "[check_core] adding assume dereference map" << endl;
        cout << tc_pair.first << endl << endl;
        cout << tc_pair.second << endl << endl;)
      assume->get_dereference_map(deref_map, tc_pair.first, tc_pair.second, tmp_invariant_lineno);
      DEBUG_ARM(
        cout << "[check_core] debugging assume dereference map 1" << endl;
        cout << "deref_map size = " << deref_map.size() << endl;
      for (auto it : deref_map) {
      cout << it.first.invariant_number << " -> " << it.second << endl;
    })
      prove->get_dereference_map(deref_map, tc_pair.first, tc_pair.second, tmp_invariant_lineno);
      DEBUG_ARM(
        cout << "[check_core] debugging prove dereference map 1" << endl;
        cout << "deref_map size = " << deref_map.size() << endl;
      for (auto it : deref_map) {
      cout << it.first.invariant_number << " -> " << it.second << endl;
    })
    }

    // Update dereference maps for the code if ARM and we have testcases
    CpuState last_target;
    CpuState last_rewrite;
    for (size_t k = 0; k < 2; ++k) {
      auto& unroll_code = k ? rewrite_unroll : target_unroll;
      auto& testcase = k ? testcases[0].second : testcases[0].first;
      auto& last = k ? last_rewrite : last_target;
      auto& linemap = k ? rewrite_linemap : target_linemap;
      DEBUG_ARM(cout << "[check_core] adding code dereferences is_rewrite=" << k << endl;)

      Cfg unroll_cfg(unroll_code);
      oc_sandbox_.set_abi_check(false);
      oc_sandbox_.set_stack_check(false);
      oc_sandbox_.reset();
      oc_sandbox_.clear_inputs();
      oc_sandbox_.insert_input(testcase);
      DataCollector oc_data_collector(oc_sandbox_);
      oc_data_collector.set_collect_before(true);

      assert(linemap.size() == unroll_code.size() - 1);

      auto traces = oc_data_collector.get_detailed_traces(unroll_cfg, &linemap);

      DEBUG_ARM(
        cout << "Unroll code: " << endl << unroll_code << endl;
        cout << "[check_core] traces.size() = " << traces.size() << endl;
        cout << "[check_core] traces[0].size() = " << traces[0].size() << endl;
        cout << "[check_core] unroll_code.size() = " << unroll_code.size() << endl;)
      for (size_t i = 0; i < traces[0].size(); ++i) {
        auto instr = unroll_code[i];
        DEBUG_ARM(cout << "[check_core] dereferences for " << instr << endl;)
        if (instr.is_memory_dereference()) {
          auto dri = linemap[i].deref;
          auto state = traces[0][i].cs;
          auto addr = state.get_addr(instr, linemap[i].rip_offset);
          DEBUG_ARM(
            cout << "[check_core]     * found one!" << endl;
            cout << "                 addr = " << addr << endl;
          for (auto r : r64s) {
          cout << "                         " << r << " = " << state[r] << endl;
          })
          deref_maps[0][dri] = addr;
        }
        last = traces[0][i].cs;
      }
    }

    for (size_t i = 0; i < deref_maps.size(); ++i) {
      size_t tmp_invariant_lineno = invariant_lineno;
      auto& deref_map = deref_maps[i];
      const auto& tc_pair = testcases[i];
      //cout << "[check_core] adding prove dereference map" << endl;
      prove->get_dereference_map(deref_map, last_target, last_rewrite, tmp_invariant_lineno);
    }
  }

  if (arm_model) {
    /** When we read out the constraint for the proof, we want to get the ending
      state of the heap, not the initial state. */
    auto target_arm = static_cast<ArmMemory*>(state_t.memory);
    auto rewrite_arm = static_cast<ArmMemory*>(state_r.memory);
    target_arm->finalize_heap();
    rewrite_arm->finalize_heap();
  }


  // Extract the final states of target/rewrite
  SymState state_t_final("1_FINAL");
  SymState state_r_final("2_FINAL");

  for (auto it : state_t.equality_constraints(state_t_final, RegSet::universe(), {}))
    constraints.push_back(it);
  for (auto it : state_r.equality_constraints(state_r_final, RegSet::universe(), {}))
    constraints.push_back(it);

  // Add any extra memory constraints that are needed
  if (flat_model) {
    auto target_flat = static_cast<FlatMemory*>(state_t.memory);
    auto rewrite_flat = static_cast<FlatMemory*>(state_r.memory);
    auto target_con = target_flat->get_constraints();
    auto rewrite_con = rewrite_flat->get_constraints();
    constraints.insert(constraints.end(),
                       target_con.begin(),
                       target_con.end());
    constraints.insert(constraints.end(),
                       rewrite_con.begin(),
                       rewrite_con.end());
  } else if (arm_model) {

    auto target_arm = static_cast<ArmMemory*>(state_t.memory);
    auto rewrite_arm = static_cast<ArmMemory*>(state_r.memory);

    vector<SymBool> initial_assumptions = { assumption };
    bool sat = target_arm->generate_constraints(rewrite_arm, initial_assumptions, constraints, deref_maps);
    if (!sat) {
      // we can end early!

      uint64_t duration = duration_cast<microseconds>(system_clock::now() - start_time).count();
      ObligationChecker::Result result;
      result.solver = solver_.get_enum();
      result.strategy = alias_strategy_;
      result.smt_time_microseconds = 0;
      result.gen_time_microseconds = duration;
      result.source_version = string(version_info);
      result.verified = true;
      result.has_ceg = false;
      result.has_error = false;
      result.error_message = "";
      callback(result, optional);
      return;
    }

    auto target_con = target_arm->get_constraints();
    auto rewrite_con = rewrite_arm->get_constraints();
    constraints.insert(constraints.end(),
                       target_con.begin(),
                       target_con.end());
    constraints.insert(constraints.end(),
                       rewrite_con.begin(),
                       rewrite_con.end());
  }

  // Add prove memequ constraint
  if (prove_memequ) {
    vector<SymBitVector> excluded_badaddrs = prove_memequ->get_excluded_addresses(state_t, state_r);

    auto target_heap = arm_model ? static_cast<ArmMemory*>(state_t.memory)->get_variable()
                       : static_cast<FlatMemory*>(state_t.memory)->get_variable();
    auto rewrite_heap = arm_model ? static_cast<ArmMemory*>(state_r.memory)->get_variable()
                        : static_cast<FlatMemory*>(state_r.memory)->get_variable();

    if (excluded_badaddrs.size()) {
      SymBitVector badaddr = SymBitVector::tmp_var(64);
      SymBool prove_part1 = SymBool::_false();
      SymBool is_badaddr = SymBool::_true();
      for (auto it : excluded_badaddrs)
        is_badaddr = is_badaddr & (it != badaddr);

      auto target_read = target_heap[badaddr];
      auto rewrite_read = rewrite_heap[badaddr];

      is_badaddr = is_badaddr & (target_read != rewrite_read);
      prove_part1 = prove_part1 | is_badaddr;
      //cout << "Generating prove_part1 = " << prove_part1 << endl;

      auto prove_constraint = prove_part1 | prove_part2;
      constraints.push_back(prove_constraint);
    } else {
      auto prove_constraint = !(target_heap == rewrite_heap) | prove_part2;
      constraints.push_back(prove_constraint);
    }
  } else {
    constraints.push_back(prove_part2);
  }


  CONSTRAINT_DEBUG(print_m.lock();)
  CONSTRAINT_DEBUG(cout << "[ConstraintDebug] for P: " << P << " Q: " << Q << endl;)
  CONSTRAINT_DEBUG(
    cout << endl << "CONSTRAINTS" << endl << endl;;
  for (auto it : constraints) {
  cout << it << endl;
})
  CONSTRAINT_DEBUG(print_m.unlock();)

  // Step 4: Invoke the solver
#ifdef DEBUG_CHECKER_PERFORMANCE
  microseconds perf_constr_end = duration_cast<microseconds>(system_clock::now().time_since_epoch());
  constraint_gen_time_ += (perf_constr_end - perf_constr_start).count();
#endif

  auto sat_start = system_clock::now();

  //simplifier_.simplify(constraints);
  bool is_sat = solver_.is_sat(constraints);
  uint64_t smt_duration = duration_cast<microseconds>(system_clock::now() - sat_start).count();
  uint64_t gen_duration = duration_cast<microseconds>(sat_start - start_time).count();

  if (solver_.has_error()) {
    stringstream err;
    err << "solver: " << solver_.get_error();
    auto str = err.str();
    return_error(callback, str, optional, smt_duration, gen_duration);
    return;
  }

#ifdef DEBUG_CHECKER_PERFORMANCE
  microseconds perf_solve = duration_cast<microseconds>(system_clock::now().time_since_epoch());
  solver_time_ += (perf_solve - perf_constr_end).count();
#endif



  ObligationChecker::Result result;
  result.solver = solver_.get_enum();
  result.strategy = alias_strategy_;
  result.smt_time_microseconds = smt_duration;
  result.gen_time_microseconds = gen_duration;
  result.source_version = string(version_info);

  if (is_sat) {
    bool have_ceg;
    CpuState ceg_t = state_from_model("_1_INIT");
    CpuState ceg_r = state_from_model("_2_INIT");
    CpuState ceg_tf = state_from_model("_1_FINAL");
    CpuState ceg_rf = state_from_model("_2_FINAL");

    auto target_rsp = ceg_t[rsp];
    auto rewrite_rsp = ceg_r[rsp];

    bool ok = true;
    if (flat_model) {
      auto target_flat = static_cast<FlatMemory*>(state_t.memory);
      auto rewrite_flat = static_cast<FlatMemory*>(state_r.memory);

      vector<map<const SymBitVectorAbstract*, uint64_t>> other_maps;
      other_maps.push_back(target_flat->get_access_list());
      other_maps.push_back(rewrite_flat->get_access_list());
      auto other_map = append_maps(other_maps);

      ok &= build_testcase_from_array(ceg_t, target_flat->get_start_variable(),
                                      target_flat->get_stack_start_variables(), other_map, target_rsp);
      ok &= build_testcase_from_array(ceg_r, rewrite_flat->get_start_variable(),
                                      rewrite_flat->get_stack_start_variables(), other_map, rewrite_rsp);
      build_testcase_from_array(ceg_tf, target_flat->get_variable(),
                                target_flat->get_stack_end_variables(), other_map, target_rsp);
      build_testcase_from_array(ceg_rf, rewrite_flat->get_variable(),
                                rewrite_flat->get_stack_end_variables(), other_map, rewrite_rsp);

    } else if (arm_model) {
      auto target_arm = static_cast<ArmMemory*>(state_t.memory);
      auto rewrite_arm = static_cast<ArmMemory*>(state_r.memory);

      vector<map<const SymBitVectorAbstract*, uint64_t>> other_maps;
      other_maps.push_back(target_arm->get_access_list());
      other_maps.push_back(rewrite_arm->get_access_list());
      auto other_map = append_maps(other_maps);

      ok &= build_testcase_from_array(ceg_t, target_arm->get_start_variable(),
                                      target_arm->get_stack_start_variables(), other_map, target_rsp);
      ok &= build_testcase_from_array(ceg_r, rewrite_arm->get_start_variable(),
                                      rewrite_arm->get_stack_start_variables(), other_map, rewrite_rsp);
      build_testcase_from_array(ceg_tf, target_arm->get_variable(),
                                target_arm->get_stack_end_variables(), other_map, target_rsp);
      build_testcase_from_array(ceg_rf, rewrite_arm->get_variable(),
                                rewrite_arm->get_stack_end_variables(), other_map, rewrite_rsp);
    }

    if (!ok) {
      // We don't have memory accurate in our counterexample.  Just leave.
      CEG_DEBUG(cout << "[counterexample-debug] for P: " << P << " Q: " << Q << endl;)
      CEG_DEBUG(cout << "(  Counterexample does not have accurate memory)" << endl;)
    }

    CEG_DEBUG(print_m.lock();)
    CEG_DEBUG(cout << "[counterexample-debug] for P: " << P << " Q: " << Q << endl;)
    CEG_DEBUG(cout << "  (Got counterexample)" << endl;)
    CEG_DEBUG(cout << "TARGET START STATE" << endl;)
    CEG_DEBUG(cout << ceg_t << endl;)
    CEG_DEBUG(cout << "REWRITE START STATE" << endl;)
    CEG_DEBUG(cout << ceg_r << endl;)
    CEG_DEBUG(cout << "TARGET (expected) END STATE" << endl;)
    CEG_DEBUG(cout << ceg_tf << endl;)
    CEG_DEBUG(cout << "REWRITE (expected) END STATE" << endl;)
    CEG_DEBUG(cout << ceg_rf << endl;)
    CEG_DEBUG(print_m.unlock();)


    /** Checks ceg with sandbox. */
    if (!check_counterexamples_ || check_counterexample(target, rewrite, target_unroll, rewrite_unroll, P, Q, target_linemap, rewrite_linemap, assume, prove, ceg_t, ceg_r, ceg_tf, ceg_rf, separate_stack)) {
    } else {
      ok = false;
      CEG_DEBUG(cout << "  (Spurious counterexample detected) P=" << P << " Q=" << Q << endl;)
    }

    delete state_t.memory;
    delete state_r.memory;

#ifdef DEBUG_CHECKER_PERFORMANCE
    microseconds perf_ceg = duration_cast<microseconds>(system_clock::now().time_since_epoch());
    ceg_time_ += (perf_ceg - perf_solve).count();
    print_performance();
#endif

    result.verified = false;
    result.has_ceg = ok;
    result.has_error = false;
    result.error_message = "";
    result.target_ceg = ceg_t;
    result.rewrite_ceg = ceg_r;
    result.target_final_ceg = ceg_tf;
    result.rewrite_final_ceg = ceg_rf;

    callback(result, optional);

  } else {

    delete state_t.memory;
    delete state_r.memory;

    CEG_DEBUG(cout << "  (This case verified)" << endl;)

#ifdef DEBUG_CHECKER_PERFORMANCE
    microseconds perf_ceg = duration_cast<microseconds>(system_clock::now().time_since_epoch());
    ceg_time_ += (perf_ceg - perf_solve).count();
#endif

    result.verified = true;
    result.has_ceg = false;
    result.has_error = false;
    result.error_message = "";
    callback(result, optional);

  }


}



CpuState SmtObligationChecker::state_from_model(const string& name_suffix) {

  CpuState cs;

  // 64-bit GP registers
  for (size_t i = 0; i < r64s.size(); ++i) {
    stringstream name;
    name << r64s[i] << name_suffix;
    cs.gp[r64s[i]] = solver_.get_model_bv(name.str(), 64);
    //cout << "Var " << name.str() << " has value " << hex << cs.gp[r64s[i]].get_fixed_quad(0) << endl;
  }

  // XMMs/YMMs
  for (size_t i = 0; i < ymms.size(); ++i) {
    stringstream name;
    name << ymms[i] << name_suffix;
    cs.sse[ymms[i]] = solver_.get_model_bv(name.str(), 256);
  }

  // flags
  for (size_t i = 0; i < eflags.size(); ++i) {
    if (!cs.rf.is_status(eflags[i].index()))
      continue;

    stringstream name;
    name << eflags[i] << name_suffix;
    cs.rf.set(eflags[i].index(), solver_.get_model_bool(name.str()));
  }

  // Figure out error code
  if (solver_.get_model_bool("sigbus" + name_suffix)) {
    cs.code = ErrorCode::SIGBUS_;
  } else if (solver_.get_model_bool("sigfpe" + name_suffix)) {
    cs.code = ErrorCode::SIGFPE_;
  } else if (solver_.get_model_bool("sigsegv" + name_suffix)) {
    cs.code = ErrorCode::SIGSEGV_;
  } else {
    cs.code = ErrorCode::NORMAL;
  }

  return cs;
}

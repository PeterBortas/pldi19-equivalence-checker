// Copyright 2013-2015 Stanford University
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

#include "src/cfg/cfg.h"
#include "src/cfg/path_enumerator.h"
#include "src/validator/bounded.h"

using namespace std;
using namespace stoke;
using namespace x64asm;

bool BoundedValidator::find_pair_testcase(const Cfg& target, const Cfg& rewrite,
    const Path& P, const Path& Q, CpuState& tc) {

  auto target_tcs = path_to_testcase_[0][P];
  auto rewrite_tcs = path_to_testcase_[1][Q];


  // Do they have something in common?  if so, we're done.
  // Both of these vectors are sorted --> O(n) time.
  size_t j = 0;
  size_t winner = (size_t)-1;
  for(size_t i : target_tcs) {
    while(j < rewrite_tcs.size() && rewrite_tcs[j] < i) {
      j++;
    }
    if(rewrite_tcs[j] == i) {
      // we're done!
      tc = *(sandbox_->get_input(i));
      return true;
    }
  }

  // Couldn't find anything
  return false;

}

void BoundedValidator::learn_paths(const Cfg& cfg, bool is_rewrite) {
  sandbox_->insert_function(cfg);
  sandbox_->set_entrypoint(cfg.get_code()[0].get_operand<x64asm::Label>(0));
  sandbox_->insert_after(BoundedValidator::sandbox_path_callback, this);
  for(size_t i = 0; i < sandbox_->num_inputs(); ++i) {
    Path p;
    current_path_ = &p;
    p.push_back(cfg.get_entry());

    sandbox_->run(i);

    // check the path to see if it's in the bound
    std::map<Cfg::id_type, size_t> counts;
    bool keep = true;
    for(auto node : p) {
      counts[node]++;
      if(counts[node] > bound_) {
        keep = false;
        break;
      }
    }

    if(keep) {
      p.push_back(cfg.get_exit());
      path_to_testcase_[is_rewrite][p].push_back(i);
    }
  }
}

void BoundedValidator::sandbox_path_callback(const StateCallbackData& data, void* arg) {

  auto ptr = (BoundedValidator*)arg;

  // Record whenever a basic block is entered
  auto cfg = Cfg(data.code, RegSet::universe(), RegSet::universe());
  auto location = cfg.get_loc(data.line);
  if(location.second == 0) {
    auto bb = location.first;
    ptr->current_path_->push_back(bb);
  }

}


void BoundedValidator::build_circuit(const Cfg& cfg, Cfg::id_type bb, JumpType jump, SymState& state, size_t& line_no) {

  if(cfg.num_instrs(bb) == 0)
    return;

  size_t start_index = cfg.get_index(std::pair<Cfg::id_type, size_t>(bb, 0));
  size_t end_index = start_index + cfg.num_instrs(bb);

  for(size_t i = start_index; i < end_index; ++i) {
    line_no++;
    auto instr = cfg.get_code()[i];

    if(instr.is_jcc()) {
      // get the name of the condition
      string name = opcode_write_att(instr.get_opcode());
      string condition = name.substr(1);
      auto constraint = ConditionalHandler::condition_predicate(condition, state);

      // figure out if its this condition (jump case) or negation (fallthrough)
      //cout << "INSTR: " << instr << endl;
      switch(jump) {
      case JumpType::JUMP:
        state.constraints.push_back(constraint);
        //cout << "Constraint: (jump) " << constraint << endl;
        break;
      case JumpType::FALL_THROUGH:
        constraint = !constraint;
        state.constraints.push_back(constraint);
        //cout << "Constraint: (ft) " << constraint << endl;
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
      state.set_lineno(line_no-1);
      handler_.build_circuit(instr, state);

      if(handler_.has_error()) {
        stringstream ss;
        ss << "Error building circuit for: " << instr << ".";
        ss << "Handler says: " << handler_.error();
        throw VALIDATOR_ERROR(ss.str());
      }
    }
  }

}

BoundedValidator::JumpType BoundedValidator::is_jump(const Cfg& cfg, const Path& P, size_t i) {

  if(i == P.size() - 1)
    return JumpType::NONE;

  auto block = P[i];

  auto first = cfg.succ_begin(block);
  if(first == cfg.succ_end(block)) {
    // there are no successors
    return JumpType::NONE;
  }

  auto second = first++;
  if(second == cfg.succ_end(block)) {
    // there is only only successor
    return JumpType::NONE;
  }

  // ok, there are at least 2 successors
  auto next_block = P[i+1];
  if(next_block == block + 1)
    return JumpType::FALL_THROUGH;
  else
    return JumpType::JUMP;
}

bool BoundedValidator::verify_pair(const Cfg& target, const Cfg& rewrite, const Path& P, const Path& Q) {

  // Step 0: Check if there's any memory access
  bool memory = false;
  for(size_t i = 0; i < 2; ++i) {
    auto& path = i ? P : Q;
    auto& cfg = i ? target : rewrite;
    for(auto bb : path) {
      if(!cfg.num_instrs(bb))
        continue;
      size_t start = cfg.get_index(std::pair<Cfg::id_type, size_t>(bb, 0));
      size_t end = start + cfg.num_instrs(bb);
      for(size_t j = start; j < end; ++j) {
        auto instr = cfg.get_code()[j];
        if(instr.is_memory_dereference() && !instr.is_ret()) {
          memory = true;
          break;
        }
      }
      if(memory)
        break;
    }
    if(memory)
      break;
  }

  // Step 1: Learn aliasing relationships
  auto memories = std::pair<CellMemory*, CellMemory*>(NULL, NULL);
  if(memory) {
    CpuState testcase;
    if(!find_pair_testcase(target, rewrite, P, Q, testcase)) {
      cout << "DEBUG Could not find testcase for this path" << endl;
      return false;
    }

    memories = am.build_cell_model(target, rewrite, testcase);
    if(memories.first == NULL || memories.second == NULL) {
      has_error_ = true;
      error_ = "Overlapping memory accesses found.";
      return false;
    }
  }

  // Step 3: Build circuits
  init_mm();

  vector<SymBool> constraints;

  SymState init("");
  SymState state_t("1_INIT");
  SymState state_r("2_INIT");

  try {

    for(auto it : state_t.equality_constraints(init, target.def_ins()))
      constraints.push_back(it);
    for(auto it : state_r.equality_constraints(init, rewrite.def_ins()))
      constraints.push_back(it);

    if(memory) {
      state_t.memory = memories.first;
      state_r.memory = memories.second;
      cout << "Start memory constraint: " << (memories.first->equality_constraint(*memories.second)) << endl;
      constraints.push_back(memories.first->equality_constraint(*memories.second));
    }

    size_t line_no = 0;
    for(size_t i = 0; i < P.size(); ++i)
      build_circuit(target, P[i], is_jump(target,P,i), state_t, line_no);
    line_no = 0;
    for(size_t i = 0; i < Q.size(); ++i)
      build_circuit(rewrite, Q[i], is_jump(rewrite,Q,i), state_r, line_no);

    constraints.insert(constraints.begin(), state_t.constraints.begin(), state_t.constraints.end());
    constraints.insert(constraints.begin(), state_r.constraints.begin(), state_r.constraints.end());

    SymBool inequality = SymBool::_false();
    for(auto it : state_t.equality_constraints(state_r, target.live_outs())) {
      inequality = inequality | !it;
    }
    if(memory) {
      cout << "End memory constraint: " << memories.first->equality_constraint(*memories.second) << endl;
      inequality = inequality | !memories.first->equality_constraint(*memories.second);
    }

    constraints.push_back(inequality);

    // Step 4: Invoke the solver
    bool is_sat = solver_.is_sat(constraints);
    if(solver_.has_error())
      throw VALIDATOR_ERROR("solver: " + solver_.get_error());

    stop_mm();
    return !is_sat;

  } catch (validator_error e) {
    has_error_ = true;
    error_ = e.get_message();
    error_file_ = e.get_file();
    error_line_ = e.get_line();

    stop_mm();
    return false;
  }

  return false;

}

bool BoundedValidator::verify(const Cfg& target, const Cfg& rewrite) {

#ifdef DEBUG_VALIDATOR
  std::cout << "Enter the dragon!" << std::endl;
#endif
  // State
  has_error_ = false;

  // Step 0: Background checks
  // - all the instructions are supported in target/rewrite
  for(const auto& cfg : {
  target, rewrite
}) {
    for(auto instr : cfg.get_code()) {
      if(instr.is_label_defn() || instr.is_any_jump() || instr.is_ret()) {
        continue;
      }
      else if(!is_supported(instr)) {
        stringstream ss;
        ss << "Instruction " << instr << " is unsupported.";
        SET_ERROR(ss.str());
        return false;
      }
    }
  }

  // - def-ins/live-outs match
  if(target.def_ins() != rewrite.def_ins()) {
    SET_ERROR("Target def-ins do not match rewrite def-ins");
    return false;
  }
  if(target.live_outs() != target.live_outs()) {
    SET_ERROR("Target live-outs do not match rewrite live-outs");
    return false;
  }

  // - def-in/live-out are supported
  for(const auto& cfg : {
  target, rewrite
}) {
    if(!handler_.regset_is_supported(cfg.def_ins())) {
      SET_ERROR("Def-ins are not supported");
      return false;
    }
    if(!handler_.regset_is_supported(cfg.live_outs())) {
      SET_ERROR("Live outs are not supported");
      return false;
    }
  }

  // Step 1: get all the paths from the enumerator
  for(auto path : PathEnumerator::find_paths(target, bound_))
    paths_[false].push_back(path);
  for(auto path : PathEnumerator::find_paths(rewrite, bound_))
    paths_[true].push_back(path);

  // Step 2: get the paths taken by every testcase
  learn_paths(target, false);
  learn_paths(rewrite, true);

  // Step 3: check each pair of paths
  bool ok = true;
  size_t total = paths_[false].size() * paths_[true].size();
  size_t count = 0;
  for(auto target_path : paths_[false]) {
    for(auto rewrite_path : paths_[true]) {
      count++;
      //cout << "Verifying pair " << count << "/" << total << endl;
      ok &= verify_pair(target, rewrite, target_path, rewrite_path);
    }
  }

  return ok;
}




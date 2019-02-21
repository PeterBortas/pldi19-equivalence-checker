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

#ifndef STOKE_SRC_VALIDATOR_DDEC_H
#define STOKE_SRC_VALIDATOR_DDEC_H

#include "src/validator/paa.h"
#include "src/validator/data_collector.h"
#include "src/validator/invariant.h"
#include "src/validator/invariants/conjunction.h"
#include "src/validator/learner.h"
#include "src/validator/obligation_checker.h"
#include "src/validator/validator.h"


namespace stoke {


class DdecValidator : public Validator {

public:

  DdecValidator(ObligationChecker& checker, Sandbox& sandbox, InvariantLearner& inv) :
    Validator(checker),
    target_({}), rewrite_({}),
          sandbox_(sandbox),
          data_collector_(sandbox),
          invariant_learner_(inv),
          alignment_predicate_(),
          training_set_size_(20)
  {
  }

  DdecValidator(const DdecValidator& rhs) :
    Validator(rhs),
    target_(rhs.target_),
    rewrite_(rhs.rewrite_),
    sandbox_(rhs.sandbox_),
    data_collector_(sandbox_),
    invariant_learner_(rhs.invariant_learner_),
    training_set_size_(rhs.training_set_size_) {

    target_bound_ = rhs.target_bound_;
    rewrite_bound_ = rhs.rewrite_bound_;
  }

  /** Set the bound for bounded validator */
  DdecValidator& set_bound(size_t target_bound, size_t rewrite_bound) {
    target_bound_ = target_bound;
    rewrite_bound_ = rewrite_bound;
    return *this;
  }

  DdecValidator& set_training_set_size(size_t n) {
    training_set_size_ = n;
    return *this;
  }

  /** Add an assumption that holds at every point (e.g. read-only memory) */
  DdecValidator& assume_always(std::shared_ptr<Invariant> assumption) {
    assume_always_.push_back(assumption);
    return *this;
  }

  /** Specify an alignment predicate */
  DdecValidator& set_alignment_predicate(std::shared_ptr<Invariant> inv) {
    alignment_predicate_ = inv;
    return *this;
  }


  /** Verify if target and rewrite are equivalent. */
  bool verify(const Cfg& target, const Cfg& rewrite);


private:

  // Dependencies
  Cfg target_;
  Cfg rewrite_;

  Sandbox& sandbox_;
  DataCollector data_collector_;
  InvariantLearner invariant_learner_;

  /** Generate a warning for the user about a possible failure reason. */
  void warn(std::string s);

  /** Compute the initial invariant */
  std::shared_ptr<ConjunctionInvariant> get_initial_invariant(ProgramAlignmentAutomata&) const;
  std::shared_ptr<ConjunctionInvariant> get_final_invariant(ProgramAlignmentAutomata&) const;
  std::shared_ptr<ConjunctionInvariant> get_fail_invariant() const;

  /** Verify that a paa is correct */
  bool verify_paa(ProgramAlignmentAutomata& paa);

  std::vector<Variable> get_stack_locations(bool is_rewrite);


  bool build_paa_for_alignment_predicate(std::shared_ptr<Invariant> inv, ProgramAlignmentAutomata&);
  std::vector<uint64_t> find_alignment_predicate_constants(size_t target_point, size_t rewrite_point, EqualityInvariant inv);
  void get_states_at_cutpoint(size_t trace, size_t target_point, size_t rewrite_point, std::vector<DataCollector::TracePoint>& target_states, std::vector<DataCollector::TracePoint>& rewrite_states, bool bound) const;
  bool test_alignment_predicate(std::shared_ptr<Invariant> inv);

  /** Invariants assumed to hold at any point. */
  std::vector<std::shared_ptr<Invariant>> assume_always_;

  /** Bound */
  size_t target_bound_;
  size_t rewrite_bound_;

  /** Traces */
  std::vector<DataCollector::Trace> target_traces_;
  std::vector<DataCollector::Trace> rewrite_traces_;

  /** Try to sign extend values? */
  bool try_sign_extend_;

  size_t callbacks_expected_;
  size_t callbacks_count_;
  size_t verified_;

  std::shared_ptr<Invariant> alignment_predicate_;

  std::chrono::time_point<std::chrono::system_clock> benchmark_starttime_;
  std::chrono::time_point<std::chrono::system_clock> benchmark_searchstart_;
  uint64_t benchmark_total_search_time_;
  bool benchmark_proof_succeeded_;

  size_t training_set_size_;
};

} // namespace stoke

#endif

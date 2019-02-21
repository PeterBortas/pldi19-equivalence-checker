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

#ifndef STOKE_SRC_VALIDATOR_POSTGRES_OBLIGATION_CHECKER_H
#define STOKE_SRC_VALIDATOR_POSTGRES_OBLIGATION_CHECKER_H

#include <functional>
#include <vector>
#include <pqxx/pqxx>

#include "src/validator/obligation_checker.h"
#include "src/validator/handlers/combo_handler.h"
#include "src/validator/smt_obligation_checker.h"

namespace stoke {

class PostgresObligationChecker : public ObligationChecker {

public:

  PostgresObligationChecker(std::string connection_string, SmtObligationChecker& smt_checker) :
    handler_(), filter_(handler_), connection_string_(connection_string),
    connection_(connection_string.c_str()), pipeline_(NULL), pipeline_tx_(NULL),
    dispatches_(0),
    smt_checker_(smt_checker)
  {
    enable_z3(true);
    enable_cvc4(true);
    enable_flat(true);
    enable_arm(true);
    enable_shortcircuit(0);

    if (!connection_.is_open()) {
      std::cerr << "Failed to open connection to database." << std::endl;
    } else {
      std::cout << "Database connection open." << std::endl;
    }

    make_tables();
  }

  ~PostgresObligationChecker() {
    std::cout << "Closing database connection." << std::endl;
    connection_.disconnect();
  }

  PostgresObligationChecker& enable_z3(bool b) {
    enable_z3_ = b;
    return *this;
  }

  PostgresObligationChecker& enable_cvc4(bool b) {
    enable_cvc4_ = b;
    return *this;
  }

  PostgresObligationChecker& enable_flat(bool b) {
    enable_flat_ = b;
    return *this;
  }

  PostgresObligationChecker& enable_arm(bool b) {
    enable_arm_ = b;
    return *this;
  }

  PostgresObligationChecker& enable_shortcircuit(size_t milliseconds) {
    shortcircuit_ = milliseconds;
    smt_checker_.get_solver().set_timeout(milliseconds);
    return *this;
  }

  virtual void check(const Cfg& target, const Cfg& rewrite,
                     Cfg::id_type target_block, Cfg::id_type rewrite_block,
                     const CfgPath& p, const CfgPath& q,
                     std::shared_ptr<Invariant> assume, std::shared_ptr<Invariant> prove,
                     const std::vector<std::pair<CpuState, CpuState>>& testcases,
                     Callback& callback,
                     bool override_separate_stack,
                     void* optional) override;

  /** Blocks until all the checking has done and the callbacks have been called. */
  virtual void block_until_complete();

  /** Checks to see if we can make any callbacks now. */
  virtual void check_for_callbacks() {
    poll_database();
  }

  /** Forget about everything that has been started. */
  virtual void delete_all() {
    if (dispatches_ > 0) {
      std::cout << "Waiting on pipeline..." << std::endl;
      pipeline_->complete();
      std::cout << "Closing up nontransaction..." << std::endl;
      pipeline_tx_->commit();
    }
    dispatches_ = 0;
    outstanding_jobs.clear();
    delete pipeline_;
    delete pipeline_tx_;
    pipeline_ = NULL;
    pipeline_tx_ = NULL;
  }



  /** Get the filter */
  virtual Filter& get_filter() {
    return filter_;
  }

private:


  /** Book keeping */
  ComboHandler handler_;
  DefaultFilter filter_;

  /** Database connection */
  std::string connection_string_;
  pqxx::connection connection_;
  pqxx::pipeline* pipeline_;
  pqxx::transaction_base* pipeline_tx_;

  size_t dispatches_;
  /** Solver functionality */
  bool enable_z3_;
  bool enable_cvc4_;
  bool enable_flat_;
  bool enable_arm_;

  /** For quick obligations. */
  size_t shortcircuit_;
  SmtObligationChecker smt_checker_;

  /** Make the tables we need, if they don't already exist. */
  void make_tables();
  /** Poll the database for callbacks */
  void poll_database();

  /** Info to track the jobs that should be running. */
  /** Sometimes two jobs with the same hash will be submitted, in which case we need to
    be prepared to perform the callback multiple times. */
  struct Job {
    std::string hash;
    std::vector<Callback*> callbacks;
    std::vector<void*> optionals;
    bool completed;

    void invoke_callbacks(ObligationChecker::Result r) {
      for (size_t i = 0; i < callbacks.size(); ++i) {
        (*callbacks[i])(r, optionals[i]);
      }
    }

    Job() {
      completed = false;
      hash = "";
    }
  };

  /** A list of the jobs we don't have results for. */
  std::map<std::string, Job> outstanding_jobs;
  std::map<std::string, Result> local_cache_;

};

} //namespace stoke

#endif

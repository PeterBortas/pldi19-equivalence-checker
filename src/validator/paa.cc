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


#include <fstream>
#include <queue>

#include "src/serialize/serialize.h"
#include "src/state/cpu_states.h"
#include "src/validator/paa.h"

using namespace stoke;
using namespace std;

#define DEBUG_LEARN_STATE_DATA(X) { if(1) { X } }
#define DEBUG_IS_PREFIX(X) { if(0) { X } }
#define DEBUG_CFG_FRINGE(X) { if(0) { cout << "[cfg_fringe] " << X;} }
#define DEBUG_IN_SCC(X) { if(0) { X } }
#define DEBUG_IN_CYCLE(X) { if(0) { X } }

bool ProgramAlignmentAutomata::State::operator<(const ProgramAlignmentAutomata::State& other) const {
  if (ts != other.ts)
    return ts < other.ts;
  if (rs != other.rs)
    return rs < other.rs;

  assert(*this == other);
  return false;
}

bool ProgramAlignmentAutomata::State::operator==(const ProgramAlignmentAutomata::State& other) const {
  return (ts == other.ts && rs == other.rs);
}

bool ProgramAlignmentAutomata::Edge::operator<(const ProgramAlignmentAutomata::Edge& other) const {
  if (from != other.from)
    return from < other.from;
  if (to != other.to)
    return to < other.to;
  if (te.size() != other.te.size())
    return te.size() < other.te.size();
  if (re.size() != other.re.size())
    return re.size() < other.re.size();

  assert(te.size() == other.te.size());
  for (size_t i = 0; i < te.size(); ++i)
    if (te[i] != other.te[i])
      return te[i] < other.te[i];

  assert(re.size() == other.re.size());
  for (size_t i = 0; i < re.size(); ++i)
    if (re[i] != other.re[i])
      return re[i] < other.re[i];

  assert(*this == other);
  return false;
}

bool ProgramAlignmentAutomata::Edge::operator==(const ProgramAlignmentAutomata::Edge& other) const {
  return (from == other.from && to == other.to && te == other.te && re == other.re);
}

ProgramAlignmentAutomata::Edge::Edge(ProgramAlignmentAutomata::State tail, const CfgPath& tp, const CfgPath& rp) {
  to = tail;
  te = tp;
  re = rp;
  empty_ = false;

  if (tp.size()) {
    from.ts = tp[0];
  } else {
    from.ts = to.ts;
  }

  if (rp.size()) {
    from.rs = rp[0];
  } else {
    from.rs = to.rs;
  }
}

bool ProgramAlignmentAutomata::is_prefix(const CfgPath& tr1, const DataCollector::Trace& tr2) {
  if (tr1.size() > tr2.size()) {
    DEBUG_IS_PREFIX(cout << "[is_prefix]     tr1:" << tr1.size() << " > tr2:" << tr2.size() << endl;)
    return false;
  }

  for (size_t i = 0; i < tr1.size(); ++i) {
    DEBUG_IS_PREFIX(cout << "[is_prefix]      tr1[" << i << "]=" << tr1[i] << "; tr2[" << i << "]=" << tr2[i].block_id << endl;)
    if (tr1[i] != tr2[i].block_id) {
      return false;
    }
  }

  return true;
}

bool ProgramAlignmentAutomata::is_edge_prefix(const CfgPath& tr1, const CfgPath& tr2) {
  if (tr1.size() > tr2.size()) {
    //cout << "     tr1:" << tr1.size() << " > tr2:" << tr2.size() << endl;
    return false;
  }

  for (size_t i = 0; i < tr1.size(); ++i) {
    //cout << "      tr1[" << i << "]=" << tr1[i] << "; tr2[" << i << "]=" << tr2[i].first << endl;
    if (tr1[i] != tr2[i]) {
      return false;
    }
  }

  return true;
}

void ProgramAlignmentAutomata::remove_prefix(const CfgPath& tr1, DataCollector::Trace& tr2) {
  assert(is_prefix(tr1, tr2));

  for (size_t i = 0; i < tr1.size(); ++i) {
    tr2.erase(tr2.begin());
  }
}

/** Here we trace one test case through the Automata along every possible path.
  Returns false on error. */
bool ProgramAlignmentAutomata::learn_state_data(const DataCollector::Trace& orig_target_trace,
    const DataCollector::Trace& orig_rewrite_trace) {

  /** Copy traces */
  auto target_trace = orig_target_trace;
  auto rewrite_trace = orig_rewrite_trace;

  /** Setup initial state */
  TraceState initial;
  initial.state = start_state();
  initial.target_current = target_trace[0].cs;
  initial.rewrite_current = rewrite_trace[0].cs;

  /** Configure initial traces */
  auto tt_copy = target_trace;
  auto rt_copy = rewrite_trace;

  initial.target_trace = tt_copy;
  initial.rewrite_trace = rt_copy;

  /** Record initial data */
  target_state_data_[initial.state].push_back(initial.target_current);
  rewrite_state_data_[initial.state].push_back(initial.rewrite_current);

  /** Setup worklist */
  vector<TraceState> current;
  vector<TraceState> next;
  next.push_back(initial);
  data_reachable_states_.insert(initial.state);

  auto exit = exit_state();

  /** Let the fun begin! */
  while (next.size()) {
    current = next;
    next.clear();

    // iterate over items in the worklist
    for (auto tr_state : current) {

      if (exit == tr_state.state) {

        if (tr_state.target_trace.size() != 1) {
          DEBUG_LEARN_STATE_DATA(cout << "[lsd] problem: at exit state, but there's still unconsumed target trace" << endl;)
          return false;
        }

        if (tr_state.rewrite_trace.size() != 1) {
          DEBUG_LEARN_STATE_DATA(cout << "[lsd] problem: at exit state, but there's still unconsumed rewrite trace" << endl;)
          return false;
        }

        continue;
      }

      DEBUG_LEARN_STATE_DATA(
        cout << "[lsd] processing trace state @ " << tr_state.state << endl;
        cout << "[lsd]            target rem  = " << DataCollector::project_states(tr_state.target_trace) << endl;
        cout << "[lsd]            rewrite rem = " << DataCollector::project_states(tr_state.rewrite_trace) << endl;)
      bool found_matching_edge = false;

      for (auto edge : next_edges_[tr_state.state]) {
        DEBUG_LEARN_STATE_DATA(
          cout << "[lsd]   Considering edge: " << edge.from << " -> " << edge.to << endl;
          cout << "     ";
          for (auto blk : edge.te)
          cout << blk << "  ";
          cout << "; ";
          for (auto blk : edge.re)
            cout << blk << "  ";
            cout << endl;)

            // check if edge's target path is prefix of tr_state's target path
            auto te_copy = edge.te;
        te_copy.push_back(edge.to.ts);
        if (!is_prefix(te_copy, tr_state.target_trace)) {
          DEBUG_LEARN_STATE_DATA(cout << "     target prefix fail" << endl;)
          continue;
        }

        // check if edge's rewrite path is prefix of tr_state's rewrite path
        auto re_copy = edge.re;
        re_copy.push_back(edge.to.rs);
        if (!is_prefix(re_copy, tr_state.rewrite_trace)) {
          DEBUG_LEARN_STATE_DATA(cout << "     rewrite prefix fail" << endl;)
          continue;
        }

        // if so, we:

        // (0) celebrate!
        found_matching_edge = true;

        // (1) update the state
        TraceState follow = tr_state;
        follow.state = edge.to;

        // (2) update the CpuStates
        if (edge.te.size() < follow.target_trace.size())
          follow.target_current = follow.target_trace[edge.te.size()].cs;
        if (edge.re.size() < follow.rewrite_trace.size())
          follow.rewrite_current = follow.rewrite_trace[edge.re.size()].cs;

        // (3) remove the prefixes from both traces
        remove_prefix(edge.te, follow.target_trace);
        remove_prefix(edge.re, follow.rewrite_trace);

        // (4) record the CpuState in the right place
        target_state_data_[edge.to].push_back(follow.target_current);
        rewrite_state_data_[edge.to].push_back(follow.rewrite_current);
        target_edge_data_[edge].push_back(tr_state.target_current);
        rewrite_edge_data_[edge].push_back(tr_state.rewrite_current);

        // (5) setup new worklist item
        next.push_back(follow);
        data_reachable_states_.insert(follow.state);

        DEBUG_LEARN_STATE_DATA(std::cout << "   - REACHABLE: " << follow.state << std::endl;
                               cout << "drs: ";
        for (auto it : data_reachable_states_) {
        cout << it << "    ";
      }
      cout << endl;)
      }

      if (!found_matching_edge) {
        DEBUG_LEARN_STATE_DATA(std::cout << "   - Could not find matching edge" << std::endl;)
        return false;
      }
    }
  }

  return true;

}

bool ProgramAlignmentAutomata::test_paa(DataCollector& dc, bool ignore_failures) {

  data_reachable_states_.clear();
  target_state_data_.clear();
  rewrite_state_data_.clear();

  auto target_traces = dc.get_traces(target_);
  auto rewrite_traces = dc.get_traces(rewrite_);

  // Step 1: get data at each state.
  for (size_t i = 0; i < target_traces.size(); ++i) {
    //cout << "TESTCASE " << i << endl;
    auto target_trace = target_traces[i];
    auto rewrite_trace = rewrite_traces[i];

    /*
    auto target_last = target_trace.back();
    target_last.block_id = target_.get_exit();
    target_trace.push_back(target_last);

    auto rewrite_last = rewrite_trace.back();
    rewrite_last.block_id = rewrite_.get_exit();
    rewrite_trace.push_back(rewrite_last);*/


    DEBUG_LEARN_STATE_DATA(
      cout << "[learn_invariants] target trace: " << DataCollector::project_states(target_trace) << endl;
      cout << "[learn_invariants] rewrite trace: " << DataCollector::project_states(rewrite_trace) << endl;
    )


    bool ok = learn_state_data(target_trace, rewrite_trace);
    if (!ok && !ignore_failures) {
      cout << "[learn_invariants] PAA doesn't accept test inputs" << endl;
      return false;
    }
  }

  return true;
}

bool ProgramAlignmentAutomata::learn_invariants(InvariantLearner& learner, ImplicationGraph& graph, vector<State> states) {

  // Step 2: learn the invariants
  target_.recompute();
  rewrite_.recompute();

  if (states.size() == 0)
    states.insert(states.begin(), data_reachable_states_.begin(), data_reachable_states_.end());

  for (auto state : states) {
    cout << "[learn_invariants] Learning invariants at " << state << endl;
    if (state == exit_state() || state == start_state() || state == fail_state())
      continue;

    /* For debugging states encountered. */
    /*
    DEBUG_LEARN_STATE_DATA(
      stringstream ts;
      ts << "state" << state << "-target.txt";
      string target_filename = ts.str();
      ofstream target_file;
      target_file.open(target_filename, ios::out);
      CpuStates target_out(target_state_data_[state]);
      target_out.write_text(target_file);
      target_file.close();

      stringstream rs;
      rs << "state" << state << "-rewrite.txt";
      string rewrite_filename = rs.str();
      ofstream rewrite_file;
      rewrite_file.open(rewrite_filename, ios::out);
      CpuStates rewrite_out(rewrite_state_data_[state]);
      rewrite_out.write_text(rewrite_file);
      rewrite_file.close();)*/

    // TODO: if there aren't enough states here, sound a warning
    auto target_state_count = target_state_data_[state].size();
    auto rewrite_state_count = rewrite_state_data_[state].size();
    cout << "[learn_invariants] learning over " << target_state_count << " target states and "
         << rewrite_state_count << " rewrite states" << endl;

    auto inv = learner.learn(target_.def_outs(state.ts), rewrite_.def_outs(state.rs),
                             target_state_data_[state], rewrite_state_data_[state], graph,
                             "", "");
    invariants_[state] = inv;
    cout << "About to print invariant at " << state << endl;
    cout << "[learn_invariants] Invariant at " << state << ": " << *inv << endl;

    /** Check to make sure we got an invariant. */
    if (inv->size() == 0)
      return false;
  }

  return true;

}


void ProgramAlignmentAutomata::compute_topological_sort(CfgSccs& target_scc, CfgSccs& rewrite_scc) {
  // get all the relevant blocks from target/rewrite
  vector<ProgramAlignmentAutomata::State> nodes;
  for (auto pair : invariants_) {
    auto node = pair.first;
    if (node == start_state() || node == exit_state() || node == fail_state())
      continue;
    nodes.push_back(node);
  }

  // sort the nodes by SCC (which should already be topolically sorted)
  auto compare = [&](ProgramAlignmentAutomata::State a, ProgramAlignmentAutomata::State b) -> bool {
    auto a_target_scc = target_scc.get_scc(a.ts);
    auto a_rewrite_scc = rewrite_scc.get_scc(a.rs);
    auto b_target_scc = target_scc.get_scc(b.ts);
    auto b_rewrite_scc = rewrite_scc.get_scc(b.rs);

    if (a_target_scc > b_target_scc)
      return true;
    return a_rewrite_scc > b_rewrite_scc;
  };

  sort(nodes.begin(), nodes.end(), compare);
  nodes.insert(nodes.begin(), start_state());
  nodes.insert(nodes.end(), exit_state());
  topological_sort_ = nodes;
}

void ProgramAlignmentAutomata::print_all() const {

  for (auto p : next_edges_) {
    auto state = p.first;
    cout << "STATE " << state << endl;
    auto conj = get_invariant(state);
    conj->write_pretty(cout);
    for (auto e : p.second) {
      cout << "    to " << e.to << " via target: ";
      for (auto n : e.te) {
        cout << n << "  ";
      }
      cout << "rewrite: ";
      for (auto n : e.re) {
        cout << n << "  ";
      }
      cout << endl;
    }
  }

  for (auto state : {
         exit_state(), fail_state()
       }) {
    cout << "STATE " << state << endl;
    auto conj = get_invariant(state);
    conj->write_pretty(cout);
  }


  if (topological_sort_.size() > 0) {
    cout << "TOPOLOGIAL SORT " << endl;
    for (auto it : topological_sort_)
      cout << it << "   ";
    cout << endl;
  }
}


vector<vector<ProgramAlignmentAutomata::Edge>> ProgramAlignmentAutomata::get_paths(
ProgramAlignmentAutomata::State start, ProgramAlignmentAutomata::State end) {
  cout << "Calling get_paths with " << start << " and " << end << endl;

  vector<vector<Edge>> results;

  auto edges_from_start = next_edges(start);
  for (auto e : edges_from_start) {
    auto successor = e.to;

    if (successor == e.from) //ignore self-loops
      continue;

    if (successor == end) {
      vector<Edge> path;
      path.push_back(e);
      results.push_back(path);
    } else {
      auto continuation_paths = get_paths(successor, end);
      for (auto path : continuation_paths) {
        path.insert(path.begin(), e);
        results.push_back(path);
      }
    }
  }

  return results;
}

void ProgramAlignmentAutomata::remove_prefixes() {

  bool done = false;

  auto states = get_edge_reachable_states();
  while (!done) {
    done = true;
    for (auto state : states) {
      auto edges = next_edges_[state];

      for (auto e1 : edges) {
        for (auto e2 : edges) {
          if (e1 == e2)
            continue;

          //cout << "Checking for prefix relationship between " << e1 << " AND " << e2 << endl;
          if (is_edge_prefix(e1.te, e2.te) && is_edge_prefix(e1.re, e2.re)) {
            //cout << "   Yes, prefix found" << endl;
            remove_edge(e2);
            done = false;
            break;
          } else {
            //cout << "   No prefix here" << endl;
          }
        }
        if (done == false)
          break;
      }
      if (done == false)
        break;
    }
  }
}

std::set<ProgramAlignmentAutomata::State> ProgramAlignmentAutomata::get_edge_reachable_states() const {

  set<State> global_reachable;
  global_reachable.insert(start_state());

  size_t init, curr;
  do {
    init = global_reachable.size();
    for (auto r : global_reachable) {
      //cout << "[sanity] from " << r << endl;
      for (auto p : next_states(r)) {
        //cout << "[sanity]    inserting " << p << endl;
        if (p == fail_state())
          continue;
        global_reachable.insert(p);
      }
    }

    curr = global_reachable.size();
  } while (curr > init);

  return global_reachable;
}

std::set<CfgPath> ProgramAlignmentAutomata::get_cfg_fringe(const Cfg& cfg, State state, bool is_rewrite) const {

  Cfg::id_type starting_block = is_rewrite ? state.rs : state.ts;
  std::set<CfgPath> outputs;

  /** Extract the list of safe paths starting at 'state' */
  vector<CfgPath> safe_paths;
  auto safe_edges = next_edges(state);
  DEBUG_CFG_FRINGE("safe paths" << endl)
  for (auto edge : safe_edges) {
    auto& path = is_rewrite ? edge.re : edge.te;
    safe_paths.push_back(path);
    DEBUG_CFG_FRINGE("   " << path << endl)
  }

  /** Enumerate all the paths through the Cfg starting at a given basic block.
    Once we get to a block that's not on any of the edges we record it as an
    answer and stop searching. */
  vector<CfgPath> current_paths;
  vector<CfgPath> next_paths;
  current_paths.push_back( { starting_block } );
  //for(auto it = cfg.succ_begin(starting_block); it != cfg.succ_end(starting_block); ++it) {
  //  current_paths.push_back({ *it });
  //}
  while (current_paths.size()) {

    DEBUG_CFG_FRINGE("current paths" << endl)

    // find all of the next paths
    next_paths.clear();
    for (const auto& cp : current_paths) {
      DEBUG_CFG_FRINGE("   " << cp << endl)
      assert(cp.size() > 0);
      auto last_block = cp[cp.size() - 1];
      for (auto it = cfg.succ_begin(last_block); it != cfg.succ_end(last_block); ++it) {
        auto new_path = cp;
        new_path.push_back(*it);
        next_paths.push_back(new_path);
      }
    }

    // if any of the next paths are not covered by the safe edges, add it to
    // the solution set
    if (next_paths.size()) {
      vector<size_t> to_remove;
      DEBUG_CFG_FRINGE("next paths" << endl)
      assert(next_paths.size() > 0);
      for (int i = (int)next_paths.size()-1; i >= 0; --i) {
        assert((size_t)i < next_paths.size() && (size_t)i >= 0);
        const auto& np = next_paths[i];
        bool in_answers = true;
        for (const auto& sp : safe_paths) {
          if (CfgPaths::is_prefix(np, sp) && np != sp) {
            in_answers = false;
            break;
          }
        }
        if (in_answers) {
          DEBUG_CFG_FRINGE("   " << np << "  (output)" << endl)
          outputs.insert(np);
          to_remove.push_back(i);
        } else {
          DEBUG_CFG_FRINGE("   " << np << "  (next round)" << endl)
        }
      }

      for (auto item : to_remove) {
        assert(item < next_paths.size());
        next_paths.erase(next_paths.begin() + item);
      }
    }
    current_paths = next_paths;
  }

  return outputs;
}

std::vector<ProgramAlignmentAutomata::Edge> ProgramAlignmentAutomata::compute_failure_edges(const Cfg& target, const Cfg& rewrite) const {
  std::vector<Edge> outputs;

  /** for each state */
  auto states = get_edge_reachable_states();
  for (auto state : states) {
    if (state == exit_state())
      continue;
    if (state == fail_state())
      continue;

    /** get the "fringe" points on each of the target and rewrite CFGs */
    //cout << "====== FRINGE FOR TARGET PROGRAM AND STATE " << state << endl;
    auto target_fringe = get_cfg_fringe(target, state, false);
    //cout << "====== FRINGE FOR TARGET PROGRAM AND STATE " << state << endl;
    auto rewrite_fringe = get_cfg_fringe(rewrite, state, true);

    /** for every pair of fringe points, figure out if the comparison is needed. */
    auto edges = next_edges(state);
    for (auto target_path : target_fringe) {
      for (auto rewrite_path : rewrite_fringe) {
        //cout << "Considering target_path=" << target_path;
        //cout << " rewrite_path=" << rewrite_path << endl;
        bool match = false;

        for (auto edge : edges) {
          // cout << "   Considering edge=" << edge << endl;
          if (CfgPaths::is_prefix(edge.te, target_path) &&
              CfgPaths::is_prefix(edge.re, rewrite_path)) {
            //cout << "      * match found!" << endl;
            match = true;
            break;
          }
        }

        if (!match) {
          Edge e;
          e.te = target_path;
          e.re = rewrite_path;
          e.from = state;
          e.to = fail_state();
          outputs.push_back(e);
        }
      }
    }
  }

  //cout << "FAILURE EDGES" << endl;
  //for(auto edge : outputs) {
  //  cout << edge << endl;
  //}

  return outputs;
}

void ProgramAlignmentAutomata::State::serialize(std::ostream& os) const {
  os << ts << " " << rs << endl;
}

ProgramAlignmentAutomata::State ProgramAlignmentAutomata::State::deserialize(std::istream& is) {
  Cfg::id_type a, b;
  is >> a >> ws >> b >> ws;
  return ProgramAlignmentAutomata::State(a, b);
}

void ProgramAlignmentAutomata::Edge::serialize(std::ostream& os) const {
  from.serialize(os);
  to.serialize(os);
  stoke::serialize<CfgPath>(os, te);
  stoke::serialize<CfgPath>(os, re);
}

ProgramAlignmentAutomata::Edge ProgramAlignmentAutomata::Edge::deserialize(std::istream& is) {
  ProgramAlignmentAutomata::Edge e;
  e.from = ProgramAlignmentAutomata::State::deserialize(is);
  e.to = ProgramAlignmentAutomata::State::deserialize(is);
  e.te = stoke::deserialize<CfgPath>(is);
  e.re = stoke::deserialize<CfgPath>(is);
  return e;
}

void ProgramAlignmentAutomata::serialize(std::ostream& os) const {
  stoke::serialize<Cfg>(os, target_);
  stoke::serialize<Cfg>(os, rewrite_);
  stoke::serialize<map<State, vector<Edge>>>(os, next_edges_);
  stoke::serialize<map<State, vector<Edge>>>(os, prev_edges_);
  stoke::serialize<map<State, std::shared_ptr<ConjunctionInvariant>>>(os, invariants_);
  stoke::serialize<vector<State>>(os, topological_sort_);
}

ProgramAlignmentAutomata ProgramAlignmentAutomata::deserialize(std::istream& is) {
  auto* target = stoke::deserialize<Cfg*>(is);
  auto* rewrite = stoke::deserialize<Cfg*>(is);

  ProgramAlignmentAutomata pod(*target, *rewrite);
  pod.next_edges_ = stoke::deserialize<map<State, vector<Edge>>>(is);
  pod.prev_edges_ = stoke::deserialize<map<State, vector<Edge>>>(is);
  pod.invariants_ = stoke::deserialize<map<State, std::shared_ptr<ConjunctionInvariant>>>(is);
  pod.topological_sort_ = stoke::deserialize<vector<State>>(is);
  return pod;
}

/** We are searching for cycles where the edges only contain blocks of the target /
   the edges only contain blocks of the rewrite. */
bool ProgramAlignmentAutomata::one_program_cycle(State s, bool is_target)  const {
  DEBUG_IN_CYCLE(cout << "[in_cycle] called for " << s << " is_target=" << is_target << endl;)
  map<State, bool> visited;
  visited[s] = true;
  queue<State> worklist;
  worklist.push(s);

  while (worklist.size()) {
    State t = worklist.front();
    worklist.pop();
    DEBUG_IN_CYCLE(cout << "[in_cycle] visiting " << t << endl;)
    if (next_edges_.count(t) == 0)
      continue;
    auto next = next_edges_.at(t);
    for (auto e : next) {
      DEBUG_IN_CYCLE(cout << "[in_cycle] considering edge " << e << endl;)
      if (is_target && e.re.size() != 0) {
        DEBUG_IN_CYCLE(cout << "[in_cycle]    skipping -- nonempty rewrite edge" << endl;)
        continue;
      }
      if (!is_target && e.te.size() != 0) {
        DEBUG_IN_CYCLE(cout << "[in_cycle]    skipping -- nonempty target edge" << endl;)
        continue;
      }

      auto u = e.to;
      DEBUG_IN_CYCLE(cout << "[in_cycle]     next is " << u << endl;)
      if (u == s) {
        DEBUG_IN_CYCLE(cout << "[in_cycle] returning true for " << s << endl;)
        return true;
      }
      if (!visited[u]) {
        worklist.push(u);
        visited[u] = true;
      }
    }
  }

  DEBUG_IN_CYCLE(cout << "[in_cycle] returning false for " << s << endl;)
  return false;
}

bool ProgramAlignmentAutomata::in_scc(State s) const {
  DEBUG_IN_SCC(cout << "[in_scc] called for " << s << endl;)
  map<State, bool> visited;
  visited[s] = true;
  queue<State> worklist;
  worklist.push(s);

  while (worklist.size()) {
    State t = worklist.front();
    worklist.pop();
    DEBUG_IN_SCC(cout << "[in_scc] visiting " << t << endl;)
    auto next = next_states(t);
    for (auto u : next) {
      DEBUG_IN_SCC(cout << "[in_scc]     next is " << u << endl;)
      if (u == s) {
        DEBUG_IN_SCC(cout << "[in_scc] returning true for " << s << endl;)
        return true;
      }
      if (!visited[u]) {
        worklist.push(u);
        visited[u] = true;
      }
    }
  }

  DEBUG_IN_SCC(cout << "[in_scc] returning false for " << s << endl;)
  return false;
}

/** Remove edges that aren't needed. */
bool ProgramAlignmentAutomata::simplify() {

  bool changes_made = false;
  /** Step 1: remove nodes that are not contained in a strongly connected component. */
  auto start = start_state();
  auto end = exit_state();

  bool fixpoint = false;
  while (!fixpoint) {
    fixpoint = true;
    auto edge_reachable = get_edge_reachable_states();
    //cout << "[simplify] starting fixpoint iteration" << endl;
    for (auto s = edge_reachable.rbegin(); s != edge_reachable.rend(); s++) {
      if (*s == start)
        continue;
      if (*s == end)
        continue;

      if (has_self_loop(*s))
        continue;

      //cout << "[simplify] State " << s << " not in SCC; trying to remove." << endl;
      auto edges_in = prev_edges(*s);
      auto edges_out = next_edges(*s);

      for (auto in : edges_in) {
        for (auto out : edges_out) {
          Edge e = in;
          e.to = out.to;
          e.te.insert(e.te.end(), out.te.begin(), out.te.end());
          e.re.insert(e.re.end(), out.re.begin(), out.re.end());
          add_edge(e);
          //cout << "[simplify] adding edge " << e << endl;
        }
      }

      for (auto in : edges_in) {
        //cout << "[simplify] removing edge " << in << endl;
        remove_edge(in);
      }
      for (auto out : edges_out) {
        //cout << "[simplify] removing edge " << out << endl;
        remove_edge(out);
      }
      prev_edges_.erase(*s);
      next_edges_.erase(*s);
      fixpoint = false;
      changes_made = true;
      break;
    }
  }

  //cout << "[simplify] proceeding to second step" << endl;
  changes_made |= simplify_edges();

  // redo until fixpoint
  if (changes_made)
    simplify();

  return changes_made;
}


/** Remove edges where another edge is a prefix.
  * Returns true if changes are made. */
bool ProgramAlignmentAutomata::simplify_edges() {
  bool changes_made = false;
  auto states = get_edge_reachable_states();
  for (auto s : states) {
    // check for any edges which are the prefix of another
    auto edges = next_edges_[s];
    set<Edge> edges_to_remove;

    for (size_t i = 0; i < edges.size(); ++i) {
      for (size_t j = 0; j < edges.size(); ++j) {
        if (i == j)
          continue;

        auto first = edges[i];
        auto second = edges[j];

        auto first_target_edge = first.te;
        first_target_edge.push_back(first.to.ts);
        auto second_target_edge = second.te;
        second_target_edge.push_back(second.to.ts);

        auto first_rewrite_edge = first.re;
        first_rewrite_edge.push_back(first.to.rs);
        auto second_rewrite_edge = second.re;
        second_rewrite_edge.push_back(second.to.rs);


        if (CfgPaths::is_prefix(first_target_edge, second_target_edge) &&
            CfgPaths::is_prefix(first_rewrite_edge, second_rewrite_edge)) {
          // remove 'second'
          edges_to_remove.insert(second);
          changes_made = true;
        }
      }
    }

    for (auto e : edges_to_remove)
      remove_edge(e);
  }

  return changes_made;
}



namespace std {

ostream& operator<<(ostream& os, const ProgramAlignmentAutomata::State& s) {
  return s.print(os);
}
ostream& operator<<(ostream& os, const ProgramAlignmentAutomata::Edge& s) {
  return s.print(os);
}



}



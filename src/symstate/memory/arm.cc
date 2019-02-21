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

#include <iomanip>
#include <utility>

#include "src/symstate/memory/arm.h"
#include "src/unionfind/unionfind.h"

using namespace std;
using namespace stoke;

#define DEBUG_ARM(X) { if(ENABLE_DEBUG_ARM) { X } }


bool ArmMemory::generate_constraints(
  ArmMemory* am,
  std::vector<SymBool>& initial_constraints,
  std::vector<SymBool>& all_constraints,
  const DereferenceMaps& deref_maps) {

  DEBUG_ARM(cout << "=========== PRINTING DEREFERENCE MAPS WOHOOOOOOOOOO =============" << endl;)
  DEBUG_ARM(
    size_t count = 0;
  for (auto& dm : deref_maps) {
  cout << "==== MAP " << count++ << endl;
  for (auto pair : dm) {
      auto di = pair.first;
      cout << "   is_rewrite: " << di.is_rewrite;
      if (di.is_invariant)
        cout << "; invariant: " << di.invariant_number;
      else
        cout << "; line: " << di.line_number;
      cout << " --> " << pair.second << endl;
    }
  }
  cout << "Map done..." << endl;
  )

  all_accesses_.clear();
  cells_.clear();
  access_offsets_.clear();

  // Check that initial invariants are sane
  //for(auto it : initial_constraints)
  //  cout << "initial_constraints = " << it << endl;
  bool sane = solver_.is_sat(initial_constraints);
  if (!sane) {
    DEBUG_ARM(cout << "Initial constraints unsatisfiable... ARM is easy here :)" << endl;)
    return false;
  }

  // 0. Get all the memory accesses in one place to look at.
  auto other_accesses = am->accesses_;
  all_accesses_.insert(all_accesses_.begin(), accesses_.begin(), accesses_.end());
  for (auto& it : all_accesses_) {
    it.is_other = false;
  }
  for (auto& it : other_accesses) {
    it.is_other = true;
    all_accesses_.push_back(it);
  }

  DEBUG_ARM(cout << "==== ARM ON " << all_accesses_.size() << " ACCESSES " << endl;)
  DEBUG_ARM(
  for (size_t i = 0; i < all_accesses_.size(); ++i) {
  cout << " ACCESS " << i << ":" << endl;
  auto access = all_accesses_[i];
    auto di = access.deref;
    cout << "      address = " << access.address << endl;
    cout << "      value = " << access.value << endl;
    cout << "      size = " << access.size << endl;
    cout << "      write = " << access.write << endl;
    cout << "      di.is_rewrite = " << (di.is_rewrite ? "true" : "false") << endl;
    cout << "      di.is_invariant = " << (di.is_invariant ? "true" : "false") << endl;
    cout << "      di.implicit = " << (di.implicit_dereference ? "true" : "false") << endl;
    cout << "      di.line_number = " << di.line_number << endl;
    cout << "      di.invariant_number = " << di.invariant_number << endl;
    for (auto deref_map : deref_maps) {
      if (deref_map.count(di) > 0) {
        auto addr = deref_map[di];
        cout << "      addr = " << addr << endl;
      } else {
        cout << "      ~~ addr missing from deref map ~~ " << endl;
      }
    }
  }
  )

  // 1. figure out relationships between offsets
  if (deref_maps.size() > 0) {
    generate_constraints_offsets_data(initial_constraints, deref_maps);
  } else {
    generate_constraints_offsets_nodata(initial_constraints);
  }

  // 2. get the cells and enumerate constraints
  generate_constraints_enumerate_cells();
  generate_constraints_given_cells(am, initial_constraints);

  return true;
}

void ArmMemory::generate_constraints_offsets_data(std::vector<SymBool>& initial_constraints,
    const DereferenceMaps& deref_maps) {
  UnionFind<size_t> unionfind;

  const auto& deref_map = deref_maps[0];

  for (size_t i = 0; i < all_accesses_.size(); ++i) {
    auto i_access = all_accesses_[i];
    auto i_di = i_access.deref;

    if (deref_map.count(i_di) == 0) {
      DEBUG_ARM(cout << "-> Initial deref map has nothing for access " << i << endl;)
      continue;
    }

    auto i_addr = deref_map.at(i_di);
    auto components = unionfind.components();
    unionfind.add(i);

    for (auto j : components) {
      auto j_access = all_accesses_[j];
      auto j_di = j_access.deref;

      if (deref_map.count(j_di) == 0) {
        DEBUG_ARM(cout << "-> Initial deref map has nothing for access " << j << endl;)
        continue;
      }

      auto j_addr = deref_map.at(j_di);
      auto diff = (j_addr - i_addr);

      bool works = true;
      for (size_t k = 1; k < deref_maps.size(); ++k) {
        auto& test_map = deref_maps[k];

        // skip any test cases that look inconclusive
        if (!test_map.count(j_di) || !test_map.count(i_di))
          continue;

        // if the test case looks legit, see if it matches the first one
        auto j_addr_test = test_map.at(j_di);
        auto i_addr_test = test_map.at(i_di);
        auto diff_test = (j_addr_test - i_addr_test);
        if (diff_test != diff) {
          works = false;
          break;
        }
      }

      if (!works) {
        DEBUG_ARM(cout << "-> No fixed relationship between accesses " << i << " , " << j << endl;)
        continue;
      }

      DEBUG_ARM(cout << "-> CONJECTURE: accesses " << i << " , " << j << " are offset by " << diff << endl;)

      /** Try to prove that the address of deref i is always a fixed offset of deref j */
      //cout << "Initial constraints: " << initial_constraints[0] << endl;
      auto check = !(i_access.address + SymBitVector::constant(64, diff) == j_access.address);
      initial_constraints.push_back(check);

      //cout << "CHECKING " << check << endl;
      bool correct = !solver_.is_sat(initial_constraints) && !solver_.has_error();
      initial_constraints.pop_back();
      if (correct) {
        access_offsets_[i][j] = diff;
        access_offsets_[j][i] = -diff;
        unionfind.join(i,j);
        DEBUG_ARM(cout << "    * TRUE" << endl;)
        /** no need to check with other components since we know they're separate. */
        break;
      } else {
        DEBUG_ARM(cout << "    * FALSE" << endl;)
      }
    }
  }
}

void ArmMemory::generate_constraints_offsets_nodata(std::vector<SymBool>& initial_constraints) {
  // 1. For every pair of memory accesses, perform up to three queries to determine if they belong in the same cell
  for (size_t i = 1; i < all_accesses_.size(); ++i) {
    for (size_t j = 0; j < i; ++j) {
      auto a1 = all_accesses_[i];
      auto a2 = all_accesses_[j];

      // check if initial_constraints => a1 == a2
      auto check_same = !(a1.address == a2.address);

      initial_constraints.push_back(check_same);
      if (stop_now_ && *stop_now_) return;
      bool is_same = !solver_.is_sat(initial_constraints);
      initial_constraints.pop_back();
      if (is_same) {
        access_offsets_[i][j] = 0;
        access_offsets_[j][i] = 0;
        DEBUG_ARM(cout << "-> accesses " << i << " , " << j << " are the same" << endl;)
        continue;
      }

      // chack if initial_constraints => a1+size == a2
      assert(a1.size % 8 == 0);
      auto check_a1first = !(a1.address + SymBitVector::constant(64, a1.size/8) == a2.address);
      initial_constraints.push_back(check_a1first);
      if (stop_now_ && *stop_now_) return;
      bool is_a1first = !solver_.is_sat(initial_constraints);
      initial_constraints.pop_back();
      if (is_a1first) {
        access_offsets_[i][j] = a1.size/8;
        access_offsets_[j][i] = (int64_t)-a1.size/8;
        DEBUG_ARM(cout << "-> accesses " << i << " , " << j << " are offset by " << a1.size/8 << endl;)
        continue;
      }

      // check if initial_constraints => a2+size == a1
      assert(a2.size % 8 == 0);
      auto check_a2first = !(a2.address + SymBitVector::constant(64, a2.size/8) == a1.address);
      initial_constraints.push_back(check_a2first);
      if (stop_now_ && *stop_now_) return;
      bool is_a2first = !solver_.is_sat(initial_constraints);
      initial_constraints.pop_back();
      if (is_a2first) {
        access_offsets_[i][j] = (int64_t)-a2.size/8;
        access_offsets_[j][i] = a2.size/8;
        DEBUG_ARM(cout << "-> accesses " << i << " , " << j << " are offset by " << (int64_t)-a2.size/8 << endl;)
        continue;
      }
      DEBUG_ARM(cout << "-> accesses " << i << " , " << j << " not related." << endl;)
    }
  }

  DEBUG_ARM(
  for (size_t i = 0; i < access_offsets_.size(); ++i) {
  for (size_t j = 0; j < access_offsets_.size(); ++j) {
      if (access_offsets_[i].count(j))
        cout << setw(2) << access_offsets_[i][j] << "  ";
      else
        cout << " X  ";
    }
    cout << endl;
  })
}

void ArmMemory::generate_constraints_enumerate_cells() {

  // 2. Enumerate cells

  if (stop_now_ && *stop_now_) return;
  // (a) initialize all accesses to be associated to no cell
  for (size_t i = 0; i < all_accesses_.size(); ++i) {
    all_accesses_[i].cell = (size_t)(-1);
    all_accesses_[i].cell_offset = 0;
    all_accesses_[i].index = i;
  }

  if (stop_now_ && *stop_now_) return;
  // (b) work out the associations for each cell
  for (size_t i = 0; i < all_accesses_.size(); ++i) {
    if (all_accesses_[i].cell != (size_t)(-1))
      continue;

    Cell initial_cell(all_accesses_[i].address);
    initial_cell.index = cells_.size();;
    cells_.push_back(initial_cell);
    all_accesses_[i].cell = cells_.size()-1;
    all_accesses_[i].cell_offset = 0;
    cells_[cells_.size()-1].tmp_min_offset = 0;
    cells_[cells_.size()-1].tmp_max_offset = all_accesses_[i].size/8;

    recurse_cell_assignment(i);
  }

  if (stop_now_ && *stop_now_) return;
  // (c) calculate the size of each cell and the offsets of each access
  for (auto& cell : cells_) {
    cell.size = cell.tmp_max_offset - cell.tmp_min_offset;
    cell.address = cell.address + SymBitVector::constant(64, cell.tmp_min_offset);

    for (auto& access : all_accesses_) {
      if (access.cell == cell.index)
        access.cell_offset = access.cell_offset - cell.tmp_min_offset;
    }

  }

  if (stop_now_ && *stop_now_) return;
  // (d) check for debugging
  DEBUG_ARM(
  for (auto& cell : cells_) {
  cout << "Cell " << cell.index << " has size " << cell.size << endl;
  cout << "   tmp_min_offset " << cell.tmp_min_offset << " max " << cell.tmp_max_offset << endl;
  cout << "   address " << cell.address << endl;
  for (auto& access : all_accesses_) {
      if (access.cell != cell.index)
        continue;
      cout << "  Access " << access.index << " has size " << access.size/8 << " and offset " << access.cell_offset << endl;
    }
  }

  for (size_t i = 0; i < all_accesses_.size(); ++i) {
  auto& access = all_accesses_[i];
    cout << "i=" << i << " access " << access.index << " size " << access.size/8 << " cell " << access.cell << " offset " << access.cell_offset << endl;
  })

}

bool ArmMemory::check_nonoverlapping(ArmMemory* am, const vector<SymBool>& initial_constraints) {


  /** Map from cell ID to a union find.  The union find builds equivalence classes
    of ranges of cell offsets that are consecutive. */
  std::map<size_t, UnionFind<uint64_t>> runs;

  for (const auto& access : all_accesses_) {
    auto& uf = runs[access.cell];
    for (size_t i = 0; i < access.size/8; ++i) {
      uint64_t offset = access.cell_offset + i;
      uf.add(offset);
      if (uf.contains(offset-1)) {
        uf.join(offset, offset-1);
      }
      if (uf.contains(offset+1)) {
        uf.join(offset, offset+1);
      }
    }
  }

  /** Now, for each cell and each component we add a range. */
  map<size_t, vector<pair<SymBitVector, SymBitVector>>> ranges;

  for (auto pair : runs) {
    auto cellid = pair.first;
    auto& uf = pair.second;
    auto components = uf.components();
    for (auto component : components) {
      uint64_t low = component;
      uint64_t high = uf.max_value(component);

      auto low_addr = cells_[cellid].address + SymBitVector::constant(64, low);
      auto high_addr = cells_[cellid].address + SymBitVector::constant(64, high);
      auto range = make_pair(low_addr, high_addr);
      ranges[cellid].push_back(range);
      DEBUG_ARM(cout << "[check_nonoverlapping] Cell " << cellid << " has range " << low << " -> " << high << " with addrs " << low_addr << " to " << high_addr << endl;)
    }
  }

  /** For each pair of cells, for each pair of ranges, we check that they're disjoint. */
  auto constraints = initial_constraints;
  for (size_t i = 0; i < cells_.size(); ++i) {
    for (size_t j = 0; j < cells_.size(); ++j) {
      if (i <= j)
        continue;

      auto ranges_i = ranges[i];
      auto ranges_j = ranges[j];

      for (auto range_i : ranges_i) {
        for (auto range_j : ranges_j) {


          DEBUG_ARM(cout << "[check_nonoverlapping] Checking cell " << i << " , " << j
                    << " with ranges " << range_i.first << " to " << range_i.second << " AND "
                    << range_j.first << " to " << range_j.second << endl;)

          // check that range_i.first > range_j.second OR range_i.second < range_j.first
          auto check = (range_i.first > range_j.second) | (range_i.second < range_j.first);
          constraints.push_back(!check);
          bool passes = !solver_.is_sat(constraints) && !solver_.has_error();
          DEBUG_ARM(
            if (solver_.has_error())
            cout << "SOLVER ERROR: " << solver_.get_error() << endl;)
            DEBUG_ARM(cout << "   PASSES: " << passes << endl;)
            constraints.pop_back();
          if (!passes)
            return false;
        }
      }
    }
  }


  return true;
}

bool ArmMemory::generate_constraints_given_no_cell_overlap(ArmMemory* am) {

  if (stop_now_ && *stop_now_) return true;

  // these are maps: cell x offset -> bitvector
  std::map<uint64_t, std::map<uint64_t, SymBitVector>> my_memory_locations;
  std::map<uint64_t, std::map<uint64_t, SymBitVector>> other_memory_locations;

  // first, figure out what the cells are
  for (auto& access : all_accesses_) {
    auto& memloc = access.is_other ? other_memory_locations : my_memory_locations;
    for (size_t i = 0; i < access.size/8; ++i) {
      if (memloc[access.cell].count(access.cell_offset + i) == 0) {
        memloc[access.cell][access.cell_offset + i] = SymBitVector::tmp_var(8);
      }
    }
  }

  // second, construct a heap for each initial value.
  // TODO: we'll be much better off constructing one heap per cell
  //      will also help with modeling stack, etc.
  for (size_t k = 0; k < 2; ++k) {
    auto& memloc = k ? other_memory_locations : my_memory_locations;
    auto& heap = k ? am->heap_ : heap_;

    for (auto pair : memloc) {
      auto cell = cells_[pair.first];
      for (auto pair2 : pair.second)
        heap = heap.update(SymBitVector::constant(64, pair2.first) + cell.address, pair2.second);
    }
  }
  constraints_.push_back(start_variable_ == heap_);
  constraints_.push_back(am->start_variable_ == am->heap_);

  // now update the cells
  for (auto & access : all_accesses_) {
    auto& memloc = access.is_other ? other_memory_locations : my_memory_locations;
    if (access.write)
      for (size_t i = 0; i < access.size/8; ++i)
        memloc[access.cell][access.cell_offset + i] = access.value[8*i+7][8*i];
    else
      for (size_t i = 0; i < access.size/8; ++i)
        constraints_.push_back(memloc[access.cell][access.cell_offset + i] == access.value[8*i+7][8*i]);
  }

  /** Create heap */
  for (size_t k = 0; k < 2; ++k) {
    auto& memloc = k ? other_memory_locations : my_memory_locations;
    auto& heap = k ? am->heap_ : heap_;

    for (auto pair : memloc) {
      auto cell = cells_[pair.first];
      for (auto pair2 : pair.second)
        heap = heap.update(SymBitVector::constant(64, pair2.first) + cell.address, pair2.second);
    }
  }

  /** Get a final heap variable for reading out a model */
  constraints_.push_back(final_heap_ == heap_);
  constraints_.push_back(am->final_heap_ == am->heap_);

  return true;

}

void ArmMemory::generate_constraints_given_cells(ArmMemory* am, const vector<SymBool>& initial_constraints) {

  if (stop_now_ && *stop_now_) return;
  // 3. Simulate execution
  //      ... each "cell" is like a cache.
  //      ... You don't write it unless you need to read from another cell.
  //      ... You don't read it unless another cell performed a write.

  if (cells_.size() == 1 || unsound_ || check_nonoverlapping(am, initial_constraints)) {
    generate_constraints_given_no_cell_overlap(am);
    return;
  }

  // to setup, let's "cache" the result of each cell.
  for (auto& cell : cells_) {
    cell.cache = SymBitVector();
    for (size_t i = 0; i < cell.size; ++i)
      cell.cache = cell.cache || heap_[cell.address + SymBitVector::constant(64, cell.size - i - 1)];

    cell.other_cache = SymBitVector();
    for (size_t i = 0; i < cell.size; ++i)
      cell.other_cache = cell.other_cache || am->heap_[cell.address + SymBitVector::constant(64, cell.size - i - 1)];
  }

  auto debug_state = [&]() {
    /*DEBUG_ARM(
      cout << "HEAP 1: " << heap_ << endl;
    for (auto cell : cells_) {
    cout << "  cell " << cell.index << ": dirty " << cell.dirty << " : " << cell.cache << endl;
    }
    cout << "HEAP 2: " << am->heap_ << endl;
    for (auto cell : cells_) {
    cout << "  cell " << cell.index << ": dirty " << cell.other_dirty << " : " << cell.other_cache << endl;
    }) */
  };

  auto flush_dirty = [&](size_t skip_index = (size_t)(-1)) -> bool{
    /** write all dirty cells back into the heap */
    bool update_required = false;
    for (auto& cell : cells_) {
      if (stop_now_ && *stop_now_)
        break;

      if (cell.index == skip_index)
        continue;

      if (cell.dirty) {
        update_required = true;
        for (size_t i = 0; i < cell.size; ++i)
          heap_ = heap_.update(cell.address + SymBitVector::constant(64, i), cell.cache[8*i+7][8*i]);
        cell.dirty = false;
      }
      if (cell.other_dirty) {
        update_required = true;
        for (size_t i = 0; i < cell.size; ++i)
          am->heap_ = am->heap_.update(cell.address + SymBitVector::constant(64, i), cell.other_cache[8*i+7][8*i]);
        cell.other_dirty = false;
      }
    }
    return update_required;
  };

  // now symbolically execute each of the accesses
  for (auto& access : all_accesses_) {
    DEBUG_ARM(
      cout << "==================== PROCESSING ACCESS " << access.index
      << " IS OTHER: " << access.is_other
      << " IS WRITE: " << access.write
      << " CELL: " << access.cell
      << " OFFSET: " << access.cell_offset << endl;
      debug_state();
    )
    if (stop_now_ && *stop_now_) return;

    auto& cell = cells_[access.cell];
    bool& dirty = access.is_other ? cell.other_dirty : cell.dirty;
    auto& cache = access.is_other ? cell.other_cache : cell.cache;
    auto& heap = access.is_other ? am->heap_ : heap_;

    DEBUG_ARM(cout << "==================== WRITING DIRTY CELLS INTO HEAP" << endl;)
    bool needs_update = flush_dirty(cell.index);
    debug_state();

    DEBUG_ARM(cout << "==================== UPDATING OUR CELL IF NEEDED : " << needs_update << endl;)
    /** if a dirty cell got written into the heap, read out this cell */
    if (stop_now_ && *stop_now_) return;
    if (needs_update) {
      //DEBUG_ARM(cout << "Heap updated with dirty cells flushed: " << heap << endl;)
      cache = SymBitVector();
      for (size_t i = 0; i < cell.size; ++i)
        cache = cache || heap[cell.address + SymBitVector::constant(64, cell.size - i - 1)];
      //DEBUG_ARM(cout << "Updated cell " << cell.index << " cache: " << cache;)

      debug_state();
    }

    /* perform the read/write on the cached copy; set dirty bit if needed. */
    if (stop_now_ && *stop_now_) return;
    if (access.write) {

      SymBitVector prefix, suffix;
      if (access.cell_offset + access.size/8 < cell.size) {
        prefix = cache[cell.size*8-1][access.cell_offset*8 + access.size];
      }
      if (access.cell_offset > 0) {
        suffix = cache[access.cell_offset*8-1][0];
      }
      cache = prefix || access.value || suffix;
      dirty = true;
      DEBUG_ARM(cout << "==================== PERFORMED WRITE " << endl;)
      debug_state();
    } else {
      constraints_.push_back(access.value ==
                             cache[access.cell_offset*8+access.size-1][access.cell_offset*8]);
      DEBUG_ARM(
        cout << "==================== PERFORMED READ "  << endl;)
      //cout << access.value << " : " << cache[access.cell_offset*8+access.size-1][access.cell_offset*8] << endl;)
    }
  }

  if (stop_now_ && *stop_now_) return;
  flush_dirty();
  DEBUG_ARM(
    cout << "==================== FINAL: WRITE DIRTY CELLS BACK INTO HEAP" << endl;
    cout << "==================== ALL DONE" << endl;
    debug_state();)

  /** get a final heap variable for reading out a model */
  constraints_.push_back(final_heap_ == heap_);
  constraints_.push_back(am->final_heap_ == am->heap_);

  /*
  DEBUG_ARM(
    cout << "[arm] Adding constraints " << final_heap_ << " = " << heap_ << endl;
    cout << "[arm] Adding constraint " << am->final_heap_ << " = " << am->heap_ << endl;
  )
  */

}


void ArmMemory::recurse_cell_assignment(size_t access_index) {

  if (stop_now_ && *stop_now_)
    return;

  auto& access = all_accesses_[access_index];
  auto cell_index = access.cell;
  auto& cell = cells_[cell_index];

  for (size_t i = 0; i < all_accesses_.size(); ++i) {

    if (stop_now_ && *stop_now_)
      return;

    // check if this access belongs to a cell
    if (all_accesses_[i].cell != (size_t)(-1))
      continue;

    // check if access is related at all
    if (!access_offsets_[access_index].count(i))
      continue;

    // now we have another offset to put into this cell
    all_accesses_[i].cell = cell_index;
    all_accesses_[i].cell_offset = access.cell_offset + access_offsets_[access_index][i];

    // update the cell min/max offsets
    if (all_accesses_[i].cell_offset < cell.tmp_min_offset)
      cell.tmp_min_offset = all_accesses_[i].cell_offset;
    if (all_accesses_[i].cell_offset + (int64_t)all_accesses_[i].size/8 > cell.tmp_max_offset)
      cell.tmp_max_offset = all_accesses_[i].cell_offset + all_accesses_[i].size/8;

    // recurse
    recurse_cell_assignment(i);
  }

}

SymBool ArmMemory::equality_constraint(ArmMemory& other) {
  //if(exclusions.size() == 0)
  return get_variable() == other.get_variable();

  /*
  auto my_heap = get_variable();
  auto their_heap = other.get_variable();

  auto tmp = SymBitVector::tmp_var(64);
  SymBool tmp_is_excluded = SymBool::_false();
  for(auto e : exclusions) {
    tmp_is_excluded = tmp_is_excluded | (e == tmp);
  }

  return (tmp_is_excluded | (my_heap[tmp] == their_heap[tmp])).forall({tmp}, {});*/
  //return ((!tmp_is_excluded).implies(my_heap[tmp] == their_heap[tmp])).forall({tmp}, {});
  //return ((my_heap[tmp] != their_heap[tmp]).implies(tmp_is_excluded)).forall({tmp},{});

}

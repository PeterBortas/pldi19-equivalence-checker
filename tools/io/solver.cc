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

#include <array>
#include <string>
#include <utility>

#include "tools/io/generic.h"
#include "tools/io/solver.h"

using namespace std;
using namespace stoke;

namespace {

array<pair<string, Solver>, 3> pts {{
    {"cvc4", Solver::CVC4},
    {"z3",   Solver::Z3 },
    {"race", Solver::RACE }
  }
};

} // namespace

namespace stoke {

void SolverReader::operator()(std::istream& is, Solver& pt) {
  string s;
  is >> s;
  if (!generic_read(pts, s, pt)) {
    is.setstate(ios::failbit);
  }
}

void SolverWriter::operator()(std::ostream& os, const Solver pt) {
  string s;
  generic_write(pts, s, pt);
  os << s;
}

} // namespace stoke

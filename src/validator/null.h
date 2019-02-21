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

#ifndef STOKE_SRC_VALIDATOR_NULL_H
#define STOKE_SRC_VALIDATOR_NULL_H

/** Sound nullspace computation */
namespace BitvectorNullspace {

size_t nullspace(long* inputs, size_t rows, size_t cols, uint64_t*** output);

}

/** Nullspace wrapper */
namespace stoke {

class Nullspace {

public:

  static size_t bv_nullspace(uint64_t* inputs, size_t rows, size_t cols, uint64_t*** output) {
    return BitvectorNullspace::nullspace((long*)inputs, rows, cols, output);
  }

  static size_t z_nullspace(uint64_t* inputs, size_t rows, size_t cols, uint64_t*** output);
};

}



#endif

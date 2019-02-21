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

#include <string>
#include <vector>

#ifndef _STOKE_SRC_SYMSTATE_BOOL_H
#define _STOKE_SRC_SYMSTATE_BOOL_H

#include "src/symstate/ast.h"
#include "src/symstate/memory_manager.h"

namespace stoke {



class SymBitVector;
class SymBitVectorAbstract;
class SymBitVectorVar;
class SymBoolAbstract;
class SymArrayAbstract;


class SymBoolAnd;
class SymBoolArrayEq;
class SymBoolEq;
class SymBoolFalse;
class SymBoolForAll;
class SymBoolGe;
class SymBoolGt;
class SymBoolIff;
class SymBoolImplies;
class SymBoolLe;
class SymBoolLt;
class SymBoolNot;
class SymBoolOr;
class SymBoolSignGe;
class SymBoolSignGt;
class SymBoolSignLe;
class SymBoolSignLt;
class SymBoolTrue;
class SymBoolVar;
class SymBoolXor;

class SymBool {
  friend class SymBitVector;

public:

  enum Type {
    NONE,
    AND,
    ARRAY_EQ,
    EQ,
    FALSE,
    FOR_ALL,
    GE,
    GT,
    IFF,
    IMPLIES,
    LE,
    LT,
    NOT,
    OR,
    SIGN_GE,
    SIGN_GT,
    SIGN_LE,
    SIGN_LT,
    TRUE,
    VAR,
    XOR,
  };

  /** Get the type of this bool expression; helps for recursive algorithms on the tree. */
  virtual Type type() const;

  /** Builds a false value */
  static SymBool _false();
  /** Builds a true value */
  static SymBool _true();
  /** Creates a bool temporary variable */
  static SymBool tmp_var();
  /** Builds a boolean */
  static SymBool constant(bool b) {
    return ( b ? _true() : _false() );
  }
  /** Builds a boolean variable */
  static SymBool var(std::string name);

  /** Constructs the logical AND of two bools */
  SymBool operator&(const SymBool other) const;
  /** Constructs the logical 'if-and-only-if' for two bools */
  SymBool operator==(const SymBool other) const;
  /** Constructs the logical implication of two bools */
  SymBool implies(const SymBool other) const;
  /** Constructs the logical negation of this bool */
  SymBool operator!() const;
  /** Constructs the logical OR of two bools */
  SymBool operator|(const SymBool other) const;
  /** Constructs the logical XOR of two bools */
  SymBool operator^(const SymBool other) const;
  /** Returns the negation of the logical 'if-and-only-if' */
  SymBool operator!=(const SymBool other) const;
  /** Builds an if-then-else expression */
  SymBool ite(const SymBool t, const SymBool f) const;
  /** Builds an if-then-else expression for bitvectors */
  SymBitVector ite(const SymBitVector t, const SymBitVector f) const;
  /** Builds an expression quantified over specified variables. */
  SymBool forall(const std::vector<SymBitVector>& vars,
                 const std::vector<SymBitVector>& patterns) const;

  /** Tells if two symbolic bools are identical */
  bool equals(const SymBool other) const;

  /** The pointer to the underlying object */
  const SymBoolAbstract * ptr;

  /** Construct a SymBool pointing to nothing */
  SymBool() : ptr(NULL) {}
  /** Constructs a new SymBool from a pointer to the AST hierarchy */
  SymBool(const SymBoolAbstract * ptr_) : ptr(ptr_) {
    if (memory_manager_)
      memory_manager_->add(ptr_);
  }

  /** Set a memory manager */
  static void set_memory_manager(SymMemoryManager* mm) {
    memory_manager_ = mm;
  }

  /** Get the memory manager */
  static SymMemoryManager* get_memory_manager() {
    return memory_manager_;
  }

private:

  /** Memory Manager */
  static thread_local SymMemoryManager* memory_manager_;
  /** Counter for temporaries. */
  static uint64_t tmp_counter_;

};

class SymBoolAbstract : public SymAstAbstract {

public:
  virtual SymBool::Type type() const = 0;
  virtual bool equals(const SymBoolAbstract * const) const = 0;

  virtual ~SymBoolAbstract() = 0;
};

inline SymBoolAbstract::~SymBoolAbstract() {}

class SymBoolCompare : public SymBoolAbstract {

protected:
  SymBoolCompare(const SymBitVectorAbstract * const a, const SymBitVectorAbstract * const b) : a_(a), b_(b) {}

public:
  const SymBitVectorAbstract * const a_;
  const SymBitVectorAbstract * const b_;

  bool equals(const SymBoolAbstract * const other) const;
};

class SymBoolBinop : public SymBoolAbstract {

protected:
  SymBoolBinop(const SymBoolAbstract * const a, const SymBoolAbstract * const b) : a_(a), b_(b) {}

public:
  const SymBoolAbstract * const a_;
  const SymBoolAbstract * const b_;

  bool equals(const SymBoolAbstract * const other) const {
    if (type() != other->type()) return false;
    auto cast = static_cast<const SymBoolBinop * const>(other);
    return a_->equals(cast->a_) && b_->equals(cast->b_);
  }

};


class SymBoolAnd : public SymBoolBinop {
  friend class SymBool;
  friend class SymTransformVisitor;
  using SymBoolBinop::SymBoolBinop;

public:
  SymBool::Type type() const {
    return SymBool::Type::AND;
  }
};

class SymBoolArrayEq : public SymBoolAbstract {
  friend class SymArray;
  friend class SymTransformVisitor;

protected:
  SymBoolArrayEq(const SymArrayAbstract * const a, const SymArrayAbstract * const b) : a_(a), b_(b) {}

public:
  const SymArrayAbstract * const a_;
  const SymArrayAbstract * const b_;

  bool equals(const SymBoolAbstract * const other) const;

  SymBool::Type type() const {
    return SymBool::Type::ARRAY_EQ;
  }
};

class SymBoolEq : public SymBoolCompare {
  friend class SymBool;
  friend class SymTransformVisitor;
  friend class SymBitVector;
  using SymBoolCompare::SymBoolCompare;

public:
  SymBool::Type type() const {
    return SymBool::Type::EQ;
  }
};

class SymBoolFalse : public SymBoolAbstract {
  friend class SymBool;
  friend class SymTransformVisitor;

public:
  SymBool::Type type() const {
    return SymBool::Type::FALSE;
  }

  bool equals(const SymBoolAbstract * const other) const {
    return other->type() == SymBool::Type::FALSE;
  }
};

class SymBoolForAll : public SymBoolAbstract {
  friend class SymBool;
  friend class SymTransformVisitor;

protected:

  SymBoolForAll(const SymBoolAbstract * const a, const std::vector<SymBitVectorVar>& vars, const std::vector<SymBitVector>& patterns) : a_(a), vars_(vars), patterns_(patterns) {
  }

public:
  const SymBoolAbstract * const a_;
  const std::vector<SymBitVectorVar> vars_;
  const std::vector<SymBitVector> patterns_;

  SymBool::Type type() const {
    return SymBool::Type::FOR_ALL;
  }

  bool equals(const SymBoolAbstract * const other) const;
};

class SymBoolGe : public SymBoolCompare {
  friend class SymBool;
  friend class SymTransformVisitor;
  friend class SymBitVector;
  using SymBoolCompare::SymBoolCompare;

public:
  SymBool::Type type() const {
    return SymBool::Type::GE;
  }
};

class SymBoolGt : public SymBoolCompare {
  friend class SymBool;
  friend class SymTransformVisitor;
  friend class SymBitVector;
  using SymBoolCompare::SymBoolCompare;

public:
  SymBool::Type type() const {
    return SymBool::Type::GT;
  }
};

class SymBoolIff : public SymBoolBinop {
  friend class SymBool;
  friend class SymTransformVisitor;
  using SymBoolBinop::SymBoolBinop;

public:
  SymBool::Type type() const {
    return SymBool::Type::IFF;
  }
};

class SymBoolImplies : public SymBoolBinop {
  friend class SymBool;
  friend class SymTransformVisitor;
  using SymBoolBinop::SymBoolBinop;

public:
  SymBool::Type type() const {
    return SymBool::Type::IMPLIES;
  }
};

class SymBoolLe : public SymBoolCompare {
  friend class SymBool;
  friend class SymTransformVisitor;
  friend class SymBitVector;
  using SymBoolCompare::SymBoolCompare;

public:
  SymBool::Type type() const {
    return SymBool::Type::LE;
  }
};

class SymBoolLt : public SymBoolCompare {
  friend class SymBool;
  friend class SymTransformVisitor;
  friend class SymBitVector;
  using SymBoolCompare::SymBoolCompare;

public:
  SymBool::Type type() const {
    return SymBool::Type::LT;
  }
};

class SymBoolNot : public SymBoolAbstract {
  friend class SymBool;
  friend class SymTransformVisitor;

private:
  SymBoolNot(const SymBoolAbstract * const b) : b_(b) {}

public:
  SymBool::Type type() const {
    return SymBool::Type::NOT;
  }

  bool equals(const SymBoolAbstract * const other) const {
    if (other->type() != SymBool::Type::NOT) return false;
    auto cast = static_cast<const SymBoolNot * const>(other);
    return b_->equals(cast->b_);
  }

  const SymBoolAbstract * const b_;
};

class SymBoolOr : public SymBoolBinop {
  friend class SymBool;
  friend class SymTransformVisitor;
  using SymBoolBinop::SymBoolBinop;

public:
  SymBool::Type type() const {
    return SymBool::Type::OR;
  }
};

class SymBoolSignGe : public SymBoolCompare {
  friend class SymBool;
  friend class SymTransformVisitor;
  friend class SymBitVector;
  using SymBoolCompare::SymBoolCompare;

public:
  SymBool::Type type() const {
    return SymBool::Type::SIGN_GE;
  }
};

class SymBoolSignGt : public SymBoolCompare {
  friend class SymBool;
  friend class SymTransformVisitor;
  friend class SymBitVector;
  using SymBoolCompare::SymBoolCompare;

public:
  SymBool::Type type() const {
    return SymBool::Type::SIGN_GT;
  }
};

class SymBoolSignLe : public SymBoolCompare {
  friend class SymBool;
  friend class SymTransformVisitor;
  friend class SymBitVector;
  using SymBoolCompare::SymBoolCompare;

public:
  SymBool::Type type() const {
    return SymBool::Type::SIGN_LE;
  }
};

class SymBoolSignLt : public SymBoolCompare {
  friend class SymBool;
  friend class SymTransformVisitor;
  friend class SymBitVector;
  using SymBoolCompare::SymBoolCompare;

public:
  SymBool::Type type() const {
    return SymBool::Type::SIGN_LT;
  }
};

class SymBoolTrue : public SymBoolAbstract {
  friend class SymBool;
  friend class SymTransformVisitor;

public:
  SymBool::Type type() const {
    return SymBool::Type::TRUE;
  }

  bool equals(const SymBoolAbstract * const other) const {
    return other->type() == SymBool::Type::TRUE;
  }
};

class SymBoolVar : public SymBoolAbstract {
  friend class SymBool;
  friend class SymTransformVisitor;

private:
  SymBoolVar(const std::string name) : name_(name) {}

public:
  SymBool::Type type() const {
    return SymBool::Type::VAR;
  }

  std::string get_name() const {
    return name_;
  }

  bool equals(const SymBoolAbstract * const other) const {
    if (other->type() != SymBool::Type::VAR) return false;
    auto cast = static_cast<const SymBoolVar * const>(other);
    return name_ == cast->name_;
  }

  const std::string name_;
};


class SymBoolXor : public SymBoolBinop {
  friend class SymBool;
  friend class SymTransformVisitor;
  using SymBoolBinop::SymBoolBinop;

public:
  SymBool::Type type() const {
    return SymBool::Type::XOR;
  }
};



} //namespace stoke

namespace std {

std::ostream& operator<< (std::ostream& out, const stoke::SymBool& bv);

}
#endif

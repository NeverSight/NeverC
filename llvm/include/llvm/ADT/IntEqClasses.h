//===-- llvm/ADT/IntEqClasses.h - Equiv. Classes of Integers ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Equivalence classes for small integers. This is a mapping of the integers
/// 0 .. N-1 into M equivalence classes numbered 0 .. M-1.
///
/// Initially each integer has its own equivalence class. Classes are joined by
/// passing a representative member of each class to join().
///
/// Once the classes are built, compress() will number them 0 .. M-1 and prevent
/// further changes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_INTEQCLASSES_H
#define LLVM_ADT_INTEQCLASSES_H

#include "llvm/ADT/SmallVector.h"
#include <assert.h>

extern "C" {
unsigned csupport_uf_join(unsigned *ec, unsigned a, unsigned b);
unsigned csupport_uf_find_leader(const unsigned *ec, unsigned a);
unsigned csupport_uf_compress(unsigned *ec, unsigned size);
}

namespace llvm {

class IntEqClasses {
  SmallVector<unsigned, 8> EC;
  unsigned NumClasses = 0;

public:
  IntEqClasses(unsigned N = 0) { grow(N); }

  inline void grow(unsigned N) {
    assert(NumClasses == 0 && "grow() called after compress().");
    EC.reserve(N);
    while (EC.size() < N)
      EC.push_back(EC.size());
  }

  void clear() {
    EC.clear();
    NumClasses = 0;
  }

  inline unsigned join(unsigned a, unsigned b) {
    assert(NumClasses == 0 && "join() called after compress().");
    return csupport_uf_join(EC.data(), a, b);
  }

  inline unsigned findLeader(unsigned a) const {
    assert(NumClasses == 0 && "findLeader() called after compress().");
    return csupport_uf_find_leader(EC.data(), a);
  }

  inline void compress() {
    if (NumClasses)
      return;
    NumClasses = csupport_uf_compress(EC.data(), EC.size());
  }

  unsigned getNumClasses() const { return NumClasses; }

  unsigned operator[](unsigned a) const {
    assert(NumClasses && "operator[] called before compress()");
    return EC[a];
  }

  inline void uncompress() {
    if (!NumClasses)
      return;
    SmallVector<unsigned, 8> Leader;
    for (unsigned i = 0, e = EC.size(); i != e; ++i)
      if (EC[i] < Leader.size())
        EC[i] = Leader[EC[i]];
      else
        Leader.push_back(EC[i] = i);
    NumClasses = 0;
  }
};

} // namespace llvm

#endif

//===- Comdat.cpp - Implement Metadata classes ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Comdat class. The LLVM-C bindings have been
// removed in NeverC; only the C++ interface used by the compiler itself
// remains.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Comdat.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMapEntry.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/GlobalObject.h"

using namespace llvm;

Comdat::Comdat(Comdat &&C) : Name(C.Name), SK(C.SK) {}

Comdat::Comdat() = default;

StringRef Comdat::getName() const { return Name->first(); }

void Comdat::addUser(GlobalObject *GO) { Users.insert(GO); }

void Comdat::removeUser(GlobalObject *GO) { Users.erase(GO); }

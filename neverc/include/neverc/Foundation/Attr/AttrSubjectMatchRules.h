#ifndef NEVERC_BASIC_ATTRSUBJECTMATCHRULES_H
#define NEVERC_BASIC_ATTRSUBJECTMATCHRULES_H

#include "llvm/ADT/DenseMap.h"

namespace neverc {

class SourceRange;

namespace attr {

enum SubjectMatchRule {
#define ATTR_MATCH_RULE(X, Spelling, IsAbstract) X,
#include "neverc/Foundation/AttrSubMatchRulesList.td.h"
  SubjectMatchRule_Last = -1
#define ATTR_MATCH_RULE(X, Spelling, IsAbstract) +1
#include "neverc/Foundation/AttrSubMatchRulesList.td.h"
};

const char *getSubjectMatchRuleSpelling(SubjectMatchRule Rule);

using ParsedSubjectMatchRuleSet = llvm::DenseMap<int, SourceRange>;

} // end namespace attr
} // end namespace neverc

#endif

#ifndef NEVERC_FOUNDATION_ATTRIBUTES_H
#define NEVERC_FOUNDATION_ATTRIBUTES_H

#include "neverc/Foundation/Attr/AttributeCommonInfo.h"

namespace neverc {

class IdentifierInfo;
class LangOptions;
class TargetInfo;

int hasAttribute(AttributeCommonInfo::Syntax Syntax,
                 const IdentifierInfo *Scope, const IdentifierInfo *Attr,
                 const TargetInfo &Target, const LangOptions &LangOpts);

} // end namespace neverc

#endif // NEVERC_FOUNDATION_ATTRIBUTES_H

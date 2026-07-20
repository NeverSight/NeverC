#include "neverc/Plugin/Host/PluginTargetInfo.h"

using namespace llvm;

namespace neverc::plugin {

const VerifiedTargetConstraint *
PluginTargetInfo::findPluginConstraint(StringRef Spelling) const {
  for (const VerifiedTargetConstraint &Constraint : Record.Constraints)
    if (Spelling.starts_with(Constraint.Spelling))
      return &Constraint;
  return nullptr;
}

bool PluginTargetInfo::validateAsmConstraint(
    const char *&Name, ConstraintInfo &Info) const {
  const VerifiedTargetConstraint *Constraint =
      findPluginConstraint(Name);
  if (!Constraint)
    return false;
  if ((Constraint->Flags &
       NEVERC_TARGET_CONSTRAINT_ALLOWS_MEMORY) != 0)
    Info.setAllowsMemory();
  if ((Constraint->Flags &
       NEVERC_TARGET_CONSTRAINT_ALLOWS_REGISTER) != 0)
    Info.setAllowsRegister();
  if ((Constraint->Flags & NEVERC_TARGET_CONSTRAINT_IMMEDIATE) != 0) {
    if (!Constraint->ImmediateValues.empty())
      Info.setRequiresImmediate(Constraint->ImmediateValues);
    else
      Info.setRequiresImmediate(Constraint->ImmediateMinimum,
                                Constraint->ImmediateMaximum);
  }
  Name += Constraint->Spelling.size() - 1;
  return true;
}

std::string PluginTargetInfo::convertConstraint(
    const char *&ConstraintValue) const {
  const VerifiedTargetConstraint *Constraint =
      findPluginConstraint(ConstraintValue);
  if (!Constraint)
    return TargetInfo::convertConstraint(ConstraintValue);
  ConstraintValue += Constraint->Spelling.size() - 1;
  return Constraint->ConvertedConstraint.empty()
             ? Constraint->Spelling
             : Constraint->ConvertedConstraint;
}

std::string_view PluginTargetInfo::getClobbers() const {
  return Record.Clobbers;
}

ArrayRef<const char *> PluginTargetInfo::getGCCRegNames() const {
  return RegisterNames;
}

ArrayRef<TargetInfo::GCCRegAlias>
PluginTargetInfo::getGCCRegAliases() const {
  return RegisterAliases;
}

ArrayRef<TargetInfo::AddlRegName>
PluginTargetInfo::getGCCAddlRegNames() const {
  return RegisterAdditionalNames;
}

} // namespace neverc::plugin

#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Foundation/Target/TargetInfo.h"

namespace neverc::plugin {

const BuiltinTargetRoute *
selectBuiltinNeverCTargetRoute(const neverc::TargetInfo &Target) {
  return findBuiltinTargetRoute(Target.getTriple().str());
}

} // namespace neverc::plugin

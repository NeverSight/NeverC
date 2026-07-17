#include "FrontendPluginInterfaces.h"
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginPrep.h"
#include "neverc/Plugin/PluginSema.h"
#include "neverc/Plugin/PluginSource.h"

namespace neverc::plugin {

NevercInterfaceID sourceLocationPluginInterfaceID() {
  return {NEVERC_INTERFACE_SOURCE_LOCATION_HIGH,
          NEVERC_INTERFACE_SOURCE_LOCATION_LOW};
}

NevercInterfaceID prepPluginInterfaceID() {
  return {NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW};
}

NevercInterfaceID astPluginInterfaceID() {
  return {NEVERC_INTERFACE_AST_HIGH, NEVERC_INTERFACE_AST_LOW};
}

NevercInterfaceID parserPluginInterfaceID() {
  return {NEVERC_INTERFACE_PARSER_HIGH, NEVERC_INTERFACE_PARSER_LOW};
}

NevercInterfaceID semaPluginInterfaceID() {
  return {NEVERC_INTERFACE_SEMA_HIGH, NEVERC_INTERFACE_SEMA_LOW};
}

} // namespace neverc::plugin

#ifndef NEVERC_PLUGIN_NEVERCPLUGINAPI_H
#define NEVERC_PLUGIN_NEVERCPLUGINAPI_H

/*
 * Convenience aggregate for NeverC's first public plugin ABI.
 *
 * Plugins may include the narrower domain headers directly to minimize their
 * compile surface. This header intentionally contains no compatibility aliases
 * for the removed pre-release NevercHostAPI/nevercGetPluginInfo prototype.
 */
#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginSource.h"
#include "neverc/Plugin/PluginPrep.h"
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
#include "neverc/Plugin/PluginIR.h"
#include "neverc/Plugin/PluginMIR.h"
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"
#include "neverc/Plugin/PluginDynCode.h"
#include "neverc/Plugin/PluginPhaseSchema.h"

#endif /* NEVERC_PLUGIN_NEVERCPLUGINAPI_H */

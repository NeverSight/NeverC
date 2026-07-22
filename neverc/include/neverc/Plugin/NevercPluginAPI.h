#ifndef NEVERC_PLUGIN_NEVERCPLUGINAPI_H
#define NEVERC_PLUGIN_NEVERCPLUGINAPI_H

/*
 * Convenience aggregate for NeverC's first public plugin ABI.
 *
 * Plugins may include the narrower domain headers directly to minimize their
 * compile surface. This header intentionally contains no compatibility aliases
 * for the removed pre-release NevercHostAPI/nevercGetPluginInfo prototype.
 */
/*
 * Canonical module order. This list is validated against
 * utils/plugin-api/plugin-api-modules.json (the single source of truth) by
 * utils/plugin-api/check-single-header.py, and mirrors the order the
 * distributed single header inlines its modules.
 */
#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include "neverc/Plugin/PluginSource.h"
#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginPrep.h"
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
#include "neverc/Plugin/PluginIR.h"
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMIR.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"
#include "neverc/Plugin/PluginDynCode.h"

#endif /* NEVERC_PLUGIN_NEVERCPLUGINAPI_H */

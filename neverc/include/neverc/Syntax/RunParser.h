#ifndef NEVERC_SYNTAX_RUNPARSER_H
#define NEVERC_SYNTAX_RUNPARSER_H

namespace neverc {
class ParserPluginHooks;
class Sema;

void RunParser(Sema &S, ParserPluginHooks *PluginHooks = nullptr,
               bool PrintStats = false, bool InputAlreadyInitialized = false);

} // end namespace neverc

#endif

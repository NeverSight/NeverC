#ifndef NEVERC_SYNTAX_RUNPARSER_H
#define NEVERC_SYNTAX_RUNPARSER_H

namespace neverc {
class Sema;

void RunParser(Sema &S, bool PrintStats = false);

} // end namespace neverc

#endif

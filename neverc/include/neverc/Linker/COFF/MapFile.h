#ifndef LINKER_COFF_MAPFILE_H
#define LINKER_COFF_MAPFILE_H

namespace linker::coff {
class COFFLinkerContext;
void writeMapFile(COFFLinkerContext &ctx);
} // namespace linker::coff

#endif

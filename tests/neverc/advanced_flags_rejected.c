// RUN: ! %neverc -fcoroutines -c %s -o %t.o 2> %t.coroutines.err
// RUN: grep -F "unknown argument: '-fcoroutines'" %t.coroutines.err
// RUN: ! %neverc -fno-coroutines -c %s -o %t.o 2> %t.ncoroutines.err
// RUN: grep -F "unknown argument: '-fno-coroutines'" %t.ncoroutines.err
// RUN: ! %neverc -fcxx-exceptions -c %s -o %t.o 2> %t.cxxeh.err
// RUN: grep -F "unknown argument: '-fcxx-exceptions'" %t.cxxeh.err
// RUN: ! %neverc -fno-cxx-exceptions -c %s -o %t.o 2> %t.ncxxeh.err
// RUN: grep -F "unknown argument: '-fno-cxx-exceptions'" %t.ncxxeh.err
// RUN: ! %neverc -fdelayed-template-parsing -c %s -o %t.o 2> %t.dtp.err
// RUN: grep -F "unknown argument: '-fdelayed-template-parsing'" %t.dtp.err
// RUN: ! %neverc -fno-delayed-template-parsing -c %s -o %t.o 2> %t.ndtp.err
// RUN: grep -F "unknown argument: '-fno-delayed-template-parsing'" %t.ndtp.err
// RUN: ! %neverc -fmodule-header -c %s -o %t.o 2> %t.modhdr.err
// RUN: grep -F "unknown argument: '-fmodule-header'" %t.modhdr.err
// RUN: ! %neverc -fmodule-header=user -c %s -o %t.o 2> %t.modhdrv.err
// RUN: grep -F "unknown argument: '-fmodule-header=user'" %t.modhdrv.err
// RUN: ! %neverc -fclang-abi-compat=latest -c %s -o %t.o 2> %t.clangabic.err
// RUN: grep -F "unknown argument: '-fclang-abi-compat=latest'" %t.clangabic.err
// RUN: ! %neverc -fapple-kext -c %s -o %t.o 2> %t.kext.err
// RUN: grep -F "unknown argument: '-fapple-kext'" %t.kext.err
// RUN: ! %neverc -ftemplate-depth=1024 -c %s -o %t.o 2> %t.tdepth.err
// RUN: grep -F "unknown argument: '-ftemplate-depth=1024'" %t.tdepth.err
// RUN: ! %neverc -ftemplate-backtrace-limit=0 -c %s -o %t.o 2> %t.tblimit.err
// RUN: grep -F "unknown argument: '-ftemplate-backtrace-limit=0'" %t.tblimit.err
// RUN: ! %neverc -fdiagnostics-show-template-tree -c %s -o %t.o 2> %t.ttree.err
// RUN: grep -F "unknown argument: '-fdiagnostics-show-template-tree'" %t.ttree.err
// RUN: ! %neverc -fnew-alignment=64 -c %s -o %t.o 2> %t.newalign.err
// RUN: grep -F "unknown argument: '-fnew-alignment=64'" %t.newalign.err
// RUN: ! %neverc -faligned-new=64 -c %s -o %t.o 2> %t.alignednew.err
// RUN: grep -F "unknown argument: '-faligned-new=64'" %t.alignednew.err
// RUN: ! %neverc -frtti -c %s -o %t.o 2> %t.rtti.err
// RUN: grep -F "unknown argument: '-frtti'" %t.rtti.err
// RUN: ! %neverc -fno-rtti -c %s -o %t.o 2> %t.nrtti.err
// RUN: grep -F "unknown argument: '-fno-rtti'" %t.nrtti.err
// RUN: ! %neverc -frtti-data -c %s -o %t.o 2> %t.rttid.err
// RUN: grep -F "unknown argument: '-frtti-data'" %t.rttid.err
// RUN: ! %neverc -fno-rtti-data -c %s -o %t.o 2> %t.nrttid.err
// RUN: grep -F "unknown argument: '-fno-rtti-data'" %t.nrttid.err
// RUN: ! %neverc -fchar8_t -c %s -o %t.o 2> %t.char8.err
// RUN: grep -F "unknown argument: '-fchar8_t'" %t.char8.err
// RUN: ! %neverc -fno-char8_t -c %s -o %t.o 2> %t.nchar8.err
// RUN: grep -F "unknown argument: '-fno-char8_t'" %t.nchar8.err
// RUN: ! %neverc -fcxx-modules -c %s -o %t.o 2> %t.cxxmods.err
// RUN: grep -F "unknown argument: '-fcxx-modules'" %t.cxxmods.err
// RUN: ! %neverc -fno-cxx-modules -c %s -o %t.o 2> %t.ncxxmods.err
// RUN: grep -F "unknown argument: '-fno-cxx-modules'" %t.ncxxmods.err
// RUN: ! %neverc -fexperimental-library -c %s -o %t.o 2> %t.explib.err
// RUN: grep -F "unknown argument: '-fexperimental-library'" %t.explib.err
// RUN: ! %neverc -fno-experimental-library -c %s -o %t.o 2> %t.nexplib.err
// RUN: grep -F "unknown argument: '-fno-experimental-library'" %t.nexplib.err
// RUN: ! %neverc -fcoro-aligned-allocation -c %s -o %t.o 2> %t.coroalign.err
// RUN: grep -F "unknown argument: '-fcoro-aligned-allocation'" %t.coroalign.err
// RUN: ! %neverc -fno-coro-aligned-allocation -c %s -o %t.o 2> %t.ncoroalign.err
// RUN: grep -F "unknown argument: '-fno-coro-aligned-allocation'" %t.ncoroalign.err
// RUN: ! %neverc -fc++-static-destructors -c %s -o %t.o 2> %t.cxxdtor.err
// RUN: grep -F "unknown argument: '-fc++-static-destructors'" %t.cxxdtor.err
// RUN: ! %neverc -fno-c++-static-destructors -c %s -o %t.o 2> %t.ncxxdtor.err
// RUN: grep -F "unknown argument: '-fno-c++-static-destructors'" %t.ncxxdtor.err
// RUN: ! %neverc -fthreadsafe-statics -c %s -o %t.o 2> %t.tss.err
// RUN: grep -F "unknown argument: '-fthreadsafe-statics'" %t.tss.err
// RUN: ! %neverc -fno-threadsafe-statics -c %s -o %t.o 2> %t.ntss.err
// RUN: grep -F "unknown argument: '-fno-threadsafe-statics'" %t.ntss.err
// RUN: ! %neverc -fsized-deallocation -c %s -o %t.o 2> %t.sizeddel.err
// RUN: grep -F "unknown argument: '-fsized-deallocation'" %t.sizeddel.err
// RUN: ! %neverc -fno-sized-deallocation -c %s -o %t.o 2> %t.nsizeddel.err
// RUN: grep -F "unknown argument: '-fno-sized-deallocation'" %t.nsizeddel.err
// RUN: ! %neverc -faligned-allocation -c %s -o %t.o 2> %t.alignedalloc.err
// RUN: grep -F "unknown argument: '-faligned-allocation'" %t.alignedalloc.err
// RUN: ! %neverc -fno-aligned-allocation -c %s -o %t.o 2> %t.nalignedalloc.err
// RUN: grep -F "unknown argument: '-fno-aligned-allocation'" %t.nalignedalloc.err
// RUN: ! %neverc -fcomplete-member-pointers -c %s -o %t.o 2> %t.cmptr.err
// RUN: grep -F "unknown argument: '-fcomplete-member-pointers'" %t.cmptr.err
// RUN: ! %neverc -fno-complete-member-pointers -c %s -o %t.o 2> %t.ncmptr.err
// RUN: grep -F "unknown argument: '-fno-complete-member-pointers'" %t.ncmptr.err
// RUN: ! %neverc -frelaxed-template-template-args -c %s -o %t.o 2> %t.rtta.err
// RUN: grep -F "unknown argument: '-frelaxed-template-template-args'" %t.rtta.err
// RUN: ! %neverc -fno-relaxed-template-template-args -c %s -o %t.o 2> %t.nrtta.err
// RUN: grep -F "unknown argument: '-fno-relaxed-template-template-args'" %t.nrtta.err
// RUN: ! %neverc -fstrict-vtable-pointers -c %s -o %t.o 2> %t.svtp.err
// RUN: grep -F "unknown argument: '-fstrict-vtable-pointers'" %t.svtp.err
// RUN: ! %neverc -fno-strict-vtable-pointers -c %s -o %t.o 2> %t.nsvtp.err
// RUN: grep -F "unknown argument: '-fno-strict-vtable-pointers'" %t.nsvtp.err
// RUN: ! %neverc -fforce-emit-vtables -c %s -o %t.o 2> %t.fevt.err
// RUN: grep -F "unknown argument: '-fforce-emit-vtables'" %t.fevt.err
// RUN: ! %neverc -fno-force-emit-vtables -c %s -o %t.o 2> %t.nfevt.err
// RUN: grep -F "unknown argument: '-fno-force-emit-vtables'" %t.nfevt.err
// RUN: ! %neverc -fvirtual-function-elimination -c %s -o %t.o 2> %t.vfe.err
// RUN: grep -F "unknown argument: '-fvirtual-function-elimination'" %t.vfe.err
// RUN: ! %neverc -fno-virtual-function-elimination -c %s -o %t.o 2> %t.nvfe.err
// RUN: grep -F "unknown argument: '-fno-virtual-function-elimination'" %t.nvfe.err
// RUN: ! %neverc -fexperimental-relative-c++-abi-vtables -c %s -o %t.o 2> %t.rcabiv.err
// RUN: grep -F "unknown argument: '-fexperimental-relative-c++-abi-vtables'" %t.rcabiv.err
// RUN: ! %neverc -fno-experimental-relative-c++-abi-vtables -c %s -o %t.o 2> %t.nrcabiv.err
// RUN: grep -F "unknown argument: '-fno-experimental-relative-c++-abi-vtables'" %t.nrcabiv.err
// RUN: ! %neverc -fvisibility-inlines-hidden -c %s -o %t.o 2> %t.vih.err
// RUN: grep -F "unknown argument: '-fvisibility-inlines-hidden'" %t.vih.err
// RUN: ! %neverc -fno-visibility-inlines-hidden -c %s -o %t.o 2> %t.nvih.err
// RUN: grep -F "unknown argument: '-fno-visibility-inlines-hidden'" %t.nvih.err
// RUN: ! %neverc -fvisibility-inlines-hidden-static-local-var -c %s -o %t.o 2> %t.vihslv.err
// RUN: grep -F "unknown argument: '-fvisibility-inlines-hidden-static-local-var'" %t.vihslv.err
// RUN: ! %neverc -fno-visibility-inlines-hidden-static-local-var -c %s -o %t.o 2> %t.nvihslv.err
// RUN: grep -F "unknown argument: '-fno-visibility-inlines-hidden-static-local-var'" %t.nvihslv.err
// RUN: ! %neverc -fnew-infallible -c %s -o %t.o 2> %t.newinf.err
// RUN: grep -F "unknown argument: '-fnew-infallible'" %t.newinf.err
// RUN: ! %neverc -fno-new-infallible -c %s -o %t.o 2> %t.nnewinf.err
// RUN: grep -F "unknown argument: '-fno-new-infallible'" %t.nnewinf.err
// RUN: ! %neverc -fassume-sane-operator-new -c %s -o %t.o 2> %t.sanenew.err
// RUN: grep -F "unknown argument: '-fassume-sane-operator-new'" %t.sanenew.err
// RUN: ! %neverc -fno-assume-sane-operator-new -c %s -o %t.o 2> %t.nsanenew.err
// RUN: grep -F "unknown argument: '-fno-assume-sane-operator-new'" %t.nsanenew.err
// RUN: ! %neverc -fvisibility-global-new-delete-hidden -c %s -o %t.o 2> %t.vgndh.err
// RUN: grep -F "unknown argument: '-fvisibility-global-new-delete-hidden'" %t.vgndh.err

int neverc_driver_flag_guard;

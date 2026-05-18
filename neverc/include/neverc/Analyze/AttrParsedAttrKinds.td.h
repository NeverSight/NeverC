
static AttributeCommonInfo::Kind
getAttrKind(llvm::StringRef Name, AttributeCommonInfo::Syntax Syntax) {
  if (AttributeCommonInfo::AS_GNU == Syntax) {
    switch (Name.size()) {
    default:
      break;
    case 3: // 1 string to match.
      if (memcmp(Name.data() + 0, "hot", 3) != 0)
        break;
      return AttributeCommonInfo::AT_Hot; // "hot"
    case 4:                               // 6 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 1 string to match.
        if (memcmp(Name.data() + 1, "old", 3) != 0)
          break;
        return AttributeCommonInfo::AT_Cold; // "cold"
      case 'l':                              // 1 string to match.
        if (memcmp(Name.data() + 1, "eaf", 3) != 0)
          break;
        return AttributeCommonInfo::AT_Leaf; // "leaf"
      case 'm':                              // 1 string to match.
        if (memcmp(Name.data() + 1, "ode", 3) != 0)
          break;
        return AttributeCommonInfo::AT_Mode; // "mode"
      case 'p':                              // 1 string to match.
        if (memcmp(Name.data() + 1, "ure", 3) != 0)
          break;
        return AttributeCommonInfo::AT_Pure; // "pure"
      case 'u':                              // 1 string to match.
        if (memcmp(Name.data() + 1, "sed", 3) != 0)
          break;
        return AttributeCommonInfo::AT_Used; // "used"
      case 'w':                              // 1 string to match.
        if (memcmp(Name.data() + 1, "eak", 3) != 0)
          break;
        return AttributeCommonInfo::AT_Weak; // "weak"
      }
      break;
    case 5: // 7 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 1, "lias", 4) != 0)
          break;
        return AttributeCommonInfo::AT_Alias; // "alias"
      case 'c':                               // 2 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'd': // 1 string to match.
          if (memcmp(Name.data() + 2, "ecl", 3) != 0)
            break;
          return AttributeCommonInfo::AT_CDecl; // "cdecl"
        case 'o':                               // 1 string to match.
          if (memcmp(Name.data() + 2, "nst", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Const; // "const"
        }
        break;
      case 'e': // 1 string to match.
        if (memcmp(Name.data() + 1, "rror", 4) != 0)
          break;
        return AttributeCommonInfo::AT_Error; // "error"
      case 'g':                               // 1 string to match.
        if (memcmp(Name.data() + 1, "uard", 4) != 0)
          break;
        return AttributeCommonInfo::AT_CFGuard; // "guard"
      case 'i':                                 // 1 string to match.
        if (memcmp(Name.data() + 1, "func", 4) != 0)
          break;
        return AttributeCommonInfo::AT_IFunc; // "ifunc"
      case 'n':                               // 1 string to match.
        if (memcmp(Name.data() + 1, "aked", 4) != 0)
          break;
        return AttributeCommonInfo::AT_Naked; // "naked"
      }
      break;
    case 6: // 9 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 1, "ssume", 5) != 0)
          break;
        return AttributeCommonInfo::AT_Assumption; // "assume"
      case 'c':                                    // 1 string to match.
        if (memcmp(Name.data() + 1, "ommon", 5) != 0)
          break;
        return AttributeCommonInfo::AT_Common; // "common"
      case 'f':                                // 1 string to match.
        if (memcmp(Name.data() + 1, "ormat", 5) != 0)
          break;
        return AttributeCommonInfo::AT_Format; // "format"
      case 'm':                                // 2 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 2, "lloc", 4) != 0)
            break;
          return AttributeCommonInfo::AT_Restrict; // "malloc"
        case 's':                                  // 1 string to match.
          if (memcmp(Name.data() + 2, "_abi", 4) != 0)
            break;
          return AttributeCommonInfo::AT_MSABI; // "ms_abi"
        }
        break;
      case 'p': // 1 string to match.
        if (memcmp(Name.data() + 1, "acked", 5) != 0)
          break;
        return AttributeCommonInfo::AT_Packed; // "packed"
      case 'r':                                // 1 string to match.
        if (memcmp(Name.data() + 1, "etain", 5) != 0)
          break;
        return AttributeCommonInfo::AT_Retain; // "retain"
      case 't':                                // 1 string to match.
        if (memcmp(Name.data() + 1, "arget", 5) != 0)
          break;
        return AttributeCommonInfo::AT_Target; // "target"
      case 'u':                                // 1 string to match.
        if (memcmp(Name.data() + 1, "nused", 5) != 0)
          break;
        return AttributeCommonInfo::AT_Unused; // "unused"
      }
      break;
    case 7: // 17 strings to match.
      switch (Name[0]) {
      default:
        break;
      case '_': // 1 string to match.
        if (memcmp(Name.data() + 1, "_const", 6) != 0)
          break;
        return AttributeCommonInfo::AT_Const; // "__const"
      case 'a':                               // 1 string to match.
        if (memcmp(Name.data() + 1, "ligned", 6) != 0)
          break;
        return AttributeCommonInfo::AT_Aligned; // "aligned"
      case 'c':                                 // 1 string to match.
        if (memcmp(Name.data() + 1, "leanup", 6) != 0)
          break;
        return AttributeCommonInfo::AT_Cleanup; // "cleanup"
      case 'f':                                 // 1 string to match.
        if (memcmp(Name.data() + 1, "latten", 6) != 0)
          break;
        return AttributeCommonInfo::AT_Flatten; // "flatten"
      case 'm':                                 // 1 string to match.
        if (memcmp(Name.data() + 1, "insize", 6) != 0)
          break;
        return AttributeCommonInfo::AT_MinSize; // "minsize"
      case 'n':                                 // 5 strings to match.
        if (Name[1] != 'o')
          break;
        switch (Name[2]) {
        default:
          break;
        case 'd': // 2 strings to match.
          if (Name[3] != 'e')
            break;
          switch (Name[4]) {
          default:
            break;
          case 'b': // 1 string to match.
            if (memcmp(Name.data() + 5, "ug", 2) != 0)
              break;
            return AttributeCommonInfo::AT_NoDebug; // "nodebug"
          case 'r':                                 // 1 string to match.
            if (memcmp(Name.data() + 5, "ef", 2) != 0)
              break;
            return AttributeCommonInfo::AT_NoDeref; // "noderef"
          }
          break;
        case 'm': // 1 string to match.
          if (memcmp(Name.data() + 3, "erge", 4) != 0)
            break;
          return AttributeCommonInfo::AT_NoMerge; // "nomerge"
        case 'n':                                 // 1 string to match.
          if (memcmp(Name.data() + 3, "null", 4) != 0)
            break;
          return AttributeCommonInfo::AT_NonNull; // "nonnull"
        case 't':                                 // 1 string to match.
          if (memcmp(Name.data() + 3, "hrow", 4) != 0)
            break;
          return AttributeCommonInfo::AT_NoThrow; // "nothrow"
        }
        break;
      case 'o': // 1 string to match.
        if (memcmp(Name.data() + 1, "ptnone", 6) != 0)
          break;
        return AttributeCommonInfo::AT_OptimizeNone; // "optnone"
      case 'r':                                      // 2 strings to match.
        if (memcmp(Name.data() + 1, "eg", 2) != 0)
          break;
        switch (Name[3]) {
        default:
          break;
        case 'c': // 1 string to match.
          if (memcmp(Name.data() + 4, "all", 3) != 0)
            break;
          return AttributeCommonInfo::AT_RegCall; // "regcall"
        case 'p':                                 // 1 string to match.
          if (memcmp(Name.data() + 4, "arm", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Regparm; // "regparm"
        }
        break;
      case 's': // 2 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 2, "ction", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Section; // "section"
        case 't':                                 // 1 string to match.
          if (memcmp(Name.data() + 2, "dcall", 5) != 0)
            break;
          return AttributeCommonInfo::AT_StdCall; // "stdcall"
        }
        break;
      case 'w': // 2 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 2, "rning", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Error; // "warning"
        case 'e':                               // 1 string to match.
          if (memcmp(Name.data() + 2, "akref", 5) != 0)
            break;
          return AttributeCommonInfo::AT_WeakRef; // "weakref"
        }
        break;
      }
      break;
    case 8: // 13 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 1, "nnotate", 7) != 0)
          break;
        return AttributeCommonInfo::AT_Annotate; // "annotate"
      case 'c':                                  // 1 string to match.
        if (memcmp(Name.data() + 1, "allback", 7) != 0)
          break;
        return AttributeCommonInfo::AT_Callback; // "callback"
      case 'f':                                  // 1 string to match.
        if (memcmp(Name.data() + 1, "astcall", 7) != 0)
          break;
        return AttributeCommonInfo::AT_FastCall; // "fastcall"
      case 'm':                                  // 1 string to match.
        if (memcmp(Name.data() + 1, "usttail", 7) != 0)
          break;
        return AttributeCommonInfo::AT_MustTail; // "musttail"
      case 'n':                                  // 4 strings to match.
        if (Name[1] != 'o')
          break;
        switch (Name[2]) {
        default:
          break;
        case 'c': // 1 string to match.
          if (memcmp(Name.data() + 3, "ommon", 5) != 0)
            break;
          return AttributeCommonInfo::AT_NoCommon; // "nocommon"
        case 'e':                                  // 1 string to match.
          if (memcmp(Name.data() + 3, "scape", 5) != 0)
            break;
          return AttributeCommonInfo::AT_NoEscape; // "noescape"
        case 'i':                                  // 1 string to match.
          if (memcmp(Name.data() + 3, "nline", 5) != 0)
            break;
          return AttributeCommonInfo::AT_NoInline; // "noinline"
        case 'r':                                  // 1 string to match.
          if (memcmp(Name.data() + 3, "eturn", 5) != 0)
            break;
          return AttributeCommonInfo::AT_NoReturn; // "noreturn"
        }
        break;
      case 'o': // 1 string to match.
        if (memcmp(Name.data() + 1, "verride", 7) != 0)
          break;
        return AttributeCommonInfo::AT_Override; // "override"
      case 's':                                  // 3 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 2, "ntinel", 6) != 0)
            break;
          return AttributeCommonInfo::AT_Sentinel; // "sentinel"
        case 'u':                                  // 1 string to match.
          if (memcmp(Name.data() + 2, "ppress", 6) != 0)
            break;
          return AttributeCommonInfo::AT_Suppress; // "suppress"
        case 'y':                                  // 1 string to match.
          if (memcmp(Name.data() + 2, "sv_abi", 6) != 0)
            break;
          return AttributeCommonInfo::AT_SysVABI; // "sysv_abi"
        }
        break;
      case 'v': // 1 string to match.
        if (memcmp(Name.data() + 1, "olatile", 7) != 0)
          break;
        return AttributeCommonInfo::AT_Volatile; // "volatile"
      }
      break;
    case 9: // 10 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'd': // 2 strings to match.
        if (memcmp(Name.data() + 1, "ll", 2) != 0)
          break;
        switch (Name[3]) {
        default:
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 4, "xport", 5) != 0)
            break;
          return AttributeCommonInfo::AT_DLLExport; // "dllexport"
        case 'i':                                   // 1 string to match.
          if (memcmp(Name.data() + 4, "mport", 5) != 0)
            break;
          return AttributeCommonInfo::AT_DLLImport; // "dllimport"
        }
        break;
      case 'e': // 1 string to match.
        if (memcmp(Name.data() + 1, "nable_if", 8) != 0)
          break;
        return AttributeCommonInfo::AT_EnableIf; // "enable_if"
      case 'f':                                  // 1 string to match.
        if (memcmp(Name.data() + 1, "lag_enum", 8) != 0)
          break;
        return AttributeCommonInfo::AT_FlagEnum; // "flag_enum"
      case 'i':                                  // 1 string to match.
        if (memcmp(Name.data() + 1, "nterrupt", 8) != 0)
          break;
        return AttributeCommonInfo::AT_Interrupt; // "interrupt"
      case 'm':                                   // 2 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 2, "y_alias", 7) != 0)
            break;
          return AttributeCommonInfo::AT_MayAlias; // "may_alias"
        case 's':                                  // 1 string to match.
          if (memcmp(Name.data() + 2, "_struct", 7) != 0)
            break;
          return AttributeCommonInfo::AT_MSStruct; // "ms_struct"
        }
        break;
      case 'n': // 1 string to match.
        if (memcmp(Name.data() + 1, "ouwtable", 8) != 0)
          break;
        return AttributeCommonInfo::AT_NoUwtable; // "nouwtable"
      case 's':                                   // 1 string to match.
        if (memcmp(Name.data() + 1, "electany", 8) != 0)
          break;
        return AttributeCommonInfo::AT_SelectAny; // "selectany"
      case 't':                                   // 1 string to match.
        if (memcmp(Name.data() + 1, "ls_model", 8) != 0)
          break;
        return AttributeCommonInfo::AT_TLSModel; // "tls_model"
      }
      break;
    case 10: // 14 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 2 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'l': // 1 string to match.
          if (memcmp(Name.data() + 2, "loc_size", 8) != 0)
            break;
          return AttributeCommonInfo::AT_AllocSize; // "alloc_size"
        case 'r':                                   // 1 string to match.
          if (memcmp(Name.data() + 2, "tificial", 8) != 0)
            break;
          return AttributeCommonInfo::AT_Artificial; // "artificial"
        }
        break;
      case 'c': // 2 strings to match.
        if (Name[1] != 'o')
          break;
        switch (Name[2]) {
        default:
          break;
        case 'd': // 1 string to match.
          if (memcmp(Name.data() + 3, "e_align", 7) != 0)
            break;
          return AttributeCommonInfo::AT_CodeAlign; // "code_align"
        case 'n':                                   // 1 string to match.
          if (memcmp(Name.data() + 3, "vergent", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Convergent; // "convergent"
        }
        break;
      case 'd': // 2 strings to match.
        if (Name[1] != 'e')
          break;
        switch (Name[2]) {
        default:
          break;
        case 'p': // 1 string to match.
          if (memcmp(Name.data() + 3, "recated", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Deprecated; // "deprecated"
        case 's':                                    // 1 string to match.
          if (memcmp(Name.data() + 3, "tructor", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Destructor; // "destructor"
        }
        break;
      case 'f': // 1 string to match.
        if (memcmp(Name.data() + 1, "ormat_arg", 9) != 0)
          break;
        return AttributeCommonInfo::AT_FormatArg; // "format_arg"
      case 'g':                                   // 1 string to match.
        if (memcmp(Name.data() + 1, "nu_inline", 9) != 0)
          break;
        return AttributeCommonInfo::AT_GNUInline; // "gnu_inline"
      case 'n':                                   // 3 strings to match.
        if (Name[1] != 'o')
          break;
        switch (Name[2]) {
        default:
          break;
        case '_': // 2 strings to match.
          switch (Name[3]) {
          default:
            break;
          case 'b': // 1 string to match.
            if (memcmp(Name.data() + 4, "uiltin", 6) != 0)
              break;
            return AttributeCommonInfo::AT_NoBuiltin; // "no_builtin"
          case 'd':                                   // 1 string to match.
            if (memcmp(Name.data() + 4, "estroy", 6) != 0)
              break;
            return AttributeCommonInfo::AT_NoDestroy; // "no_destroy"
          }
          break;
        case 'c': // 1 string to match.
          if (memcmp(Name.data() + 3, "f_check", 7) != 0)
            break;
          return AttributeCommonInfo::AT_AnyX86NoCfCheck; // "nocf_check"
        }
        break;
      case 'u': // 1 string to match.
        if (memcmp(Name.data() + 1, "se_handle", 9) != 0)
          break;
        return AttributeCommonInfo::AT_UseHandle; // "use_handle"
      case 'v':                                   // 2 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 2, "ctorcall", 8) != 0)
            break;
          return AttributeCommonInfo::AT_VectorCall; // "vectorcall"
        case 'i':                                    // 1 string to match.
          if (memcmp(Name.data() + 2, "sibility", 8) != 0)
            break;
          return AttributeCommonInfo::AT_Visibility; // "visibility"
        }
        break;
      }
      break;
    case 11: // 13 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 2 strings to match.
        if (Name[1] != 'l')
          break;
        switch (Name[2]) {
        default:
          break;
        case 'i': // 1 string to match.
          if (memcmp(Name.data() + 3, "gn_value", 8) != 0)
            break;
          return AttributeCommonInfo::AT_AlignValue; // "align_value"
        case 'l':                                    // 1 string to match.
          if (memcmp(Name.data() + 3, "oc_align", 8) != 0)
            break;
          return AttributeCommonInfo::AT_AllocAlign; // "alloc_align"
        }
        break;
      case 'c': // 1 string to match.
        if (memcmp(Name.data() + 1, "onstructor", 10) != 0)
          break;
        return AttributeCommonInfo::AT_Constructor; // "constructor"
      case 'd':                                     // 1 string to match.
        if (memcmp(Name.data() + 1, "iagnose_if", 10) != 0)
          break;
        return AttributeCommonInfo::AT_DiagnoseIf; // "diagnose_if"
      case 'e':                                    // 1 string to match.
        if (memcmp(Name.data() + 1, "nforce_tcb", 10) != 0)
          break;
        return AttributeCommonInfo::AT_EnforceTCB; // "enforce_tcb"
      case 'f':                                    // 1 string to match.
        if (memcmp(Name.data() + 1, "allthrough", 10) != 0)
          break;
        return AttributeCommonInfo::AT_FallThrough; // "fallthrough"
      case 'm':                                     // 2 strings to match.
        if (Name[1] != 'a')
          break;
        switch (Name[2]) {
        default:
          break;
        case 't': // 1 string to match.
          if (memcmp(Name.data() + 3, "rix_type", 8) != 0)
            break;
          return AttributeCommonInfo::AT_MatrixType; // "matrix_type"
        case 'y':                                    // 1 string to match.
          if (memcmp(Name.data() + 3, "be_undef", 8) != 0)
            break;
          return AttributeCommonInfo::AT_MaybeUndef; // "maybe_undef"
        }
        break;
      case 'n': // 1 string to match.
        if (memcmp(Name.data() + 1, "oduplicate", 10) != 0)
          break;
        return AttributeCommonInfo::AT_NoDuplicate; // "noduplicate"
      case 'u':                                     // 1 string to match.
        if (memcmp(Name.data() + 1, "navailable", 10) != 0)
          break;
        return AttributeCommonInfo::AT_Unavailable; // "unavailable"
      case 'v':                                     // 1 string to match.
        if (memcmp(Name.data() + 1, "ector_size", 10) != 0)
          break;
        return AttributeCommonInfo::AT_VectorSize; // "vector_size"
      case 'w':                                    // 2 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 2, "rn_unused", 9) != 0)
            break;
          return AttributeCommonInfo::AT_WarnUnused; // "warn_unused"
        case 'e':                                    // 1 string to match.
          if (memcmp(Name.data() + 2, "ak_import", 9) != 0)
            break;
          return AttributeCommonInfo::AT_WeakImport; // "weak_import"
        }
        break;
      }
      break;
    case 12: // 7 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 1, "vailability", 11) != 0)
          break;
        return AttributeCommonInfo::AT_Availability; // "availability"
      case 'b':                                      // 2 strings to match.
        if (memcmp(Name.data() + 1, "tf_", 3) != 0)
          break;
        switch (Name[4]) {
        default:
          break;
        case 'd': // 1 string to match.
          if (memcmp(Name.data() + 5, "ecl_tag", 7) != 0)
            break;
          return AttributeCommonInfo::AT_BTFDeclTag; // "btf_decl_tag"
        case 't':                                    // 1 string to match.
          if (memcmp(Name.data() + 5, "ype_tag", 7) != 0)
            break;
          return AttributeCommonInfo::AT_BTFTypeTag; // "btf_type_tag"
        }
        break;
      case 'c': // 2 strings to match.
        if (memcmp(Name.data() + 1, "pu_", 3) != 0)
          break;
        switch (Name[4]) {
        default:
          break;
        case 'd': // 1 string to match.
          if (memcmp(Name.data() + 5, "ispatch", 7) != 0)
            break;
          return AttributeCommonInfo::AT_CPUDispatch; // "cpu_dispatch"
        case 's':                                     // 1 string to match.
          if (memcmp(Name.data() + 5, "pecific", 7) != 0)
            break;
          return AttributeCommonInfo::AT_CPUSpecific; // "cpu_specific"
        }
        break;
      case 'o': // 1 string to match.
        if (memcmp(Name.data() + 1, "verloadable", 11) != 0)
          break;
        return AttributeCommonInfo::AT_Overloadable; // "overloadable"
      case 'p':                                      // 1 string to match.
        if (memcmp(Name.data() + 1, "reserve_all", 11) != 0)
          break;
        return AttributeCommonInfo::AT_PreserveAll; // "preserve_all"
      }
      break;
    case 13: // 6 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 2 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'd': // 1 string to match.
          if (memcmp(Name.data() + 2, "dress_space", 11) != 0)
            break;
          return AttributeCommonInfo::AT_AddressSpace; // "address_space"
        case 'l':                                      // 1 string to match.
          if (memcmp(Name.data() + 2, "ways_inline", 11) != 0)
            break;
          return AttributeCommonInfo::AT_AlwaysInline; // "always_inline"
        }
        break;
      case 'p': // 1 string to match.
        if (memcmp(Name.data() + 1, "reserve_most", 12) != 0)
          break;
        return AttributeCommonInfo::AT_PreserveMost; // "preserve_most"
      case 'r':                                      // 1 string to match.
        if (memcmp(Name.data() + 1, "eturns_twice", 12) != 0)
          break;
        return AttributeCommonInfo::AT_ReturnsTwice; // "returns_twice"
      case 't':                                      // 1 string to match.
        if (memcmp(Name.data() + 1, "arget_clones", 12) != 0)
          break;
        return AttributeCommonInfo::AT_TargetClones; // "target_clones"
      case 'u':                                      // 1 string to match.
        if (memcmp(Name.data() + 1, "ninitialized", 12) != 0)
          break;
        return AttributeCommonInfo::AT_Uninitialized; // "uninitialized"
      }
      break;
    case 14: // 7 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 3 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'c': // 1 string to match.
          if (memcmp(Name.data() + 2, "quire_handle", 12) != 0)
            break;
          return AttributeCommonInfo::AT_AcquireHandle; // "acquire_handle"
        case 'l':                                       // 1 string to match.
          if (memcmp(Name.data() + 2, "ways_destroy", 12) != 0)
            break;
          return AttributeCommonInfo::AT_AlwaysDestroy; // "always_destroy"
        case 's':                                       // 1 string to match.
          if (memcmp(Name.data() + 2, "sume_aligned", 12) != 0)
            break;
          return AttributeCommonInfo::AT_AssumeAligned; // "assume_aligned"
        }
        break;
      case 'n': // 1 string to match.
        if (memcmp(Name.data() + 1, "o_split_stack", 13) != 0)
          break;
        return AttributeCommonInfo::AT_NoSplitStack; // "no_split_stack"
      case 'p':                                      // 1 string to match.
        if (memcmp(Name.data() + 1, "referred_type", 13) != 0)
          break;
        return AttributeCommonInfo::AT_PreferredType; // "preferred_type"
      case 'r':                                       // 1 string to match.
        if (memcmp(Name.data() + 1, "elease_handle", 13) != 0)
          break;
        return AttributeCommonInfo::AT_ReleaseHandle; // "release_handle"
      case 't':                                       // 1 string to match.
        if (memcmp(Name.data() + 1, "arget_version", 13) != 0)
          break;
        return AttributeCommonInfo::AT_TargetVersion; // "target_version"
      }
      break;
    case 15: // 6 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 1, "arch64_sve_pcs", 14) != 0)
          break;
        return AttributeCommonInfo::AT_AArch64SVEPcs; // "aarch64_sve_pcs"
      case 'e':                                       // 1 string to match.
        if (memcmp(Name.data() + 1, "xt_vector_type", 14) != 0)
          break;
        return AttributeCommonInfo::AT_ExtVectorType; // "ext_vector_type"
      case 'f':                                       // 1 string to match.
        if (memcmp(Name.data() + 1, "unction_return", 14) != 0)
          break;
        return AttributeCommonInfo::
            AT_FunctionReturnThunks; // "function_return"
      case 'n':                      // 1 string to match.
        if (memcmp(Name.data() + 1, "ot_tail_called", 14) != 0)
          break;
        return AttributeCommonInfo::AT_NotTailCalled; // "not_tail_called"
      case 'r':                                       // 1 string to match.
        if (memcmp(Name.data() + 1, "eturns_nonnull", 14) != 0)
          break;
        return AttributeCommonInfo::AT_ReturnsNonNull; // "returns_nonnull"
      case 't':                                        // 1 string to match.
        if (memcmp(Name.data() + 1, "ype_visibility", 14) != 0)
          break;
        return AttributeCommonInfo::AT_TypeVisibility; // "type_visibility"
      }
      break;
    case 16: // 7 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'd': // 1 string to match.
        if (memcmp(Name.data() + 1, "isable_try_stmt", 15) != 0)
          break;
        return AttributeCommonInfo::AT_DisableTryStmt; // "disable_try_stmt"
      case 'e':                                        // 1 string to match.
        if (memcmp(Name.data() + 1, "nforce_tcb_leaf", 15) != 0)
          break;
        return AttributeCommonInfo::AT_EnforceTCBLeaf; // "enforce_tcb_leaf"
      case 'i':                                        // 1 string to match.
        if (memcmp(Name.data() + 1, "nternal_linkage", 15) != 0)
          break;
        return AttributeCommonInfo::AT_InternalLinkage; // "internal_linkage"
      case 'm':                                         // 1 string to match.
        if (memcmp(Name.data() + 1, "in_vector_width", 15) != 0)
          break;
        return AttributeCommonInfo::AT_MinVectorWidth; // "min_vector_width"
      case 'n':                                        // 1 string to match.
        if (memcmp(Name.data() + 1, "eon_vector_type", 15) != 0)
          break;
        return AttributeCommonInfo::AT_NeonVectorType; // "neon_vector_type"
      case 'p':                                        // 1 string to match.
        if (memcmp(Name.data() + 1, "ass_object_size", 15) != 0)
          break;
        return AttributeCommonInfo::AT_PassObjectSize; // "pass_object_size"
      case 'r':                                        // 1 string to match.
        if (memcmp(Name.data() + 1, "andomize_layout", 15) != 0)
          break;
        return AttributeCommonInfo::AT_RandomizeLayout; // "randomize_layout"
      }
      break;
    case 17: // 2 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 1, "nalyzer_noreturn", 16) != 0)
          break;
        return AttributeCommonInfo::AT_AnalyzerNoReturn; // "analyzer_noreturn"
      case 't':                                          // 1 string to match.
        if (memcmp(Name.data() + 1, "ransparent_union", 16) != 0)
          break;
        return AttributeCommonInfo::AT_TransparentUnion; // "transparent_union"
      }
      break;
    case 18: // 6 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 1, "arch64_vector_pcs", 17) != 0)
          break;
        return AttributeCommonInfo::AT_AArch64VectorPcs; // "aarch64_vector_pcs"
      case 'c':                                          // 1 string to match.
        if (memcmp(Name.data() + 1, "arries_dependency", 17) != 0)
          break;
        return AttributeCommonInfo::
            AT_CarriesDependency; // "carries_dependency"
      case 'd':                   // 1 string to match.
        if (memcmp(Name.data() + 1, "isable_tail_calls", 17) != 0)
          break;
        return AttributeCommonInfo::AT_DisableTailCalls; // "disable_tail_calls"
      case 'e':                                          // 1 string to match.
        if (memcmp(Name.data() + 1, "num_extensibility", 17) != 0)
          break;
        return AttributeCommonInfo::
            AT_EnumExtensibility; // "enum_extensibility"
      case 'n':                   // 1 string to match.
        if (memcmp(Name.data() + 1, "o_stack_protector", 17) != 0)
          break;
        return AttributeCommonInfo::AT_NoStackProtector; // "no_stack_protector"
      case 'w':                                          // 1 string to match.
        if (memcmp(Name.data() + 1, "arn_unused_result", 17) != 0)
          break;
        return AttributeCommonInfo::AT_WarnUnusedResult; // "warn_unused_result"
      }
      break;
    case 19: // 5 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 1, "rm_sve_vector_bits", 18) != 0)
          break;
        return AttributeCommonInfo::
            AT_ArmSveVectorBits; // "arm_sve_vector_bits"
      case 'd':                  // 1 string to match.
        if (memcmp(Name.data() + 1, "iagnose_as_builtin", 18) != 0)
          break;
        return AttributeCommonInfo::
            AT_DiagnoseAsBuiltin; // "diagnose_as_builtin"
      case 'n':                   // 1 string to match.
        if (memcmp(Name.data() + 1, "o_randomize_layout", 18) != 0)
          break;
        return AttributeCommonInfo::
            AT_NoRandomizeLayout; // "no_randomize_layout"
      case 'u':                   // 1 string to match.
        if (memcmp(Name.data() + 1, "nsafe_buffer_usage", 18) != 0)
          break;
        return AttributeCommonInfo::
            AT_UnsafeBufferUsage; // "unsafe_buffer_usage"
      case 'z':                   // 1 string to match.
        if (memcmp(Name.data() + 1, "ero_call_used_regs", 18) != 0)
          break;
        return AttributeCommonInfo::
            AT_ZeroCallUsedRegs; // "zero_call_used_regs"
      }
      break;
    case 20: // 3 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'l': // 1 string to match.
        if (memcmp(Name.data() + 1, "oader_uninitialized", 19) != 0)
          break;
        return AttributeCommonInfo::
            AT_LoaderUninitialized; // "loader_uninitialized"
      case 'n':                     // 2 strings to match.
        if (Name[1] != 'e')
          break;
        switch (Name[2]) {
        default:
          break;
        case 'o': // 1 string to match.
          if (memcmp(Name.data() + 3, "n_polyvector_type", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_NeonPolyVectorType; // "neon_polyvector_type"
        case 'v':                    // 1 string to match.
          if (memcmp(Name.data() + 3, "erc_builtin_alias", 17) != 0)
            break;
          return AttributeCommonInfo::AT_BuiltinAlias; // "neverc_builtin_alias"
        }
        break;
      }
      break;
    case 21: // 3 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'l': // 1 string to match.
        if (memcmp(Name.data() + 1, "to_visibility_public", 20) != 0)
          break;
        return AttributeCommonInfo::
            AT_LTOVisibilityPublic; // "lto_visibility_public"
      case 'p':                     // 1 string to match.
        if (memcmp(Name.data() + 1, "ointer_with_type_tag", 20) != 0)
          break;
        return AttributeCommonInfo::
            AT_ArgumentWithTypeTag; // "pointer_with_type_tag"
      case 't':                     // 1 string to match.
        if (memcmp(Name.data() + 1, "ype_tag_for_datatype", 20) != 0)
          break;
        return AttributeCommonInfo::
            AT_TypeTagForDatatype; // "type_tag_for_datatype"
      }
      break;
    case 22: // 1 string to match.
      if (memcmp(Name.data() + 0, "argument_with_type_tag", 22) != 0)
        break;
      return AttributeCommonInfo::
          AT_ArgumentWithTypeTag; // "argument_with_type_tag"
    case 23:                      // 1 string to match.
      if (memcmp(Name.data() + 0, "force_align_arg_pointer", 23) != 0)
        break;
      return AttributeCommonInfo::
          AT_X86ForceAlignArgPointer; // "force_align_arg_pointer"
    case 24:                          // 2 strings to match.
      if (memcmp(Name.data() + 0, "pa", 2) != 0)
        break;
      switch (Name[2]) {
      default:
        break;
      case 's': // 1 string to match.
        if (memcmp(Name.data() + 3, "s_dynamic_object_size", 21) != 0)
          break;
        return AttributeCommonInfo::
            AT_PassObjectSize; // "pass_dynamic_object_size"
      case 't':                // 1 string to match.
        if (memcmp(Name.data() + 3, "chable_function_entry", 21) != 0)
          break;
        return AttributeCommonInfo::
            AT_PatchableFunctionEntry; // "patchable_function_entry"
      }
      break;
    case 25: // 1 string to match.
      if (memcmp(Name.data() + 0, "no_caller_saved_registers", 25) != 0)
        break;
      return AttributeCommonInfo::
          AT_AnyX86NoCallerSavedRegisters; // "no_caller_saved_registers"
    case 26:                               // 2 strings to match.
      switch (Name[0]) {
      default:
        break;
      case '_': // 1 string to match.
        if (memcmp(Name.data() + 1, "_neverc_arm_builtin_alias", 25) != 0)
          break;
        return AttributeCommonInfo::
            AT_ArmBuiltinAlias; // "__neverc_arm_builtin_alias"
      case 's':                 // 1 string to match.
        if (memcmp(Name.data() + 1, "peculative_load_hardening", 25) != 0)
          break;
        return AttributeCommonInfo::
            AT_SpeculativeLoadHardening; // "speculative_load_hardening"
      }
      break;
    case 27: // 1 string to match.
      if (memcmp(Name.data() + 0, "enforce_read_only_placement", 27) != 0)
        break;
      return AttributeCommonInfo::
          AT_ReadOnlyPlacement; // "enforce_read_only_placement"
    case 29:                    // 1 string to match.
      if (memcmp(Name.data() + 0, "no_speculative_load_hardening", 29) != 0)
        break;
      return AttributeCommonInfo::
          AT_NoSpeculativeLoadHardening; // "no_speculative_load_hardening"
    case 37:                             // 1 string to match.
      if (memcmp(Name.data() + 0, "available_only_in_default_eval_method",
                 37) != 0)
        break;
      return AttributeCommonInfo::
          AT_AvailableOnlyInDefaultEvalMethod; // "available_only_in_default_eval_method"
    }
  } else if (AttributeCommonInfo::AS_Declspec == Syntax) {
    switch (Name.size()) {
    default:
      break;
    case 5: // 3 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 1, "lign", 4) != 0)
          break;
        return AttributeCommonInfo::AT_Aligned; // "align"
      case 'g':                                 // 1 string to match.
        if (memcmp(Name.data() + 1, "uard", 4) != 0)
          break;
        return AttributeCommonInfo::AT_CFGuard; // "guard"
      case 'n':                                 // 1 string to match.
        if (memcmp(Name.data() + 1, "aked", 4) != 0)
          break;
        return AttributeCommonInfo::AT_Naked; // "naked"
      }
      break;
    case 6: // 1 string to match.
      if (memcmp(Name.data() + 0, "thread", 6) != 0)
        break;
      return AttributeCommonInfo::AT_Thread; // "thread"
    case 7:                                  // 2 strings to match.
      if (memcmp(Name.data() + 0, "no", 2) != 0)
        break;
      switch (Name[2]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 3, "lias", 4) != 0)
          break;
        return AttributeCommonInfo::AT_NoAlias; // "noalias"
      case 't':                                 // 1 string to match.
        if (memcmp(Name.data() + 3, "hrow", 4) != 0)
          break;
        return AttributeCommonInfo::AT_NoThrow; // "nothrow"
      }
      break;
    case 8: // 8 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 1, "llocate", 7) != 0)
          break;
        return AttributeCommonInfo::AT_Section; // "allocate"
      case 'c':                                 // 1 string to match.
        if (memcmp(Name.data() + 1, "ode_seg", 7) != 0)
          break;
        return AttributeCommonInfo::AT_CodeSeg; // "code_seg"
      case 'n':                                 // 2 strings to match.
        if (Name[1] != 'o')
          break;
        switch (Name[2]) {
        default:
          break;
        case 'i': // 1 string to match.
          if (memcmp(Name.data() + 3, "nline", 5) != 0)
            break;
          return AttributeCommonInfo::AT_NoInline; // "noinline"
        case 'r':                                  // 1 string to match.
          if (memcmp(Name.data() + 3, "eturn", 5) != 0)
            break;
          return AttributeCommonInfo::AT_NoReturn; // "noreturn"
        }
        break;
      case 'o': // 1 string to match.
        if (memcmp(Name.data() + 1, "verride", 7) != 0)
          break;
        return AttributeCommonInfo::AT_Override; // "override"
      case 'p':                                  // 1 string to match.
        if (memcmp(Name.data() + 1, "roperty", 7) != 0)
          break;
        return AttributeCommonInfo::IgnoredAttribute; // "property"
      case 'r':                                       // 1 string to match.
        if (memcmp(Name.data() + 1, "estrict", 7) != 0)
          break;
        return AttributeCommonInfo::AT_Restrict; // "restrict"
      case 'v':                                  // 1 string to match.
        if (memcmp(Name.data() + 1, "olatile", 7) != 0)
          break;
        return AttributeCommonInfo::AT_Volatile; // "volatile"
      }
      break;
    case 9: // 4 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 1, "llocator", 8) != 0)
          break;
        return AttributeCommonInfo::AT_MSAllocator; // "allocator"
      case 'd':                                     // 2 strings to match.
        if (memcmp(Name.data() + 1, "ll", 2) != 0)
          break;
        switch (Name[3]) {
        default:
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 4, "xport", 5) != 0)
            break;
          return AttributeCommonInfo::AT_DLLExport; // "dllexport"
        case 'i':                                   // 1 string to match.
          if (memcmp(Name.data() + 4, "mport", 5) != 0)
            break;
          return AttributeCommonInfo::AT_DLLImport; // "dllimport"
        }
        break;
      case 's': // 1 string to match.
        if (memcmp(Name.data() + 1, "electany", 8) != 0)
          break;
        return AttributeCommonInfo::AT_SelectAny; // "selectany"
      }
      break;
    case 10: // 1 string to match.
      if (memcmp(Name.data() + 0, "deprecated", 10) != 0)
        break;
      return AttributeCommonInfo::AT_Deprecated; // "deprecated"
    case 11:                                     // 1 string to match.
      if (memcmp(Name.data() + 0, "safebuffers", 11) != 0)
        break;
      return AttributeCommonInfo::AT_NoStackProtector; // "safebuffers"
    case 12:                                           // 2 strings to match.
      if (memcmp(Name.data() + 0, "cpu_", 4) != 0)
        break;
      switch (Name[4]) {
      default:
        break;
      case 'd': // 1 string to match.
        if (memcmp(Name.data() + 5, "ispatch", 7) != 0)
          break;
        return AttributeCommonInfo::AT_CPUDispatch; // "cpu_dispatch"
      case 's':                                     // 1 string to match.
        if (memcmp(Name.data() + 5, "pecific", 7) != 0)
          break;
        return AttributeCommonInfo::AT_CPUSpecific; // "cpu_specific"
      }
      break;
    case 15: // 1 string to match.
      if (memcmp(Name.data() + 0, "strict_gs_check", 15) != 0)
        break;
      return AttributeCommonInfo::AT_StrictGuardStackCheck; // "strict_gs_check"
    case 16: // 1 string to match.
      if (memcmp(Name.data() + 0, "disable_try_stmt", 16) != 0)
        break;
      return AttributeCommonInfo::AT_DisableTryStmt; // "disable_try_stmt"
    }
  } else if (AttributeCommonInfo::AS_Microsoft == Syntax) {
  } else if (AttributeCommonInfo::AS_Bracket == Syntax) {
    switch (Name.size()) {
    default:
      break;
    case 6: // 1 string to match.
      if (memcmp(Name.data() + 0, "likely", 6) != 0)
        break;
      return AttributeCommonInfo::AT_Likely; // "likely"
    case 8:                                  // 3 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'g': // 1 string to match.
        if (memcmp(Name.data() + 1, "nu::hot", 7) != 0)
          break;
        return AttributeCommonInfo::AT_Hot; // "gnu::hot"
      case 'n':                             // 1 string to match.
        if (memcmp(Name.data() + 1, "oreturn", 7) != 0)
          break;
        return AttributeCommonInfo::AT_StandardNoReturn; // "noreturn"
      case 'u':                                          // 1 string to match.
        if (memcmp(Name.data() + 1, "nlikely", 7) != 0)
          break;
        return AttributeCommonInfo::AT_Unlikely; // "unlikely"
      }
      break;
    case 9: // 7 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'g': // 6 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'c': // 1 string to match.
          if (memcmp(Name.data() + 6, "old", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Cold; // "gnu::cold"
        case 'l':                              // 1 string to match.
          if (memcmp(Name.data() + 6, "eaf", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Leaf; // "gnu::leaf"
        case 'm':                              // 1 string to match.
          if (memcmp(Name.data() + 6, "ode", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Mode; // "gnu::mode"
        case 'p':                              // 1 string to match.
          if (memcmp(Name.data() + 6, "ure", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Pure; // "gnu::pure"
        case 'u':                              // 1 string to match.
          if (memcmp(Name.data() + 6, "sed", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Used; // "gnu::used"
        case 'w':                              // 1 string to match.
          if (memcmp(Name.data() + 6, "eak", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Weak; // "gnu::weak"
        }
        break;
      case 'n': // 1 string to match.
        if (memcmp(Name.data() + 1, "odiscard", 8) != 0)
          break;
        return AttributeCommonInfo::AT_WarnUnusedResult; // "nodiscard"
      }
      break;
    case 10: // 7 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'd': // 1 string to match.
        if (memcmp(Name.data() + 1, "eprecated", 9) != 0)
          break;
        return AttributeCommonInfo::AT_Deprecated; // "deprecated"
      case 'g':                                    // 6 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 6, "lias", 4) != 0)
            break;
          return AttributeCommonInfo::AT_Alias; // "gnu::alias"
        case 'c':                               // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 7, "ecl", 3) != 0)
              break;
            return AttributeCommonInfo::AT_CDecl; // "gnu::cdecl"
          case 'o':                               // 1 string to match.
            if (memcmp(Name.data() + 7, "nst", 3) != 0)
              break;
            return AttributeCommonInfo::AT_Const; // "gnu::const"
          }
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 6, "rror", 4) != 0)
            break;
          return AttributeCommonInfo::AT_Error; // "gnu::error"
        case 'i':                               // 1 string to match.
          if (memcmp(Name.data() + 6, "func", 4) != 0)
            break;
          return AttributeCommonInfo::AT_IFunc; // "gnu::ifunc"
        case 'n':                               // 1 string to match.
          if (memcmp(Name.data() + 6, "aked", 4) != 0)
            break;
          return AttributeCommonInfo::AT_Naked; // "gnu::naked"
        }
        break;
      }
      break;
    case 11: // 9 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'f': // 1 string to match.
        if (memcmp(Name.data() + 1, "allthrough", 10) != 0)
          break;
        return AttributeCommonInfo::AT_FallThrough; // "fallthrough"
      case 'g':                                     // 8 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'c': // 1 string to match.
          if (memcmp(Name.data() + 6, "ommon", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Common; // "gnu::common"
        case 'f':                                // 1 string to match.
          if (memcmp(Name.data() + 6, "ormat", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Format; // "gnu::format"
        case 'm':                                // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'a': // 1 string to match.
            if (memcmp(Name.data() + 7, "lloc", 4) != 0)
              break;
            return AttributeCommonInfo::AT_Restrict; // "gnu::malloc"
          case 's':                                  // 1 string to match.
            if (memcmp(Name.data() + 7, "_abi", 4) != 0)
              break;
            return AttributeCommonInfo::AT_MSABI; // "gnu::ms_abi"
          }
          break;
        case 'p': // 1 string to match.
          if (memcmp(Name.data() + 6, "acked", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Packed; // "gnu::packed"
        case 'r':                                // 1 string to match.
          if (memcmp(Name.data() + 6, "etain", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Retain; // "gnu::retain"
        case 't':                                // 1 string to match.
          if (memcmp(Name.data() + 6, "arget", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Target; // "gnu::target"
        case 'u':                                // 1 string to match.
          if (memcmp(Name.data() + 6, "nused", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Unused; // "gnu::unused"
        }
        break;
      }
      break;
    case 12: // 15 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 1 string to match.
        if (memcmp(Name.data() + 1, "lang::guard", 11) != 0)
          break;
        return AttributeCommonInfo::AT_CFGuard; // "clang::guard"
      case 'g':                                 // 13 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case '_': // 1 string to match.
          if (memcmp(Name.data() + 6, "_const", 6) != 0)
            break;
          return AttributeCommonInfo::AT_Const; // "gnu::__const"
        case 'a':                               // 1 string to match.
          if (memcmp(Name.data() + 6, "ligned", 6) != 0)
            break;
          return AttributeCommonInfo::AT_Aligned; // "gnu::aligned"
        case 'c':                                 // 1 string to match.
          if (memcmp(Name.data() + 6, "leanup", 6) != 0)
            break;
          return AttributeCommonInfo::AT_Cleanup; // "gnu::cleanup"
        case 'f':                                 // 1 string to match.
          if (memcmp(Name.data() + 6, "latten", 6) != 0)
            break;
          return AttributeCommonInfo::AT_Flatten; // "gnu::flatten"
        case 'n':                                 // 3 strings to match.
          if (Name[6] != 'o')
            break;
          switch (Name[7]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 8, "ebug", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NoDebug; // "gnu::nodebug"
          case 'n':                                 // 1 string to match.
            if (memcmp(Name.data() + 8, "null", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NonNull; // "gnu::nonnull"
          case 't':                                 // 1 string to match.
            if (memcmp(Name.data() + 8, "hrow", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NoThrow; // "gnu::nothrow"
          }
          break;
        case 'r': // 2 strings to match.
          if (memcmp(Name.data() + 6, "eg", 2) != 0)
            break;
          switch (Name[8]) {
          default:
            break;
          case 'c': // 1 string to match.
            if (memcmp(Name.data() + 9, "all", 3) != 0)
              break;
            return AttributeCommonInfo::AT_RegCall; // "gnu::regcall"
          case 'p':                                 // 1 string to match.
            if (memcmp(Name.data() + 9, "arm", 3) != 0)
              break;
            return AttributeCommonInfo::AT_Regparm; // "gnu::regparm"
          }
          break;
        case 's': // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'e': // 1 string to match.
            if (memcmp(Name.data() + 7, "ction", 5) != 0)
              break;
            return AttributeCommonInfo::AT_Section; // "gnu::section"
          case 't':                                 // 1 string to match.
            if (memcmp(Name.data() + 7, "dcall", 5) != 0)
              break;
            return AttributeCommonInfo::AT_StdCall; // "gnu::stdcall"
          }
          break;
        case 'w': // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'a': // 1 string to match.
            if (memcmp(Name.data() + 7, "rning", 5) != 0)
              break;
            return AttributeCommonInfo::AT_Error; // "gnu::warning"
          case 'e':                               // 1 string to match.
            if (memcmp(Name.data() + 7, "akref", 5) != 0)
              break;
            return AttributeCommonInfo::AT_WeakRef; // "gnu::weakref"
          }
          break;
        }
        break;
      case 'm': // 1 string to match.
        if (memcmp(Name.data() + 1, "aybe_unused", 11) != 0)
          break;
        return AttributeCommonInfo::AT_Unused; // "maybe_unused"
      }
      break;
    case 13: // 11 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 1 string to match.
        if (memcmp(Name.data() + 1, "lang::assume", 12) != 0)
          break;
        return AttributeCommonInfo::AT_Assumption; // "clang::assume"
      case 'g':                                    // 9 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'n': // 8 strings to match.
          if (memcmp(Name.data() + 2, "u::", 3) != 0)
            break;
          switch (Name[5]) {
          default:
            break;
          case 'f': // 1 string to match.
            if (memcmp(Name.data() + 6, "astcall", 7) != 0)
              break;
            return AttributeCommonInfo::AT_FastCall; // "gnu::fastcall"
          case 'n':                                  // 3 strings to match.
            if (Name[6] != 'o')
              break;
            switch (Name[7]) {
            default:
              break;
            case 'c': // 1 string to match.
              if (memcmp(Name.data() + 8, "ommon", 5) != 0)
                break;
              return AttributeCommonInfo::AT_NoCommon; // "gnu::nocommon"
            case 'i':                                  // 1 string to match.
              if (memcmp(Name.data() + 8, "nline", 5) != 0)
                break;
              return AttributeCommonInfo::AT_NoInline; // "gnu::noinline"
            case 'r':                                  // 1 string to match.
              if (memcmp(Name.data() + 8, "eturn", 5) != 0)
                break;
              return AttributeCommonInfo::AT_NoReturn; // "gnu::noreturn"
            }
            break;
          case 'o': // 1 string to match.
            if (memcmp(Name.data() + 6, "verride", 7) != 0)
              break;
            return AttributeCommonInfo::AT_Override; // "gnu::override"
          case 's':                                  // 2 strings to match.
            switch (Name[6]) {
            default:
              break;
            case 'e': // 1 string to match.
              if (memcmp(Name.data() + 7, "ntinel", 6) != 0)
                break;
              return AttributeCommonInfo::AT_Sentinel; // "gnu::sentinel"
            case 'y':                                  // 1 string to match.
              if (memcmp(Name.data() + 7, "sv_abi", 6) != 0)
                break;
              return AttributeCommonInfo::AT_SysVABI; // "gnu::sysv_abi"
            }
            break;
          case 'v': // 1 string to match.
            if (memcmp(Name.data() + 6, "olatile", 7) != 0)
              break;
            return AttributeCommonInfo::AT_Volatile; // "gnu::volatile"
          }
          break;
        case 's': // 1 string to match.
          if (memcmp(Name.data() + 2, "l::suppress", 11) != 0)
            break;
          return AttributeCommonInfo::AT_Suppress; // "gsl::suppress"
        }
        break;
      case 'n': // 1 string to match.
        if (memcmp(Name.data() + 1, "everc::guard", 12) != 0)
          break;
        return AttributeCommonInfo::AT_CFGuard; // "neverc::guard"
      }
      break;
    case 14: // 12 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 4 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'm': // 1 string to match.
          if (memcmp(Name.data() + 8, "insize", 6) != 0)
            break;
          return AttributeCommonInfo::AT_MinSize; // "clang::minsize"
        case 'n':                                 // 2 strings to match.
          if (Name[8] != 'o')
            break;
          switch (Name[9]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 10, "eref", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NoDeref; // "clang::noderef"
          case 'm':                                 // 1 string to match.
            if (memcmp(Name.data() + 10, "erge", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NoMerge; // "clang::nomerge"
          }
          break;
        case 'o': // 1 string to match.
          if (memcmp(Name.data() + 8, "ptnone", 6) != 0)
            break;
          return AttributeCommonInfo::AT_OptimizeNone; // "clang::optnone"
        }
        break;
      case 'g': // 7 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'd': // 2 strings to match.
          if (memcmp(Name.data() + 6, "ll", 2) != 0)
            break;
          switch (Name[8]) {
          default:
            break;
          case 'e': // 1 string to match.
            if (memcmp(Name.data() + 9, "xport", 5) != 0)
              break;
            return AttributeCommonInfo::AT_DLLExport; // "gnu::dllexport"
          case 'i':                                   // 1 string to match.
            if (memcmp(Name.data() + 9, "mport", 5) != 0)
              break;
            return AttributeCommonInfo::AT_DLLImport; // "gnu::dllimport"
          }
          break;
        case 'i': // 1 string to match.
          if (memcmp(Name.data() + 6, "nterrupt", 8) != 0)
            break;
          return AttributeCommonInfo::AT_Interrupt; // "gnu::interrupt"
        case 'm':                                   // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'a': // 1 string to match.
            if (memcmp(Name.data() + 7, "y_alias", 7) != 0)
              break;
            return AttributeCommonInfo::AT_MayAlias; // "gnu::may_alias"
          case 's':                                  // 1 string to match.
            if (memcmp(Name.data() + 7, "_struct", 7) != 0)
              break;
            return AttributeCommonInfo::AT_MSStruct; // "gnu::ms_struct"
          }
          break;
        case 's': // 1 string to match.
          if (memcmp(Name.data() + 6, "electany", 8) != 0)
            break;
          return AttributeCommonInfo::AT_SelectAny; // "gnu::selectany"
        case 't':                                   // 1 string to match.
          if (memcmp(Name.data() + 6, "ls_model", 8) != 0)
            break;
          return AttributeCommonInfo::AT_TLSModel; // "gnu::tls_model"
        }
        break;
      case 'n': // 1 string to match.
        if (memcmp(Name.data() + 1, "everc::assume", 13) != 0)
          break;
        return AttributeCommonInfo::AT_Assumption; // "neverc::assume"
      }
      break;
    case 15: // 17 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 5 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 8, "nnotate", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Annotate; // "clang::annotate"
        case 'c':                                  // 1 string to match.
          if (memcmp(Name.data() + 8, "allback", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Callback; // "clang::callback"
        case 'm':                                  // 1 string to match.
          if (memcmp(Name.data() + 8, "usttail", 7) != 0)
            break;
          return AttributeCommonInfo::AT_MustTail; // "clang::musttail"
        case 'n':                                  // 1 string to match.
          if (memcmp(Name.data() + 8, "oescape", 7) != 0)
            break;
          return AttributeCommonInfo::AT_NoEscape; // "clang::noescape"
        case 's':                                  // 1 string to match.
          if (memcmp(Name.data() + 8, "uppress", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Suppress; // "clang::suppress"
        }
        break;
      case 'g': // 8 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'a': // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'l': // 1 string to match.
            if (memcmp(Name.data() + 7, "loc_size", 8) != 0)
              break;
            return AttributeCommonInfo::AT_AllocSize; // "gnu::alloc_size"
          case 'r':                                   // 1 string to match.
            if (memcmp(Name.data() + 7, "tificial", 8) != 0)
              break;
            return AttributeCommonInfo::AT_Artificial; // "gnu::artificial"
          }
          break;
        case 'd': // 2 strings to match.
          if (Name[6] != 'e')
            break;
          switch (Name[7]) {
          default:
            break;
          case 'p': // 1 string to match.
            if (memcmp(Name.data() + 8, "recated", 7) != 0)
              break;
            return AttributeCommonInfo::AT_Deprecated; // "gnu::deprecated"
          case 's':                                    // 1 string to match.
            if (memcmp(Name.data() + 8, "tructor", 7) != 0)
              break;
            return AttributeCommonInfo::AT_Destructor; // "gnu::destructor"
          }
          break;
        case 'f': // 1 string to match.
          if (memcmp(Name.data() + 6, "ormat_arg", 9) != 0)
            break;
          return AttributeCommonInfo::AT_FormatArg; // "gnu::format_arg"
        case 'g':                                   // 1 string to match.
          if (memcmp(Name.data() + 6, "nu_inline", 9) != 0)
            break;
          return AttributeCommonInfo::AT_GNUInline; // "gnu::gnu_inline"
        case 'n':                                   // 1 string to match.
          if (memcmp(Name.data() + 6, "ocf_check", 9) != 0)
            break;
          return AttributeCommonInfo::AT_AnyX86NoCfCheck; // "gnu::nocf_check"
        case 'v':                                         // 1 string to match.
          if (memcmp(Name.data() + 6, "isibility", 9) != 0)
            break;
          return AttributeCommonInfo::AT_Visibility; // "gnu::visibility"
        }
        break;
      case 'n': // 4 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'm': // 1 string to match.
          if (memcmp(Name.data() + 9, "insize", 6) != 0)
            break;
          return AttributeCommonInfo::AT_MinSize; // "neverc::minsize"
        case 'n':                                 // 2 strings to match.
          if (Name[9] != 'o')
            break;
          switch (Name[10]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 11, "eref", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NoDeref; // "neverc::noderef"
          case 'm':                                 // 1 string to match.
            if (memcmp(Name.data() + 11, "erge", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NoMerge; // "neverc::nomerge"
          }
          break;
        case 'o': // 1 string to match.
          if (memcmp(Name.data() + 9, "ptnone", 6) != 0)
            break;
          return AttributeCommonInfo::AT_OptimizeNone; // "neverc::optnone"
        }
        break;
      }
      break;
    case 16: // 13 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 2 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'f': // 1 string to match.
          if (memcmp(Name.data() + 8, "lag_enum", 8) != 0)
            break;
          return AttributeCommonInfo::AT_FlagEnum; // "clang::flag_enum"
        case 'n':                                  // 1 string to match.
          if (memcmp(Name.data() + 8, "ouwtable", 8) != 0)
            break;
          return AttributeCommonInfo::AT_NoUwtable; // "clang::nouwtable"
        }
        break;
      case 'g': // 5 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 6, "lloc_align", 10) != 0)
            break;
          return AttributeCommonInfo::AT_AllocAlign; // "gnu::alloc_align"
        case 'c':                                    // 1 string to match.
          if (memcmp(Name.data() + 6, "onstructor", 10) != 0)
            break;
          return AttributeCommonInfo::AT_Constructor; // "gnu::constructor"
        case 'f':                                     // 1 string to match.
          if (memcmp(Name.data() + 6, "allthrough", 10) != 0)
            break;
          return AttributeCommonInfo::AT_FallThrough; // "gnu::fallthrough"
        case 'v':                                     // 1 string to match.
          if (memcmp(Name.data() + 6, "ector_size", 10) != 0)
            break;
          return AttributeCommonInfo::AT_VectorSize; // "gnu::vector_size"
        case 'w':                                    // 1 string to match.
          if (memcmp(Name.data() + 6, "arn_unused", 10) != 0)
            break;
          return AttributeCommonInfo::AT_WarnUnused; // "gnu::warn_unused"
        }
        break;
      case 'n': // 6 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 9, "nnotate", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Annotate; // "neverc::annotate"
        case 'c':                                  // 1 string to match.
          if (memcmp(Name.data() + 9, "allback", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Callback; // "neverc::callback"
        case 'm':                                  // 1 string to match.
          if (memcmp(Name.data() + 9, "usttail", 7) != 0)
            break;
          return AttributeCommonInfo::AT_MustTail; // "neverc::musttail"
        case 'n':                                  // 2 strings to match.
          if (Name[9] != 'o')
            break;
          switch (Name[10]) {
          default:
            break;
          case 'e': // 1 string to match.
            if (memcmp(Name.data() + 11, "scape", 5) != 0)
              break;
            return AttributeCommonInfo::AT_NoEscape; // "neverc::noescape"
          case 'i':                                  // 1 string to match.
            if (memcmp(Name.data() + 11, "nline", 5) != 0)
              break;
            return AttributeCommonInfo::AT_NoInline; // "neverc::noinline"
          }
          break;
        case 's': // 1 string to match.
          if (memcmp(Name.data() + 9, "uppress", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Suppress; // "neverc::suppress"
        }
        break;
      }
      break;
    case 17: // 8 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 6 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'c': // 2 strings to match.
          if (Name[8] != 'o')
            break;
          switch (Name[9]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 10, "e_align", 7) != 0)
              break;
            return AttributeCommonInfo::AT_CodeAlign; // "clang::code_align"
          case 'n':                                   // 1 string to match.
            if (memcmp(Name.data() + 10, "vergent", 7) != 0)
              break;
            return AttributeCommonInfo::AT_Convergent; // "clang::convergent"
          }
          break;
        case 'n': // 2 strings to match.
          if (memcmp(Name.data() + 8, "o_", 2) != 0)
            break;
          switch (Name[10]) {
          default:
            break;
          case 'b': // 1 string to match.
            if (memcmp(Name.data() + 11, "uiltin", 6) != 0)
              break;
            return AttributeCommonInfo::AT_NoBuiltin; // "clang::no_builtin"
          case 'd':                                   // 1 string to match.
            if (memcmp(Name.data() + 11, "estroy", 6) != 0)
              break;
            return AttributeCommonInfo::AT_NoDestroy; // "clang::no_destroy"
          }
          break;
        case 'u': // 1 string to match.
          if (memcmp(Name.data() + 8, "se_handle", 9) != 0)
            break;
          return AttributeCommonInfo::AT_UseHandle; // "clang::use_handle"
        case 'v':                                   // 1 string to match.
          if (memcmp(Name.data() + 8, "ectorcall", 9) != 0)
            break;
          return AttributeCommonInfo::AT_VectorCall; // "clang::vectorcall"
        }
        break;
      case 'n': // 2 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'f': // 1 string to match.
          if (memcmp(Name.data() + 9, "lag_enum", 8) != 0)
            break;
          return AttributeCommonInfo::AT_FlagEnum; // "neverc::flag_enum"
        case 'n':                                  // 1 string to match.
          if (memcmp(Name.data() + 9, "ouwtable", 8) != 0)
            break;
          return AttributeCommonInfo::AT_NoUwtable; // "neverc::nouwtable"
        }
        break;
      }
      break;
    case 18: // 16 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 7 strings to match.
        switch (Name[1]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 2, "rries_dependency", 16) != 0)
            break;
          return AttributeCommonInfo::
              AT_CarriesDependency; // "carries_dependency"
        case 'l':                   // 6 strings to match.
          if (memcmp(Name.data() + 2, "ang::", 5) != 0)
            break;
          switch (Name[7]) {
          default:
            break;
          case 'e': // 1 string to match.
            if (memcmp(Name.data() + 8, "nforce_tcb", 10) != 0)
              break;
            return AttributeCommonInfo::AT_EnforceTCB; // "clang::enforce_tcb"
          case 'm':                                    // 2 strings to match.
            if (Name[8] != 'a')
              break;
            switch (Name[9]) {
            default:
              break;
            case 't': // 1 string to match.
              if (memcmp(Name.data() + 10, "rix_type", 8) != 0)
                break;
              return AttributeCommonInfo::AT_MatrixType; // "clang::matrix_type"
            case 'y':                                    // 1 string to match.
              if (memcmp(Name.data() + 10, "be_undef", 8) != 0)
                break;
              return AttributeCommonInfo::AT_MaybeUndef; // "clang::maybe_undef"
            }
            break;
          case 'n': // 1 string to match.
            if (memcmp(Name.data() + 8, "oduplicate", 10) != 0)
              break;
            return AttributeCommonInfo::AT_NoDuplicate; // "clang::noduplicate"
          case 'u':                                     // 1 string to match.
            if (memcmp(Name.data() + 8, "navailable", 10) != 0)
              break;
            return AttributeCommonInfo::AT_Unavailable; // "clang::unavailable"
          case 'w':                                     // 1 string to match.
            if (memcmp(Name.data() + 8, "eak_import", 10) != 0)
              break;
            return AttributeCommonInfo::AT_WeakImport; // "clang::weak_import"
          }
          break;
        }
        break;
      case 'g': // 3 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 6, "lways_inline", 12) != 0)
            break;
          return AttributeCommonInfo::AT_AlwaysInline; // "gnu::always_inline"
        case 'r':                                      // 1 string to match.
          if (memcmp(Name.data() + 6, "eturns_twice", 12) != 0)
            break;
          return AttributeCommonInfo::AT_ReturnsTwice; // "gnu::returns_twice"
        case 't':                                      // 1 string to match.
          if (memcmp(Name.data() + 6, "arget_clones", 12) != 0)
            break;
          return AttributeCommonInfo::AT_TargetClones; // "gnu::target_clones"
        }
        break;
      case 'n': // 6 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'c': // 2 strings to match.
          if (Name[9] != 'o')
            break;
          switch (Name[10]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 11, "e_align", 7) != 0)
              break;
            return AttributeCommonInfo::AT_CodeAlign; // "neverc::code_align"
          case 'n':                                   // 1 string to match.
            if (memcmp(Name.data() + 11, "vergent", 7) != 0)
              break;
            return AttributeCommonInfo::AT_Convergent; // "neverc::convergent"
          }
          break;
        case 'n': // 2 strings to match.
          if (memcmp(Name.data() + 9, "o_", 2) != 0)
            break;
          switch (Name[11]) {
          default:
            break;
          case 'b': // 1 string to match.
            if (memcmp(Name.data() + 12, "uiltin", 6) != 0)
              break;
            return AttributeCommonInfo::AT_NoBuiltin; // "neverc::no_builtin"
          case 'd':                                   // 1 string to match.
            if (memcmp(Name.data() + 12, "estroy", 6) != 0)
              break;
            return AttributeCommonInfo::AT_NoDestroy; // "neverc::no_destroy"
          }
          break;
        case 'u': // 1 string to match.
          if (memcmp(Name.data() + 9, "se_handle", 9) != 0)
            break;
          return AttributeCommonInfo::AT_UseHandle; // "neverc::use_handle"
        case 'v':                                   // 1 string to match.
          if (memcmp(Name.data() + 9, "ectorcall", 9) != 0)
            break;
          return AttributeCommonInfo::AT_VectorCall; // "neverc::vectorcall"
        }
        break;
      }
      break;
    case 19: // 17 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 7 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 8, "vailability", 11) != 0)
            break;
          return AttributeCommonInfo::AT_Availability; // "clang::availability"
        case 'b':                                      // 2 strings to match.
          if (memcmp(Name.data() + 8, "tf_", 3) != 0)
            break;
          switch (Name[11]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 12, "ecl_tag", 7) != 0)
              break;
            return AttributeCommonInfo::AT_BTFDeclTag; // "clang::btf_decl_tag"
          case 't':                                    // 1 string to match.
            if (memcmp(Name.data() + 12, "ype_tag", 7) != 0)
              break;
            return AttributeCommonInfo::AT_BTFTypeTag; // "clang::btf_type_tag"
          }
          break;
        case 'c': // 2 strings to match.
          if (memcmp(Name.data() + 8, "pu_", 3) != 0)
            break;
          switch (Name[11]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 12, "ispatch", 7) != 0)
              break;
            return AttributeCommonInfo::AT_CPUDispatch; // "clang::cpu_dispatch"
          case 's':                                     // 1 string to match.
            if (memcmp(Name.data() + 12, "pecific", 7) != 0)
              break;
            return AttributeCommonInfo::AT_CPUSpecific; // "clang::cpu_specific"
          }
          break;
        case 'o': // 1 string to match.
          if (memcmp(Name.data() + 8, "verloadable", 11) != 0)
            break;
          return AttributeCommonInfo::AT_Overloadable; // "clang::overloadable"
        case 'p':                                      // 1 string to match.
          if (memcmp(Name.data() + 8, "reserve_all", 11) != 0)
            break;
          return AttributeCommonInfo::AT_PreserveAll; // "clang::preserve_all"
        }
        break;
      case 'g': // 3 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 6, "ssume_aligned", 13) != 0)
            break;
          return AttributeCommonInfo::AT_AssumeAligned; // "gnu::assume_aligned"
        case 'n':                                       // 1 string to match.
          if (memcmp(Name.data() + 6, "o_split_stack", 13) != 0)
            break;
          return AttributeCommonInfo::AT_NoSplitStack; // "gnu::no_split_stack"
        case 't':                                      // 1 string to match.
          if (memcmp(Name.data() + 6, "arget_version", 13) != 0)
            break;
          return AttributeCommonInfo::AT_TargetVersion; // "gnu::target_version"
        }
        break;
      case 'n': // 7 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 9, "nforce_tcb", 10) != 0)
            break;
          return AttributeCommonInfo::AT_EnforceTCB; // "neverc::enforce_tcb"
        case 'f':                                    // 1 string to match.
          if (memcmp(Name.data() + 9, "allthrough", 10) != 0)
            break;
          return AttributeCommonInfo::AT_FallThrough; // "neverc::fallthrough"
        case 'm':                                     // 2 strings to match.
          if (Name[9] != 'a')
            break;
          switch (Name[10]) {
          default:
            break;
          case 't': // 1 string to match.
            if (memcmp(Name.data() + 11, "rix_type", 8) != 0)
              break;
            return AttributeCommonInfo::AT_MatrixType; // "neverc::matrix_type"
          case 'y':                                    // 1 string to match.
            if (memcmp(Name.data() + 11, "be_undef", 8) != 0)
              break;
            return AttributeCommonInfo::AT_MaybeUndef; // "neverc::maybe_undef"
          }
          break;
        case 'n': // 1 string to match.
          if (memcmp(Name.data() + 9, "oduplicate", 10) != 0)
            break;
          return AttributeCommonInfo::AT_NoDuplicate; // "neverc::noduplicate"
        case 'u':                                     // 1 string to match.
          if (memcmp(Name.data() + 9, "navailable", 10) != 0)
            break;
          return AttributeCommonInfo::AT_Unavailable; // "neverc::unavailable"
        case 'w':                                     // 1 string to match.
          if (memcmp(Name.data() + 9, "eak_import", 10) != 0)
            break;
          return AttributeCommonInfo::AT_WeakImport; // "neverc::weak_import"
        }
        break;
      }
      break;
    case 20: // 12 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 3 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 8, "ddress_space", 12) != 0)
            break;
          return AttributeCommonInfo::AT_AddressSpace; // "clang::address_space"
        case 'p':                                      // 1 string to match.
          if (memcmp(Name.data() + 8, "reserve_most", 12) != 0)
            break;
          return AttributeCommonInfo::AT_PreserveMost; // "clang::preserve_most"
        case 'u':                                      // 1 string to match.
          if (memcmp(Name.data() + 8, "ninitialized", 12) != 0)
            break;
          return AttributeCommonInfo::
              AT_Uninitialized; // "clang::uninitialized"
        }
        break;
      case 'g': // 2 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'f': // 1 string to match.
          if (memcmp(Name.data() + 6, "unction_return", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_FunctionReturnThunks; // "gnu::function_return"
        case 'r':                      // 1 string to match.
          if (memcmp(Name.data() + 6, "eturns_nonnull", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_ReturnsNonNull; // "gnu::returns_nonnull"
        }
        break;
      case 'n': // 7 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 9, "vailability", 11) != 0)
            break;
          return AttributeCommonInfo::AT_Availability; // "neverc::availability"
        case 'b':                                      // 2 strings to match.
          if (memcmp(Name.data() + 9, "tf_", 3) != 0)
            break;
          switch (Name[12]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 13, "ecl_tag", 7) != 0)
              break;
            return AttributeCommonInfo::AT_BTFDeclTag; // "neverc::btf_decl_tag"
          case 't':                                    // 1 string to match.
            if (memcmp(Name.data() + 13, "ype_tag", 7) != 0)
              break;
            return AttributeCommonInfo::AT_BTFTypeTag; // "neverc::btf_type_tag"
          }
          break;
        case 'c': // 2 strings to match.
          if (memcmp(Name.data() + 9, "pu_", 3) != 0)
            break;
          switch (Name[12]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 13, "ispatch", 7) != 0)
              break;
            return AttributeCommonInfo::
                AT_CPUDispatch; // "neverc::cpu_dispatch"
          case 's':             // 1 string to match.
            if (memcmp(Name.data() + 13, "pecific", 7) != 0)
              break;
            return AttributeCommonInfo::
                AT_CPUSpecific; // "neverc::cpu_specific"
          }
          break;
        case 'o': // 1 string to match.
          if (memcmp(Name.data() + 9, "verloadable", 11) != 0)
            break;
          return AttributeCommonInfo::AT_Overloadable; // "neverc::overloadable"
        case 'p':                                      // 1 string to match.
          if (memcmp(Name.data() + 9, "reserve_all", 11) != 0)
            break;
          return AttributeCommonInfo::AT_PreserveAll; // "neverc::preserve_all"
        }
        break;
      }
      break;
    case 21: // 12 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 4 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'a': // 2 strings to match.
          switch (Name[8]) {
          default:
            break;
          case 'c': // 1 string to match.
            if (memcmp(Name.data() + 9, "quire_handle", 12) != 0)
              break;
            return AttributeCommonInfo::
                AT_AcquireHandle; // "clang::acquire_handle"
          case 'l':               // 1 string to match.
            if (memcmp(Name.data() + 9, "ways_destroy", 12) != 0)
              break;
            return AttributeCommonInfo::
                AT_AlwaysDestroy; // "clang::always_destroy"
          }
          break;
        case 'p': // 1 string to match.
          if (memcmp(Name.data() + 8, "referred_type", 13) != 0)
            break;
          return AttributeCommonInfo::
              AT_PreferredType; // "clang::preferred_type"
        case 'r':               // 1 string to match.
          if (memcmp(Name.data() + 8, "elease_handle", 13) != 0)
            break;
          return AttributeCommonInfo::
              AT_ReleaseHandle; // "clang::release_handle"
        }
        break;
      case 'g': // 2 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'd': // 1 string to match.
          if (memcmp(Name.data() + 6, "isable_try_stmt", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_DisableTryStmt; // "gnu::disable_try_stmt"
        case 'r':                // 1 string to match.
          if (memcmp(Name.data() + 6, "andomize_layout", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_RandomizeLayout; // "gnu::randomize_layout"
        }
        break;
      case 'n': // 6 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 3 strings to match.
          switch (Name[9]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 10, "dress_space", 11) != 0)
              break;
            return AttributeCommonInfo::
                AT_AddressSpace; // "neverc::address_space"
          case 'l':              // 1 string to match.
            if (memcmp(Name.data() + 10, "ways_inline", 11) != 0)
              break;
            return AttributeCommonInfo::
                AT_AlwaysInline; // "neverc::always_inline"
          case 'n':              // 1 string to match.
            if (memcmp(Name.data() + 10, "notate_type", 11) != 0)
              break;
            return AttributeCommonInfo::
                AT_AnnotateType; // "neverc::annotate_type"
          }
          break;
        case 'b': // 1 string to match.
          if (memcmp(Name.data() + 9, "uiltin_alias", 12) != 0)
            break;
          return AttributeCommonInfo::
              AT_BuiltinAlias; // "neverc::builtin_alias"
        case 'p':              // 1 string to match.
          if (memcmp(Name.data() + 9, "reserve_most", 12) != 0)
            break;
          return AttributeCommonInfo::
              AT_PreserveMost; // "neverc::preserve_most"
        case 'u':              // 1 string to match.
          if (memcmp(Name.data() + 9, "ninitialized", 12) != 0)
            break;
          return AttributeCommonInfo::
              AT_Uninitialized; // "neverc::uninitialized"
        }
        break;
      }
      break;
    case 22: // 8 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 3 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 8, "arch64_sve_pcs", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_AArch64SVEPcs; // "clang::aarch64_sve_pcs"
        case 'n':               // 1 string to match.
          if (memcmp(Name.data() + 8, "ot_tail_called", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_NotTailCalled; // "clang::not_tail_called"
        case 't':               // 1 string to match.
          if (memcmp(Name.data() + 8, "ype_visibility", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_TypeVisibility; // "clang::type_visibility"
        }
        break;
      case 'g': // 1 string to match.
        if (memcmp(Name.data() + 1, "nu::transparent_union", 21) != 0)
          break;
        return AttributeCommonInfo::
            AT_TransparentUnion; // "gnu::transparent_union"
      case 'n':                  // 4 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 2 strings to match.
          switch (Name[9]) {
          default:
            break;
          case 'c': // 1 string to match.
            if (memcmp(Name.data() + 10, "quire_handle", 12) != 0)
              break;
            return AttributeCommonInfo::
                AT_AcquireHandle; // "neverc::acquire_handle"
          case 'l':               // 1 string to match.
            if (memcmp(Name.data() + 10, "ways_destroy", 12) != 0)
              break;
            return AttributeCommonInfo::
                AT_AlwaysDestroy; // "neverc::always_destroy"
          }
          break;
        case 'p': // 1 string to match.
          if (memcmp(Name.data() + 9, "referred_type", 13) != 0)
            break;
          return AttributeCommonInfo::
              AT_PreferredType; // "neverc::preferred_type"
        case 'r':               // 1 string to match.
          if (memcmp(Name.data() + 9, "elease_handle", 13) != 0)
            break;
          return AttributeCommonInfo::
              AT_ReleaseHandle; // "neverc::release_handle"
        }
        break;
      }
      break;
    case 23: // 10 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 5 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 8, "nforce_tcb_leaf", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_EnforceTCBLeaf; // "clang::enforce_tcb_leaf"
        case 'i':                // 1 string to match.
          if (memcmp(Name.data() + 8, "nternal_linkage", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_InternalLinkage; // "clang::internal_linkage"
        case 'm':                 // 1 string to match.
          if (memcmp(Name.data() + 8, "in_vector_width", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_MinVectorWidth; // "clang::min_vector_width"
        case 'n':                // 1 string to match.
          if (memcmp(Name.data() + 8, "eon_vector_type", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_NeonVectorType; // "clang::neon_vector_type"
        case 'p':                // 1 string to match.
          if (memcmp(Name.data() + 8, "ass_object_size", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_PassObjectSize; // "clang::pass_object_size"
        }
        break;
      case 'g': // 2 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'n': // 1 string to match.
          if (memcmp(Name.data() + 6, "o_stack_protector", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_NoStackProtector; // "gnu::no_stack_protector"
        case 'w':                  // 1 string to match.
          if (memcmp(Name.data() + 6, "arn_unused_result", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_WarnUnusedResult; // "gnu::warn_unused_result"
        }
        break;
      case 'n': // 3 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 9, "arch64_sve_pcs", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_AArch64SVEPcs; // "neverc::aarch64_sve_pcs"
        case 'n':               // 1 string to match.
          if (memcmp(Name.data() + 9, "ot_tail_called", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_NotTailCalled; // "neverc::not_tail_called"
        case 't':               // 1 string to match.
          if (memcmp(Name.data() + 9, "ype_visibility", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_TypeVisibility; // "neverc::type_visibility"
        }
        break;
      }
      break;
    case 24: // 7 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'g': // 2 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'n': // 1 string to match.
          if (memcmp(Name.data() + 6, "o_randomize_layout", 18) != 0)
            break;
          return AttributeCommonInfo::
              AT_NoRandomizeLayout; // "gnu::no_randomize_layout"
        case 'z':                   // 1 string to match.
          if (memcmp(Name.data() + 6, "ero_call_used_regs", 18) != 0)
            break;
          return AttributeCommonInfo::
              AT_ZeroCallUsedRegs; // "gnu::zero_call_used_regs"
        }
        break;
      case 'n': // 5 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 9, "nforce_tcb_leaf", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_EnforceTCBLeaf; // "neverc::enforce_tcb_leaf"
        case 'i':                // 1 string to match.
          if (memcmp(Name.data() + 9, "nternal_linkage", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_InternalLinkage; // "neverc::internal_linkage"
        case 'm':                 // 1 string to match.
          if (memcmp(Name.data() + 9, "in_vector_width", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_MinVectorWidth; // "neverc::min_vector_width"
        case 'n':                // 1 string to match.
          if (memcmp(Name.data() + 9, "eon_vector_type", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_NeonVectorType; // "neverc::neon_vector_type"
        case 'p':                // 1 string to match.
          if (memcmp(Name.data() + 9, "ass_object_size", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_PassObjectSize; // "neverc::pass_object_size"
        }
        break;
      }
      break;
    case 25: // 4 strings to match.
      if (memcmp(Name.data() + 0, "clang::", 7) != 0)
        break;
      switch (Name[7]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 8, "arch64_vector_pcs", 17) != 0)
          break;
        return AttributeCommonInfo::
            AT_AArch64VectorPcs; // "clang::aarch64_vector_pcs"
      case 'd':                  // 1 string to match.
        if (memcmp(Name.data() + 8, "isable_tail_calls", 17) != 0)
          break;
        return AttributeCommonInfo::
            AT_DisableTailCalls; // "clang::disable_tail_calls"
      case 'e':                  // 1 string to match.
        if (memcmp(Name.data() + 8, "num_extensibility", 17) != 0)
          break;
        return AttributeCommonInfo::
            AT_EnumExtensibility; // "clang::enum_extensibility"
      case 'n':                   // 1 string to match.
        if (memcmp(Name.data() + 8, "o_stack_protector", 17) != 0)
          break;
        return AttributeCommonInfo::
            AT_NoStackProtector; // "clang::no_stack_protector"
      }
      break;
    case 26: // 7 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 2 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'd': // 1 string to match.
          if (memcmp(Name.data() + 8, "iagnose_as_builtin", 18) != 0)
            break;
          return AttributeCommonInfo::
              AT_DiagnoseAsBuiltin; // "clang::diagnose_as_builtin"
        case 'u':                   // 1 string to match.
          if (memcmp(Name.data() + 8, "nsafe_buffer_usage", 18) != 0)
            break;
          return AttributeCommonInfo::
              AT_UnsafeBufferUsage; // "clang::unsafe_buffer_usage"
        }
        break;
      case 'n': // 5 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 9, "arch64_vector_pcs", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_AArch64VectorPcs; // "neverc::aarch64_vector_pcs"
        case 'd':                  // 1 string to match.
          if (memcmp(Name.data() + 9, "isable_tail_calls", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_DisableTailCalls; // "neverc::disable_tail_calls"
        case 'e':                  // 1 string to match.
          if (memcmp(Name.data() + 9, "num_extensibility", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_EnumExtensibility; // "neverc::enum_extensibility"
        case 'n':                   // 1 string to match.
          if (memcmp(Name.data() + 9, "o_stack_protector", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_NoStackProtector; // "neverc::no_stack_protector"
        case 'w':                  // 1 string to match.
          if (memcmp(Name.data() + 9, "arn_unused_result", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_WarnUnusedResult; // "neverc::warn_unused_result"
        }
        break;
      }
      break;
    case 27: // 4 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 2 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'l': // 1 string to match.
          if (memcmp(Name.data() + 8, "oader_uninitialized", 19) != 0)
            break;
          return AttributeCommonInfo::
              AT_LoaderUninitialized; // "clang::loader_uninitialized"
        case 'n':                     // 1 string to match.
          if (memcmp(Name.data() + 8, "eon_polyvector_type", 19) != 0)
            break;
          return AttributeCommonInfo::
              AT_NeonPolyVectorType; // "clang::neon_polyvector_type"
        }
        break;
      case 'n': // 2 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'd': // 1 string to match.
          if (memcmp(Name.data() + 9, "iagnose_as_builtin", 18) != 0)
            break;
          return AttributeCommonInfo::
              AT_DiagnoseAsBuiltin; // "neverc::diagnose_as_builtin"
        case 'u':                   // 1 string to match.
          if (memcmp(Name.data() + 9, "nsafe_buffer_usage", 18) != 0)
            break;
          return AttributeCommonInfo::
              AT_UnsafeBufferUsage; // "neverc::unsafe_buffer_usage"
        }
        break;
      }
      break;
    case 28: // 6 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 3 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'l': // 1 string to match.
          if (memcmp(Name.data() + 8, "to_visibility_public", 20) != 0)
            break;
          return AttributeCommonInfo::
              AT_LTOVisibilityPublic; // "clang::lto_visibility_public"
        case 'p':                     // 1 string to match.
          if (memcmp(Name.data() + 8, "ointer_with_type_tag", 20) != 0)
            break;
          return AttributeCommonInfo::
              AT_ArgumentWithTypeTag; // "clang::pointer_with_type_tag"
        case 't':                     // 1 string to match.
          if (memcmp(Name.data() + 8, "ype_tag_for_datatype", 20) != 0)
            break;
          return AttributeCommonInfo::
              AT_TypeTagForDatatype; // "clang::type_tag_for_datatype"
        }
        break;
      case 'g': // 1 string to match.
        if (memcmp(Name.data() + 1, "nu::force_align_arg_pointer", 27) != 0)
          break;
        return AttributeCommonInfo::
            AT_X86ForceAlignArgPointer; // "gnu::force_align_arg_pointer"
      case 'n':                         // 2 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'l': // 1 string to match.
          if (memcmp(Name.data() + 9, "oader_uninitialized", 19) != 0)
            break;
          return AttributeCommonInfo::
              AT_LoaderUninitialized; // "neverc::loader_uninitialized"
        case 'n':                     // 1 string to match.
          if (memcmp(Name.data() + 9, "eon_polyvector_type", 19) != 0)
            break;
          return AttributeCommonInfo::
              AT_NeonPolyVectorType; // "neverc::neon_polyvector_type"
        }
        break;
      }
      break;
    case 29: // 5 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 1 string to match.
        if (memcmp(Name.data() + 1, "lang::argument_with_type_tag", 28) != 0)
          break;
        return AttributeCommonInfo::
            AT_ArgumentWithTypeTag; // "clang::argument_with_type_tag"
      case 'g':                     // 1 string to match.
        if (memcmp(Name.data() + 1, "nu::patchable_function_entry", 28) != 0)
          break;
        return AttributeCommonInfo::
            AT_PatchableFunctionEntry; // "gnu::patchable_function_entry"
      case 'n':                        // 3 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'l': // 1 string to match.
          if (memcmp(Name.data() + 9, "to_visibility_public", 20) != 0)
            break;
          return AttributeCommonInfo::
              AT_LTOVisibilityPublic; // "neverc::lto_visibility_public"
        case 'p':                     // 1 string to match.
          if (memcmp(Name.data() + 9, "ointer_with_type_tag", 20) != 0)
            break;
          return AttributeCommonInfo::
              AT_ArgumentWithTypeTag; // "neverc::pointer_with_type_tag"
        case 't':                     // 1 string to match.
          if (memcmp(Name.data() + 9, "ype_tag_for_datatype", 20) != 0)
            break;
          return AttributeCommonInfo::
              AT_TypeTagForDatatype; // "neverc::type_tag_for_datatype"
        }
        break;
      }
      break;
    case 30: // 2 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'g': // 1 string to match.
        if (memcmp(Name.data() + 1, "nu::no_caller_saved_registers", 29) != 0)
          break;
        return AttributeCommonInfo::
            AT_AnyX86NoCallerSavedRegisters; // "gnu::no_caller_saved_registers"
      case 'n':                              // 1 string to match.
        if (memcmp(Name.data() + 1, "everc::argument_with_type_tag", 29) != 0)
          break;
        return AttributeCommonInfo::
            AT_ArgumentWithTypeTag; // "neverc::argument_with_type_tag"
      }
      break;
    case 31: // 1 string to match.
      if (memcmp(Name.data() + 0, "clang::pass_dynamic_object_size", 31) != 0)
        break;
      return AttributeCommonInfo::
          AT_PassObjectSize; // "clang::pass_dynamic_object_size"
    case 32:                 // 1 string to match.
      if (memcmp(Name.data() + 0, "neverc::pass_dynamic_object_size", 32) != 0)
        break;
      return AttributeCommonInfo::
          AT_PassObjectSize; // "neverc::pass_dynamic_object_size"
    case 33:                 // 2 strings to match.
      if (memcmp(Name.data() + 0, "clang::", 7) != 0)
        break;
      switch (Name[7]) {
      default:
        break;
      case '_': // 1 string to match.
        if (memcmp(Name.data() + 8, "_neverc_arm_builtin_alias", 25) != 0)
          break;
        return AttributeCommonInfo::
            AT_ArmBuiltinAlias; // "clang::__neverc_arm_builtin_alias"
      case 's':                 // 1 string to match.
        if (memcmp(Name.data() + 8, "peculative_load_hardening", 25) != 0)
          break;
        return AttributeCommonInfo::
            AT_SpeculativeLoadHardening; // "clang::speculative_load_hardening"
      }
      break;
    case 34: // 3 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 1 string to match.
        if (memcmp(Name.data() + 1, "lang::enforce_read_only_placement", 33) !=
            0)
          break;
        return AttributeCommonInfo::
            AT_ReadOnlyPlacement; // "clang::enforce_read_only_placement"
      case 'n':                   // 2 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case '_': // 1 string to match.
          if (memcmp(Name.data() + 9, "_neverc_arm_builtin_alias", 25) != 0)
            break;
          return AttributeCommonInfo::
              AT_ArmBuiltinAlias; // "neverc::__neverc_arm_builtin_alias"
        case 's':                 // 1 string to match.
          if (memcmp(Name.data() + 9, "peculative_load_hardening", 25) != 0)
            break;
          return AttributeCommonInfo::
              AT_SpeculativeLoadHardening; // "neverc::speculative_load_hardening"
        }
        break;
      }
      break;
    case 35: // 1 string to match.
      if (memcmp(Name.data() + 0, "neverc::enforce_read_only_placement", 35) !=
          0)
        break;
      return AttributeCommonInfo::
          AT_ReadOnlyPlacement; // "neverc::enforce_read_only_placement"
    case 36:                    // 1 string to match.
      if (memcmp(Name.data() + 0, "clang::no_speculative_load_hardening", 36) !=
          0)
        break;
      return AttributeCommonInfo::
          AT_NoSpeculativeLoadHardening; // "clang::no_speculative_load_hardening"
    case 37:                             // 1 string to match.
      if (memcmp(Name.data() + 0, "neverc::no_speculative_load_hardening",
                 37) != 0)
        break;
      return AttributeCommonInfo::
          AT_NoSpeculativeLoadHardening; // "neverc::no_speculative_load_hardening"
    case 44:                             // 1 string to match.
      if (memcmp(Name.data() + 0,
                 "clang::available_only_in_default_eval_method", 44) != 0)
        break;
      return AttributeCommonInfo::
          AT_AvailableOnlyInDefaultEvalMethod; // "clang::available_only_in_default_eval_method"
    case 45:                                   // 1 string to match.
      if (memcmp(Name.data() + 0,
                 "neverc::available_only_in_default_eval_method", 45) != 0)
        break;
      return AttributeCommonInfo::
          AT_AvailableOnlyInDefaultEvalMethod; // "neverc::available_only_in_default_eval_method"
    }
  } else if (AttributeCommonInfo::AS_C23 == Syntax) {
    switch (Name.size()) {
    default:
      break;
    case 8: // 2 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'g': // 1 string to match.
        if (memcmp(Name.data() + 1, "nu::hot", 7) != 0)
          break;
        return AttributeCommonInfo::AT_Hot; // "gnu::hot"
      case 'n':                             // 1 string to match.
        if (memcmp(Name.data() + 1, "oreturn", 7) != 0)
          break;
        return AttributeCommonInfo::AT_StandardNoReturn; // "noreturn"
      }
      break;
    case 9: // 8 strings to match.
      switch (Name[0]) {
      default:
        break;
      case '_': // 1 string to match.
        if (memcmp(Name.data() + 1, "Noreturn", 8) != 0)
          break;
        return AttributeCommonInfo::AT_StandardNoReturn; // "_Noreturn"
      case 'g':                                          // 6 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'c': // 1 string to match.
          if (memcmp(Name.data() + 6, "old", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Cold; // "gnu::cold"
        case 'l':                              // 1 string to match.
          if (memcmp(Name.data() + 6, "eaf", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Leaf; // "gnu::leaf"
        case 'm':                              // 1 string to match.
          if (memcmp(Name.data() + 6, "ode", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Mode; // "gnu::mode"
        case 'p':                              // 1 string to match.
          if (memcmp(Name.data() + 6, "ure", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Pure; // "gnu::pure"
        case 'u':                              // 1 string to match.
          if (memcmp(Name.data() + 6, "sed", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Used; // "gnu::used"
        case 'w':                              // 1 string to match.
          if (memcmp(Name.data() + 6, "eak", 3) != 0)
            break;
          return AttributeCommonInfo::AT_Weak; // "gnu::weak"
        }
        break;
      case 'n': // 1 string to match.
        if (memcmp(Name.data() + 1, "odiscard", 8) != 0)
          break;
        return AttributeCommonInfo::AT_WarnUnusedResult; // "nodiscard"
      }
      break;
    case 10: // 7 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'd': // 1 string to match.
        if (memcmp(Name.data() + 1, "eprecated", 9) != 0)
          break;
        return AttributeCommonInfo::AT_Deprecated; // "deprecated"
      case 'g':                                    // 6 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 6, "lias", 4) != 0)
            break;
          return AttributeCommonInfo::AT_Alias; // "gnu::alias"
        case 'c':                               // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 7, "ecl", 3) != 0)
              break;
            return AttributeCommonInfo::AT_CDecl; // "gnu::cdecl"
          case 'o':                               // 1 string to match.
            if (memcmp(Name.data() + 7, "nst", 3) != 0)
              break;
            return AttributeCommonInfo::AT_Const; // "gnu::const"
          }
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 6, "rror", 4) != 0)
            break;
          return AttributeCommonInfo::AT_Error; // "gnu::error"
        case 'i':                               // 1 string to match.
          if (memcmp(Name.data() + 6, "func", 4) != 0)
            break;
          return AttributeCommonInfo::AT_IFunc; // "gnu::ifunc"
        case 'n':                               // 1 string to match.
          if (memcmp(Name.data() + 6, "aked", 4) != 0)
            break;
          return AttributeCommonInfo::AT_Naked; // "gnu::naked"
        }
        break;
      }
      break;
    case 11: // 9 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'f': // 1 string to match.
        if (memcmp(Name.data() + 1, "allthrough", 10) != 0)
          break;
        return AttributeCommonInfo::AT_FallThrough; // "fallthrough"
      case 'g':                                     // 8 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'c': // 1 string to match.
          if (memcmp(Name.data() + 6, "ommon", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Common; // "gnu::common"
        case 'f':                                // 1 string to match.
          if (memcmp(Name.data() + 6, "ormat", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Format; // "gnu::format"
        case 'm':                                // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'a': // 1 string to match.
            if (memcmp(Name.data() + 7, "lloc", 4) != 0)
              break;
            return AttributeCommonInfo::AT_Restrict; // "gnu::malloc"
          case 's':                                  // 1 string to match.
            if (memcmp(Name.data() + 7, "_abi", 4) != 0)
              break;
            return AttributeCommonInfo::AT_MSABI; // "gnu::ms_abi"
          }
          break;
        case 'p': // 1 string to match.
          if (memcmp(Name.data() + 6, "acked", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Packed; // "gnu::packed"
        case 'r':                                // 1 string to match.
          if (memcmp(Name.data() + 6, "etain", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Retain; // "gnu::retain"
        case 't':                                // 1 string to match.
          if (memcmp(Name.data() + 6, "arget", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Target; // "gnu::target"
        case 'u':                                // 1 string to match.
          if (memcmp(Name.data() + 6, "nused", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Unused; // "gnu::unused"
        }
        break;
      }
      break;
    case 12: // 15 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 1 string to match.
        if (memcmp(Name.data() + 1, "lang::guard", 11) != 0)
          break;
        return AttributeCommonInfo::AT_CFGuard; // "clang::guard"
      case 'g':                                 // 13 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case '_': // 1 string to match.
          if (memcmp(Name.data() + 6, "_const", 6) != 0)
            break;
          return AttributeCommonInfo::AT_Const; // "gnu::__const"
        case 'a':                               // 1 string to match.
          if (memcmp(Name.data() + 6, "ligned", 6) != 0)
            break;
          return AttributeCommonInfo::AT_Aligned; // "gnu::aligned"
        case 'c':                                 // 1 string to match.
          if (memcmp(Name.data() + 6, "leanup", 6) != 0)
            break;
          return AttributeCommonInfo::AT_Cleanup; // "gnu::cleanup"
        case 'f':                                 // 1 string to match.
          if (memcmp(Name.data() + 6, "latten", 6) != 0)
            break;
          return AttributeCommonInfo::AT_Flatten; // "gnu::flatten"
        case 'n':                                 // 3 strings to match.
          if (Name[6] != 'o')
            break;
          switch (Name[7]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 8, "ebug", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NoDebug; // "gnu::nodebug"
          case 'n':                                 // 1 string to match.
            if (memcmp(Name.data() + 8, "null", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NonNull; // "gnu::nonnull"
          case 't':                                 // 1 string to match.
            if (memcmp(Name.data() + 8, "hrow", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NoThrow; // "gnu::nothrow"
          }
          break;
        case 'r': // 2 strings to match.
          if (memcmp(Name.data() + 6, "eg", 2) != 0)
            break;
          switch (Name[8]) {
          default:
            break;
          case 'c': // 1 string to match.
            if (memcmp(Name.data() + 9, "all", 3) != 0)
              break;
            return AttributeCommonInfo::AT_RegCall; // "gnu::regcall"
          case 'p':                                 // 1 string to match.
            if (memcmp(Name.data() + 9, "arm", 3) != 0)
              break;
            return AttributeCommonInfo::AT_Regparm; // "gnu::regparm"
          }
          break;
        case 's': // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'e': // 1 string to match.
            if (memcmp(Name.data() + 7, "ction", 5) != 0)
              break;
            return AttributeCommonInfo::AT_Section; // "gnu::section"
          case 't':                                 // 1 string to match.
            if (memcmp(Name.data() + 7, "dcall", 5) != 0)
              break;
            return AttributeCommonInfo::AT_StdCall; // "gnu::stdcall"
          }
          break;
        case 'w': // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'a': // 1 string to match.
            if (memcmp(Name.data() + 7, "rning", 5) != 0)
              break;
            return AttributeCommonInfo::AT_Error; // "gnu::warning"
          case 'e':                               // 1 string to match.
            if (memcmp(Name.data() + 7, "akref", 5) != 0)
              break;
            return AttributeCommonInfo::AT_WeakRef; // "gnu::weakref"
          }
          break;
        }
        break;
      case 'm': // 1 string to match.
        if (memcmp(Name.data() + 1, "aybe_unused", 11) != 0)
          break;
        return AttributeCommonInfo::AT_Unused; // "maybe_unused"
      }
      break;
    case 13: // 10 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 1 string to match.
        if (memcmp(Name.data() + 1, "lang::assume", 12) != 0)
          break;
        return AttributeCommonInfo::AT_Assumption; // "clang::assume"
      case 'g':                                    // 8 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'f': // 1 string to match.
          if (memcmp(Name.data() + 6, "astcall", 7) != 0)
            break;
          return AttributeCommonInfo::AT_FastCall; // "gnu::fastcall"
        case 'n':                                  // 3 strings to match.
          if (Name[6] != 'o')
            break;
          switch (Name[7]) {
          default:
            break;
          case 'c': // 1 string to match.
            if (memcmp(Name.data() + 8, "ommon", 5) != 0)
              break;
            return AttributeCommonInfo::AT_NoCommon; // "gnu::nocommon"
          case 'i':                                  // 1 string to match.
            if (memcmp(Name.data() + 8, "nline", 5) != 0)
              break;
            return AttributeCommonInfo::AT_NoInline; // "gnu::noinline"
          case 'r':                                  // 1 string to match.
            if (memcmp(Name.data() + 8, "eturn", 5) != 0)
              break;
            return AttributeCommonInfo::AT_NoReturn; // "gnu::noreturn"
          }
          break;
        case 'o': // 1 string to match.
          if (memcmp(Name.data() + 6, "verride", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Override; // "gnu::override"
        case 's':                                  // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'e': // 1 string to match.
            if (memcmp(Name.data() + 7, "ntinel", 6) != 0)
              break;
            return AttributeCommonInfo::AT_Sentinel; // "gnu::sentinel"
          case 'y':                                  // 1 string to match.
            if (memcmp(Name.data() + 7, "sv_abi", 6) != 0)
              break;
            return AttributeCommonInfo::AT_SysVABI; // "gnu::sysv_abi"
          }
          break;
        case 'v': // 1 string to match.
          if (memcmp(Name.data() + 6, "olatile", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Volatile; // "gnu::volatile"
        }
        break;
      case 'n': // 1 string to match.
        if (memcmp(Name.data() + 1, "everc::guard", 12) != 0)
          break;
        return AttributeCommonInfo::AT_CFGuard; // "neverc::guard"
      }
      break;
    case 14: // 13 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 4 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'm': // 1 string to match.
          if (memcmp(Name.data() + 8, "insize", 6) != 0)
            break;
          return AttributeCommonInfo::AT_MinSize; // "clang::minsize"
        case 'n':                                 // 2 strings to match.
          if (Name[8] != 'o')
            break;
          switch (Name[9]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 10, "eref", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NoDeref; // "clang::noderef"
          case 'm':                                 // 1 string to match.
            if (memcmp(Name.data() + 10, "erge", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NoMerge; // "clang::nomerge"
          }
          break;
        case 'o': // 1 string to match.
          if (memcmp(Name.data() + 8, "ptnone", 6) != 0)
            break;
          return AttributeCommonInfo::AT_OptimizeNone; // "clang::optnone"
        }
        break;
      case 'g': // 7 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'd': // 2 strings to match.
          if (memcmp(Name.data() + 6, "ll", 2) != 0)
            break;
          switch (Name[8]) {
          default:
            break;
          case 'e': // 1 string to match.
            if (memcmp(Name.data() + 9, "xport", 5) != 0)
              break;
            return AttributeCommonInfo::AT_DLLExport; // "gnu::dllexport"
          case 'i':                                   // 1 string to match.
            if (memcmp(Name.data() + 9, "mport", 5) != 0)
              break;
            return AttributeCommonInfo::AT_DLLImport; // "gnu::dllimport"
          }
          break;
        case 'i': // 1 string to match.
          if (memcmp(Name.data() + 6, "nterrupt", 8) != 0)
            break;
          return AttributeCommonInfo::AT_Interrupt; // "gnu::interrupt"
        case 'm':                                   // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'a': // 1 string to match.
            if (memcmp(Name.data() + 7, "y_alias", 7) != 0)
              break;
            return AttributeCommonInfo::AT_MayAlias; // "gnu::may_alias"
          case 's':                                  // 1 string to match.
            if (memcmp(Name.data() + 7, "_struct", 7) != 0)
              break;
            return AttributeCommonInfo::AT_MSStruct; // "gnu::ms_struct"
          }
          break;
        case 's': // 1 string to match.
          if (memcmp(Name.data() + 6, "electany", 8) != 0)
            break;
          return AttributeCommonInfo::AT_SelectAny; // "gnu::selectany"
        case 't':                                   // 1 string to match.
          if (memcmp(Name.data() + 6, "ls_model", 8) != 0)
            break;
          return AttributeCommonInfo::AT_TLSModel; // "gnu::tls_model"
        }
        break;
      case 'n': // 2 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 9, "ssume", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Assumption; // "neverc::assume"
        case 'l':                                    // 1 string to match.
          if (memcmp(Name.data() + 9, "ikely", 5) != 0)
            break;
          return AttributeCommonInfo::AT_Likely; // "neverc::likely"
        }
        break;
      }
      break;
    case 15: // 17 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 5 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 8, "nnotate", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Annotate; // "clang::annotate"
        case 'c':                                  // 1 string to match.
          if (memcmp(Name.data() + 8, "allback", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Callback; // "clang::callback"
        case 'm':                                  // 1 string to match.
          if (memcmp(Name.data() + 8, "usttail", 7) != 0)
            break;
          return AttributeCommonInfo::AT_MustTail; // "clang::musttail"
        case 'n':                                  // 1 string to match.
          if (memcmp(Name.data() + 8, "oescape", 7) != 0)
            break;
          return AttributeCommonInfo::AT_NoEscape; // "clang::noescape"
        case 's':                                  // 1 string to match.
          if (memcmp(Name.data() + 8, "uppress", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Suppress; // "clang::suppress"
        }
        break;
      case 'g': // 8 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'a': // 2 strings to match.
          switch (Name[6]) {
          default:
            break;
          case 'l': // 1 string to match.
            if (memcmp(Name.data() + 7, "loc_size", 8) != 0)
              break;
            return AttributeCommonInfo::AT_AllocSize; // "gnu::alloc_size"
          case 'r':                                   // 1 string to match.
            if (memcmp(Name.data() + 7, "tificial", 8) != 0)
              break;
            return AttributeCommonInfo::AT_Artificial; // "gnu::artificial"
          }
          break;
        case 'd': // 2 strings to match.
          if (Name[6] != 'e')
            break;
          switch (Name[7]) {
          default:
            break;
          case 'p': // 1 string to match.
            if (memcmp(Name.data() + 8, "recated", 7) != 0)
              break;
            return AttributeCommonInfo::AT_Deprecated; // "gnu::deprecated"
          case 's':                                    // 1 string to match.
            if (memcmp(Name.data() + 8, "tructor", 7) != 0)
              break;
            return AttributeCommonInfo::AT_Destructor; // "gnu::destructor"
          }
          break;
        case 'f': // 1 string to match.
          if (memcmp(Name.data() + 6, "ormat_arg", 9) != 0)
            break;
          return AttributeCommonInfo::AT_FormatArg; // "gnu::format_arg"
        case 'g':                                   // 1 string to match.
          if (memcmp(Name.data() + 6, "nu_inline", 9) != 0)
            break;
          return AttributeCommonInfo::AT_GNUInline; // "gnu::gnu_inline"
        case 'n':                                   // 1 string to match.
          if (memcmp(Name.data() + 6, "ocf_check", 9) != 0)
            break;
          return AttributeCommonInfo::AT_AnyX86NoCfCheck; // "gnu::nocf_check"
        case 'v':                                         // 1 string to match.
          if (memcmp(Name.data() + 6, "isibility", 9) != 0)
            break;
          return AttributeCommonInfo::AT_Visibility; // "gnu::visibility"
        }
        break;
      case 'n': // 4 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'm': // 1 string to match.
          if (memcmp(Name.data() + 9, "insize", 6) != 0)
            break;
          return AttributeCommonInfo::AT_MinSize; // "neverc::minsize"
        case 'n':                                 // 2 strings to match.
          if (Name[9] != 'o')
            break;
          switch (Name[10]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 11, "eref", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NoDeref; // "neverc::noderef"
          case 'm':                                 // 1 string to match.
            if (memcmp(Name.data() + 11, "erge", 4) != 0)
              break;
            return AttributeCommonInfo::AT_NoMerge; // "neverc::nomerge"
          }
          break;
        case 'o': // 1 string to match.
          if (memcmp(Name.data() + 9, "ptnone", 6) != 0)
            break;
          return AttributeCommonInfo::AT_OptimizeNone; // "neverc::optnone"
        }
        break;
      }
      break;
    case 16: // 14 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 2 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'f': // 1 string to match.
          if (memcmp(Name.data() + 8, "lag_enum", 8) != 0)
            break;
          return AttributeCommonInfo::AT_FlagEnum; // "clang::flag_enum"
        case 'n':                                  // 1 string to match.
          if (memcmp(Name.data() + 8, "ouwtable", 8) != 0)
            break;
          return AttributeCommonInfo::AT_NoUwtable; // "clang::nouwtable"
        }
        break;
      case 'g': // 5 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 6, "lloc_align", 10) != 0)
            break;
          return AttributeCommonInfo::AT_AllocAlign; // "gnu::alloc_align"
        case 'c':                                    // 1 string to match.
          if (memcmp(Name.data() + 6, "onstructor", 10) != 0)
            break;
          return AttributeCommonInfo::AT_Constructor; // "gnu::constructor"
        case 'f':                                     // 1 string to match.
          if (memcmp(Name.data() + 6, "allthrough", 10) != 0)
            break;
          return AttributeCommonInfo::AT_FallThrough; // "gnu::fallthrough"
        case 'v':                                     // 1 string to match.
          if (memcmp(Name.data() + 6, "ector_size", 10) != 0)
            break;
          return AttributeCommonInfo::AT_VectorSize; // "gnu::vector_size"
        case 'w':                                    // 1 string to match.
          if (memcmp(Name.data() + 6, "arn_unused", 10) != 0)
            break;
          return AttributeCommonInfo::AT_WarnUnused; // "gnu::warn_unused"
        }
        break;
      case 'n': // 7 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 9, "nnotate", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Annotate; // "neverc::annotate"
        case 'c':                                  // 1 string to match.
          if (memcmp(Name.data() + 9, "allback", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Callback; // "neverc::callback"
        case 'm':                                  // 1 string to match.
          if (memcmp(Name.data() + 9, "usttail", 7) != 0)
            break;
          return AttributeCommonInfo::AT_MustTail; // "neverc::musttail"
        case 'n':                                  // 2 strings to match.
          if (Name[9] != 'o')
            break;
          switch (Name[10]) {
          default:
            break;
          case 'e': // 1 string to match.
            if (memcmp(Name.data() + 11, "scape", 5) != 0)
              break;
            return AttributeCommonInfo::AT_NoEscape; // "neverc::noescape"
          case 'i':                                  // 1 string to match.
            if (memcmp(Name.data() + 11, "nline", 5) != 0)
              break;
            return AttributeCommonInfo::AT_NoInline; // "neverc::noinline"
          }
          break;
        case 's': // 1 string to match.
          if (memcmp(Name.data() + 9, "uppress", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Suppress; // "neverc::suppress"
        case 'u':                                  // 1 string to match.
          if (memcmp(Name.data() + 9, "nlikely", 7) != 0)
            break;
          return AttributeCommonInfo::AT_Unlikely; // "neverc::unlikely"
        }
        break;
      }
      break;
    case 17: // 7 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 5 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'c': // 2 strings to match.
          if (Name[8] != 'o')
            break;
          switch (Name[9]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 10, "e_align", 7) != 0)
              break;
            return AttributeCommonInfo::AT_CodeAlign; // "clang::code_align"
          case 'n':                                   // 1 string to match.
            if (memcmp(Name.data() + 10, "vergent", 7) != 0)
              break;
            return AttributeCommonInfo::AT_Convergent; // "clang::convergent"
          }
          break;
        case 'n': // 1 string to match.
          if (memcmp(Name.data() + 8, "o_builtin", 9) != 0)
            break;
          return AttributeCommonInfo::AT_NoBuiltin; // "clang::no_builtin"
        case 'u':                                   // 1 string to match.
          if (memcmp(Name.data() + 8, "se_handle", 9) != 0)
            break;
          return AttributeCommonInfo::AT_UseHandle; // "clang::use_handle"
        case 'v':                                   // 1 string to match.
          if (memcmp(Name.data() + 8, "ectorcall", 9) != 0)
            break;
          return AttributeCommonInfo::AT_VectorCall; // "clang::vectorcall"
        }
        break;
      case 'n': // 2 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'f': // 1 string to match.
          if (memcmp(Name.data() + 9, "lag_enum", 8) != 0)
            break;
          return AttributeCommonInfo::AT_FlagEnum; // "neverc::flag_enum"
        case 'n':                                  // 1 string to match.
          if (memcmp(Name.data() + 9, "ouwtable", 8) != 0)
            break;
          return AttributeCommonInfo::AT_NoUwtable; // "neverc::nouwtable"
        }
        break;
      }
      break;
    case 18: // 14 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 6 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 8, "nforce_tcb", 10) != 0)
            break;
          return AttributeCommonInfo::AT_EnforceTCB; // "clang::enforce_tcb"
        case 'm':                                    // 2 strings to match.
          if (Name[8] != 'a')
            break;
          switch (Name[9]) {
          default:
            break;
          case 't': // 1 string to match.
            if (memcmp(Name.data() + 10, "rix_type", 8) != 0)
              break;
            return AttributeCommonInfo::AT_MatrixType; // "clang::matrix_type"
          case 'y':                                    // 1 string to match.
            if (memcmp(Name.data() + 10, "be_undef", 8) != 0)
              break;
            return AttributeCommonInfo::AT_MaybeUndef; // "clang::maybe_undef"
          }
          break;
        case 'n': // 1 string to match.
          if (memcmp(Name.data() + 8, "oduplicate", 10) != 0)
            break;
          return AttributeCommonInfo::AT_NoDuplicate; // "clang::noduplicate"
        case 'u':                                     // 1 string to match.
          if (memcmp(Name.data() + 8, "navailable", 10) != 0)
            break;
          return AttributeCommonInfo::AT_Unavailable; // "clang::unavailable"
        case 'w':                                     // 1 string to match.
          if (memcmp(Name.data() + 8, "eak_import", 10) != 0)
            break;
          return AttributeCommonInfo::AT_WeakImport; // "clang::weak_import"
        }
        break;
      case 'g': // 3 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 6, "lways_inline", 12) != 0)
            break;
          return AttributeCommonInfo::AT_AlwaysInline; // "gnu::always_inline"
        case 'r':                                      // 1 string to match.
          if (memcmp(Name.data() + 6, "eturns_twice", 12) != 0)
            break;
          return AttributeCommonInfo::AT_ReturnsTwice; // "gnu::returns_twice"
        case 't':                                      // 1 string to match.
          if (memcmp(Name.data() + 6, "arget_clones", 12) != 0)
            break;
          return AttributeCommonInfo::AT_TargetClones; // "gnu::target_clones"
        }
        break;
      case 'n': // 5 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'c': // 2 strings to match.
          if (Name[9] != 'o')
            break;
          switch (Name[10]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 11, "e_align", 7) != 0)
              break;
            return AttributeCommonInfo::AT_CodeAlign; // "neverc::code_align"
          case 'n':                                   // 1 string to match.
            if (memcmp(Name.data() + 11, "vergent", 7) != 0)
              break;
            return AttributeCommonInfo::AT_Convergent; // "neverc::convergent"
          }
          break;
        case 'n': // 1 string to match.
          if (memcmp(Name.data() + 9, "o_builtin", 9) != 0)
            break;
          return AttributeCommonInfo::AT_NoBuiltin; // "neverc::no_builtin"
        case 'u':                                   // 1 string to match.
          if (memcmp(Name.data() + 9, "se_handle", 9) != 0)
            break;
          return AttributeCommonInfo::AT_UseHandle; // "neverc::use_handle"
        case 'v':                                   // 1 string to match.
          if (memcmp(Name.data() + 9, "ectorcall", 9) != 0)
            break;
          return AttributeCommonInfo::AT_VectorCall; // "neverc::vectorcall"
        }
        break;
      }
      break;
    case 19: // 16 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 7 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 8, "vailability", 11) != 0)
            break;
          return AttributeCommonInfo::AT_Availability; // "clang::availability"
        case 'b':                                      // 2 strings to match.
          if (memcmp(Name.data() + 8, "tf_", 3) != 0)
            break;
          switch (Name[11]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 12, "ecl_tag", 7) != 0)
              break;
            return AttributeCommonInfo::AT_BTFDeclTag; // "clang::btf_decl_tag"
          case 't':                                    // 1 string to match.
            if (memcmp(Name.data() + 12, "ype_tag", 7) != 0)
              break;
            return AttributeCommonInfo::AT_BTFTypeTag; // "clang::btf_type_tag"
          }
          break;
        case 'c': // 2 strings to match.
          if (memcmp(Name.data() + 8, "pu_", 3) != 0)
            break;
          switch (Name[11]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 12, "ispatch", 7) != 0)
              break;
            return AttributeCommonInfo::AT_CPUDispatch; // "clang::cpu_dispatch"
          case 's':                                     // 1 string to match.
            if (memcmp(Name.data() + 12, "pecific", 7) != 0)
              break;
            return AttributeCommonInfo::AT_CPUSpecific; // "clang::cpu_specific"
          }
          break;
        case 'o': // 1 string to match.
          if (memcmp(Name.data() + 8, "verloadable", 11) != 0)
            break;
          return AttributeCommonInfo::AT_Overloadable; // "clang::overloadable"
        case 'p':                                      // 1 string to match.
          if (memcmp(Name.data() + 8, "reserve_all", 11) != 0)
            break;
          return AttributeCommonInfo::AT_PreserveAll; // "clang::preserve_all"
        }
        break;
      case 'g': // 3 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 6, "ssume_aligned", 13) != 0)
            break;
          return AttributeCommonInfo::AT_AssumeAligned; // "gnu::assume_aligned"
        case 'n':                                       // 1 string to match.
          if (memcmp(Name.data() + 6, "o_split_stack", 13) != 0)
            break;
          return AttributeCommonInfo::AT_NoSplitStack; // "gnu::no_split_stack"
        case 't':                                      // 1 string to match.
          if (memcmp(Name.data() + 6, "arget_version", 13) != 0)
            break;
          return AttributeCommonInfo::AT_TargetVersion; // "gnu::target_version"
        }
        break;
      case 'n': // 6 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 9, "nforce_tcb", 10) != 0)
            break;
          return AttributeCommonInfo::AT_EnforceTCB; // "neverc::enforce_tcb"
        case 'm':                                    // 2 strings to match.
          if (Name[9] != 'a')
            break;
          switch (Name[10]) {
          default:
            break;
          case 't': // 1 string to match.
            if (memcmp(Name.data() + 11, "rix_type", 8) != 0)
              break;
            return AttributeCommonInfo::AT_MatrixType; // "neverc::matrix_type"
          case 'y':                                    // 1 string to match.
            if (memcmp(Name.data() + 11, "be_undef", 8) != 0)
              break;
            return AttributeCommonInfo::AT_MaybeUndef; // "neverc::maybe_undef"
          }
          break;
        case 'n': // 1 string to match.
          if (memcmp(Name.data() + 9, "oduplicate", 10) != 0)
            break;
          return AttributeCommonInfo::AT_NoDuplicate; // "neverc::noduplicate"
        case 'u':                                     // 1 string to match.
          if (memcmp(Name.data() + 9, "navailable", 10) != 0)
            break;
          return AttributeCommonInfo::AT_Unavailable; // "neverc::unavailable"
        case 'w':                                     // 1 string to match.
          if (memcmp(Name.data() + 9, "eak_import", 10) != 0)
            break;
          return AttributeCommonInfo::AT_WeakImport; // "neverc::weak_import"
        }
        break;
      }
      break;
    case 20: // 11 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 2 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 8, "ddress_space", 12) != 0)
            break;
          return AttributeCommonInfo::AT_AddressSpace; // "clang::address_space"
        case 'p':                                      // 1 string to match.
          if (memcmp(Name.data() + 8, "reserve_most", 12) != 0)
            break;
          return AttributeCommonInfo::AT_PreserveMost; // "clang::preserve_most"
        }
        break;
      case 'g': // 2 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'f': // 1 string to match.
          if (memcmp(Name.data() + 6, "unction_return", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_FunctionReturnThunks; // "gnu::function_return"
        case 'r':                      // 1 string to match.
          if (memcmp(Name.data() + 6, "eturns_nonnull", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_ReturnsNonNull; // "gnu::returns_nonnull"
        }
        break;
      case 'n': // 7 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 9, "vailability", 11) != 0)
            break;
          return AttributeCommonInfo::AT_Availability; // "neverc::availability"
        case 'b':                                      // 2 strings to match.
          if (memcmp(Name.data() + 9, "tf_", 3) != 0)
            break;
          switch (Name[12]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 13, "ecl_tag", 7) != 0)
              break;
            return AttributeCommonInfo::AT_BTFDeclTag; // "neverc::btf_decl_tag"
          case 't':                                    // 1 string to match.
            if (memcmp(Name.data() + 13, "ype_tag", 7) != 0)
              break;
            return AttributeCommonInfo::AT_BTFTypeTag; // "neverc::btf_type_tag"
          }
          break;
        case 'c': // 2 strings to match.
          if (memcmp(Name.data() + 9, "pu_", 3) != 0)
            break;
          switch (Name[12]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 13, "ispatch", 7) != 0)
              break;
            return AttributeCommonInfo::
                AT_CPUDispatch; // "neverc::cpu_dispatch"
          case 's':             // 1 string to match.
            if (memcmp(Name.data() + 13, "pecific", 7) != 0)
              break;
            return AttributeCommonInfo::
                AT_CPUSpecific; // "neverc::cpu_specific"
          }
          break;
        case 'o': // 1 string to match.
          if (memcmp(Name.data() + 9, "verloadable", 11) != 0)
            break;
          return AttributeCommonInfo::AT_Overloadable; // "neverc::overloadable"
        case 'p':                                      // 1 string to match.
          if (memcmp(Name.data() + 9, "reserve_all", 11) != 0)
            break;
          return AttributeCommonInfo::AT_PreserveAll; // "neverc::preserve_all"
        }
        break;
      }
      break;
    case 21: // 10 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 3 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 8, "cquire_handle", 13) != 0)
            break;
          return AttributeCommonInfo::
              AT_AcquireHandle; // "clang::acquire_handle"
        case 'p':               // 1 string to match.
          if (memcmp(Name.data() + 8, "referred_type", 13) != 0)
            break;
          return AttributeCommonInfo::
              AT_PreferredType; // "clang::preferred_type"
        case 'r':               // 1 string to match.
          if (memcmp(Name.data() + 8, "elease_handle", 13) != 0)
            break;
          return AttributeCommonInfo::
              AT_ReleaseHandle; // "clang::release_handle"
        }
        break;
      case 'g': // 2 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'd': // 1 string to match.
          if (memcmp(Name.data() + 6, "isable_try_stmt", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_DisableTryStmt; // "gnu::disable_try_stmt"
        case 'r':                // 1 string to match.
          if (memcmp(Name.data() + 6, "andomize_layout", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_RandomizeLayout; // "gnu::randomize_layout"
        }
        break;
      case 'n': // 5 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 3 strings to match.
          switch (Name[9]) {
          default:
            break;
          case 'd': // 1 string to match.
            if (memcmp(Name.data() + 10, "dress_space", 11) != 0)
              break;
            return AttributeCommonInfo::
                AT_AddressSpace; // "neverc::address_space"
          case 'l':              // 1 string to match.
            if (memcmp(Name.data() + 10, "ways_inline", 11) != 0)
              break;
            return AttributeCommonInfo::
                AT_AlwaysInline; // "neverc::always_inline"
          case 'n':              // 1 string to match.
            if (memcmp(Name.data() + 10, "notate_type", 11) != 0)
              break;
            return AttributeCommonInfo::
                AT_AnnotateType; // "neverc::annotate_type"
          }
          break;
        case 'b': // 1 string to match.
          if (memcmp(Name.data() + 9, "uiltin_alias", 12) != 0)
            break;
          return AttributeCommonInfo::
              AT_BuiltinAlias; // "neverc::builtin_alias"
        case 'p':              // 1 string to match.
          if (memcmp(Name.data() + 9, "reserve_most", 12) != 0)
            break;
          return AttributeCommonInfo::
              AT_PreserveMost; // "neverc::preserve_most"
        }
        break;
      }
      break;
    case 22: // 7 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 3 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 8, "arch64_sve_pcs", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_AArch64SVEPcs; // "clang::aarch64_sve_pcs"
        case 'n':               // 1 string to match.
          if (memcmp(Name.data() + 8, "ot_tail_called", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_NotTailCalled; // "clang::not_tail_called"
        case 't':               // 1 string to match.
          if (memcmp(Name.data() + 8, "ype_visibility", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_TypeVisibility; // "clang::type_visibility"
        }
        break;
      case 'g': // 1 string to match.
        if (memcmp(Name.data() + 1, "nu::transparent_union", 21) != 0)
          break;
        return AttributeCommonInfo::
            AT_TransparentUnion; // "gnu::transparent_union"
      case 'n':                  // 3 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 9, "cquire_handle", 13) != 0)
            break;
          return AttributeCommonInfo::
              AT_AcquireHandle; // "neverc::acquire_handle"
        case 'p':               // 1 string to match.
          if (memcmp(Name.data() + 9, "referred_type", 13) != 0)
            break;
          return AttributeCommonInfo::
              AT_PreferredType; // "neverc::preferred_type"
        case 'r':               // 1 string to match.
          if (memcmp(Name.data() + 9, "elease_handle", 13) != 0)
            break;
          return AttributeCommonInfo::
              AT_ReleaseHandle; // "neverc::release_handle"
        }
        break;
      }
      break;
    case 23: // 10 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 5 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 8, "nforce_tcb_leaf", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_EnforceTCBLeaf; // "clang::enforce_tcb_leaf"
        case 'i':                // 1 string to match.
          if (memcmp(Name.data() + 8, "nternal_linkage", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_InternalLinkage; // "clang::internal_linkage"
        case 'm':                 // 1 string to match.
          if (memcmp(Name.data() + 8, "in_vector_width", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_MinVectorWidth; // "clang::min_vector_width"
        case 'n':                // 1 string to match.
          if (memcmp(Name.data() + 8, "eon_vector_type", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_NeonVectorType; // "clang::neon_vector_type"
        case 'p':                // 1 string to match.
          if (memcmp(Name.data() + 8, "ass_object_size", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_PassObjectSize; // "clang::pass_object_size"
        }
        break;
      case 'g': // 2 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'n': // 1 string to match.
          if (memcmp(Name.data() + 6, "o_stack_protector", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_NoStackProtector; // "gnu::no_stack_protector"
        case 'w':                  // 1 string to match.
          if (memcmp(Name.data() + 6, "arn_unused_result", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_WarnUnusedResult; // "gnu::warn_unused_result"
        }
        break;
      case 'n': // 3 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 9, "arch64_sve_pcs", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_AArch64SVEPcs; // "neverc::aarch64_sve_pcs"
        case 'n':               // 1 string to match.
          if (memcmp(Name.data() + 9, "ot_tail_called", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_NotTailCalled; // "neverc::not_tail_called"
        case 't':               // 1 string to match.
          if (memcmp(Name.data() + 9, "ype_visibility", 14) != 0)
            break;
          return AttributeCommonInfo::
              AT_TypeVisibility; // "neverc::type_visibility"
        }
        break;
      }
      break;
    case 24: // 7 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'g': // 2 strings to match.
        if (memcmp(Name.data() + 1, "nu::", 4) != 0)
          break;
        switch (Name[5]) {
        default:
          break;
        case 'n': // 1 string to match.
          if (memcmp(Name.data() + 6, "o_randomize_layout", 18) != 0)
            break;
          return AttributeCommonInfo::
              AT_NoRandomizeLayout; // "gnu::no_randomize_layout"
        case 'z':                   // 1 string to match.
          if (memcmp(Name.data() + 6, "ero_call_used_regs", 18) != 0)
            break;
          return AttributeCommonInfo::
              AT_ZeroCallUsedRegs; // "gnu::zero_call_used_regs"
        }
        break;
      case 'n': // 5 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'e': // 1 string to match.
          if (memcmp(Name.data() + 9, "nforce_tcb_leaf", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_EnforceTCBLeaf; // "neverc::enforce_tcb_leaf"
        case 'i':                // 1 string to match.
          if (memcmp(Name.data() + 9, "nternal_linkage", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_InternalLinkage; // "neverc::internal_linkage"
        case 'm':                 // 1 string to match.
          if (memcmp(Name.data() + 9, "in_vector_width", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_MinVectorWidth; // "neverc::min_vector_width"
        case 'n':                // 1 string to match.
          if (memcmp(Name.data() + 9, "eon_vector_type", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_NeonVectorType; // "neverc::neon_vector_type"
        case 'p':                // 1 string to match.
          if (memcmp(Name.data() + 9, "ass_object_size", 15) != 0)
            break;
          return AttributeCommonInfo::
              AT_PassObjectSize; // "neverc::pass_object_size"
        }
        break;
      }
      break;
    case 25: // 4 strings to match.
      if (memcmp(Name.data() + 0, "clang::", 7) != 0)
        break;
      switch (Name[7]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 8, "arch64_vector_pcs", 17) != 0)
          break;
        return AttributeCommonInfo::
            AT_AArch64VectorPcs; // "clang::aarch64_vector_pcs"
      case 'd':                  // 1 string to match.
        if (memcmp(Name.data() + 8, "isable_tail_calls", 17) != 0)
          break;
        return AttributeCommonInfo::
            AT_DisableTailCalls; // "clang::disable_tail_calls"
      case 'e':                  // 1 string to match.
        if (memcmp(Name.data() + 8, "num_extensibility", 17) != 0)
          break;
        return AttributeCommonInfo::
            AT_EnumExtensibility; // "clang::enum_extensibility"
      case 'n':                   // 1 string to match.
        if (memcmp(Name.data() + 8, "o_stack_protector", 17) != 0)
          break;
        return AttributeCommonInfo::
            AT_NoStackProtector; // "clang::no_stack_protector"
      }
      break;
    case 26: // 6 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 2 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'd': // 1 string to match.
          if (memcmp(Name.data() + 8, "iagnose_as_builtin", 18) != 0)
            break;
          return AttributeCommonInfo::
              AT_DiagnoseAsBuiltin; // "clang::diagnose_as_builtin"
        case 'u':                   // 1 string to match.
          if (memcmp(Name.data() + 8, "nsafe_buffer_usage", 18) != 0)
            break;
          return AttributeCommonInfo::
              AT_UnsafeBufferUsage; // "clang::unsafe_buffer_usage"
        }
        break;
      case 'n': // 4 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'a': // 1 string to match.
          if (memcmp(Name.data() + 9, "arch64_vector_pcs", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_AArch64VectorPcs; // "neverc::aarch64_vector_pcs"
        case 'd':                  // 1 string to match.
          if (memcmp(Name.data() + 9, "isable_tail_calls", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_DisableTailCalls; // "neverc::disable_tail_calls"
        case 'e':                  // 1 string to match.
          if (memcmp(Name.data() + 9, "num_extensibility", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_EnumExtensibility; // "neverc::enum_extensibility"
        case 'n':                   // 1 string to match.
          if (memcmp(Name.data() + 9, "o_stack_protector", 17) != 0)
            break;
          return AttributeCommonInfo::
              AT_NoStackProtector; // "neverc::no_stack_protector"
        }
        break;
      }
      break;
    case 27: // 4 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 2 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'l': // 1 string to match.
          if (memcmp(Name.data() + 8, "oader_uninitialized", 19) != 0)
            break;
          return AttributeCommonInfo::
              AT_LoaderUninitialized; // "clang::loader_uninitialized"
        case 'n':                     // 1 string to match.
          if (memcmp(Name.data() + 8, "eon_polyvector_type", 19) != 0)
            break;
          return AttributeCommonInfo::
              AT_NeonPolyVectorType; // "clang::neon_polyvector_type"
        }
        break;
      case 'n': // 2 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'd': // 1 string to match.
          if (memcmp(Name.data() + 9, "iagnose_as_builtin", 18) != 0)
            break;
          return AttributeCommonInfo::
              AT_DiagnoseAsBuiltin; // "neverc::diagnose_as_builtin"
        case 'u':                   // 1 string to match.
          if (memcmp(Name.data() + 9, "nsafe_buffer_usage", 18) != 0)
            break;
          return AttributeCommonInfo::
              AT_UnsafeBufferUsage; // "neverc::unsafe_buffer_usage"
        }
        break;
      }
      break;
    case 28: // 6 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 3 strings to match.
        if (memcmp(Name.data() + 1, "lang::", 6) != 0)
          break;
        switch (Name[7]) {
        default:
          break;
        case 'l': // 1 string to match.
          if (memcmp(Name.data() + 8, "to_visibility_public", 20) != 0)
            break;
          return AttributeCommonInfo::
              AT_LTOVisibilityPublic; // "clang::lto_visibility_public"
        case 'p':                     // 1 string to match.
          if (memcmp(Name.data() + 8, "ointer_with_type_tag", 20) != 0)
            break;
          return AttributeCommonInfo::
              AT_ArgumentWithTypeTag; // "clang::pointer_with_type_tag"
        case 't':                     // 1 string to match.
          if (memcmp(Name.data() + 8, "ype_tag_for_datatype", 20) != 0)
            break;
          return AttributeCommonInfo::
              AT_TypeTagForDatatype; // "clang::type_tag_for_datatype"
        }
        break;
      case 'g': // 1 string to match.
        if (memcmp(Name.data() + 1, "nu::force_align_arg_pointer", 27) != 0)
          break;
        return AttributeCommonInfo::
            AT_X86ForceAlignArgPointer; // "gnu::force_align_arg_pointer"
      case 'n':                         // 2 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'l': // 1 string to match.
          if (memcmp(Name.data() + 9, "oader_uninitialized", 19) != 0)
            break;
          return AttributeCommonInfo::
              AT_LoaderUninitialized; // "neverc::loader_uninitialized"
        case 'n':                     // 1 string to match.
          if (memcmp(Name.data() + 9, "eon_polyvector_type", 19) != 0)
            break;
          return AttributeCommonInfo::
              AT_NeonPolyVectorType; // "neverc::neon_polyvector_type"
        }
        break;
      }
      break;
    case 29: // 5 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 1 string to match.
        if (memcmp(Name.data() + 1, "lang::argument_with_type_tag", 28) != 0)
          break;
        return AttributeCommonInfo::
            AT_ArgumentWithTypeTag; // "clang::argument_with_type_tag"
      case 'g':                     // 1 string to match.
        if (memcmp(Name.data() + 1, "nu::patchable_function_entry", 28) != 0)
          break;
        return AttributeCommonInfo::
            AT_PatchableFunctionEntry; // "gnu::patchable_function_entry"
      case 'n':                        // 3 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case 'l': // 1 string to match.
          if (memcmp(Name.data() + 9, "to_visibility_public", 20) != 0)
            break;
          return AttributeCommonInfo::
              AT_LTOVisibilityPublic; // "neverc::lto_visibility_public"
        case 'p':                     // 1 string to match.
          if (memcmp(Name.data() + 9, "ointer_with_type_tag", 20) != 0)
            break;
          return AttributeCommonInfo::
              AT_ArgumentWithTypeTag; // "neverc::pointer_with_type_tag"
        case 't':                     // 1 string to match.
          if (memcmp(Name.data() + 9, "ype_tag_for_datatype", 20) != 0)
            break;
          return AttributeCommonInfo::
              AT_TypeTagForDatatype; // "neverc::type_tag_for_datatype"
        }
        break;
      }
      break;
    case 30: // 2 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'g': // 1 string to match.
        if (memcmp(Name.data() + 1, "nu::no_caller_saved_registers", 29) != 0)
          break;
        return AttributeCommonInfo::
            AT_AnyX86NoCallerSavedRegisters; // "gnu::no_caller_saved_registers"
      case 'n':                              // 1 string to match.
        if (memcmp(Name.data() + 1, "everc::argument_with_type_tag", 29) != 0)
          break;
        return AttributeCommonInfo::
            AT_ArgumentWithTypeTag; // "neverc::argument_with_type_tag"
      }
      break;
    case 31: // 1 string to match.
      if (memcmp(Name.data() + 0, "clang::pass_dynamic_object_size", 31) != 0)
        break;
      return AttributeCommonInfo::
          AT_PassObjectSize; // "clang::pass_dynamic_object_size"
    case 32:                 // 1 string to match.
      if (memcmp(Name.data() + 0, "neverc::pass_dynamic_object_size", 32) != 0)
        break;
      return AttributeCommonInfo::
          AT_PassObjectSize; // "neverc::pass_dynamic_object_size"
    case 33:                 // 2 strings to match.
      if (memcmp(Name.data() + 0, "clang::", 7) != 0)
        break;
      switch (Name[7]) {
      default:
        break;
      case '_': // 1 string to match.
        if (memcmp(Name.data() + 8, "_neverc_arm_builtin_alias", 25) != 0)
          break;
        return AttributeCommonInfo::
            AT_ArmBuiltinAlias; // "clang::__neverc_arm_builtin_alias"
      case 's':                 // 1 string to match.
        if (memcmp(Name.data() + 8, "peculative_load_hardening", 25) != 0)
          break;
        return AttributeCommonInfo::
            AT_SpeculativeLoadHardening; // "clang::speculative_load_hardening"
      }
      break;
    case 34: // 3 strings to match.
      switch (Name[0]) {
      default:
        break;
      case 'c': // 1 string to match.
        if (memcmp(Name.data() + 1, "lang::enforce_read_only_placement", 33) !=
            0)
          break;
        return AttributeCommonInfo::
            AT_ReadOnlyPlacement; // "clang::enforce_read_only_placement"
      case 'n':                   // 2 strings to match.
        if (memcmp(Name.data() + 1, "everc::", 7) != 0)
          break;
        switch (Name[8]) {
        default:
          break;
        case '_': // 1 string to match.
          if (memcmp(Name.data() + 9, "_neverc_arm_builtin_alias", 25) != 0)
            break;
          return AttributeCommonInfo::
              AT_ArmBuiltinAlias; // "neverc::__neverc_arm_builtin_alias"
        case 's':                 // 1 string to match.
          if (memcmp(Name.data() + 9, "peculative_load_hardening", 25) != 0)
            break;
          return AttributeCommonInfo::
              AT_SpeculativeLoadHardening; // "neverc::speculative_load_hardening"
        }
        break;
      }
      break;
    case 35: // 1 string to match.
      if (memcmp(Name.data() + 0, "neverc::enforce_read_only_placement", 35) !=
          0)
        break;
      return AttributeCommonInfo::
          AT_ReadOnlyPlacement; // "neverc::enforce_read_only_placement"
    case 36:                    // 1 string to match.
      if (memcmp(Name.data() + 0, "clang::no_speculative_load_hardening", 36) !=
          0)
        break;
      return AttributeCommonInfo::
          AT_NoSpeculativeLoadHardening; // "clang::no_speculative_load_hardening"
    case 37:                             // 1 string to match.
      if (memcmp(Name.data() + 0, "neverc::no_speculative_load_hardening",
                 37) != 0)
        break;
      return AttributeCommonInfo::
          AT_NoSpeculativeLoadHardening; // "neverc::no_speculative_load_hardening"
    case 44:                             // 1 string to match.
      if (memcmp(Name.data() + 0,
                 "clang::available_only_in_default_eval_method", 44) != 0)
        break;
      return AttributeCommonInfo::
          AT_AvailableOnlyInDefaultEvalMethod; // "clang::available_only_in_default_eval_method"
    case 45:                                   // 1 string to match.
      if (memcmp(Name.data() + 0,
                 "neverc::available_only_in_default_eval_method", 45) != 0)
        break;
      return AttributeCommonInfo::
          AT_AvailableOnlyInDefaultEvalMethod; // "neverc::available_only_in_default_eval_method"
    }
  } else if (AttributeCommonInfo::AS_Keyword == Syntax ||
             AttributeCommonInfo::AS_ContextSensitiveKeyword == Syntax) {
    switch (Name.size()) {
    default:
      break;
    case 5: // 2 strings to match.
      switch (Name[0]) {
      default:
        break;
      case '_': // 1 string to match.
        if (memcmp(Name.data() + 1, "_w64", 4) != 0)
          break;
        return AttributeCommonInfo::IgnoredAttribute; // "__w64"
      case 'c':                                       // 1 string to match.
        if (memcmp(Name.data() + 1, "decl", 4) != 0)
          break;
        return AttributeCommonInfo::AT_CDecl; // "cdecl"
      }
      break;
    case 6: // 3 strings to match.
      if (Name[0] != '_')
        break;
      switch (Name[1]) {
      default:
        break;
      case '_': // 2 strings to match.
        switch (Name[2]) {
        default:
          break;
        case 's': // 1 string to match.
          if (memcmp(Name.data() + 3, "ptr", 3) != 0)
            break;
          return AttributeCommonInfo::AT_SPtr; // "__sptr"
        case 'u':                              // 1 string to match.
          if (memcmp(Name.data() + 3, "ptr", 3) != 0)
            break;
          return AttributeCommonInfo::AT_UPtr; // "__uptr"
        }
        break;
      case 'c': // 1 string to match.
        if (memcmp(Name.data() + 2, "decl", 4) != 0)
          break;
        return AttributeCommonInfo::AT_CDecl; // "_cdecl"
      }
      break;
    case 7: // 4 strings to match.
      switch (Name[0]) {
      default:
        break;
      case '_': // 3 strings to match.
        if (Name[1] != '_')
          break;
        switch (Name[2]) {
        default:
          break;
        case 'c': // 1 string to match.
          if (memcmp(Name.data() + 3, "decl", 4) != 0)
            break;
          return AttributeCommonInfo::AT_CDecl; // "__cdecl"
        case 'p':                               // 2 strings to match.
          if (memcmp(Name.data() + 3, "tr", 2) != 0)
            break;
          switch (Name[5]) {
          default:
            break;
          case '3': // 1 string to match.
            if (Name[6] != '2')
              break;
            return AttributeCommonInfo::AT_Ptr32; // "__ptr32"
          case '6':                               // 1 string to match.
            if (Name[6] != '4')
              break;
            return AttributeCommonInfo::AT_Ptr64; // "__ptr64"
          }
          break;
        }
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 1, "lignas", 6) != 0)
          break;
        return AttributeCommonInfo::AT_Aligned; // "alignas"
      }
      break;
    case 8: // 3 strings to match.
      if (Name[0] != '_')
        break;
      switch (Name[1]) {
      default:
        break;
      case 'A': // 1 string to match.
        if (memcmp(Name.data() + 2, "lignas", 6) != 0)
          break;
        return AttributeCommonInfo::AT_Aligned; // "_Alignas"
      case 'N':                                 // 1 string to match.
        if (memcmp(Name.data() + 2, "onnull", 6) != 0)
          break;
        return AttributeCommonInfo::AT_TypeNonNull; // "_Nonnull"
      case 's':                                     // 1 string to match.
        if (memcmp(Name.data() + 2, "tdcall", 6) != 0)
          break;
        return AttributeCommonInfo::AT_StdCall; // "_stdcall"
      }
      break;
    case 9: // 4 strings to match.
      if (Name[0] != '_')
        break;
      switch (Name[1]) {
      default:
        break;
      case 'N': // 1 string to match.
        if (memcmp(Name.data() + 2, "ullable", 7) != 0)
          break;
        return AttributeCommonInfo::AT_TypeNullable; // "_Nullable"
      case '_':                                      // 2 strings to match.
        switch (Name[2]) {
        default:
          break;
        case 'r': // 1 string to match.
          if (memcmp(Name.data() + 3, "egcall", 6) != 0)
            break;
          return AttributeCommonInfo::AT_RegCall; // "__regcall"
        case 's':                                 // 1 string to match.
          if (memcmp(Name.data() + 3, "tdcall", 6) != 0)
            break;
          return AttributeCommonInfo::AT_StdCall; // "__stdcall"
        }
        break;
      case 'f': // 1 string to match.
        if (memcmp(Name.data() + 2, "astcall", 7) != 0)
          break;
        return AttributeCommonInfo::AT_FastCall; // "_fastcall"
      }
      break;
    case 10: // 1 string to match.
      if (memcmp(Name.data() + 0, "__fastcall", 10) != 0)
        break;
      return AttributeCommonInfo::AT_FastCall; // "__fastcall"
    case 11:                                   // 1 string to match.
      if (memcmp(Name.data() + 0, "_vectorcall", 11) != 0)
        break;
      return AttributeCommonInfo::AT_VectorCall; // "_vectorcall"
    case 12:                                     // 3 strings to match.
      if (memcmp(Name.data() + 0, "__", 2) != 0)
        break;
      switch (Name[2]) {
      default:
        break;
      case 'a': // 1 string to match.
        if (memcmp(Name.data() + 3, "rm_new_za", 9) != 0)
          break;
        return AttributeCommonInfo::AT_ArmNewZA; // "__arm_new_za"
      case 'n':                                  // 1 string to match.
        if (memcmp(Name.data() + 3, "oinline__", 9) != 0)
          break;
        return AttributeCommonInfo::AT_NoInline; // "__noinline__"
      case 'v':                                  // 1 string to match.
        if (memcmp(Name.data() + 3, "ectorcall", 9) != 0)
          break;
        return AttributeCommonInfo::AT_VectorCall; // "__vectorcall"
      }
      break;
    case 13: // 1 string to match.
      if (memcmp(Name.data() + 0, "__forceinline", 13) != 0)
        break;
      return AttributeCommonInfo::AT_AlwaysInline; // "__forceinline"
    case 15:                                       // 2 strings to match.
      if (memcmp(Name.data() + 0, "__arm_s", 7) != 0)
        break;
      switch (Name[7]) {
      default:
        break;
      case 'h': // 1 string to match.
        if (memcmp(Name.data() + 8, "ared_za", 7) != 0)
          break;
        return AttributeCommonInfo::AT_ArmSharedZA; // "__arm_shared_za"
      case 't':                                     // 1 string to match.
        if (memcmp(Name.data() + 8, "reaming", 7) != 0)
          break;
        return AttributeCommonInfo::AT_ArmStreaming; // "__arm_streaming"
      }
      break;
    case 17: // 1 string to match.
      if (memcmp(Name.data() + 0, "_Null_unspecified", 17) != 0)
        break;
      return AttributeCommonInfo::AT_TypeNullUnspecified; // "_Null_unspecified"
    case 18:                                              // 1 string to match.
      if (memcmp(Name.data() + 0, "__arm_preserves_za", 18) != 0)
        break;
      return AttributeCommonInfo::AT_ArmPreservesZA; // "__arm_preserves_za"
    case 23:                                         // 1 string to match.
      if (memcmp(Name.data() + 0, "__arm_locally_streaming", 23) != 0)
        break;
      return AttributeCommonInfo::
          AT_ArmLocallyStreaming; // "__arm_locally_streaming"
    case 26:                      // 1 string to match.
      if (memcmp(Name.data() + 0, "__arm_streaming_compatible", 26) != 0)
        break;
      return AttributeCommonInfo::
          AT_ArmStreamingCompatible; // "__arm_streaming_compatible"
    }
  } else if (AttributeCommonInfo::AS_Pragma == Syntax) {
  }
  return AttributeCommonInfo::UnknownAttribute;
}

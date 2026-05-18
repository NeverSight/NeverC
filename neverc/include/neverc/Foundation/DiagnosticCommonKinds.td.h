#ifdef COMMONSTART
__COMMONSTART = DIAG_START_COMMON,
#undef COMMONSTART
#endif

    // Inline Assembly Issue (category 4)
    DIAG(err_asm_invalid_type, CLASS_ERROR, (unsigned)diag::Severity::Error,
         "invalid type %0 in asm %select{input|output}1", 0, false, true, true,
         false, 4) DIAG(err_asm_invalid_type_in_input, CLASS_ERROR,
                        (unsigned)diag::Severity::Error,
                        "invalid type %0 in asm input for constraint '%1'", 0,
                        false, true, true, false, 4)

    // Parse Issue (category 3)
    DIAG(err_attribute_not_type_attr, CLASS_ERROR,
         (unsigned)diag::Severity::Error,
         "%0%select{ attribute|}1 cannot be applied to types", 0, false, true,
         true, false, 3)

        DIAG(err_attribute_uuid_malformed_guid, CLASS_ERROR,
             (unsigned)diag::Severity::Error,
             "uuid attribute contains a malformed GUID", 0, false, true, true,
             false,
             0) DIAG(err_cannot_open_file, CLASS_ERROR,
                     (unsigned)diag::Severity::Fatal,
                     "cannot open file '%0': %1", 0, false, true, true, false,
                     0) DIAG(err_duplicate_declspec, CLASS_ERROR,
                             (unsigned)diag::Severity::Error,
                             "duplicate '%0' declaration specifier", 0, false,
                             true, true, false, 0)
            DIAG(err_expected, CLASS_ERROR, (unsigned)diag::Severity::Error,
                 "expected %0", 0, false, true, true, false,
                 3) DIAG(err_expected_after, CLASS_ERROR,
                         (unsigned)diag::Severity::Error,
                         "expected %1 after %0", 0, false, true, true, false,
                         3) DIAG(err_expected_either, CLASS_ERROR,
                                 (unsigned)diag::Severity::Error,
                                 "expected %0 or %1", 0, false, true, true,
                                 false, 3)
                DIAG(err_expected_string_literal, CLASS_ERROR,
                     (unsigned)diag::Severity::Error,
                     "expected string literal %select{in %1|for diagnostic "
                     "message in static_assert|for optional message in "
                     "'availability' attribute|as argument of '%1' attribute}0",
                     0, false, true, true, false,
                     1) DIAG(err_file_modified, CLASS_ERROR,
                             (unsigned)diag::Severity::Fatal,
                             "file '%0' modified since it was first processed",
                             0, false, true, true, false,
                             0) DIAG(err_file_too_large, CLASS_ERROR,
                                     (unsigned)diag::Severity::Error,
                                     "sorry, unsupported: file '%0' is too "
                                     "large for NeverC to process",
                                     0, false, true, true, false, 0)
                    DIAG(err_integer_literal_too_large, CLASS_ERROR,
                         (unsigned)diag::Severity::Error,
                         "integer literal is too large to be represented in "
                         "any %select{signed |}0integer type",
                         0, false, true, true,
                         false, 0) DIAG(err_invalid_storage_class_in_func_decl,
                                        CLASS_ERROR,
                                        (unsigned)diag::Severity::Error,
                                        "invalid storage class specifier in "
                                        "function declarator",
                                        0, false, true, true, false, 3)
                        DIAG(err_keyword_not_supported_on_target, CLASS_ERROR,
                             (unsigned)diag::Severity::Error,
                             "%0 is not supported on this target", 0, false,
                             true, true,
                             false, 0) DIAG(err_ms_asm_bitfield_unsupported,
                                            CLASS_ERROR,
                                            (unsigned)diag::Severity::Error,
                                            "an inline asm block cannot have "
                                            "an operand which is a bit-field",
                                            0, false, true, true, false, 4)
                            DIAG(err_nullability_conflicting, CLASS_ERROR,
                                 (unsigned)diag::Severity::Error,
                                 "nullability specifier %0 conflicts with "
                                 "existing specifier %1",
                                 0, false, true, true,
                                 false, 7) DIAG(err_opt_not_valid_on_target,
                                                CLASS_ERROR,
                                                (unsigned)diag::Severity::Error,
                                                "option '%0' cannot be "
                                                "specified on this target",
                                                0, false, true, true, false, 0)
                                DIAG(err_param_redefinition, CLASS_ERROR,
                                     (unsigned)diag::Severity::Error,
                                     "redefinition of parameter %0", 0, false,
                                     true, true, false, 3)
                                    DIAG(err_seh___except_block, CLASS_ERROR,
                                         (unsigned)diag::Severity::Error,
                                         "%0 only allowed in __except block or "
                                         "filter expression",
                                         0, false, true, true, false, 0)
                                        DIAG(err_seh___except_filter,
                                             CLASS_ERROR,
                                             (unsigned)diag::Severity::Error,
                                             "%0 only allowed in __except "
                                             "filter expression",
                                             0, false, true, true, false,
                                             0) DIAG(err_seh_expected_handler,
                                                     CLASS_ERROR,
                                                     (unsigned)
                                                         diag::Severity::Error,
                                                     "expected '__except' or "
                                                     "'__finally' block",
                                                     0, false, true, true,
                                                     false, 0)
                                            DIAG(err_size_t_literal_too_large,
                                                 CLASS_ERROR,
                                                 (unsigned)
                                                     diag::Severity::Error,
                                                 "%select{signed |}0'size_t' "
                                                 "literal is out of range of "
                                                 "possible %select{signed "
                                                 "|}0'size_t' values",
                                                 0, false, true, true, false,
                                                 0) DIAG(err_size_t_suffix,
                                                         CLASS_ERROR,
                                                         (unsigned)diag::
                                                             Severity::Error,
                                                         "'size_t' suffix for "
                                                         "literals is not "
                                                         "supported",
                                                         0, false, true, true,
                                                         false, 0)
                                                DIAG(
                                                    err_sloc_space_too_large,
                                                    CLASS_ERROR,
                                                    (unsigned)
                                                        diag::Severity::Fatal,
                                                    "sorry, the translation "
                                                    "unit is too large for "
                                                    "NeverC to process: ran "
                                                    "out of source locations",
                                                    0, false, true, true, false,
                                                    0) DIAG(err_target_unknown_abi,
                                                            CLASS_ERROR,
                                                            (unsigned)
                                                                diag::
                                                                    Severity::
                                                                        Error,
                                                            "unknown target "
                                                            "ABI '%0'",
                                                            0, false, true,
                                                            true, false, 0)
                                                    DIAG(err_target_unknown_cpu,
                                                         CLASS_ERROR,
                                                         (unsigned)diag::
                                                             Severity::Error,
                                                         "unknown target CPU "
                                                         "'%0'",
                                                         0,
                                                         false, true, true,
                                                         false, 0)
                                                        DIAG(
                                                            err_target_unknown_fpmath,
                                                            CLASS_ERROR,
                                                            (unsigned)diag::
                                                                Severity::Error,
                                                            "unknown FP unit "
                                                            "'%0'",
                                                            0, false, true,
                                                            true, false,
                                                            0) DIAG(err_target_unknown_triple,
                                                                    CLASS_ERROR,
                                                                    (unsigned)
                                                                        diag::
                                                                            Severity::
                                                                                Error,
                                                                    "unknown "
                                                                    "target "
                                                                    "triple "
                                                                    "'%0'",
                                                                    0, false,
                                                                    true, true,
                                                                    false, 0)
                                                            DIAG(
                                                                err_target_unsupported_fpmath,
                                                                CLASS_ERROR,
                                                                (unsigned)diag::
                                                                    Severity::
                                                                        Error,
                                                                "the '%0' unit "
                                                                "is not "
                                                                "supported "
                                                                "with this "
                                                                "instruction "
                                                                "set",
                                                                0, false, true,
                                                                true, false, 0)
                                                                DIAG(
                                                                    err_too_large_for_fixed_point,
                                                                    CLASS_ERROR,
                                                                    (unsigned)diag::
                                                                        Severity::
                                                                            Error,
                                                                    "this "
                                                                    "value is "
                                                                    "too large "
                                                                    "for this "
                                                                    "fixed "
                                                                    "point "
                                                                    "type",
                                                                    0, false,
                                                                    true, true,
                                                                    false,
                                                                    0) DIAG(err_unable_to_make_temp,
                                                                            CLASS_ERROR,
                                                                            (unsigned)
                                                                                diag::
                                                                                    Severity::
                                                                                        Error,
                                                                            "un"
                                                                            "ab"
                                                                            "le"
                                                                            " t"
                                                                            "o "
                                                                            "ma"
                                                                            "ke"
                                                                            " t"
                                                                            "em"
                                                                            "po"
                                                                            "ra"
                                                                            "ry"
                                                                            " f"
                                                                            "il"
                                                                            "e:"
                                                                            " %"
                                                                            "0",
                                                                            0, false,
                                                                            true,
                                                                            true,
                                                                            false,
                                                                            0)
                                                                    DIAG(
                                                                        err_unable_to_rename_temp,
                                                                        CLASS_ERROR,
                                                                        (unsigned)diag::
                                                                            Severity::Error,
                                                                        "unable"
                                                                        " to "
                                                                        "rename"
                                                                        " tempo"
                                                                        "rary "
                                                                        "'%0' "
                                                                        "to "
                                                                        "output"
                                                                        " file "
                                                                        "'%1': "
                                                                        "'%2'",
                                                                        0,
                                                                        false,
                                                                        true,
                                                                        true,
                                                                        false,
                                                                        0)
                                                                        DIAG(
                                                                            err_unimplemented_conversion_with_fixed_point_type,
                                                                            CLASS_ERROR,
                                                                            (unsigned)
                                                                                diag::Severity::
                                                                                    Error,
                                                                            "co"
                                                                            "nv"
                                                                            "er"
                                                                            "si"
                                                                            "on"
                                                                            " b"
                                                                            "et"
                                                                            "we"
                                                                            "en"
                                                                            " f"
                                                                            "ix"
                                                                            "ed"
                                                                            " p"
                                                                            "oi"
                                                                            "nt"
                                                                            " a"
                                                                            "nd"
                                                                            " %"
                                                                            "0 "
                                                                            "is"
                                                                            " n"
                                                                            "ot"
                                                                            " y"
                                                                            "et"
                                                                            " s"
                                                                            "up"
                                                                            "po"
                                                                            "rt"
                                                                            "e"
                                                                            "d",
                                                                            0,
                                                                            false,
                                                                            true,
                                                                            true,
                                                                            false,
                                                                            0)
                                                                            DIAG(
                                                                                err_unsupported_bom,
                                                                                CLASS_ERROR,
                                                                                (unsigned)
                                                                                    diag::Severity::
                                                                                        Fatal,
                                                                                "%0 byte order mark detected in '%1', but encoding is not supported",
                                                                                0,
                                                                                false,
                                                                                true,
                                                                                true,
                                                                                false,
                                                                                0)
                                                                                DIAG(
                                                                                    err_use_of_tag_name_without_tag,
                                                                                    CLASS_ERROR,
                                                                                    (unsigned)
                                                                                        diag::Severity::
                                                                                            Error,
                                                                                    "must use '%1' tag to refer to type %0%select{| in this scope}2",
                                                                                    0,
                                                                                    false,
                                                                                    true,
                                                                                    true,
                                                                                    false,
                                                                                    0)

    // Extensions (InGroup references)
    DIAG(ext_c23_bitint_suffix, CLASS_EXTENSION,
         (unsigned)diag::Severity::Warning,
         "'_BitInt' suffix for literals is a C23 extension", 57, false, false,
         true, false,
         0) DIAG(ext_c99_longlong, CLASS_EXTENSION,
                 (unsigned)diag::Severity::Ignored,
                 "'long long' is an extension when C99 mode is not enabled",
                 269, false, false, true, false, 0)
        DIAG(ext_c_empty_initializer, CLASS_EXTENSION,
             (unsigned)diag::Severity::Ignored,
             "use of an empty initializer is a C23 extension", 57, false, false,
             true, false, 3) DIAG(ext_duplicate_declspec, CLASS_EXTENSION,
                                  (unsigned)diag::Severity::Ignored,
                                  "duplicate '%0' declaration specifier", 115,
                                  false, false, true, false, 0)
            DIAG(ext_integer_literal_too_large_for_signed, CLASS_EXTENSION,
                 (unsigned)diag::Severity::Warning,
                 "integer literal is too large to be represented in a signed "
                 "integer type, interpreting as unsigned",
                 222, false, false, true, false, 0)
                DIAG(ext_neverc_diagnose_if, CLASS_EXTENSION,
                     (unsigned)diag::Severity::Ignored,
                     "'diagnose_if' is a NeverC extension", 170, false, false,
                     true, false, 0) DIAG(ext_neverc_enable_if, CLASS_EXTENSION,
                                          (unsigned)diag::Severity::Ignored,
                                          "'enable_if' is a NeverC extension",
                                          170, false, false, true, false, 0)
                    DIAG(ext_warn_duplicate_declspec, CLASS_EXTENSION,
                         (unsigned)diag::Severity::Warning,
                         "duplicate '%0' declaration specifier", 115, false,
                         false, true, false, 0)

                        DIAG(fatal_too_many_errors, CLASS_ERROR,
                             (unsigned)diag::Severity::Fatal,
                             "too many errors emitted, stopping now", 0, false,
                             true, true, false, 0)

    // Notes
    DIAG(note_decl_hiding_tag_type, CLASS_NOTE, (unsigned)diag::Severity::Fatal,
         "%1 %0 is hidden by a non-type declaration of %0 here", 0, false,
         false, true, false,
         3) DIAG(note_declared_at, CLASS_NOTE, (unsigned)diag::Severity::Fatal,
                 "declared here", 0, false, false, true, false, 0)
        DIAG(note_duplicate_case_prev, CLASS_NOTE,
             (unsigned)diag::Severity::Fatal, "previous case defined here", 0,
             false, false, true, false, 0)
            DIAG(note_file_misc_sloc_usage, CLASS_NOTE,
                 (unsigned)diag::Severity::Fatal,
                 "%0 additional files entered using a total of %1B of space", 0,
                 false, false, true, false,
                 0) DIAG(note_file_sloc_usage, CLASS_NOTE,
                         (unsigned)diag::Severity::Fatal,
                         "file entered %0 time%s0 using %1B of "
                         "space%plural{0:|: plus %2B for macro expansions}2",
                         0, false, false, true, false, 0)
                DIAG(note_forward_declaration, CLASS_NOTE,
                     (unsigned)diag::Severity::Fatal,
                     "forward declaration of %0", 0, false, false, true, false,
                     0) DIAG(note_invalid_subexpr_in_const_expr, CLASS_NOTE,
                             (unsigned)diag::Severity::Fatal,
                             "subexpression not valid in a constant expression",
                             0, false, false, true,
                             false, 0) DIAG(note_matching, CLASS_NOTE,
                                            (unsigned)diag::Severity::Fatal,
                                            "to match this %0", 0, false, false,
                                            true, false, 0)
                    DIAG(note_pragma_entered_here, CLASS_NOTE,
                         (unsigned)diag::Severity::Fatal,
                         "#pragma entered here", 0, false, false, true,
                         false, 3) DIAG(note_previous_declaration, CLASS_NOTE,
                                        (unsigned)diag::Severity::Fatal,
                                        "previous declaration is here",
                                        0, false, false, true, false, 0)
                        DIAG(note_previous_definition, CLASS_NOTE,
                             (unsigned)diag::Severity::Fatal,
                             "previous definition is here", 0, false, false,
                             true, false, 0)
                            DIAG(note_previous_implicit_declaration, CLASS_NOTE,
                                 (unsigned)diag::Severity::Fatal,
                                 "previous implicit declaration is here", 0,
                                 false, false, true, false,
                                 0) DIAG(note_previous_use, CLASS_NOTE,
                                         (unsigned)diag::Severity::Fatal,
                                         "previous use is here", 0, false,
                                         false, true, false, 0)
                                DIAG(note_total_sloc_usage, CLASS_NOTE,
                                     (unsigned)diag::Severity::Fatal,
                                     "%0B in local locations, %1B in locations "
                                     "loaded from AST files, for a total of "
                                     "%2B (%3%% of available space)",
                                     0, false, false, true, false, 0)
                                    DIAG(note_type_being_defined, CLASS_NOTE,
                                         (unsigned)diag::Severity::Fatal,
                                         "definition of %0 is not complete "
                                         "until the closing '}'",
                                         0, false, false, true, false, 0)
                                        DIAG(note_valid_options, CLASS_NOTE,
                                             (unsigned)diag::Severity::Fatal,
                                             "valid target CPU values are: %0",
                                             0, false, false, true, false, 0)

    // Remark
    DIAG(remark_sloc_usage, CLASS_REMARK, (unsigned)diag::Severity::Remark,
         "source manager location address space usage:", 421, false, true, true,
         false, 0)

    // Warnings
    DIAG(warn_attribute_ignored, CLASS_WARNING,
         (unsigned)diag::Severity::Warning, "%0 attribute ignored", 205, false,
         false, true, false,
         0) DIAG(warn_c23_compat_bitint_suffix, CLASS_WARNING,
                 (unsigned)diag::Severity::Ignored,
                 "'_BitInt' suffix for literals is incompatible with C "
                 "standards before C23",
                 374, false, false, true, false,
                 0) DIAG(warn_c23_compat_empty_initializer, CLASS_WARNING,
                         (unsigned)diag::Severity::Ignored,
                         "use of an empty initializer is incompatible with C "
                         "standards before C23",
                         374, false, false, true, false, 3)
        DIAG(warn_duplicate_declspec, CLASS_WARNING,
             (unsigned)diag::Severity::Warning,
             "duplicate '%0' declaration specifier", 115, false, false, true,
             false, 0) DIAG(warn_incompatible_branch_protection_option,
                            CLASS_WARNING, (unsigned)diag::Severity::Warning,
                            "'-mbranch-protection=' option is incompatible "
                            "with the '%0' architecture",
                            49, false, false, true, false, 7)
            DIAG(warn_invalid_feature_combination, CLASS_WARNING,
                 (unsigned)diag::Severity::Warning,
                 "invalid feature combination: %0", 249, false, false, true,
                 false, 0) DIAG(warn_missing_type_specifier, CLASS_WARNING,
                                (unsigned)diag::Severity::Ignored,
                                "type specifier missing, defaults to 'int'",
                                219, false, false, true, false, 3)
                DIAG(warn_nullability_duplicate, CLASS_WARNING,
                     (unsigned)diag::Severity::Warning,
                     "duplicate nullability specifier %0", 329, false, false,
                     true, false,
                     7) DIAG(warn_old_implicitly_unsigned_long, CLASS_WARNING,
                             (unsigned)diag::Severity::Warning,
                             "integer literal is too large to be represented "
                             "in type 'long', interpreting as 'unsigned long' "
                             "per C89; this literal will %select{have type "
                             "'long long'|be ill-formed}0 in C99 onwards",
                             60, false, false, true, false, 0)
                    DIAG(
                        warn_poison_system_directories, CLASS_WARNING,
                        (unsigned)diag::Severity::Ignored,
                        "include location '%0' is unsafe for cross-compilation",
                        367, false, false, true, false,
                        0) DIAG(warn_pragma_debug_missing_argument,
                                CLASS_WARNING,
                                (unsigned)diag::Severity::Warning,
                                "missing argument to debug command '%0'", 209,
                                false, false, true, false,
                                1) DIAG(warn_pragma_debug_unexpected_argument,
                                        CLASS_WARNING,
                                        (unsigned)diag::Severity::Warning,
                                        "unexpected argument to debug command",
                                        209, false, false, true, false, 1)
                        DIAG(warn_slh_does_not_support_asm_goto, CLASS_WARNING,
                             (unsigned)diag::Severity::Warning,
                             "speculative load hardening does not protect "
                             "functions with asm goto",
                             420, false, false, true, false, 4)
                            DIAG(warn_stack_clash_protection_inline_asm,
                                 CLASS_WARNING,
                                 (unsigned)diag::Severity::Warning,
                                 "unable to protect inline asm that clobbers "
                                 "stack pointer against stack clash",
                                 425, false, false, true, false, 4)
                                DIAG(warn_stack_exhausted, CLASS_WARNING,
                                     (unsigned)diag::Severity::Warning,
                                     "stack nearly exhausted; compilation time "
                                     "may suffer, and crashes due to stack "
                                     "overflow are likely",
                                     424, false, false, true, false, 0)
                                    DIAG(warn_target_unrecognized_env,
                                         CLASS_WARNING,
                                         (unsigned)diag::Severity::Warning,
                                         "mismatch between architecture and "
                                         "environment in target triple '%0'; "
                                         "did you mean '%1'?",
                                         248, false, false, true, false, 0)
                                        DIAG(warn_unknown_attribute_ignored,
                                             CLASS_WARNING,
                                             (unsigned)diag::Severity::Warning,
                                             "unknown attribute %0 ignored",
                                             500, false, false, true, false, 0)

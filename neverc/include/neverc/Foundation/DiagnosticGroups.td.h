
#ifdef GET_DIAG_ARRAYS
static const int16_t DiagArrays[] = {
    /* Empty */ -1,
    /* DiagArray1 */ diag::warn_pragma_message,
    -1,
    /* DiagArray2 */ diag::pp_hash_warning,
    -1,
    /* DiagArray5 */ diag::warn_abs_too_small,
    diag::warn_pointer_abs,
    diag::warn_unsigned_abs,
    diag::warn_wrong_absolute_value_type,
    -1,
    /* DiagArray7 */ diag::warn_taking_address_of_packed_member,
    -1,
    /* DiagArray10 */ diag::warn_param_mismatched_alignment,
    -1,
    /* DiagArray12 */ diag::warn_alloca,
    -1,
    /* DiagArray13 */ diag::warn_alloca_align_alignof,
    -1,
    /* DiagArray14 */ diag::warn_pp_ambiguous_macro,
    -1,
    /* DiagArray15 */ diag::warn_arith_conv_mixed_anon_enum_types,
    -1,
    /* DiagArray16 */ diag::warn_argument_invalid_range,
    -1,
    /* DiagArray17 */ diag::warn_array_index_exceeds_bounds,
    diag::warn_array_index_exceeds_max_addressable_bounds,
    diag::warn_array_index_precedes_bounds,
    diag::warn_ptr_arith_exceeds_max_addressable_bounds,
    diag::warn_static_array_too_small,
    -1,
    /* DiagArray18 */ diag::warn_ptr_arith_exceeds_bounds,
    diag::warn_ptr_arith_precedes_bounds,
    -1,
    /* DiagArray19 */ diag::warn_inconsistent_array_form,
    -1,
    /* DiagArray21 */ diag::warn_asm_mismatched_size_modifier,
    -1,
    /* DiagArray22 */ diag::warn_not_in_enum_assignment,
    -1,
    /* DiagArray23 */ diag::warn_assume_side_effects,
    -1,
    /* DiagArray24 */ diag::warn_atomic_member_access,
    -1,
    /* DiagArray25 */ diag::warn_atomic_op_misaligned,
    diag::warn_atomic_op_oversized,
    -1,
    /* DiagArray26 */ diag::warn_atomic_implicit_seq_cst,
    -1,
    /* DiagArray27 */ diag::warn_atomic_op_has_invalid_memory_order,
    -1,
    /* DiagArray28 */ diag::warn_attribute_packed_for_bitfield,
    -1,
    /* DiagArray29 */ diag::warn_fe_backend_warning_attr,
    -1,
    /* DiagArray31 */ diag::ext_c23_auto_non_plain_identifier,
    -1,
    /* DiagArray33 */ diag::warn_availability_and_unavailable,
    diag::warn_availability_on_static_initializer,
    diag::warn_availability_unknown_platform,
    diag::warn_availability_version_ordering,
    diag::warn_expected_consistent_version_separator,
    diag::warn_mismatched_availability,
    -1,
    /* DiagArray34 */ diag::warn_fe_backend_plugin,
    diag::warn_fe_backend_resource_limit,
    -1,
    /* DiagArray35 */ diag::backslash_newline_space,
    -1,
    /* DiagArray36 */ diag::warn_bad_function_cast,
    -1,
    /* DiagArray38 */ diag::ext_bit_int,
    -1,
    /* DiagArray39 */ diag::warn_impcast_bitfield_precision_constant,
    -1,
    /* DiagArray40 */ diag::warn_bitfield_too_small_for_enum,
    diag::warn_signed_bitfield_enum_conversion,
    diag::warn_unsigned_bitfield_assigned_signed_enum,
    -1,
    /* DiagArray41 */ diag::warn_bitfield_width_exceeds_type_width,
    -1,
    /* DiagArray42 */ diag::warn_precedence_bitwise_conditional,
    -1,
    /* DiagArray43 */ diag::warn_bitwise_instead_of_logical,
    -1,
    /* DiagArray44 */ diag::warn_bitwise_op_in_bitwise_op,
    -1,
    /* DiagArray45 */ diag::warn_impcast_bool_to_null_pointer,
    -1,
    /* DiagArray47 */ diag::warn_bitwise_negation_bool,
    -1,
    /* DiagArray48 */ diag::warn_braces_around_init,
    -1,
    /* DiagArray49 */ diag::warn_incompatible_branch_protection_option,
    diag::warn_unsupported_branch_protection,
    diag::warn_unsupported_branch_protection_spec,
    -1,
    /* DiagArray50 */ diag::warn_assume_aligned_too_great,
    -1,
    /* DiagArray51 */ diag::ext_pp_redef_builtin_macro,
    diag::ext_pp_undef_builtin_macro,
    -1,
    /* DiagArray52 */ diag::warn_builtin_chk_overflow,
    -1,
    /* DiagArray53 */ diag::warn_implicit_decl_requires_sysheader,
    -1,
    /* DiagArray54 */ diag::ext_anonymous_union,
    diag::ext_c11_anonymous_struct,
    diag::ext_c11_feature,
    diag::ext_typecheck_compare_complete_incomplete_pointers,
    -1,
    /* DiagArray55 */ diag::ext_auto_type_specifier,
    diag::ext_c23_attr,
    diag::ext_using_attribute_ns,
    -1,
    /* DiagArray56 */ diag::warn_c23_keyword,
    -1,
    /* DiagArray57 */ diag::ext_c23_bitint_suffix,
    diag::ext_c23_pp_directive,
    diag::ext_c_empty_initializer,
    diag::ext_c_label_end_of_compound_statement,
    diag::ext_c_label_followed_by_declaration,
    diag::ext_c_nullptr,
    diag::ext_c_static_assert_no_message,
    diag::ext_parameter_name_omitted_c23,
    diag::warn_ext_c23_attributes,
    -1,
    /* DiagArray60 */ diag::warn_c99_compat_unicode_id,
    diag::warn_c99_compat_unicode_literal,
    diag::warn_c99_keyword,
    diag::warn_old_implicitly_unsigned_long,
    -1,
    /* DiagArray61 */ diag::ext_designated_init,
    -1,
    /* DiagArray62 */ diag::ext_aggregate_init_not_constant,
    diag::ext_c99_array_usage,
    diag::ext_c99_compound_literal,
    diag::ext_c99_feature,
    diag::ext_c99_flexible_array_member,
    diag::ext_c99_variable_decl_in_for_loop,
    diag::ext_c99_whitespace_required_after_macro_name,
    diag::ext_empty_fnmacro_arg,
    diag::ext_enumerator_list_comma_c,
    diag::ext_hex_constant_invalid,
    -1,
    /* DiagArray63 */ diag::warn_cast_align,
    -1,
    /* DiagArray64 */ diag::warn_cast_calling_conv,
    -1,
    /* DiagArray65 */ diag::warn_cast_function_type,
    -1,
    /* DiagArray66 */ diag::warn_cast_function_type_strict,
    -1,
    /* DiagArray67 */ diag::warn_cast_qual,
    diag::warn_cast_qual2,
    -1,
    /* DiagArray69 */ diag::warn_subscript_is_char,
    -1,
    /* DiagArray70 */ diag::warn_comma_operator,
    -1,
    /* DiagArray71 */ diag::escaped_newline_block_comment_end,
    diag::ext_line_comment,
    diag::ext_multi_line_line_comment,
    diag::warn_nested_block_comment,
    -1,
    /* DiagArray73 */ diag::ext_typecheck_comparison_of_distinct_pointers,
    -1,
    /* DiagArray74 */ diag::ext_complex_component_init,
    -1,
    /* DiagArray76 */ diag::warn_compound_token_split_by_macro,
    -1,
    /* DiagArray77 */ diag::warn_compound_token_split_by_whitespace,
    -1,
    /* DiagArray78 */ diag::ext_typecheck_cond_pointer_integer_mismatch,
    -1,
    /* DiagArray80 */ diag::warn_impcast_integer_precision_constant,
    -1,
    /* DiagArray81 */ diag::warn_logical_instead_of_bitwise,
    -1,
    /* DiagArray82 */ diag::warn_impcast_complex_scalar,
    diag::warn_impcast_vector_scalar,
    -1,
    /* DiagArray84 */ diag::warn_unreachable_default,
    -1,
    /* DiagArray88 */ diag::warn_dangling_else,
    -1,
    /* DiagArray89 */ diag::warn_drv_darwin_sdk_invalid_settings,
    -1,
    /* DiagArray90 */ diag::warn_pp_date_time,
    -1,
    /* DiagArray91 */ diag::warn_debug_compression_unavailable,
    -1,
    /* DiagArray92 */ diag::ext_mixed_decls_code,
    diag::warn_mixed_decls_code,
    -1,
    /* DiagArray93 */ diag::ext_delimited_escape_sequence,
    -1,
    /* DiagArray94 */ diag::warn_O4_is_O3,
    -1,
    /* DiagArray96 */ diag::warn_deprecated_noreturn_spelling,
    diag::warn_type_attribute_deprecated_on_decl,
    diag::warn_vector_mode_deprecated,
    -1,
    /* DiagArray98 */ diag::warn_deprecated,
    diag::warn_deprecated_message,
    -1,
    /* DiagArray103 */ diag::warn_non_prototype_changes_behavior,
    diag::warn_strict_uses_without_prototype,
    -1,
    /* DiagArray104 */ diag::warn_pragma_deprecated_macro_use,
    -1,
    /* DiagArray105 */ diag::warn_ext_int_deprecated,
    -1,
    /* DiagArray106 */ diag::warn_deprecated_string_literal_conversion,
    -1,
    /* DiagArray107 */ diag::pp_disabled_macro_expansion,
    -1,
    /* DiagArray111 */ diag::warn_remainder_division_by_zero,
    -1,
    /* DiagArray112 */ diag::warn_attribute_dll_redeclaration,
    -1,
    /* DiagArray113 */ diag::ext_dollar_in_identifier,
    -1,
    /* DiagArray114 */ diag::warn_impcast_double_promotion,
    -1,
    /* DiagArray115 */ diag::ext_duplicate_declspec,
    diag::ext_warn_duplicate_declspec,
    diag::warn_attribute_address_multiple_identical_qualifiers,
    diag::warn_duplicate_declspec,
    -1,
    /* DiagArray116 */ diag::warn_duplicate_enum_values,
    -1,
    /* DiagArray117 */ diag::ext_enum_base_in_type_specifier,
    -1,
    /* DiagArray118 */ diag::ext_embedded_directive,
    -1,
    /* DiagArray119 */ diag::warn_empty_for_body,
    diag::warn_empty_if_body,
    diag::warn_empty_switch_body,
    diag::warn_empty_while_body,
    -1,
    /* DiagArray120 */ diag::ext_empty_translation_unit,
    -1,
    /* DiagArray122 */ diag::warn_comparison_mixed_enum_types,
    -1,
    /* DiagArray123 */ diag::warn_conditional_mixed_enum_types,
    -1,
    /* DiagArray124 */ diag::warn_comparison_of_mixed_enum_types_switch,
    -1,
    /* DiagArray125 */ diag::warn_impcast_different_enum_types,
    -1,
    /* DiagArray126 */ diag::warn_arith_conv_mixed_enum_types,
    -1,
    /* DiagArray127 */ diag::warn_arith_conv_enum_float,
    -1,
    /* DiagArray128 */ diag::ext_enum_too_large,
    diag::ext_enumerator_increment_too_large,
    -1,
    /* DiagArray129 */ diag::ext_excess_initializers,
    diag::ext_excess_initializers_for_sizeless_type,
    diag::ext_excess_initializers_in_char_array_initializer,
    diag::ext_initializer_string_for_char_array_too_long,
    -1,
    /* DiagArray130 */ diag::warn_anyx86_excessive_regsave,
    -1,
    /* DiagArray131 */ diag::warn_defined_in_function_type_macro,
    diag::warn_defined_in_object_type_macro,
    -1,
    /* DiagArray132 */ diag::warn_extern_init,
    -1,
    /* DiagArray134 */ diag::ext_extra_semi,
    -1,
    /* DiagArray135 */ diag::warn_null_statement,
    -1,
    /* DiagArray136 */ diag::ext_pp_extra_tokens_at_eol,
    -1,
    /* DiagArray137 */ diag::warn_pragma_final_macro,
    -1,
    /* DiagArray138 */ diag::ext_c_enum_fixed_underlying_type,
    -1,
    /* DiagArray139 */ diag::warn_fixedpoint_constant_overflow,
    -1,
    /* DiagArray140 */ diag::warn_flag_enum_constant_out_of_range,
    -1,
    /* DiagArray141 */ diag::ext_flexible_array_in_array,
    diag::ext_flexible_array_in_struct,
    -1,
    /* DiagArray142 */ diag::warn_impcast_float_integer,
    -1,
    /* DiagArray143 */ diag::warn_floatingpoint_eq,
    -1,
    /* DiagArray144 */ diag::warn_impcast_float_to_integer,
    diag::warn_impcast_float_to_integer_out_of_range,
    -1,
    /* DiagArray145 */ diag::warn_impcast_float_to_integer_zero,
    -1,
    /* DiagArray146 */ diag::warn_redundant_loop_iteration,
    diag::warn_variables_not_in_loop_body,
    -1,
    /* DiagArray147 */ diag::warn_format_P_no_precision,
    diag::warn_format_argument_needs_cast,
    diag::warn_format_bool_as_character,
    diag::warn_format_conversion_argument_type_mismatch,
    diag::warn_format_invalid_annotation,
    diag::warn_format_invalid_positional_specifier,
    diag::warn_format_mix_positional_nonpositional_args,
    diag::warn_format_nonsensical_length,
    diag::warn_format_string_is_wide_literal,
    diag::warn_format_zero_positional_specifier,
    diag::warn_missing_format_string,
    diag::warn_printf_asterisk_missing_arg,
    diag::warn_printf_asterisk_wrong_type,
    diag::warn_printf_format_string_contains_null_char,
    diag::warn_printf_format_string_not_null_terminated,
    diag::warn_printf_ignored_flag,
    diag::warn_printf_incomplete_specifier,
    diag::warn_printf_nonsensical_flag,
    diag::warn_printf_nonsensical_optional_amount,
    diag::warn_printf_positional_arg_exceeds_data_args,
    diag::warn_scanf_nonzero_width,
    diag::warn_scanf_scanlist_incomplete,
    -1,
    /* DiagArray148 */ diag::warn_printf_data_arg_not_used,
    -1,
    /* DiagArray149 */ diag::warn_printf_insufficient_data_args,
    -1,
    /* DiagArray150 */ diag::warn_format_invalid_conversion,
    -1,
    /* DiagArray151 */ diag::warn_format_non_standard,
    diag::warn_format_non_standard_conversion_spec,
    diag::warn_format_non_standard_positional_arg,
    -1,
    /* DiagArray152 */ diag::warn_format_nonliteral,
    -1,
    /* DiagArray153 */ diag::warn_format_overflow,
    -1,
    /* DiagArray154 */ diag::warn_format_argument_needs_cast_pedantic,
    diag::warn_format_conversion_argument_type_mismatch_pedantic,
    -1,
    /* DiagArray155 */ diag::warn_format_nonliteral_noargs,
    -1,
    /* DiagArray156 */ diag::warn_format_truncation,
    -1,
    /* DiagArray157 */
    diag::warn_format_conversion_argument_type_mismatch_confusion,
    -1,
    /* DiagArray159 */ diag::warn_empty_format_string,
    -1,
    /* DiagArray161 */ diag::warn_fortify_scanf_overflow,
    diag::warn_fortify_source_overflow,
    diag::warn_fortify_source_size_mismatch,
    diag::warn_fortify_strlen_overflow,
    -1,
    /* DiagArray162 */ diag::warn_four_char_character_literal,
    -1,
    /* DiagArray163 */ diag::warn_frame_address,
    -1,
    /* DiagArray164 */ diag::warn_fe_backend_frame_larger_than,
    diag::warn_fe_frame_larger_than,
    -1,
    /* DiagArray166 */ diag::warn_framework_include_private_from_public,
    -1,
    /* DiagArray167 */ diag::warn_free_nonheap_object,
    -1,
    /* DiagArray168 */ diag::warn_dispatch_body_ignored,
    diag::warn_multiversion_duplicate_entries,
    diag::warn_target_clone_duplicate_options,
    diag::warn_target_clone_no_impact_options,
    -1,
    /* DiagArray170 */ diag::ext_neverc_diagnose_if,
    diag::ext_neverc_enable_if,
    diag::warn_attribute_on_function_definition,
    diag::warn_break_binds_to_switch,
    diag::warn_gcc_ignores_type_attr,
    diag::warn_gcc_requires_variadic_function,
    diag::warn_gcc_variable_decl_in_for_loop,
    diag::warn_loop_ctrl_binds_to_inner,
    -1,
    /* DiagArray171 */ diag::ext_generic_with_type_arg,
    -1,
    /* DiagArray172 */ diag::warn_drv_global_isel_incomplete,
    diag::warn_drv_global_isel_incomplete_opt,
    -1,
    /* DiagArray174 */ diag::ext_alignof_expr,
    -1,
    /* DiagArray176 */ diag::ext_auto_type,
    -1,
    /* DiagArray177 */ diag::ext_binary_literal,
    -1,
    /* DiagArray178 */ diag::ext_gnu_case_range,
    -1,
    /* DiagArray179 */ diag::ext_integer_complex,
    -1,
    /* DiagArray180 */ diag::ext_array_init_copy,
    -1,
    /* DiagArray181 */ diag::ext_gnu_conditional_expr,
    -1,
    /* DiagArray182 */ diag::ext_gnu_array_range,
    diag::ext_gnu_missing_equal_designator,
    diag::ext_gnu_old_style_field_designator,
    -1,
    /* DiagArray184 */ diag::ext_empty_struct_union,
    diag::ext_no_named_members_in_struct_union,
    -1,
    /* DiagArray185 */ diag::ext_flexible_array_init,
    -1,
    /* DiagArray187 */ diag::ext_expr_not_ice,
    diag::ext_vla_folded_to_constant,
    -1,
    /* DiagArray188 */ diag::ext_imaginary_constant,
    -1,
    /* DiagArray189 */ diag::ext_pp_include_next_directive,
    -1,
    /* DiagArray190 */ diag::ext_gnu_address_of_label,
    diag::ext_gnu_indirect_goto,
    -1,
    /* DiagArray191 */ diag::ext_pp_gnu_line_directive,
    -1,
    /* DiagArray192 */ diag::warn_gnu_null_ptr_arith,
    -1,
    /* DiagArray193 */ diag::ext_type_defined_in_offsetof,
    -1,
    /* DiagArray194 */ diag::ext_gnu_ptr_func_arith,
    diag::ext_gnu_subscript_void_type,
    diag::ext_gnu_void_ptr,
    -1,
    /* DiagArray195 */ diag::ext_forward_ref_enum_def,
    -1,
    /* DiagArray196 */ diag::ext_gnu_statement_expr,
    -1,
    /* DiagArray197 */ diag::ext_gnu_statement_expr_macro,
    -1,
    /* DiagArray199 */ diag::ext_typecheck_cast_to_union,
    -1,
    /* DiagArray200 */ diag::ext_variable_sized_type_in_struct,
    -1,
    /* DiagArray201 */ diag::ext_pp_line_zero,
    -1,
    /* DiagArray202 */ diag::ext_missing_varargs_arg,
    diag::ext_paste_comma,
    -1,
    /* DiagArray203 */ diag::warn_header_guard,
    -1,
    /* DiagArray205 */ diag::warn_alias_to_weak_alias,
    diag::warn_alias_with_section,
    diag::warn_attribute_has_no_effect_on_infinite_loop,
    diag::warn_attribute_ignored,
    diag::warn_attribute_ignored_no_calls_in_stmt,
    diag::warn_attribute_ignored_non_function_pointer,
    diag::warn_attribute_ignored_on_inline,
    diag::warn_attribute_ignored_on_non_definition,
    diag::warn_attribute_invalid_on_definition,
    diag::warn_attribute_no_decl,
    diag::warn_attribute_nonnull_no_pointers,
    diag::warn_attribute_nonnull_parm_no_args,
    diag::warn_attribute_not_on_decl,
    diag::warn_attribute_pointer_or_reference_only,
    diag::warn_attribute_pointers_only,
    diag::warn_attribute_precede_definition,
    diag::warn_attribute_return_pointers_only,
    diag::warn_attribute_return_pointers_refs_only,
    diag::warn_attribute_sentinel_named_arguments,
    diag::warn_attribute_sentinel_not_variadic,
    diag::warn_attribute_type_not_supported,
    diag::warn_attribute_unknown_visibility,
    diag::warn_attribute_void_function,
    diag::warn_attribute_wrong_decl_type,
    diag::warn_attribute_wrong_decl_type_str,
    diag::warn_attributes_likelihood_ifstmt_conflict,
    diag::warn_cconv_unsupported,
    diag::warn_declspec_allocator_nonpointer,
    diag::warn_declspec_attribute_ignored,
    diag::warn_dllimport_dropped_from_inline_function,
    diag::warn_duplicate_attribute,
    diag::warn_duplicate_attribute_exact,
    diag::warn_function_attribute_ignored_in_stmt,
    diag::warn_function_stmt_attribute_precedence,
    diag::warn_gnu_inline_attribute_requires_inline,
    diag::warn_internal_linkage_local_storage,
    diag::warn_microsoft_qualifiers_ignored,
    diag::warn_nocf_check_attribute_ignored,
    diag::warn_noderef_on_non_pointer_or_array,
    diag::warn_transparent_union_attribute_field_size_align,
    diag::warn_transparent_union_attribute_floating,
    diag::warn_transparent_union_attribute_not_definition,
    diag::warn_transparent_union_attribute_zero_fields,
    diag::warn_type_attribute_wrong_type,
    diag::warn_unhandled_ms_attribute_ignored,
    diag::warn_unsupported_target_attribute,
    diag::warn_unused_result_typedef_unsupported_spelling,
    diag::warn_wrong_attr_namespace,
    -1,
    /* DiagArray206 */ diag::warn_drv_unsupported_opt_for_target,
    -1,
    /* DiagArray207 */ diag::warn_pragma_intrinsic_builtin,
    -1,
    /* DiagArray209 */ diag::warn_pragma_align_expected_equal,
    diag::warn_pragma_align_invalid_option,
    diag::warn_pragma_comment_ignored,
    diag::warn_pragma_debug_dependent_argument,
    diag::warn_pragma_debug_missing_argument,
    diag::warn_pragma_debug_missing_command,
    diag::warn_pragma_debug_unexpected_argument,
    diag::warn_pragma_debug_unexpected_command,
    diag::warn_pragma_expected_action_or_r_paren,
    diag::warn_pragma_expected_comma,
    diag::warn_pragma_expected_identifier,
    diag::warn_pragma_expected_init_seg,
    diag::warn_pragma_expected_lparen,
    diag::warn_pragma_expected_non_wide_string,
    diag::warn_pragma_expected_punc,
    diag::warn_pragma_expected_rparen,
    diag::warn_pragma_expected_section_label_or_name,
    diag::warn_pragma_expected_section_name,
    diag::warn_pragma_expected_section_push_pop_or_name,
    diag::warn_pragma_expected_string,
    diag::warn_pragma_extra_tokens_at_eol,
    diag::warn_pragma_fp_ignored,
    diag::warn_pragma_init_seg_unsupported_target,
    diag::warn_pragma_invalid_action,
    diag::warn_pragma_invalid_argument,
    diag::warn_pragma_invalid_specific_action,
    diag::warn_pragma_missing_argument,
    diag::warn_pragma_ms_fenv_access,
    diag::warn_pragma_ms_struct,
    diag::warn_pragma_options_align_reset_failed,
    diag::warn_pragma_options_expected_align,
    diag::warn_pragma_pack_invalid_alignment,
    diag::warn_pragma_pack_malformed,
    diag::warn_pragma_pop_failed,
    diag::warn_pragma_pop_macro_no_push,
    diag::warn_pragma_unsupported_action,
    diag::warn_pragma_unused_expected_var,
    diag::warn_pragma_unused_expected_var_arg,
    diag::warn_pragma_unused_undeclared_var,
    diag::warn_stdc_unknown_rounding_mode,
    -1,
    /* DiagArray210 */ diag::warn_qual_return_type,
    -1,
    /* DiagArray212 */ diag::warn_impcast_integer_float_precision_constant,
    -1,
    /* DiagArray213 */ diag::warn_impcast_floating_point_to_bool,
    -1,
    /* DiagArray216 */ diag::warn_impcast_fixed_point_range,
    -1,
    /* DiagArray217 */ diag::warn_impcast_float_precision,
    diag::warn_impcast_float_result_precision,
    -1,
    /* DiagArray218 */ diag::ext_implicit_function_decl_c99,
    diag::ext_implicit_lib_function_decl,
    diag::ext_implicit_lib_function_decl_c99,
    diag::warn_builtin_unknown,
    diag::warn_implicit_function_decl,
    -1,
    /* DiagArray219 */ diag::ext_missing_type_specifier,
    diag::ext_param_not_declared,
    diag::warn_missing_type_specifier,
    -1,
    /* DiagArray220 */ diag::warn_impcast_high_order_zero_bits,
    diag::warn_impcast_integer_precision,
    -1,
    /* DiagArray221 */ diag::warn_impcast_integer_float_precision,
    -1,
    /* DiagArray222 */ diag::ext_integer_literal_too_large_for_signed,
    -1,
    /* DiagArray224 */ diag::ext_pp_import_directive,
    -1,
    /* DiagArray225 */ diag::pp_include_next_absolute_path,
    -1,
    /* DiagArray226 */ diag::pp_include_next_in_primary,
    -1,
    /* DiagArray227 */
    diag::ext_typecheck_convert_incompatible_function_pointer,
    -1,
    /* DiagArray228 */
    diag::warn_typecheck_convert_incompatible_function_pointer_strict,
    -1,
    /* DiagArray229 */ diag::warn_redecl_library_builtin,
    -1,
    /* DiagArray230 */ diag::warn_section_msvc_compat,
    -1,
    /* DiagArray231 */ diag::warn_npot_ms_struct,
    -1,
    /* DiagArray232 */ diag::ext_typecheck_convert_incompatible_pointer,
    -1,
    /* DiagArray233 */ diag::ext_nested_pointer_qualifier_mismatch,
    diag::ext_typecheck_convert_discards_qualifiers,
    -1,
    /* DiagArray234 */ diag::warn_incompatible_sysroot,
    -1,
    /* DiagArray235 */ diag::warn_implicit_decl_no_jmp_buf,
    -1,
    /* DiagArray236 */
    diag::warn_redeclaration_without_attribute_prev_attribute_ignored,
    diag::warn_redeclaration_without_import_attribute,
    -1,
    /* DiagArray239 */ diag::warn_initializer_overrides,
    -1,
    /* DiagArray241 */ diag::warn_fe_inline_asm,
    -1,
    /* DiagArray242 */ diag::ext_typecheck_convert_int_pointer,
    diag::ext_typecheck_convert_pointer_int,
    -1,
    /* DiagArray244 */ diag::warn_enum_constant_in_bool_context,
    diag::warn_left_shift_in_bool_context,
    -1,
    /* DiagArray245 */ diag::warn_int_to_pointer_cast,
    -1,
    /* DiagArray246 */ diag::warn_int_to_void_pointer_cast,
    -1,
    /* DiagArray247 */ diag::warn_integer_constant_overflow,
    -1,
    /* DiagArray248 */ diag::warn_drv_optimization_value,
    diag::warn_fe_backend_invalid_feature_flag,
    diag::warn_fe_backend_readonly_feature_flag,
    diag::warn_target_unrecognized_env,
    -1,
    /* DiagArray249 */ diag::warn_invalid_feature_combination,
    -1,
    /* DiagArray250 */ diag::warn_attribute_no_builtin_invalid_builtin_name,
    -1,
    /* DiagArray251 */ diag::warn_noreturn_function_has_return_expr,
    -1,
    /* DiagArray254 */ diag::ext_empty_character,
    diag::ext_unterminated_char_or_string,
    -1,
    /* DiagArray255 */ diag::warn_bad_character_encoding,
    diag::warn_bad_string_encoding,
    -1,
    /* DiagArray256 */ diag::ext_pp_bad_paste_ms,
    -1,
    /* DiagArray257 */ diag::warn_unevaluated_string_prefix,
    -1,
    /* DiagArray258 */ diag::warn_invalid_utf8_in_comment,
    -1,
    /* DiagArray259 */ diag::warn_jump_out_of_seh_finally,
    -1,
    /* DiagArray260 */ diag::ext_keyword_as_ident,
    -1,
    /* DiagArray261 */ diag::warn_pp_macro_hides_keyword,
    -1,
    /* DiagArray262 */ diag::ext_param_promoted_not_compatible_with_prototype,
    -1,
    /* DiagArray263 */ diag::ext_token_used,
    -1,
    /* DiagArray264 */ diag::warn_parameter_size,
    diag::warn_return_value_size,
    -1,
    /* DiagArray265 */ diag::warn_impcast_literal_float_to_integer,
    diag::warn_impcast_literal_float_to_integer_out_of_range,
    -1,
    /* DiagArray266 */ diag::warn_float_compare_literal,
    diag::warn_float_overflow,
    diag::warn_float_underflow,
    -1,
    /* DiagArray267 */ diag::warn_logical_not_on_lhs_of_check,
    -1,
    /* DiagArray268 */ diag::warn_logical_and_in_logical_or,
    -1,
    /* DiagArray269 */ diag::ext_c99_longlong,
    -1,
    /* DiagArray271 */ diag::ext_pp_macro_redef,
    -1,
    /* DiagArray272 */ diag::ext_noreturn_main,
    diag::ext_variadic_main,
    diag::warn_main_one_arg,
    diag::warn_main_redefined,
    diag::warn_static_main,
    -1,
    /* DiagArray273 */ diag::ext_main_returns_nonint,
    -1,
    /* DiagArray274 */ diag::warn_has_warning_invalid_option,
    -1,
    /* DiagArray275 */ diag::ext_many_braces_around_init,
    -1,
    /* DiagArray276 */ diag::ext_mathematical_notation,
    -1,
    /* DiagArray277 */ diag::warn_max_tokens,
    diag::warn_max_tokens_total,
    -1,
    /* DiagArray278 */ diag::warn_suspicious_sizeof_memset,
    -1,
    /* DiagArray279 */ diag::warn_memsize_comparison,
    -1,
    /* DiagArray281 */ diag::ext_ms_anonymous_record,
    -1,
    /* DiagArray282 */ diag::ext_ms_impcast_fn_obj,
    -1,
    /* DiagArray283 */ diag::ext_charize_microsoft,
    -1,
    /* DiagArray284 */ diag::ext_comment_paste_microsoft,
    -1,
    /* DiagArray285 */ diag::ext_default_init_const,
    -1,
    /* DiagArray286 */ diag::warn_attribute_section_drectve,
    -1,
    /* DiagArray287 */ diag::ext_ctrl_z_eof_microsoft,
    -1,
    /* DiagArray288 */ diag::ext_ms_forward_ref_enum,
    -1,
    /* DiagArray289 */ diag::ext_enumerator_too_large,
    -1,
    /* DiagArray290 */ diag::ext_ms_c_enum_fixed_underlying_type,
    -1,
    /* DiagArray291 */ diag::ext_flexible_array_empty_aggregate_ms,
    diag::ext_flexible_array_union_ms,
    -1,
    /* DiagArray292 */ diag::ext_goto_into_protected_scope,
    -1,
    /* DiagArray293 */ diag::ext_pp_include_search_ms,
    -1,
    /* DiagArray294 */ diag::ext_init_from_predefined,
    -1,
    /* DiagArray295 */ diag::ext_static_non_static,
    -1,
    /* DiagArray296 */ diag::ext_ms_static_assert,
    -1,
    /* DiagArray297 */ diag::ext_string_literal_from_predefined,
    -1,
    /* DiagArray298 */ diag::warn_misleading_indentation,
    -1,
    /* DiagArray301 */ diag::warn_missing_braces,
    -1,
    /* DiagArray302 */ diag::ext_no_declarators,
    diag::ext_typedef_without_a_name,
    diag::warn_standalone_specifier,
    -1,
    /* DiagArray303 */ diag::warn_missing_field_initializers,
    -1,
    /* DiagArray307 */ diag::warn_cconv_knr,
    -1,
    /* DiagArray308 */ diag::warn_missing_prototype,
    -1,
    /* DiagArray309 */ diag::warn_missing_sysroot,
    -1,
    /* DiagArray310 */ diag::warn_assume_attribute_string_unknown_suggested,
    -1,
    /* DiagArray313 */ diag::warn_multichar_character_literal,
    -1,
    /* DiagArray315 */ diag::ext_no_newline_eof,
    -1,
    /* DiagArray316 */ diag::warn_dereference_of_noderef_type,
    diag::warn_dereference_of_noderef_type_no_decl,
    diag::warn_noderef_to_dereferenceable_pointer,
    -1,
    /* DiagArray317 */ diag::warn_non_literal_null_pointer,
    -1,
    /* DiagArray318 */ diag::warn_cannot_pass_non_pod_arg_to_vararg,
    diag::warn_non_pod_vararg_with_format_string,
    diag::warn_second_parameter_to_va_arg_not_pod,
    -1,
    /* DiagArray319 */ diag::warn_alignment_not_power_of_two,
    -1,
    /* DiagArray320 */ diag::warn_null_arg,
    diag::warn_null_ret,
    -1,
    /* DiagArray321 */ diag::pp_nonportable_path,
    -1,
    /* DiagArray322 */ diag::pp_nonportable_system_path,
    -1,
    /* DiagArray324 */ diag::null_in_char_or_string,
    diag::null_in_file,
    -1,
    /* DiagArray325 */ diag::warn_impcast_null_pointer_to_integer,
    -1,
    /* DiagArray326 */ diag::warn_indirection_through_null,
    -1,
    /* DiagArray327 */ diag::warn_pointer_arith_null_ptr,
    -1,
    /* DiagArray328 */ diag::warn_pointer_sub_null_ptr,
    -1,
    /* DiagArray329 */ diag::warn_mismatched_nullability_attr,
    diag::warn_nullability_duplicate,
    -1,
    /* DiagArray330 */ diag::warn_nullability_missing,
    -1,
    /* DiagArray331 */ diag::warn_nullability_missing_array,
    -1,
    /* DiagArray332 */ diag::warn_nullability_declspec,
    -1,
    /* DiagArray333 */ diag::ext_nullability,
    -1,
    /* DiagArray334 */ diag::warn_nullability_inferred_on_nested_type,
    -1,
    /* DiagArray335 */ diag::warn_nullability_lost,
    -1,
    /* DiagArray337 */ diag::warn_drv_fjmc_for_elf_only,
    diag::warn_drv_jmc_requires_debuginfo,
    diag::warn_drv_moutline_atomics_unsupported_opt,
    diag::warn_drv_moutline_unsupported_opt,
    -1,
    /* DiagArray338 */
    diag::ext_typecheck_ordered_comparison_of_function_pointers,
    -1,
    /* DiagArray340 */ diag::ext_use_out_of_scope_declaration,
    -1,
    /* DiagArray342 */ diag::ext_string_too_long,
    -1,
    /* DiagArray344 */ diag::warn_fe_override_module,
    -1,
    /* DiagArray345 */ diag::warn_drv_overriding_option,
    -1,
    /* DiagArray346 */ diag::warn_unnecessary_packed,
    -1,
    /* DiagArray347 */ diag::warn_unpacked_field,
    -1,
    /* DiagArray348 */ diag::warn_padded_struct_anon_field,
    diag::warn_padded_struct_field,
    diag::warn_padded_struct_size,
    -1,
    /* DiagArray349 */ diag::warn_padded_struct_anon_bitfield,
    diag::warn_padded_struct_bitfield,
    -1,
    /* DiagArray350 */ diag::warn_condition_is_assignment,
    diag::warn_precedence_bitwise_rel,
    diag::warn_precedence_conditional,
    -1,
    /* DiagArray351 */ diag::warn_equality_with_extra_parens,
    -1,
    /* DiagArray353 */ diag::remark_fe_backend_optimization_remark,
    -1,
    /* DiagArray354 */ diag::remark_fe_backend_optimization_remark_analysis,
    diag::remark_fe_backend_optimization_remark_analysis_aliasing,
    diag::remark_fe_backend_optimization_remark_analysis_fpcommute,
    -1,
    /* DiagArray355 */ diag::warn_fe_backend_optimization_failure,
    -1,
    /* DiagArray356 */ diag::remark_fe_backend_optimization_remark_missed,
    -1,
    /* DiagArray357 */ diag::ext_aggregate_init_not_constant,
    diag::ext_c23_attr,
    diag::ext_c99_array_usage,
    diag::ext_c99_compound_literal,
    diag::ext_c99_feature,
    diag::ext_c99_flexible_array_member,
    diag::ext_c99_variable_decl_in_for_loop,
    diag::ext_c_empty_initializer,
    diag::ext_c_nullptr,
    diag::ext_duplicate_declspec,
    diag::ext_empty_fnmacro_arg,
    diag::ext_enum_value_not_int,
    diag::ext_enumerator_list_comma_c,
    diag::ext_expr_not_ice,
    diag::ext_forward_ref_enum,
    diag::ext_freestanding_complex,
    diag::ext_gnu_array_range,
    diag::ext_hex_constant_invalid,
    diag::ext_ident_list_in_param,
    diag::ext_implicit_function_decl_c99,
    diag::ext_integer_complement_complex,
    diag::ext_integer_increment_complex,
    diag::ext_internal_in_extern_inline_quiet,
    diag::ext_line_comment,
    diag::ext_mixed_decls_code,
    diag::ext_multi_line_line_comment,
    diag::ext_named_variadic_macro,
    diag::ext_neverc_diagnose_if,
    diag::ext_neverc_enable_if,
    diag::ext_nonstandard_escape,
    diag::ext_pp_bad_vaargs_use,
    diag::ext_pp_comma_expr,
    diag::ext_pp_ident_directive,
    diag::ext_pp_line_too_big,
    diag::ext_pp_warning_directive,
    diag::ext_return_has_void_expr,
    diag::ext_sizeof_alignof_function_type,
    diag::ext_sizeof_alignof_void_type,
    diag::ext_subscript_non_lvalue,
    diag::ext_thread_before,
    diag::ext_typecheck_addrof_void,
    diag::ext_typecheck_cast_nonscalar,
    diag::ext_typecheck_comparison_of_fptr_to_void,
    diag::ext_typecheck_cond_one_void,
    diag::ext_typecheck_convert_pointer_void_func,
    diag::ext_variadic_macro,
    diag::ext_vla,
    diag::warn_defined_in_function_type_macro,
    diag::warn_ext_c23_attributes,
    diag::warn_format_conversion_argument_type_mismatch_pedantic,
    diag::warn_strict_prototypes,
    -1,
    /* DiagArray359 */ diag::ext_sizeof_alignof_function_type,
    diag::ext_sizeof_alignof_void_type,
    diag::warn_sub_ptr_zero_size_types,
    -1,
    /* DiagArray360 */ diag::warn_cast_nonnull_to_bool,
    diag::warn_impcast_pointer_to_bool,
    -1,
    /* DiagArray361 */ diag::warn_pointer_compare,
    -1,
    /* DiagArray362 */ diag::ext_typecheck_comparison_of_pointer_integer,
    -1,
    /* DiagArray363 */ diag::ext_typecheck_convert_incompatible_pointer_sign,
    -1,
    /* DiagArray364 */ diag::warn_pointer_to_enum_cast,
    -1,
    /* DiagArray365 */ diag::warn_pointer_to_int_cast,
    -1,
    /* DiagArray366 */ diag::ext_typecheck_cond_incompatible_pointers,
    -1,
    /* DiagArray367 */ diag::warn_poison_system_directories,
    -1,
    /* DiagArray369 */ diag::warn_pragma_attribute_unused,
    -1,
    /* DiagArray370 */ diag::pp_pragma_once_in_main_file,
    -1,
    /* DiagArray371 */ diag::warn_pragma_pack_no_pop_eof,
    -1,
    /* DiagArray372 */ diag::pp_pragma_sysheader_in_main_file,
    -1,
    /* DiagArray373 */ diag::warn_no_support_for_eval_method_source_on_m32,
    -1,
    /* DiagArray374 */ diag::warn_c17_compat_ellipsis_only_parameter,
    diag::warn_c17_compat_static_assert_no_message,
    diag::warn_c23_compat_bitint_suffix,
    diag::warn_c23_compat_digit_separator,
    diag::warn_c23_compat_empty_initializer,
    diag::warn_c23_compat_keyword,
    diag::warn_c23_compat_label_end_of_compound_statement,
    diag::warn_c23_compat_label_followed_by_declaration,
    diag::warn_c23_compat_literal_ucn_control_character,
    diag::warn_c23_compat_literal_ucn_escape_basic_scs,
    diag::warn_c23_compat_pp_directive,
    diag::warn_c23_compat_warning_directive,
    diag::warn_pre_c23_compat_attributes,
    -1,
    /* DiagArray376 */ diag::ext_predef_outside_function,
    -1,
    /* DiagArray377 */ diag::warn_private_extern,
    -1,
    /* DiagArray378 */ diag::warn_avx_calling_convention,
    -1,
    /* DiagArray379 */ diag::err_func_returning_qualified_void,
    -1,
    /* DiagArray380 */ diag::warn_quoted_include_in_framework_header,
    -1,
    /* DiagArray381 */ diag::warn_var_decl_not_read_only,
    -1,
    /* DiagArray383 */ diag::remark_fe_backend_plugin,
    -1,
    /* DiagArray385 */ diag::warn_reserved_extern_symbol,
    -1,
    /* DiagArray386 */ diag::warn_pp_macro_is_reserved_id,
    -1,
    /* DiagArray387 */ diag::warn_pragma_restrict_expansion_macro_use,
    -1,
    /* DiagArray389 */ diag::warn_ret_addr_label,
    diag::warn_ret_local_temp_addr_ref,
    diag::warn_ret_stack_addr_ref,
    -1,
    /* DiagArray390 */ diag::ext_return_has_expr,
    diag::ext_return_missing_expr,
    diag::warn_return_missing_expr,
    -1,
    /* DiagArray391 */ diag::remark_pp_search_path_usage,
    -1,
    /* DiagArray392 */ diag::warn_attribute_section_on_redeclaration,
    diag::warn_duplicate_codeseg_attribute,
    diag::warn_mismatched_section,
    -1,
    /* DiagArray393 */ diag::warn_self_assignment_builtin,
    -1,
    /* DiagArray394 */ diag::warn_identity_field_assign,
    -1,
    /* DiagArray395 */ diag::warn_missing_sentinel,
    diag::warn_not_enough_argument,
    -1,
    /* DiagArray397 */ diag::warn_decl_shadow,
    -1,
    /* DiagArray401 */ diag::warn_shift_negative,
    -1,
    /* DiagArray402 */ diag::warn_shift_gt_typewidth,
    -1,
    /* DiagArray403 */ diag::warn_shift_lhs_negative,
    -1,
    /* DiagArray404 */ diag::warn_addition_in_bitshift,
    -1,
    /* DiagArray405 */ diag::warn_shift_result_gt_typewidth,
    -1,
    /* DiagArray406 */ diag::warn_shift_result_sets_sign_bit,
    -1,
    /* DiagArray407 */ diag::warn_impcast_integer_64_32,
    -1,
    /* DiagArray408 */ diag::warn_mixed_sign_comparison,
    -1,
    /* DiagArray409 */ diag::warn_impcast_integer_sign,
    diag::warn_impcast_integer_sign_conditional,
    diag::warn_impcast_nonnegative_result,
    -1,
    /* DiagArray412 */ diag::ext_wchar_t_sign_spec,
    -1,
    /* DiagArray413 */ diag::warn_impcast_single_bit_bitield_precision_constant,
    -1,
    /* DiagArray414 */ diag::warn_sizeof_array_param,
    -1,
    /* DiagArray415 */ diag::warn_sizeof_array_decay,
    -1,
    /* DiagArray416 */ diag::warn_division_sizeof_array,
    -1,
    /* DiagArray417 */ diag::warn_division_sizeof_ptr,
    -1,
    /* DiagArray418 */ diag::warn_sizeof_pointer_expr_memaccess,
    diag::warn_sizeof_pointer_type_memaccess,
    -1,
    /* DiagArray419 */ diag::warn_slash_u_filename,
    -1,
    /* DiagArray420 */ diag::warn_slh_does_not_support_asm_goto,
    -1,
    /* DiagArray421 */ diag::remark_sloc_usage,
    -1,
    /* DiagArray423 */ diag::warn_fe_source_mgr,
    -1,
    /* DiagArray424 */ diag::warn_stack_exhausted,
    -1,
    /* DiagArray425 */ diag::warn_stack_clash_protection_inline_asm,
    -1,
    /* DiagArray427 */ diag::ext_internal_in_extern_inline,
    diag::ext_internal_in_extern_inline_quiet,
    -1,
    /* DiagArray428 */ diag::warn_static_local_in_extern_inline,
    -1,
    /* DiagArray441 */ diag::warn_strict_prototypes,
    -1,
    /* DiagArray442 */ diag::warn_stringcompare,
    -1,
    /* DiagArray443 */ diag::warn_concatenated_literal_array_init,
    -1,
    /* DiagArray444 */ diag::warn_impcast_string_literal_to_bool,
    -1,
    /* DiagArray445 */ diag::warn_string_plus_char,
    -1,
    /* DiagArray446 */ diag::warn_string_plus_int,
    -1,
    /* DiagArray447 */ diag::warn_strlcpycat_wrong_size,
    -1,
    /* DiagArray448 */ diag::warn_strncat_large_size,
    diag::warn_strncat_src_size,
    diag::warn_strncat_wrong_size,
    -1,
    /* DiagArray449 */ diag::warn_suspicious_bzero_size,
    -1,
    /* DiagArray451 */ diag::warn_case_value_overflow,
    diag::warn_missing_case,
    diag::warn_not_in_enum,
    -1,
    /* DiagArray452 */ diag::warn_bool_switch_condition,
    -1,
    /* DiagArray453 */ diag::warn_switch_default,
    -1,
    /* DiagArray454 */ diag::warn_def_missing_case,
    -1,
    /* DiagArray455 */ diag::warn_sync_op_misaligned,
    -1,
    /* DiagArray456 */ diag::warn_sync_fetch_and_nand_semantics_change,
    -1,
    /* DiagArray458 */ diag::warn_target_clone_mixed_values,
    -1,
    /* DiagArray460 */ diag::warn_alignment_builtin_useless,
    diag::warn_comparison_always,
    -1,
    /* DiagArray461 */ diag::warn_integer_constants_in_conditional_always_true,
    diag::warn_left_shift_always,
    diag::warn_tautological_bool_compare,
    -1,
    /* DiagArray463 */ diag::warn_out_of_range_compare,
    -1,
    /* DiagArray466 */ diag::warn_nonnull_expr_compare,
    diag::warn_null_pointer_compare,
    -1,
    /* DiagArray467 */ diag::warn_tautological_constant_compare,
    -1,
    /* DiagArray469 */ diag::warn_unsigned_char_always_true_comparison,
    -1,
    /* DiagArray470 */ diag::warn_unsigned_enum_always_true_comparison,
    -1,
    /* DiagArray471 */ diag::warn_unsigned_always_true_comparison,
    -1,
    /* DiagArray472 */ diag::warn_tautological_compare_value_range,
    -1,
    /* DiagArray473 */ diag::warn_tcb_enforcement_violation,
    -1,
    /* DiagArray474 */ diag::ext_typecheck_decl_incomplete_type,
    -1,
    /* DiagArray475 */ diag::trigraph_converted,
    diag::trigraph_ends_block_comment,
    diag::trigraph_ignored,
    diag::trigraph_ignored_block_comment,
    -1,
    /* DiagArray477 */ diag::warn_type_safety_null_pointer_required,
    diag::warn_type_safety_type_mismatch,
    diag::warn_type_tag_for_datatype_wrong_kind,
    -1,
    /* DiagArray478 */ diag::ext_redefinition_of_typedef,
    -1,
    /* DiagArray479 */ diag::warn_fe_unable_to_open_stats_file,
    -1,
    /* DiagArray480 */ diag::warn_unaligned_access,
    -1,
    /* DiagArray481 */ diag::warn_imp_cast_drops_unaligned,
    -1,
    /* DiagArray482 */ diag::warn_pp_undef_identifier,
    -1,
    /* DiagArray483 */ diag::warn_pp_undef_prefix,
    -1,
    /* DiagArray484 */ diag::warn_attribute_arm_sm_incompat_builtin,
    -1,
    /* DiagArray485 */ diag::warn_attribute_arm_za_builtin_no_za_state,
    -1,
    /* DiagArray487 */ diag::warn_undefined_inline,
    -1,
    /* DiagArray488 */ diag::warn_undefined_internal,
    -1,
    /* DiagArray489 */ diag::warn_side_effects_unevaluated_context,
    -1,
    /* DiagArray490 */ diag::warn_unguarded_availability,
    -1,
    /* DiagArray491 */ diag::warn_unguarded_availability_new,
    -1,
    /* DiagArray492 */ diag::warn_delimited_ucn_empty,
    diag::warn_delimited_ucn_incomplete,
    diag::warn_ucn_escape_incomplete,
    diag::warn_ucn_escape_no_digits,
    diag::warn_ucn_not_valid_in_c89,
    diag::warn_ucn_not_valid_in_c89_literal,
    -1,
    /* DiagArray493 */ diag::warn_utf8_symbol_homoglyph,
    -1,
    /* DiagArray494 */ diag::ext_unicode_whitespace,
    -1,
    /* DiagArray495 */ diag::warn_utf8_symbol_zero_width,
    -1,
    /* DiagArray498 */ diag::warn_drv_potentially_misspelled_joined_argument,
    -1,
    /* DiagArray499 */ diag::warn_assume_attribute_string_unknown,
    -1,
    /* DiagArray500 */ diag::warn_unknown_attribute_ignored,
    -1,
    /* DiagArray501 */ diag::warn_pp_invalid_directive,
    -1,
    /* DiagArray502 */ diag::ext_unknown_escape,
    -1,
    /* DiagArray503 */ diag::ext_on_off_switch_syntax,
    diag::ext_pragma_syntax_eod,
    diag::ext_stdc_pragma_ignored,
    diag::warn_pragma_diagnostic_cannot_pop,
    diag::warn_pragma_diagnostic_invalid,
    diag::warn_pragma_diagnostic_invalid_option,
    diag::warn_pragma_diagnostic_invalid_token,
    diag::warn_pragma_exec_charset_expected,
    diag::warn_pragma_exec_charset_push_invalid,
    diag::warn_pragma_exec_charset_spec_invalid,
    diag::warn_pragma_ignored,
    diag::warn_pragma_include_alias_expected,
    diag::warn_pragma_include_alias_expected_filename,
    diag::warn_pragma_include_alias_mismatch_angle,
    diag::warn_pragma_include_alias_mismatch_quote,
    diag::warn_pragma_warning_expected,
    diag::warn_pragma_warning_expected_number,
    diag::warn_pragma_warning_push_level,
    diag::warn_pragma_warning_spec_invalid,
    diag::warn_stdc_fenv_round_not_supported,
    -1,
    /* DiagArray504 */ diag::warn_pragma_diagnostic_unknown_warning,
    diag::warn_unknown_diag_option,
    diag::warn_unknown_warning_specifier,
    -1,
    /* DiagArray505 */ diag::warn_unneeded_internal_decl,
    diag::warn_unneeded_static_internal_decl,
    -1,
    /* DiagArray510 */ diag::warn_unreachable_association,
    -1,
    /* DiagArray513 */ diag::warn_unsequenced_mod_mod,
    diag::warn_unsequenced_mod_use,
    -1,
    /* DiagArray515 */ diag::warn_fe_backend_unsupported_fp_exceptions,
    diag::warn_fe_backend_unsupported_fp_rounding,
    -1,
    /* DiagArray516 */ diag::warn_drv_dwarf_version_limited_by_target,
    diag::warn_drv_unsupported_debug_info_opt_for_target,
    -1,
    /* DiagArray517 */ diag::warn_attribute_protected_visibility,
    -1,
    /* DiagArray520 */ diag::warn_unused_but_set_parameter,
    -1,
    /* DiagArray521 */ diag::warn_unused_but_set_variable,
    -1,
    /* DiagArray522 */ diag::warn_drv_empty_joined_argument,
    diag::warn_drv_input_file_unused,
    diag::warn_drv_invalid_arch_name_with_suggestion,
    diag::warn_drv_large_data_threshold_invalid_code_model,
    diag::warn_drv_preprocessed_input_file_unused,
    diag::warn_drv_unused_argument,
    diag::warn_drv_unused_x,
    diag::warn_ignoring_fdiscard_for_bitcode,
    diag::warn_ignoring_verify_debuginfo_preserve_export,
    -1,
    /* DiagArray523 */ diag::warn_unused_comparison,
    -1,
    /* DiagArray524 */ diag::warn_unused_const_variable,
    -1,
    /* DiagArray525 */ diag::warn_unused_function,
    -1,
    /* DiagArray526 */ diag::warn_unused_label,
    -1,
    /* DiagArray527 */ diag::warn_unused_local_typedef,
    -1,
    /* DiagArray529 */ diag::pp_macro_not_used,
    -1,
    /* DiagArray530 */ diag::warn_unused_parameter,
    -1,
    /* DiagArray531 */ diag::warn_unused_result,
    diag::warn_unused_result_msg,
    -1,
    /* DiagArray532 */ diag::warn_unused_call,
    diag::warn_unused_comma_left_operand,
    diag::warn_unused_expr,
    diag::warn_unused_voidptr,
    -1,
    /* DiagArray533 */ diag::warn_unused_variable,
    -1,
    /* DiagArray534 */ diag::warn_unused_volatile,
    -1,
    /* DiagArray535 */ diag::warn_used_but_marked_unused,
    -1,
    /* DiagArray536 */ diag::warn_diagnose_if_succeeded,
    -1,
    /* DiagArray537 */ diag::warn_second_arg_of_va_start_not_last_named_param,
    diag::warn_second_parameter_to_va_arg_never_compatible,
    diag::warn_va_start_type_is_undefined,
    -1,
    /* DiagArray538 */ diag::ext_named_variadic_macro,
    diag::ext_pp_bad_vaopt_use,
    diag::ext_variadic_macro,
    -1,
    /* DiagArray539 */ diag::warn_typecheck_vector_element_sizes_not_equal,
    -1,
    /* DiagArray540 */ diag::warn_incompatible_vectors,
    -1,
    /* DiagArray542 */ diag::warn_decl_in_param_list,
    diag::warn_redefinition_in_param_list,
    -1,
    /* DiagArray543 */ diag::warn_vla_used,
    -1,
    /* DiagArray544 */ diag::ext_vla,
    -1,
    /* DiagArray546 */ diag::warn_void_pointer_to_enum_cast,
    -1,
    /* DiagArray547 */ diag::warn_void_pointer_to_int_cast,
    -1,
    /* DiagArray548 */ diag::ext_typecheck_indirection_through_void_pointer,
    -1,
    /* DiagArray552 */ diag::warn_xor_used_as_pow,
    diag::warn_xor_used_as_pow_base,
    diag::warn_xor_used_as_pow_base_extra,
    -1,
    /* DiagArray553 */ diag::ext_typecheck_zero_array_size,
    -1,
};

static const int16_t DiagSubGroups[] = {
    /* Empty */ -1,
    /* DiagSubGroup0 */ 133,
    -1,
    /* DiagSubGroup3 */ 11,
    133,
    -1,
    /* DiagSubGroup6 */ 360,
    442,
    466,
    -1,
    /* DiagSubGroup11 */ 311,
    350,
    451,
    452,
    298,
    347,
    544,
    -1,
    /* DiagSubGroup15 */ 95,
    -1,
    /* DiagSubGroup20 */ 21,
    -1,
    /* DiagSubGroup30 */ 500,
    205,
    -1,
    /* DiagSubGroup37 */ 177,
    -1,
    /* DiagSubGroup39 */ 413,
    -1,
    /* DiagSubGroup45 */ 360,
    486,
    -1,
    /* DiagSubGroup46 */ 45,
    -1,
    /* DiagSubGroup47 */ 43,
    -1,
    /* DiagSubGroup58 */ 56,
    -1,
    /* DiagSubGroup59 */ 57,
    -1,
    /* DiagSubGroup62 */ 61,
    -1,
    /* DiagSubGroup65 */ 66,
    -1,
    /* DiagSubGroup72 */ 71,
    -1,
    /* DiagSubGroup75 */ 76,
    77,
    -1,
    /* DiagSubGroup80 */ 39,
    -1,
    /* DiagSubGroup82 */ 45,
    80,
    125,
    40,
    142,
    407,
    242,
    220,
    217,
    265,
    317,
    325,
    409,
    444,
    -1,
    /* DiagSubGroup83 */ 325,
    -1,
    /* DiagSubGroup85 */ 2,
    -1,
    /* DiagSubGroup87 */ 389,
    -1,
    /* DiagSubGroup94 */ 96,
    98,
    97,
    104,
    105,
    106,
    -1,
    /* DiagSubGroup110 */ 111,
    -1,
    /* DiagSubGroup121 */ 136,
    -1,
    /* DiagSubGroup122 */ 124,
    99,
    -1,
    /* DiagSubGroup123 */ 100,
    -1,
    /* DiagSubGroup125 */ 126,
    127,
    123,
    -1,
    /* DiagSubGroup126 */ 101,
    -1,
    /* DiagSubGroup127 */ 102,
    -1,
    /* DiagSubGroup133 */ 303,
    210,
    239,
    408,
    530,
    520,
    327,
    328,
    443,
    -1,
    /* DiagSubGroup142 */ 144,
    145,
    -1,
    /* DiagSubGroup147 */ 148,
    159,
    320,
    155,
    158,
    150,
    149,
    153,
    156,
    -1,
    /* DiagSubGroup160 */ 152,
    155,
    158,
    -1,
    /* DiagSubGroup161 */ 153,
    156,
    -1,
    /* DiagSubGroup165 */ 164,
    -1,
    /* DiagSubGroup168 */ 458,
    -1,
    /* DiagSubGroup173 */ 174,
    175,
    176,
    177,
    178,
    179,
    180,
    181,
    182,
    184,
    544,
    185,
    186,
    187,
    188,
    189,
    190,
    191,
    192,
    193,
    194,
    195,
    196,
    198,
    199,
    200,
    553,
    201,
    202,
    -1,
    /* DiagSubGroup196 */ 197,
    -1,
    /* DiagSubGroup209 */ 207,
    208,
    -1,
    /* DiagSubGroup211 */ 218,
    219,
    -1,
    /* DiagSubGroup214 */ 215,
    -1,
    /* DiagSubGroup217 */ 221,
    -1,
    /* DiagSubGroup221 */ 212,
    -1,
    /* DiagSubGroup232 */ 233,
    227,
    -1,
    /* DiagSubGroup243 */ 242,
    -1,
    /* DiagSubGroup245 */ 246,
    -1,
    /* DiagSubGroup248 */ 206,
    -1,
    /* DiagSubGroup270 */ 146,
    -1,
    /* DiagSubGroup280 */ 283,
    286,
    293,
    290,
    289,
    295,
    288,
    292,
    291,
    282,
    285,
    281,
    284,
    287,
    296,
    294,
    297,
    236,
    -1,
    /* DiagSubGroup311 */ 19,
    47,
    69,
    71,
    147,
    146,
    163,
    211,
    237,
    244,
    301,
    313,
    390,
    393,
    414,
    415,
    446,
    460,
    475,
    496,
    503,
    518,
    549,
    377,
    536,
    -1,
    /* DiagSubGroup312 */ 293,
    -1,
    /* DiagSubGroup327 */ 192,
    -1,
    /* DiagSubGroup330 */ 331,
    -1,
    /* DiagSubGroup343 */ 239,
    -1,
    /* DiagSubGroup346 */ 347,
    -1,
    /* DiagSubGroup348 */ 349,
    -1,
    /* DiagSubGroup350 */ 268,
    267,
    42,
    44,
    404,
    351,
    88,
    -1,
    /* DiagSubGroup352 */ 490,
    -1,
    /* DiagSubGroup357 */ 37,
    54,
    61,
    134,
    141,
    176,
    177,
    178,
    179,
    180,
    181,
    184,
    185,
    188,
    189,
    190,
    191,
    192,
    193,
    194,
    195,
    196,
    199,
    201,
    202,
    261,
    269,
    283,
    284,
    287,
    289,
    290,
    291,
    295,
    315,
    342,
    553,
    113,
    263,
    258,
    93,
    224,
    118,
    120,
    333,
    138,
    171,
    38,
    31,
    74,
    -1,
    /* DiagSubGroup358 */ 104,
    271,
    51,
    387,
    137,
    -1,
    /* DiagSubGroup359 */ 194,
    -1,
    /* DiagSubGroup364 */ 546,
    -1,
    /* DiagSubGroup365 */ 364,
    547,
    -1,
    /* DiagSubGroup373 */ 503,
    209,
    369,
    371,
    -1,
    /* DiagSubGroup375 */ 374,
    -1,
    /* DiagSubGroup384 */ 386,
    -1,
    /* DiagSubGroup385 */ 386,
    -1,
    /* DiagSubGroup388 */ 389,
    -1,
    /* DiagSubGroup393 */ 394,
    -1,
    /* DiagSubGroup396 */ 513,
    -1,
    /* DiagSubGroup398 */ 397,
    400,
    399,
    -1,
    /* DiagSubGroup426 */ 198,
    -1,
    /* DiagSubGroup441 */ 103,
    -1,
    /* DiagSubGroup450 */ 418,
    323,
    278,
    449,
    -1,
    /* DiagSubGroup460 */ 461,
    466,
    465,
    459,
    468,
    464,
    -1,
    /* DiagSubGroup461 */ 463,
    -1,
    /* DiagSubGroup462 */ 476,
    472,
    -1,
    /* DiagSubGroup476 */ 467,
    471,
    469,
    470,
    -1,
    /* DiagSubGroup489 */ 368,
    -1,
    /* DiagSubGroup490 */ 491,
    -1,
    /* DiagSubGroup496 */ 422,
    429,
    497,
    -1,
    /* DiagSubGroup506 */ 511,
    509,
    510,
    -1,
    /* DiagSubGroup507 */ 506,
    508,
    512,
    -1,
    /* DiagSubGroup518 */ 519,
    525,
    526,
    527,
    532,
    533,
    521,
    -1,
    /* DiagSubGroup525 */ 505,
    -1,
    /* DiagSubGroup528 */ 527,
    -1,
    /* DiagSubGroup532 */ 523,
    531,
    489,
    -1,
    /* DiagSubGroup533 */ 524,
    -1,
    /* DiagSubGroup541 */ 540,
    -1,
    /* DiagSubGroup543 */ 544,
    -1,
    /* DiagSubGroup544 */ 545,
    -1,
    /* DiagSubGroup547 */ 546,
    -1,
    /* DiagSubGroup550 */ 106,
    -1,
    /* DiagSubGroup551 */ 550,
    -1,
};

static const char DiagGroupNames[] = {
    "\000\020#pragma-messages\t#warnings\003CL4\003abi\016absolute-value\007"
    "address\030address-of-packed-member\024address-of-temporary\020aggregat"
    "e-return\016align-mismatch\003all\006alloca\031alloca-with-align-aligno"
    "f\017ambiguous-macro\031anon-enum-enum-conversion\026argument-outside-r"
    "ange\014array-bounds\037array-bounds-pointer-arithmetic\017array-parame"
    "ter\003asm\022asm-operand-widths\013assign-enum\006assume\015atomic-acc"
    "ess\020atomic-alignment\027atomic-implicit-seq-cst\026atomic-memory-ord"
    "ering\035attribute-packed-for-bitfield\021attribute-warning\nattributes"
    "\024auto-decl-extensions\013auto-import\014availability\016backend-plug"
    "in\030backslash-newline-escape\021bad-function-cast\016binary-literal\021"
    "bit-int-extension\034bitfield-constant-conversion\030bitfield-enum-conv"
    "ersion\016bitfield-width\037bitwise-conditional-parentheses\032bitwise-"
    "instead-of-logical\026bitwise-op-parentheses\017bool-conversion\020bool"
    "-conversions\016bool-operation\022braced-scalar-init\021branch-protecti"
    "on builtin-assume-aligned-alignment\027builtin-macro-redefined\027built"
    "in-memcpy-chk-size\027builtin-requires-header\016c11-extensions\030c23-"
    "attribute-extensions\nc23-compat\016c23-extensions\nc2x-compat\016c2x-e"
    "xtensions\nc99-compat\016c99-designator\016c99-extensions\ncast-align\027"
    "cast-calling-convention\022cast-function-type\031cast-function-type-str"
    "ict\tcast-qual\nchar-align\017char-subscripts\005comma\007comment\010co"
    "mments\036compare-distinct-pointer-types\026complex-component-init\024c"
    "ompound-token-split\035compound-token-split-by-macro\035compound-token-"
    "split-by-space\031conditional-type-mismatch\031conditional-uninitialize"
    "d\023constant-conversion\030constant-logical-operand\nconversion\017con"
    "version-null\026covered-switch-default\003cpp\021ctor-dtor-privacy\010d"
    "angling\015dangling-else\023darwin-sdk-settings\tdate-time\035debug-com"
    "pression-unavailable\033declaration-after-statement#delimited-escape-se"
    "quence-extension\ndeprecated$deprecated-anon-enum-enum-conversion\025de"
    "precated-attributes\023deprecated-builtins\027deprecated-declarations\027"
    "deprecated-enum-compare#deprecated-enum-compare-conditional\037deprecat"
    "ed-enum-enum-conversion deprecated-enum-float-conversion\030deprecated-"
    "non-prototype\021deprecated-pragma\017deprecated-type\033deprecated-wri"
    "table-strings\030disabled-macro-expansion\025disabled-optimization\014d"
    "iscard-qual\013div-by-zero\020division-by-zero\036dll-attribute-on-rede"
    "claration\036dollar-in-identifier-extension\020double-promotion\030dupl"
    "icate-decl-specifier\016duplicate-enum\024elaborated-enum-base\022embed"
    "ded-directive\nempty-body\026empty-translation-unit\014endif-labels\014"
    "enum-compare\030enum-compare-conditional\023enum-compare-switch\017enum"
    "-conversion\024enum-enum-conversion\025enum-float-conversion\016enum-to"
    "o-large\023excess-initializers\021excessive-regsave\024expansion-to-def"
    "ined\022extern-initializer\005extra\nextra-semi\017extra-semi-stmt\014e"
    "xtra-tokens\013final-macro\024fixed-enum-extension\024fixed-point-overf"
    "low\tflag-enum\031flexible-array-extensions\020float-conversion\013floa"
    "t-equal\031float-overflow-conversion\025float-zero-conversion\021for-lo"
    "op-analysis\006format\021format-extra-args\030format-insufficient-args\030"
    "format-invalid-specifier\016format-non-iso\021format-nonliteral\017form"
    "at-overflow\017format-pedantic\017format-security\021format-truncation\025"
    "format-type-confusion\nformat-y2k\022format-zero-length\010format=2\016"
    "fortify-source\023four-char-constants\015frame-address\021frame-larger-"
    "than\022frame-larger-than=%framework-include-private-from-public\023fre"
    "e-nonheap-object\025function-multiversion\015future-compat\ngcc-compat\026"
    "generic-type-extension\013global-isel\003gnu\026gnu-alignof-expression\024"
    "gnu-anonymous-struct\015gnu-auto-type\022gnu-binary-literal\016gnu-case"
    "-range\023gnu-complex-integer gnu-compound-literal-initializer\037gnu-c"
    "onditional-omitted-operand\016gnu-designator\025gnu-empty-initializer\020"
    "gnu-empty-struct\036gnu-flexible-array-initializer\037gnu-flexible-arra"
    "y-union-member\024gnu-folding-constant\026gnu-imaginary-constant\020gnu"
    "-include-next\022gnu-label-as-value\017gnu-line-marker\033gnu-null-poin"
    "ter-arithmetic\027gnu-offsetof-extensions\021gnu-pointer-arith\023gnu-r"
    "edeclared-enum\030gnu-statement-expression-gnu-statement-expression-fro"
    "m-macro-expansion\025gnu-static-float-init\016gnu-union-cast\"gnu-varia"
    "ble-sized-type-not-at-end\027gnu-zero-line-directive!gnu-zero-variadic-"
    "macro-arguments\014header-guard\016header-hygiene\022ignored-attributes"
    "\035ignored-optimization-argument\030ignored-pragma-intrinsic\027ignore"
    "d-pragma-optimize\017ignored-pragmas\022ignored-qualifiers\010implicit#"
    "implicit-const-int-float-conversion*implicit-conversion-floating-point-"
    "to-bool\024implicit-fallthrough!implicit-fallthrough-per-function\037im"
    "plicit-fixed-point-conversion\031implicit-float-conversion\035implicit-"
    "function-declaration\014implicit-int\027implicit-int-conversion\035impl"
    "icit-int-float-conversion\033implicitly-unsigned-literal\006import&impo"
    "rt-preprocessor-directive-pedantic\032include-next-absolute-path\033inc"
    "lude-next-outside-header#incompatible-function-pointer-types*incompatib"
    "le-function-pointer-types-strict\"incompatible-library-redeclaration\036"
    "incompatible-ms-pragma-section\026incompatible-ms-struct\032incompatibl"
    "e-pointer-types.incompatible-pointer-types-discards-qualifiers\024incom"
    "patible-sysroot\035incomplete-setjmp-declaration\026inconsistent-dllimp"
    "ort\022infinite-recursion\tinit-self\025initializer-overrides\006inline"
    "\ninline-asm\016int-conversion\017int-conversions\023int-in-bool-contex"
    "t\023int-to-pointer-cast\030int-to-void-pointer-cast\020integer-overflo"
    "w\035invalid-command-line-argument\033invalid-feature-combination\030in"
    "valid-no-builtin-names\020invalid-noreturn\020invalid-offsetof invalid-"
    "or-nonexistent-directory\020invalid-pp-token\027invalid-source-encoding"
    "\023invalid-token-paste\032invalid-unevaluated-string\014invalid-utf8\020"
    "jump-seh-finally\016keyword-compat\015keyword-macro\026knr-promoted-par"
    "ameter\030language-extension-token\023large-by-value-copy\022literal-co"
    "nversion\015literal-range\027logical-not-parentheses\026logical-op-pare"
    "ntheses\tlong-long\015loop-analysis\017macro-redefined\004main\020main-"
    "return-type\027malformed-warning-check\036many-braces-around-scalar-ini"
    "t*mathematical-notation-identifier-extension\nmax-tokens\026memset-tran"
    "sposed-args\022memsize-comparison\tmicrosoft\022microsoft-anon-tag\016m"
    "icrosoft-cast\021microsoft-charize\027microsoft-comment-paste\024micros"
    "oft-const-init\031microsoft-drectve-section\025microsoft-end-of-file mi"
    "crosoft-enum-forward-reference\024microsoft-enum-value\024microsoft-fix"
    "ed-enum\030microsoft-flexible-array\016microsoft-goto\021microsoft-incl"
    "ude\036microsoft-init-from-predefined\032microsoft-redeclare-static\027"
    "microsoft-static-assert(microsoft-string-literal-from-predefined\026mis"
    "leading-indentation\032mismatched-parameter-types\027mismatched-return-"
    "types\016missing-braces\024missing-declarations\032missing-field-initia"
    "lizers\030missing-format-attribute\024missing-include-dirs\020missing-n"
    "oreturn\030missing-prototype-for-cc\022missing-prototypes\017missing-sy"
    "sroot\025misspelled-assumption\004most\014msvc-include\tmultichar\016ne"
    "sted-externs\013newline-eof\007noderef\033non-literal-null-conversion\017"
    "non-pod-varargs\032non-power-of-two-alignment\007nonnull\030nonportable"
    "-include-path\037nonportable-system-include-path\024nontrivial-memacces"
    "s\016null-character\017null-conversion\020null-dereference\027null-poin"
    "ter-arithmetic\030null-pointer-subtraction\013nullability\030nullabilit"
    "y-completeness\"nullability-completeness-on-arrays\024nullability-decls"
    "pec\025nullability-extension#nullability-inferred-on-nested-type\036nul"
    "lable-to-nonnull-conversion\024old-style-definition\016option-ignored!o"
    "rdered-compare-function-pointers\027out-of-line-declaration\025out-of-s"
    "cope-function\010overflow\022overlength-strings\015override-init\017ove"
    "rride-module\021overriding-option\006packed\016packed-non-pod\006padded"
    "\017padded-bitfield\013parentheses\024parentheses-equality\024partial-a"
    "vailability\004pass\015pass-analysis\013pass-failed\013pass-missed\010p"
    "edantic\017pedantic-macros\015pointer-arith\027pointer-bool-conversion\017"
    "pointer-compare\027pointer-integer-compare\014pointer-sign\024pointer-t"
    "o-enum-cast\023pointer-to-int-cast\025pointer-type-mismatch\031poison-s"
    "ystem-directories potentially-evaluated-expression\027pragma-neverc-att"
    "ribute\032pragma-once-outside-header\013pragma-pack#pragma-system-heade"
    "r-outside-header\007pragmas\016pre-c23-compat\016pre-c2x-compat&predefi"
    "ned-identifier-outside-function\016private-extern\005psabi\032qualified"
    "-void-return-type\"quoted-include-in-framework-header\017read-only-type"
    "s\017redundant-decls\025remark-backend-plugin\021reserved-id-macro\023r"
    "eserved-identifier\031reserved-macro-identifier\022restrict-expansion\021"
    "return-local-addr\024return-stack-address\013return-type\021search-path"
    "-usage\007section\013self-assign\021self-assign-field\010sentinel\016se"
    "quence-point\006shadow\nshadow-all\014shadow-field\027shadow-uncaptured"
    "-local\024shift-count-negative\024shift-count-overflow\024shift-negativ"
    "e-value\024shift-op-parentheses\016shift-overflow\023shift-sign-overflo"
    "w\020shorten-64-to-32\014sign-compare\017sign-conversion\nsign-promo\024"
    "signed-enum-bitfield\025signed-unsigned-wchar'single-bit-bitfield-const"
    "ant-conversion\025sizeof-array-argument\022sizeof-array-decay\020sizeof"
    "-array-div\022sizeof-pointer-div\030sizeof-pointer-memaccess\020slash-u"
    "-filename\014slh-asm-goto\nsloc-usage\027sometimes-uninitialized\nsourc"
    "e-mgr\017stack-exhausted\017stack-protector\021static-float-init\020sta"
    "tic-in-inline\026static-local-in-inline\020static-self-init\017strict-a"
    "liasing\021strict-aliasing=0\021strict-aliasing=1\021strict-aliasing=2\017"
    "strict-overflow\021strict-overflow=0\021strict-overflow=1\021strict-ove"
    "rflow=2\021strict-overflow=3\021strict-overflow=4\021strict-overflow=5\021"
    "strict-prototypes\016string-compare\024string-concatenation\021string-c"
    "onversion\020string-plus-char\017string-plus-int\024strlcpy-strlcat-siz"
    "e\014strncat-size\020suspicious-bzero\024suspicious-memaccess\006switch"
    "\013switch-bool\016switch-default\013switch-enum\016sync-alignment%sync"
    "-fetch-and-nand-semantics-changed\005synth\036target-clones-mixed-speci"
    "fiers\034tautological-bitwise-compare\024tautological-compare\035tautol"
    "ogical-constant-compare&tautological-constant-in-range-compare*tautolog"
    "ical-constant-out-of-range-compare\035tautological-negation-compare\034"
    "tautological-overlap-compare\034tautological-pointer-compare\037tautolo"
    "gical-type-limit-compare\036tautological-undefined-compare'tautological"
    "-unsigned-char-zero-compare'tautological-unsigned-enum-zero-compare\"ta"
    "utological-unsigned-zero-compare tautological-value-range-compare\017tc"
    "b-enforcement$tentative-definition-incomplete-type\ttrigraphs\013type-l"
    "imits\013type-safety\024typedef-redefinition\031unable-to-open-stats-fi"
    "le\020unaligned-access!unaligned-qualifier-implicit-cast\005undef\014un"
    "def-prefix\027undefined-arm-streaming\020undefined-arm-za\031undefined-"
    "bool-conversion\020undefined-inline\022undefined-internal\026unevaluate"
    "d-expression\026unguarded-availability\032unguarded-availability-new\007"
    "unicode\021unicode-homoglyph\022unicode-whitespace\022unicode-zero-widt"
    "h\015uninitialized\035uninitialized-const-reference\020unknown-argument"
    "\022unknown-assumption\022unknown-attributes\022unknown-directives\027u"
    "nknown-escape-sequence\017unknown-pragmas\026unknown-warning-option\035"
    "unneeded-internal-declaration\020unreachable-code\033unreachable-code-a"
    "ggressive\026unreachable-code-break\034unreachable-code-fallthrough\036"
    "unreachable-code-generic-assoc\037unreachable-code-loop-increment\027un"
    "reachable-code-return\013unsequenced\017unsupported-abi\036unsupported-"
    "floating-point-opt\026unsupported-target-opt\026unsupported-visibility\006"
    "unused\017unused-argument\030unused-but-set-parameter\027unused-but-set"
    "-variable\034unused-command-line-argument\021unused-comparison\025unuse"
    "d-const-variable\017unused-function\014unused-label\024unused-local-typ"
    "edef\025unused-local-typedefs\015unused-macros\020unused-parameter\015u"
    "nused-result\014unused-value\017unused-variable\026unused-volatile-lval"
    "ue\026used-but-marked-unused\025user-defined-warnings\007varargs\017var"
    "iadic-macros\015vec-elem-size\021vector-conversion\022vector-conversion"
    "s\nvisibility\003vla\015vla-extension\033vla-extension-static-assert\031"
    "void-pointer-to-enum-cast\030void-pointer-to-int-cast\024void-ptr-deref"
    "erence\025volatile-register-var\020writable-strings\015write-strings\017"
    "xor-used-as-pow\021zero-length-array"};

#endif // GET_DIAG_ARRAYS

#ifdef DIAG_ENTRY
DIAG_ENTRY(anonymous_39 /*  */, 0, 0, /* DiagSubGroup0 */ 1, R"()")
DIAG_ENTRY(PoundPragmaMessage /* #pragma-messages */, 1, /* DiagArray1 */ 1, 0,
           R"()")
DIAG_ENTRY(PoundWarning /* #warnings */, 18, /* DiagArray2 */ 3, 0, R"()")
DIAG_ENTRY(anonymous_38 /* CL4 */, 28, 0, /* DiagSubGroup3 */ 3, R"()")
DIAG_ENTRY(anonymous_0 /* abi */, 32, 0, 0, R"()")
DIAG_ENTRY(AbsoluteValue /* absolute-value */, 36, /* DiagArray5 */ 5, 0, R"()")
DIAG_ENTRY(anonymous_37 /* address */, 51, 0, /* DiagSubGroup6 */ 6, R"()")
DIAG_ENTRY(anonymous_146 /* address-of-packed-member */, 59,
           /* DiagArray7 */ 10, 0, R"()")
DIAG_ENTRY(AddressOfTemporary /* address-of-temporary */, 84, 0, 0, R"()")
DIAG_ENTRY(anonymous_2 /* aggregate-return */, 105, 0, 0, R"()")
DIAG_ENTRY(anonymous_147 /* align-mismatch */, 122, /* DiagArray10 */ 12, 0,
           R"()")
DIAG_ENTRY(All /* all */, 137, 0, /* DiagSubGroup11 */ 10, R"()")
DIAG_ENTRY(anonymous_116 /* alloca */, 141, /* DiagArray12 */ 14, 0, R"()")
DIAG_ENTRY(anonymous_117 /* alloca-with-align-alignof */, 148,
           /* DiagArray13 */ 16, 0, R"()")
DIAG_ENTRY(AmbiguousMacro /* ambiguous-macro */, 174, /* DiagArray14 */ 18, 0,
           R"()")
DIAG_ENTRY(AnonEnumEnumConversion /* anon-enum-enum-conversion */, 190,
           /* DiagArray15 */ 20, /* DiagSubGroup15 */ 18, R"()")
DIAG_ENTRY(anonymous_159 /* argument-outside-range */, 216,
           /* DiagArray16 */ 22, 0, R"()")
DIAG_ENTRY(ArrayBounds /* array-bounds */, 239, /* DiagArray17 */ 24, 0, R"()")
DIAG_ENTRY(ArrayBoundsPointerArithmetic /* array-bounds-pointer-arithmetic */,
           252, /* DiagArray18 */ 30, 0, R"()")
DIAG_ENTRY(ArrayParameter /* array-parameter */, 284, /* DiagArray19 */ 33, 0,
           R"()")
DIAG_ENTRY(ASM /* asm */, 300, 0, /* DiagSubGroup20 */ 20, R"()")
DIAG_ENTRY(ASMOperandWidths /* asm-operand-widths */, 304, /* DiagArray21 */ 35,
           0, R"()")
DIAG_ENTRY(anonymous_158 /* assign-enum */, 323, /* DiagArray22 */ 37, 0, R"()")
DIAG_ENTRY(anonymous_111 /* assume */, 335, /* DiagArray23 */ 39, 0, R"()")
DIAG_ENTRY(anonymous_137 /* atomic-access */, 342, /* DiagArray24 */ 41, 0,
           R"()")
DIAG_ENTRY(AtomicAlignment /* atomic-alignment */, 356, /* DiagArray25 */ 43, 0,
           R"()")
DIAG_ENTRY(anonymous_153 /* atomic-implicit-seq-cst */, 373,
           /* DiagArray26 */ 46, 0, R"()")
DIAG_ENTRY(anonymous_152 /* atomic-memory-ordering */, 397,
           /* DiagArray27 */ 48, 0, R"()")
DIAG_ENTRY(anonymous_125 /* attribute-packed-for-bitfield */, 420,
           /* DiagArray28 */ 50, 0, R"()")
DIAG_ENTRY(BackendWarningAttributes /* attribute-warning */, 450,
           /* DiagArray29 */ 52, 0, R"()")
DIAG_ENTRY(Attributes /* attributes */, 468, 0, /* DiagSubGroup30 */ 22, R"()")
DIAG_ENTRY(anonymous_104 /* auto-decl-extensions */, 479, /* DiagArray31 */ 54,
           0, R"()")
DIAG_ENTRY(anonymous_3 /* auto-import */, 500, 0, 0, R"()")
DIAG_ENTRY(Availability /* availability */, 512, /* DiagArray33 */ 56, 0, R"()")
DIAG_ENTRY(BackendPlugin /* backend-plugin */, 525, /* DiagArray34 */ 63, 0,
           R"()")
DIAG_ENTRY(anonymous_68 /* backslash-newline-escape */, 540,
           /* DiagArray35 */ 66, 0, R"()")
DIAG_ENTRY(BadFunctionCast /* bad-function-cast */, 565, /* DiagArray36 */ 68,
           0, R"()")
DIAG_ENTRY(BinaryLiteral /* binary-literal */, 583, 0, /* DiagSubGroup37 */ 25,
           R"()")
DIAG_ENTRY(anonymous_100 /* bit-int-extension */, 598, /* DiagArray38 */ 70, 0,
           R"()")
DIAG_ENTRY(BitFieldConstantConversion /* bitfield-constant-conversion */, 616,
           /* DiagArray39 */ 72, /* DiagSubGroup39 */ 27, R"()")
DIAG_ENTRY(BitFieldEnumConversion /* bitfield-enum-conversion */, 645,
           /* DiagArray40 */ 74, 0, R"()")
DIAG_ENTRY(BitFieldWidth /* bitfield-width */, 670, /* DiagArray41 */ 78, 0,
           R"()")
DIAG_ENTRY(BitwiseConditionalParentheses /* bitwise-conditional-parentheses */,
           685, /* DiagArray42 */ 80, 0, R"()")
DIAG_ENTRY(BitwiseInsteadOfLogical /* bitwise-instead-of-logical */, 717,
           /* DiagArray43 */ 82, 0, R"()")
DIAG_ENTRY(BitwiseOpParentheses /* bitwise-op-parentheses */, 744,
           /* DiagArray44 */ 84, 0, R"()")
DIAG_ENTRY(BoolConversion /* bool-conversion */, 767, /* DiagArray45 */ 86,
           /* DiagSubGroup45 */ 29, R"()")
DIAG_ENTRY(anonymous_44 /* bool-conversions */, 783, 0, /* DiagSubGroup46 */ 32,
           R"()")
DIAG_ENTRY(BoolOperation /* bool-operation */, 800, /* DiagArray47 */ 88,
           /* DiagSubGroup47 */ 34, R"()")
DIAG_ENTRY(anonymous_133 /* braced-scalar-init */, 815, /* DiagArray48 */ 90, 0,
           R"()")
DIAG_ENTRY(BranchProtection /* branch-protection */, 834, /* DiagArray49 */ 92,
           0, R"()")
DIAG_ENTRY(anonymous_119 /* builtin-assume-aligned-alignment */, 852,
           /* DiagArray50 */ 96, 0, R"()")
DIAG_ENTRY(BuiltinMacroRedefined /* builtin-macro-redefined */, 885,
           /* DiagArray51 */ 98, 0, R"()")
DIAG_ENTRY(anonymous_112 /* builtin-memcpy-chk-size */, 909,
           /* DiagArray52 */ 101, 0, R"()")
DIAG_ENTRY(BuiltinRequiresHeader /* builtin-requires-header */, 933,
           /* DiagArray53 */ 103, 0, R"()")
DIAG_ENTRY(C11 /* c11-extensions */, 957, /* DiagArray54 */ 105, 0, R"()")
DIAG_ENTRY(C23Attrs /* c23-attribute-extensions */, 972, /* DiagArray55 */ 110,
           0, R"()")
DIAG_ENTRY(C23Compat /* c23-compat */, 997, /* DiagArray56 */ 114, 0, R"()")
DIAG_ENTRY(C23 /* c23-extensions */, 1008, /* DiagArray57 */ 116, 0, R"()")
DIAG_ENTRY(anonymous_4 /* c2x-compat */, 1023, 0, /* DiagSubGroup58 */ 36,
           R"()")
DIAG_ENTRY(anonymous_48 /* c2x-extensions */, 1034, 0, /* DiagSubGroup59 */ 38,
           R"()")
DIAG_ENTRY(C99Compat /* c99-compat */, 1049, /* DiagArray60 */ 126, 0, R"()")
DIAG_ENTRY(C99Designator /* c99-designator */, 1060, /* DiagArray61 */ 131, 0,
           R"()")
DIAG_ENTRY(C99 /* c99-extensions */, 1075, /* DiagArray62 */ 133,
           /* DiagSubGroup62 */ 40, R"()")
DIAG_ENTRY(CastAlign /* cast-align */, 1090, /* DiagArray63 */ 144, 0, R"()")
DIAG_ENTRY(anonymous_154 /* cast-calling-convention */, 1101,
           /* DiagArray64 */ 146, 0, R"()")
DIAG_ENTRY(CastFunctionType /* cast-function-type */, 1125,
           /* DiagArray65 */ 148, /* DiagSubGroup65 */ 42, R"()")
DIAG_ENTRY(CastFunctionTypeStrict /* cast-function-type-strict */, 1144,
           /* DiagArray66 */ 150, 0, R"()")
DIAG_ENTRY(CastQual /* cast-qual */, 1170, /* DiagArray67 */ 152, 0, R"()")
DIAG_ENTRY(anonymous_5 /* char-align */, 1180, 0, 0, R"()")
DIAG_ENTRY(CharSubscript /* char-subscripts */, 1191, /* DiagArray69 */ 155, 0,
           R"()")
DIAG_ENTRY(anonymous_102 /* comma */, 1207, /* DiagArray70 */ 157, 0, R"()")
DIAG_ENTRY(Comment /* comment */, 1213, /* DiagArray71 */ 159, 0, R"()")
DIAG_ENTRY(anonymous_42 /* comments */, 1221, 0, /* DiagSubGroup72 */ 44, R"()")
DIAG_ENTRY(CompareDistinctPointerType /* compare-distinct-pointer-types */,
           1230, /* DiagArray73 */ 164, 0, R"()")
DIAG_ENTRY(anonymous_135 /* complex-component-init */, 1261,
           /* DiagArray74 */ 166, 0, R"()")
DIAG_ENTRY(CompoundTokenSplit /* compound-token-split */, 1284, 0,
           /* DiagSubGroup75 */ 46, R"()")
DIAG_ENTRY(CompoundTokenSplitByMacro /* compound-token-split-by-macro */, 1305,
           /* DiagArray76 */ 168, 0, R"()")
DIAG_ENTRY(CompoundTokenSplitBySpace /* compound-token-split-by-space */, 1335,
           /* DiagArray77 */ 170, 0, R"()")
DIAG_ENTRY(anonymous_156 /* conditional-type-mismatch */, 1365,
           /* DiagArray78 */ 172, 0, R"()")
DIAG_ENTRY(UninitializedMaybe /* conditional-uninitialized */, 1391, 0, 0,
           R"()")
DIAG_ENTRY(ConstantConversion /* constant-conversion */, 1417,
           /* DiagArray80 */ 174, /* DiagSubGroup80 */ 49, R"()")
DIAG_ENTRY(anonymous_144 /* constant-logical-operand */, 1437,
           /* DiagArray81 */ 176, 0, R"()")
DIAG_ENTRY(Conversion /* conversion */, 1462, /* DiagArray82 */ 178,
           /* DiagSubGroup82 */ 51, R"()")
DIAG_ENTRY(anonymous_43 /* conversion-null */, 1473, 0, /* DiagSubGroup83 */ 66,
           R"()")
DIAG_ENTRY(CoveredSwitchDefault /* covered-switch-default */, 1489,
           /* DiagArray84 */ 181, 0, R"()")
DIAG_ENTRY(anonymous_41 /* cpp */, 1512, 0, /* DiagSubGroup85 */ 68, R"()")
DIAG_ENTRY(anonymous_6 /* ctor-dtor-privacy */, 1516, 0, 0, R"()")
DIAG_ENTRY(Dangling /* dangling */, 1534, 0, /* DiagSubGroup87 */ 70, R"()")
DIAG_ENTRY(DanglingElse /* dangling-else */, 1543, /* DiagArray88 */ 183, 0,
           R"()")
DIAG_ENTRY(anonymous_64 /* darwin-sdk-settings */, 1557, /* DiagArray89 */ 185,
           0, R"()")
DIAG_ENTRY(anonymous_93 /* date-time */, 1577, /* DiagArray90 */ 187, 0, R"()")
DIAG_ENTRY(anonymous_62 /* debug-compression-unavailable */, 1587,
           /* DiagArray91 */ 189, 0, R"()")
DIAG_ENTRY(DeclarationAfterStatement /* declaration-after-statement */, 1617,
           /* DiagArray92 */ 191, 0, R"()")
DIAG_ENTRY(anonymous_76 /* delimited-escape-sequence-extension */, 1645,
           /* DiagArray93 */ 194, 0, R"()")
DIAG_ENTRY(Deprecated /* deprecated */, 1681, /* DiagArray94 */ 196,
           /* DiagSubGroup94 */ 72, R"()")
DIAG_ENTRY(
    DeprecatedAnonEnumEnumConversion /* deprecated-anon-enum-enum-conversion */,
    1692, 0, 0, R"()")
DIAG_ENTRY(DeprecatedAttributes /* deprecated-attributes */, 1729,
           /* DiagArray96 */ 198, 0, R"()")
DIAG_ENTRY(DeprecatedBuiltins /* deprecated-builtins */, 1751, 0, 0, R"()")
DIAG_ENTRY(DeprecatedDeclarations /* deprecated-declarations */, 1771,
           /* DiagArray98 */ 202, 0, R"()")
DIAG_ENTRY(DeprecatedEnumCompare /* deprecated-enum-compare */, 1795, 0, 0,
           R"()")
DIAG_ENTRY(
    DeprecatedEnumCompareConditional /* deprecated-enum-compare-conditional */,
    1819, 0, 0, R"()")
DIAG_ENTRY(DeprecatedEnumEnumConversion /* deprecated-enum-enum-conversion */,
           1855, 0, 0, R"()")
DIAG_ENTRY(DeprecatedEnumFloatConversion /* deprecated-enum-float-conversion */,
           1887, 0, 0, R"()")
DIAG_ENTRY(DeprecatedNonPrototype /* deprecated-non-prototype */, 1920,
           /* DiagArray103 */ 205, 0, R"()")
DIAG_ENTRY(DeprecatedPragma /* deprecated-pragma */, 1945,
           /* DiagArray104 */ 208, 0, R"()")
DIAG_ENTRY(DeprecatedType /* deprecated-type */, 1963, /* DiagArray105 */ 210,
           0, R"()")
DIAG_ENTRY(DeprecatedWritableStr /* deprecated-writable-strings */, 1979,
           /* DiagArray106 */ 212, 0, R"()")
DIAG_ENTRY(anonymous_85 /* disabled-macro-expansion */, 2007,
           /* DiagArray107 */ 214, 0, R"()")
DIAG_ENTRY(anonymous_8 /* disabled-optimization */, 2032, 0, 0, R"()")
DIAG_ENTRY(anonymous_9 /* discard-qual */, 2054, 0, 0, R"()")
DIAG_ENTRY(anonymous_10 /* div-by-zero */, 2067, 0, /* DiagSubGroup110 */ 79,
           R"()")
DIAG_ENTRY(DivZero /* division-by-zero */, 2079, /* DiagArray111 */ 216, 0,
           R"()")
DIAG_ENTRY(anonymous_120 /* dll-attribute-on-redeclaration */, 2096,
           /* DiagArray112 */ 218, 0, R"()")
DIAG_ENTRY(anonymous_69 /* dollar-in-identifier-extension */, 2127,
           /* DiagArray113 */ 220, 0, R"()")
DIAG_ENTRY(DoublePromotion /* double-promotion */, 2158, /* DiagArray114 */ 222,
           0, R"()")
DIAG_ENTRY(DuplicateDeclSpecifier /* duplicate-decl-specifier */, 2175,
           /* DiagArray115 */ 224, 0, R"()")
DIAG_ENTRY(anonymous_101 /* duplicate-enum */, 2200, /* DiagArray116 */ 229, 0,
           R"()")
DIAG_ENTRY(anonymous_98 /* elaborated-enum-base */, 2215,
           /* DiagArray117 */ 231, 0, R"()")
DIAG_ENTRY(anonymous_90 /* embedded-directive */, 2236, /* DiagArray118 */ 233,
           0, R"()")
DIAG_ENTRY(EmptyBody /* empty-body */, 2255, /* DiagArray119 */ 235, 0, R"()")
DIAG_ENTRY(anonymous_95 /* empty-translation-unit */, 2266,
           /* DiagArray120 */ 240, 0, R"()")
DIAG_ENTRY(anonymous_40 /* endif-labels */, 2289, 0, /* DiagSubGroup121 */ 81,
           R"()")
DIAG_ENTRY(EnumCompare /* enum-compare */, 2302, /* DiagArray122 */ 242,
           /* DiagSubGroup122 */ 83, R"()")
DIAG_ENTRY(EnumCompareConditional /* enum-compare-conditional */, 2315,
           /* DiagArray123 */ 244, /* DiagSubGroup123 */ 86, R"()")
DIAG_ENTRY(EnumCompareSwitch /* enum-compare-switch */, 2340,
           /* DiagArray124 */ 246, 0, R"()")
DIAG_ENTRY(EnumConversion /* enum-conversion */, 2360, /* DiagArray125 */ 248,
           /* DiagSubGroup125 */ 88, R"()")
DIAG_ENTRY(EnumEnumConversion /* enum-enum-conversion */, 2376,
           /* DiagArray126 */ 250, /* DiagSubGroup126 */ 92, R"()")
DIAG_ENTRY(EnumFloatConversion /* enum-float-conversion */, 2397,
           /* DiagArray127 */ 252, /* DiagSubGroup127 */ 94, R"()")
DIAG_ENTRY(EnumTooLarge /* enum-too-large */, 2419, /* DiagArray128 */ 254, 0,
           R"()")
DIAG_ENTRY(ExcessInitializers /* excess-initializers */, 2434,
           /* DiagArray129 */ 257, 0, R"()")
DIAG_ENTRY(anonymous_105 /* excessive-regsave */, 2454, /* DiagArray130 */ 262,
           0, R"()")
DIAG_ENTRY(ExpansionToDefined /* expansion-to-defined */, 2472,
           /* DiagArray131 */ 264, 0, R"()")
DIAG_ENTRY(anonymous_132 /* extern-initializer */, 2493, /* DiagArray132 */ 267,
           0, R"()")
DIAG_ENTRY(Extra /* extra */, 2512, 0, /* DiagSubGroup133 */ 96, R"()")
DIAG_ENTRY(ExtraSemi /* extra-semi */, 2518, /* DiagArray134 */ 269, 0, R"()")
DIAG_ENTRY(ExtraSemiStmt /* extra-semi-stmt */, 2529, /* DiagArray135 */ 271, 0,
           R"()")
DIAG_ENTRY(ExtraTokens /* extra-tokens */, 2545, /* DiagArray136 */ 273, 0,
           R"()")
DIAG_ENTRY(FinalMacro /* final-macro */, 2558, /* DiagArray137 */ 275, 0, R"()")
DIAG_ENTRY(anonymous_97 /* fixed-enum-extension */, 2570,
           /* DiagArray138 */ 277, 0, R"()")
DIAG_ENTRY(anonymous_52 /* fixed-point-overflow */, 2591,
           /* DiagArray139 */ 279, 0, R"()")
DIAG_ENTRY(FlagEnum /* flag-enum */, 2612, /* DiagArray140 */ 281, 0, R"()")
DIAG_ENTRY(FlexibleArrayExtensions /* flexible-array-extensions */, 2622,
           /* DiagArray141 */ 283, 0, R"()")
DIAG_ENTRY(FloatConversion /* float-conversion */, 2648, /* DiagArray142 */ 286,
           /* DiagSubGroup142 */ 106, R"()")
DIAG_ENTRY(anonymous_138 /* float-equal */, 2665, /* DiagArray143 */ 288, 0,
           R"()")
DIAG_ENTRY(FloatOverflowConversion /* float-overflow-conversion */, 2677,
           /* DiagArray144 */ 290, 0, R"()")
DIAG_ENTRY(FloatZeroConversion /* float-zero-conversion */, 2703,
           /* DiagArray145 */ 293, 0, R"()")
DIAG_ENTRY(ForLoopAnalysis /* for-loop-analysis */, 2725,
           /* DiagArray146 */ 295, 0, R"()")
DIAG_ENTRY(Format /* format */, 2743, /* DiagArray147 */ 298,
           /* DiagSubGroup147 */ 109, R"()")
DIAG_ENTRY(FormatExtraArgs /* format-extra-args */, 2750,
           /* DiagArray148 */ 321, 0, R"()")
DIAG_ENTRY(FormatInsufficientArgs /* format-insufficient-args */, 2768,
           /* DiagArray149 */ 323, 0, R"()")
DIAG_ENTRY(FormatInvalidSpecifier /* format-invalid-specifier */, 2793,
           /* DiagArray150 */ 325, 0, R"()")
DIAG_ENTRY(FormatNonStandard /* format-non-iso */, 2818, /* DiagArray151 */ 327,
           0, R"()")
DIAG_ENTRY(FormatNonLiteral /* format-nonliteral */, 2833,
           /* DiagArray152 */ 331, 0, R"()")
DIAG_ENTRY(FormatOverflow /* format-overflow */, 2851, /* DiagArray153 */ 333,
           0, R"()")
DIAG_ENTRY(FormatPedantic /* format-pedantic */, 2867, /* DiagArray154 */ 335,
           0, R"()")
DIAG_ENTRY(FormatSecurity /* format-security */, 2883, /* DiagArray155 */ 338,
           0, R"()")
DIAG_ENTRY(FormatTruncation /* format-truncation */, 2899,
           /* DiagArray156 */ 340, 0, R"()")
DIAG_ENTRY(FormatTypeConfusion /* format-type-confusion */, 2917,
           /* DiagArray157 */ 342, 0, R"()")
DIAG_ENTRY(FormatY2K /* format-y2k */, 2939, 0, 0, R"()")
DIAG_ENTRY(FormatZeroLength /* format-zero-length */, 2950,
           /* DiagArray159 */ 344, 0, R"()")
DIAG_ENTRY(Format2 /* format=2 */, 2969, 0, /* DiagSubGroup160 */ 119, R"()")
DIAG_ENTRY(FortifySource /* fortify-source */, 2978, /* DiagArray161 */ 346,
           /* DiagSubGroup161 */ 123, R"()")
DIAG_ENTRY(FourByteMultiChar /* four-char-constants */, 2993,
           /* DiagArray162 */ 351, 0, R"()")
DIAG_ENTRY(FrameAddress /* frame-address */, 3013, /* DiagArray163 */ 353, 0,
           R"()")
DIAG_ENTRY(
    BackendFrameLargerThan /* frame-larger-than */, 3027,
    /* DiagArray164 */ 355, 0,
    R"(More fine grained information about the stack layout is available by adding the
`-Rpass-analysis=stack-frame-layout` command-line flag to the compiler
invocation.

The diagnostic information can be saved to a file in a machine readable format,
like YAML by adding the `-foptimization-record-file=<file>` command-line flag.

Results can be filtered by function name by passing
`-mllvm -filter-print-funcs=foo`, where `foo` is the target function's name.

   .. code-block:: console

      neverc -c a.c -Rpass-analysis=stack-frame-layout -mllvm -filter-print-funcs=foo

   .. code-block:: console

      neverc -c a.c -Rpass-analysis=stack-frame-layout -foptimization-record-file=<file>)")
DIAG_ENTRY(anonymous_50 /* frame-larger-than= */, 3045, 0,
           /* DiagSubGroup165 */ 126, R"()")
DIAG_ENTRY(FrameworkIncludePrivateFromPublic /* framework-include-private-from-public
                                              */
           ,
           3064, /* DiagArray166 */ 358, 0, R"()")
DIAG_ENTRY(FreeNonHeapObject /* free-nonheap-object */, 3102,
           /* DiagArray167 */ 360, 0, R"()")
DIAG_ENTRY(FunctionMultiVersioning /* function-multiversion */, 3122,
           /* DiagArray168 */ 362, /* DiagSubGroup168 */ 128, R"()")
DIAG_ENTRY(FutureCompat /* future-compat */, 3144, 0, 0, R"()")
DIAG_ENTRY(GccCompat /* gcc-compat */, 3158, /* DiagArray170 */ 367, 0, R"()")
DIAG_ENTRY(anonymous_99 /* generic-type-extension */, 3169,
           /* DiagArray171 */ 376, 0, R"()")
DIAG_ENTRY(GlobalISel /* global-isel */, 3192, /* DiagArray172 */ 378, 0, R"()")
DIAG_ENTRY(GNU /* gnu */, 3204, 0, /* DiagSubGroup173 */ 130, R"()")
DIAG_ENTRY(GNUAlignofExpression /* gnu-alignof-expression */, 3208,
           /* DiagArray174 */ 381, 0, R"()")
DIAG_ENTRY(GNUAnonymousStruct /* gnu-anonymous-struct */, 3231, 0, 0, R"()")
DIAG_ENTRY(GNUAutoType /* gnu-auto-type */, 3252, /* DiagArray176 */ 383, 0,
           R"()")
DIAG_ENTRY(GNUBinaryLiteral /* gnu-binary-literal */, 3266,
           /* DiagArray177 */ 385, 0, R"()")
DIAG_ENTRY(GNUCaseRange /* gnu-case-range */, 3285, /* DiagArray178 */ 387, 0,
           R"()")
DIAG_ENTRY(GNUComplexInteger /* gnu-complex-integer */, 3300,
           /* DiagArray179 */ 389, 0, R"()")
DIAG_ENTRY(GNUCompoundLiteralInitializer /* gnu-compound-literal-initializer */,
           3320, /* DiagArray180 */ 391, 0, R"()")
DIAG_ENTRY(GNUConditionalOmittedOperand /* gnu-conditional-omitted-operand */,
           3353, /* DiagArray181 */ 393, 0, R"()")
DIAG_ENTRY(GNUDesignator /* gnu-designator */, 3385, /* DiagArray182 */ 395, 0,
           R"()")
DIAG_ENTRY(anonymous_1 /* gnu-empty-initializer */, 3400, 0, 0, R"()")
DIAG_ENTRY(GNUEmptyStruct /* gnu-empty-struct */, 3422, /* DiagArray184 */ 399,
           0, R"()")
DIAG_ENTRY(GNUFlexibleArrayInitializer /* gnu-flexible-array-initializer */,
           3439, /* DiagArray185 */ 402, 0, R"()")
DIAG_ENTRY(GNUFlexibleArrayUnionMember /* gnu-flexible-array-union-member */,
           3470, 0, 0, R"()")
DIAG_ENTRY(GNUFoldingConstant /* gnu-folding-constant */, 3502,
           /* DiagArray187 */ 404, 0, R"()")
DIAG_ENTRY(GNUImaginaryConstant /* gnu-imaginary-constant */, 3523,
           /* DiagArray188 */ 407, 0, R"()")
DIAG_ENTRY(GNUIncludeNext /* gnu-include-next */, 3546, /* DiagArray189 */ 409,
           0, R"()")
DIAG_ENTRY(GNULabelsAsValue /* gnu-label-as-value */, 3563,
           /* DiagArray190 */ 411, 0, R"()")
DIAG_ENTRY(GNULineMarker /* gnu-line-marker */, 3582, /* DiagArray191 */ 414, 0,
           R"()")
DIAG_ENTRY(GNUNullPointerArithmetic /* gnu-null-pointer-arithmetic */, 3598,
           /* DiagArray192 */ 416, 0, R"()")
DIAG_ENTRY(GNUOffsetofExtensions /* gnu-offsetof-extensions */, 3626,
           /* DiagArray193 */ 418, 0, R"()")
DIAG_ENTRY(GNUPointerArith /* gnu-pointer-arith */, 3650,
           /* DiagArray194 */ 420, 0, R"()")
DIAG_ENTRY(GNURedeclaredEnum /* gnu-redeclared-enum */, 3668,
           /* DiagArray195 */ 424, 0, R"()")
DIAG_ENTRY(GNUStatementExpression /* gnu-statement-expression */, 3688,
           /* DiagArray196 */ 426, /* DiagSubGroup196 */ 160, R"()")
DIAG_ENTRY(GNUStatementExpressionFromMacroExpansion /* gnu-statement-expression-from-macro-expansion
                                                     */
           ,
           3713, /* DiagArray197 */ 428, 0, R"()")
DIAG_ENTRY(GNUStaticFloatInit /* gnu-static-float-init */, 3759, 0, 0, R"()")
DIAG_ENTRY(GNUUnionCast /* gnu-union-cast */, 3781, /* DiagArray199 */ 430, 0,
           R"()")
DIAG_ENTRY(
    GNUVariableSizedTypeNotAtEnd /* gnu-variable-sized-type-not-at-end */, 3796,
    /* DiagArray200 */ 432, 0, R"()")
DIAG_ENTRY(GNUZeroLineDirective /* gnu-zero-line-directive */, 3831,
           /* DiagArray201 */ 434, 0, R"()")
DIAG_ENTRY(
    GNUZeroVariadicMacroArguments /* gnu-zero-variadic-macro-arguments */, 3855,
    /* DiagArray202 */ 436, 0, R"()")
DIAG_ENTRY(anonymous_94 /* header-guard */, 3889, /* DiagArray203 */ 439, 0,
           R"()")
DIAG_ENTRY(HeaderHygiene /* header-hygiene */, 3902, 0, 0, R"()")
DIAG_ENTRY(IgnoredAttributes /* ignored-attributes */, 3917,
           /* DiagArray205 */ 441, 0, R"()")
DIAG_ENTRY(IgnoredOptimizationArgument /* ignored-optimization-argument */,
           3936, /* DiagArray206 */ 490, 0, R"()")
DIAG_ENTRY(IgnoredPragmaIntrinsic /* ignored-pragma-intrinsic */, 3966,
           /* DiagArray207 */ 492, 0, R"()")
DIAG_ENTRY(IgnoredPragmaOptimize /* ignored-pragma-optimize */, 3991, 0, 0,
           R"()")
DIAG_ENTRY(IgnoredPragmas /* ignored-pragmas */, 4015, /* DiagArray209 */ 494,
           /* DiagSubGroup209 */ 162, R"()")
DIAG_ENTRY(IgnoredQualifiers /* ignored-qualifiers */, 4031,
           /* DiagArray210 */ 535, 0, R"()")
DIAG_ENTRY(Implicit /* implicit */, 4050, 0, /* DiagSubGroup211 */ 165, R"()")
DIAG_ENTRY(
    ImplicitConstIntFloatConversion /* implicit-const-int-float-conversion */,
    4059, /* DiagArray212 */ 537, 0, R"()")
DIAG_ENTRY(ImplicitConversionFloatingPointToBool /* implicit-conversion-floating-point-to-bool
                                                  */
           ,
           4095, /* DiagArray213 */ 539, 0, R"()")
DIAG_ENTRY(ImplicitFallthrough /* implicit-fallthrough */, 4138, 0,
           /* DiagSubGroup214 */ 168, R"()")
DIAG_ENTRY(
    ImplicitFallthroughPerFunction /* implicit-fallthrough-per-function */,
    4159, 0, 0, R"()")
DIAG_ENTRY(ImplicitFixedPointConversion /* implicit-fixed-point-conversion */,
           4193, /* DiagArray216 */ 541, 0, R"()")
DIAG_ENTRY(ImplicitFloatConversion /* implicit-float-conversion */, 4225,
           /* DiagArray217 */ 543, /* DiagSubGroup217 */ 170, R"()")
DIAG_ENTRY(ImplicitFunctionDeclare /* implicit-function-declaration */, 4251,
           /* DiagArray218 */ 546, 0, R"()")
DIAG_ENTRY(ImplicitInt /* implicit-int */, 4281, /* DiagArray219 */ 552, 0,
           R"()")
DIAG_ENTRY(ImplicitIntConversion /* implicit-int-conversion */, 4294,
           /* DiagArray220 */ 556, 0, R"()")
DIAG_ENTRY(ImplicitIntFloatConversion /* implicit-int-float-conversion */, 4318,
           /* DiagArray221 */ 559, /* DiagSubGroup221 */ 172, R"()")
DIAG_ENTRY(ImplicitlyUnsignedLiteral /* implicitly-unsigned-literal */, 4348,
           /* DiagArray222 */ 561, 0, R"()")
DIAG_ENTRY(anonymous_13 /* import */, 4376, 0, 0, R"()")
DIAG_ENTRY(anonymous_89 /* import-preprocessor-directive-pedantic */, 4383,
           /* DiagArray224 */ 563, 0, R"()")
DIAG_ENTRY(anonymous_80 /* include-next-absolute-path */, 4422,
           /* DiagArray225 */ 565, 0, R"()")
DIAG_ENTRY(anonymous_79 /* include-next-outside-header */, 4449,
           /* DiagArray226 */ 567, 0, R"()")
DIAG_ENTRY(
    IncompatibleFunctionPointerTypes /* incompatible-function-pointer-types */,
    4477, /* DiagArray227 */ 569, 0, R"()")
DIAG_ENTRY(anonymous_151 /* incompatible-function-pointer-types-strict */, 4513,
           /* DiagArray228 */ 571, 0, R"()")
DIAG_ENTRY(anonymous_108 /* incompatible-library-redeclaration */, 4556,
           /* DiagArray229 */ 573, 0, R"()")
DIAG_ENTRY(IncompatibleMSPragmaSection /* incompatible-ms-pragma-section */,
           4591, /* DiagArray230 */ 575, 0, R"()")
DIAG_ENTRY(IncompatibleMSStruct /* incompatible-ms-struct */, 4622,
           /* DiagArray231 */ 577, 0, R"()")
DIAG_ENTRY(IncompatiblePointerTypes /* incompatible-pointer-types */, 4645,
           /* DiagArray232 */ 579, /* DiagSubGroup232 */ 174, R"()")
DIAG_ENTRY(IncompatiblePointerTypesDiscardsQualifiers /* incompatible-pointer-types-discards-qualifiers
                                                       */
           ,
           4672, /* DiagArray233 */ 581, 0, R"()")
DIAG_ENTRY(anonymous_61 /* incompatible-sysroot */, 4719,
           /* DiagArray234 */ 584, 0, R"()")
DIAG_ENTRY(anonymous_107 /* incomplete-setjmp-declaration */, 4740,
           /* DiagArray235 */ 586, 0, R"()")
DIAG_ENTRY(MicrosoftInconsistentDllImport /* inconsistent-dllimport */, 4770,
           /* DiagArray236 */ 588, 0, R"()")
DIAG_ENTRY(InfiniteRecursion /* infinite-recursion */, 4793, 0, 0, R"()")
DIAG_ENTRY(anonymous_14 /* init-self */, 4812, 0, 0, R"()")
DIAG_ENTRY(InitializerOverrides /* initializer-overrides */, 4822,
           /* DiagArray239 */ 591, 0, R"()")
DIAG_ENTRY(anonymous_15 /* inline */, 4844, 0, 0, R"()")
DIAG_ENTRY(BackendInlineAsm /* inline-asm */, 4851, /* DiagArray241 */ 593, 0,
           R"()")
DIAG_ENTRY(IntConversion /* int-conversion */, 4862, /* DiagArray242 */ 595, 0,
           R"()")
DIAG_ENTRY(anonymous_45 /* int-conversions */, 4877, 0,
           /* DiagSubGroup243 */ 177, R"()")
DIAG_ENTRY(IntInBoolContext /* int-in-bool-context */, 4893,
           /* DiagArray244 */ 598, 0, R"()")
DIAG_ENTRY(IntToPointerCast /* int-to-pointer-cast */, 4913,
           /* DiagArray245 */ 601, /* DiagSubGroup245 */ 179, R"()")
DIAG_ENTRY(IntToVoidPointerCast /* int-to-void-pointer-cast */, 4933,
           /* DiagArray246 */ 603, 0, R"()")
DIAG_ENTRY(anonymous_51 /* integer-overflow */, 4958, /* DiagArray247 */ 605, 0,
           R"()")
DIAG_ENTRY(InvalidCommandLineArgument /* invalid-command-line-argument */, 4975,
           /* DiagArray248 */ 607, /* DiagSubGroup248 */ 181, R"()")
DIAG_ENTRY(anonymous_56 /* invalid-feature-combination */, 5005,
           /* DiagArray249 */ 612, 0, R"()")
DIAG_ENTRY(anonymous_127 /* invalid-no-builtin-names */, 5033,
           /* DiagArray250 */ 614, 0, R"()")
DIAG_ENTRY(InvalidNoreturn /* invalid-noreturn */, 5058, /* DiagArray251 */ 616,
           0, R"()")
DIAG_ENTRY(InvalidOffsetof /* invalid-offsetof */, 5075, 0, 0, R"()")
DIAG_ENTRY(InvalidOrNonExistentDirectory /* invalid-or-nonexistent-directory */,
           5092, 0, 0, R"()")
DIAG_ENTRY(InvalidPPToken /* invalid-pp-token */, 5125, /* DiagArray254 */ 618,
           0, R"()")
DIAG_ENTRY(InvalidSourceEncoding /* invalid-source-encoding */, 5142,
           /* DiagArray255 */ 621, 0, R"()")
DIAG_ENTRY(anonymous_92 /* invalid-token-paste */, 5166, /* DiagArray256 */ 624,
           0, R"()")
DIAG_ENTRY(anonymous_78 /* invalid-unevaluated-string */, 5186,
           /* DiagArray257 */ 626, 0, R"()")
DIAG_ENTRY(anonymous_71 /* invalid-utf8 */, 5213, /* DiagArray258 */ 628, 0,
           R"()")
DIAG_ENTRY(anonymous_149 /* jump-seh-finally */, 5226, /* DiagArray259 */ 630,
           0, R"()")
DIAG_ENTRY(KeywordCompat /* keyword-compat */, 5243, /* DiagArray260 */ 632, 0,
           R"()")
DIAG_ENTRY(KeywordAsMacro /* keyword-macro */, 5258, /* DiagArray261 */ 634, 0,
           R"()")
DIAG_ENTRY(KNRPromotedParameter /* knr-promoted-parameter */, 5272,
           /* DiagArray262 */ 636, 0, R"()")
DIAG_ENTRY(anonymous_70 /* language-extension-token */, 5295,
           /* DiagArray263 */ 638, 0, R"()")
DIAG_ENTRY(LargeByValueCopy /* large-by-value-copy */, 5320,
           /* DiagArray264 */ 640, 0, R"()")
DIAG_ENTRY(LiteralConversion /* literal-conversion */, 5340,
           /* DiagArray265 */ 643, 0, R"()")
DIAG_ENTRY(LiteralRange /* literal-range */, 5359, /* DiagArray266 */ 646, 0,
           R"()")
DIAG_ENTRY(LogicalNotParentheses /* logical-not-parentheses */, 5373,
           /* DiagArray267 */ 650, 0, R"()")
DIAG_ENTRY(LogicalOpParentheses /* logical-op-parentheses */, 5397,
           /* DiagArray268 */ 652, 0, R"()")
DIAG_ENTRY(LongLong /* long-long */, 5420, /* DiagArray269 */ 654, 0, R"()")
DIAG_ENTRY(LoopAnalysis /* loop-analysis */, 5430, 0, /* DiagSubGroup270 */ 183,
           R"()")
DIAG_ENTRY(MacroRedefined /* macro-redefined */, 5444, /* DiagArray271 */ 656,
           0, R"()")
DIAG_ENTRY(Main /* main */, 5460, /* DiagArray272 */ 658, 0, R"()")
DIAG_ENTRY(MainReturnType /* main-return-type */, 5465, /* DiagArray273 */ 664,
           0, R"()")
DIAG_ENTRY(MalformedWarningCheck /* malformed-warning-check */, 5482,
           /* DiagArray274 */ 666, 0, R"()")
DIAG_ENTRY(anonymous_134 /* many-braces-around-scalar-init */, 5506,
           /* DiagArray275 */ 668, 0, R"()")
DIAG_ENTRY(anonymous_75 /* mathematical-notation-identifier-extension */, 5537,
           /* DiagArray276 */ 670, 0, R"()")
DIAG_ENTRY(
    MaxTokens /* max-tokens */, 5580, /* DiagArray277 */ 672, 0,
    R"(The warning is issued if the number of pre-processor tokens exceeds
the token limit, which can be set in three ways:

1. As a limit at a specific point in a file, using the ``neverc max_tokens_here``
   pragma:

   .. code-block:: c

      #pragma neverc max_tokens_here 1234

2. As a per-translation unit limit, using the ``-fmax-tokens=`` command-line
   flag:

   .. code-block:: console

      neverc -c a.c -fmax-tokens=1234

3. As a per-translation unit limit using the ``neverc max_tokens_total`` pragma,
   which works like and overrides the ``-fmax-tokens=`` flag:

   .. code-block:: c

      #pragma neverc max_tokens_total 1234

These limits can be helpful in limiting code growth through included files.

Setting a token limit of zero means no limit.

Note that the warning is disabled by default, so -Wmax-tokens must be used
in addition with the pragmas or -fmax-tokens flag to get any warnings.)")
DIAG_ENTRY(MemsetTransposedArgs /* memset-transposed-args */, 5591,
           /* DiagArray278 */ 675, 0, R"()")
DIAG_ENTRY(anonymous_110 /* memsize-comparison */, 5614, /* DiagArray279 */ 677,
           0, R"()")
DIAG_ENTRY(Microsoft /* microsoft */, 5633, 0, /* DiagSubGroup280 */ 185, R"()")
DIAG_ENTRY(MicrosoftAnonTag /* microsoft-anon-tag */, 5643,
           /* DiagArray281 */ 679, 0, R"()")
DIAG_ENTRY(MicrosoftCast /* microsoft-cast */, 5662, /* DiagArray282 */ 681, 0,
           R"()")
DIAG_ENTRY(MicrosoftCharize /* microsoft-charize */, 5677,
           /* DiagArray283 */ 683, 0, R"()")
DIAG_ENTRY(MicrosoftCommentPaste /* microsoft-comment-paste */, 5695,
           /* DiagArray284 */ 685, 0, R"()")
DIAG_ENTRY(MicrosoftConstInit /* microsoft-const-init */, 5719,
           /* DiagArray285 */ 687, 0, R"()")
DIAG_ENTRY(MicrosoftDrectveSection /* microsoft-drectve-section */, 5740,
           /* DiagArray286 */ 689, 0, R"()")
DIAG_ENTRY(MicrosoftEndOfFile /* microsoft-end-of-file */, 5766,
           /* DiagArray287 */ 691, 0, R"()")
DIAG_ENTRY(MicrosoftEnumForwardReference /* microsoft-enum-forward-reference */,
           5788, /* DiagArray288 */ 693, 0, R"()")
DIAG_ENTRY(MicrosoftEnumValue /* microsoft-enum-value */, 5821,
           /* DiagArray289 */ 695, 0, R"()")
DIAG_ENTRY(MicrosoftFixedEnum /* microsoft-fixed-enum */, 5842,
           /* DiagArray290 */ 697, 0, R"()")
DIAG_ENTRY(MicrosoftFlexibleArray /* microsoft-flexible-array */, 5863,
           /* DiagArray291 */ 699, 0, R"()")
DIAG_ENTRY(MicrosoftGoto /* microsoft-goto */, 5888, /* DiagArray292 */ 702, 0,
           R"()")
DIAG_ENTRY(MicrosoftInclude /* microsoft-include */, 5903,
           /* DiagArray293 */ 704, 0, R"()")
DIAG_ENTRY(MicrosoftInitFromPredefined /* microsoft-init-from-predefined */,
           5921, /* DiagArray294 */ 706, 0, R"()")
DIAG_ENTRY(MicrosoftRedeclareStatic /* microsoft-redeclare-static */, 5952,
           /* DiagArray295 */ 708, 0, R"()")
DIAG_ENTRY(MicrosoftStaticAssert /* microsoft-static-assert */, 5979,
           /* DiagArray296 */ 710, 0, R"()")
DIAG_ENTRY(MicrosoftStringLiteralFromPredefined /* microsoft-string-literal-from-predefined
                                                 */
           ,
           6003, /* DiagArray297 */ 712, 0, R"()")
DIAG_ENTRY(MisleadingIndentation /* misleading-indentation */, 6044,
           /* DiagArray298 */ 714, 0, R"()")
DIAG_ENTRY(MismatchedParameterTypes /* mismatched-parameter-types */, 6067, 0,
           0, R"()")
DIAG_ENTRY(MismatchedReturnTypes /* mismatched-return-types */, 6094, 0, 0,
           R"()")
DIAG_ENTRY(MissingBraces /* missing-braces */, 6118, /* DiagArray301 */ 716, 0,
           R"()")
DIAG_ENTRY(MissingDeclarations /* missing-declarations */, 6133,
           /* DiagArray302 */ 718, 0, R"()")
DIAG_ENTRY(MissingFieldInitializers /* missing-field-initializers */, 6154,
           /* DiagArray303 */ 722, 0, R"()")
DIAG_ENTRY(anonymous_16 /* missing-format-attribute */, 6181, 0, 0, R"()")
DIAG_ENTRY(anonymous_17 /* missing-include-dirs */, 6206, 0, 0, R"()")
DIAG_ENTRY(MissingNoreturn /* missing-noreturn */, 6227, 0, 0, R"()")
DIAG_ENTRY(anonymous_121 /* missing-prototype-for-cc */, 6244,
           /* DiagArray307 */ 724, 0, R"()")
DIAG_ENTRY(anonymous_128 /* missing-prototypes */, 6269, /* DiagArray308 */ 726,
           0, R"()")
DIAG_ENTRY(anonymous_60 /* missing-sysroot */, 6288, /* DiagArray309 */ 728, 0,
           R"()")
DIAG_ENTRY(MisspelledAssumption /* misspelled-assumption */, 6304,
           /* DiagArray310 */ 730, 0, R"()")
DIAG_ENTRY(Most /* most */, 6326, 0, /* DiagSubGroup311 */ 204, R"()")
DIAG_ENTRY(anonymous_49 /* msvc-include */, 6331, 0, /* DiagSubGroup312 */ 230,
           R"()")
DIAG_ENTRY(MultiChar /* multichar */, 6344, /* DiagArray313 */ 732, 0, R"()")
DIAG_ENTRY(anonymous_18 /* nested-externs */, 6354, 0, 0, R"()")
DIAG_ENTRY(NewlineEOF /* newline-eof */, 6369, /* DiagArray315 */ 734, 0, R"()")
DIAG_ENTRY(NoDeref /* noderef */, 6381, /* DiagArray316 */ 736, 0, R"()")
DIAG_ENTRY(NonLiteralNullConversion /* non-literal-null-conversion */, 6389,
           /* DiagArray317 */ 740, 0, R"()")
DIAG_ENTRY(NonPODVarargs /* non-pod-varargs */, 6417, /* DiagArray318 */ 742, 0,
           R"()")
DIAG_ENTRY(anonymous_118 /* non-power-of-two-alignment */, 6433,
           /* DiagArray319 */ 746, 0, R"()")
DIAG_ENTRY(NonNull /* nonnull */, 6460, /* DiagArray320 */ 748, 0, R"()")
DIAG_ENTRY(anonymous_81 /* nonportable-include-path */, 6468,
           /* DiagArray321 */ 751, 0, R"()")
DIAG_ENTRY(anonymous_82 /* nonportable-system-include-path */, 6493,
           /* DiagArray322 */ 753, 0, R"()")
DIAG_ENTRY(NonTrivialMemaccess /* nontrivial-memaccess */, 6525, 0, 0, R"()")
DIAG_ENTRY(NullCharacter /* null-character */, 6546, /* DiagArray324 */ 755, 0,
           R"()")
DIAG_ENTRY(NullConversion /* null-conversion */, 6561, /* DiagArray325 */ 758,
           0, R"()")
DIAG_ENTRY(NullDereference /* null-dereference */, 6577, /* DiagArray326 */ 760,
           0, R"()")
DIAG_ENTRY(NullPointerArithmetic /* null-pointer-arithmetic */, 6594,
           /* DiagArray327 */ 762, /* DiagSubGroup327 */ 232, R"()")
DIAG_ENTRY(NullPointerSubtraction /* null-pointer-subtraction */, 6618,
           /* DiagArray328 */ 764, 0, R"()")
DIAG_ENTRY(Nullability /* nullability */, 6643, /* DiagArray329 */ 766, 0,
           R"()")
DIAG_ENTRY(NullabilityCompleteness /* nullability-completeness */, 6655,
           /* DiagArray330 */ 769, /* DiagSubGroup330 */ 234, R"()")
DIAG_ENTRY(
    NullabilityCompletenessOnArrays /* nullability-completeness-on-arrays */,
    6680, /* DiagArray331 */ 771, 0, R"()")
DIAG_ENTRY(NullabilityDeclSpec /* nullability-declspec */, 6715,
           /* DiagArray332 */ 773, 0, R"()")
DIAG_ENTRY(anonymous_96 /* nullability-extension */, 6736,
           /* DiagArray333 */ 775, 0, R"()")
DIAG_ENTRY(
    NullabilityInferredOnNestedType /* nullability-inferred-on-nested-type */,
    6758, /* DiagArray334 */ 777, 0, R"()")
DIAG_ENTRY(NullableToNonNullConversion /* nullable-to-nonnull-conversion */,
           6794, /* DiagArray335 */ 779, 0, R"()")
DIAG_ENTRY(anonymous_20 /* old-style-definition */, 6825, 0, 0, R"()")
DIAG_ENTRY(OptionIgnored /* option-ignored */, 6846, /* DiagArray337 */ 781, 0,
           R"()")
DIAG_ENTRY(
    OrderedCompareFunctionPointers /* ordered-compare-function-pointers */,
    6861, /* DiagArray338 */ 786, 0, R"()")
DIAG_ENTRY(OutOfLineDeclaration /* out-of-line-declaration */, 6895, 0, 0,
           R"()")
DIAG_ENTRY(anonymous_106 /* out-of-scope-function */, 6919,
           /* DiagArray340 */ 788, 0, R"()")
DIAG_ENTRY(anonymous_21 /* overflow */, 6941, 0, 0, R"()")
DIAG_ENTRY(OverlengthStrings /* overlength-strings */, 6950,
           /* DiagArray342 */ 790, 0, R"()")
DIAG_ENTRY(anonymous_19 /* override-init */, 6969, 0, /* DiagSubGroup343 */ 236,
           R"()")
DIAG_ENTRY(anonymous_65 /* override-module */, 6983, /* DiagArray344 */ 792, 0,
           R"()")
DIAG_ENTRY(anonymous_59 /* overriding-option */, 6999, /* DiagArray345 */ 794,
           0, R"()")
DIAG_ENTRY(Packed /* packed */, 7017, /* DiagArray346 */ 796,
           /* DiagSubGroup346 */ 238, R"()")
DIAG_ENTRY(PackedNonPod /* packed-non-pod */, 7024, /* DiagArray347 */ 798, 0,
           R"()")
DIAG_ENTRY(Padded /* padded */, 7039, /* DiagArray348 */ 800,
           /* DiagSubGroup348 */ 240, R"()")
DIAG_ENTRY(PaddedBitField /* padded-bitfield */, 7046, /* DiagArray349 */ 804,
           0, R"()")
DIAG_ENTRY(Parentheses /* parentheses */, 7062, /* DiagArray350 */ 807,
           /* DiagSubGroup350 */ 242, R"()")
DIAG_ENTRY(ParenthesesOnEquality /* parentheses-equality */, 7074,
           /* DiagArray351 */ 811, 0, R"()")
DIAG_ENTRY(anonymous_7 /* partial-availability */, 7095, 0,
           /* DiagSubGroup352 */ 250, R"()")
DIAG_ENTRY(BackendOptimizationRemark /* pass */, 7116, /* DiagArray353 */ 813,
           0, R"()")
DIAG_ENTRY(BackendOptimizationRemarkAnalysis /* pass-analysis */, 7121,
           /* DiagArray354 */ 815, 0, R"()")
DIAG_ENTRY(BackendOptimizationFailure /* pass-failed */, 7135,
           /* DiagArray355 */ 819, 0, R"()")
DIAG_ENTRY(BackendOptimizationRemarkMissed /* pass-missed */, 7147,
           /* DiagArray356 */ 821, 0, R"()")
DIAG_ENTRY(Pedantic /* pedantic */, 7159, /* DiagArray357 */ 823,
           /* DiagSubGroup357 */ 252, R"()")
DIAG_ENTRY(PedanticMacros /* pedantic-macros */, 7168, 0,
           /* DiagSubGroup358 */ 303, R"()")
DIAG_ENTRY(PointerArith /* pointer-arith */, 7184, /* DiagArray359 */ 875,
           /* DiagSubGroup359 */ 309, R"()")
DIAG_ENTRY(PointerBoolConversion /* pointer-bool-conversion */, 7198,
           /* DiagArray360 */ 879, 0, R"()")
DIAG_ENTRY(anonymous_122 /* pointer-compare */, 7222, /* DiagArray361 */ 882, 0,
           R"()")
DIAG_ENTRY(anonymous_148 /* pointer-integer-compare */, 7238,
           /* DiagArray362 */ 884, 0, R"()")
DIAG_ENTRY(anonymous_150 /* pointer-sign */, 7262, /* DiagArray363 */ 886, 0,
           R"()")
DIAG_ENTRY(PointerToEnumCast /* pointer-to-enum-cast */, 7275,
           /* DiagArray364 */ 888, /* DiagSubGroup364 */ 311, R"()")
DIAG_ENTRY(PointerToIntCast /* pointer-to-int-cast */, 7296,
           /* DiagArray365 */ 890, /* DiagSubGroup365 */ 313, R"()")
DIAG_ENTRY(anonymous_155 /* pointer-type-mismatch */, 7316,
           /* DiagArray366 */ 892, 0, R"()")
DIAG_ENTRY(anonymous_58 /* poison-system-directories */, 7338,
           /* DiagArray367 */ 894, 0, R"()")
DIAG_ENTRY(
    PotentiallyEvaluatedExpression /* potentially-evaluated-expression */, 7364,
    0, 0, R"()")
DIAG_ENTRY(PragmaNeverCAttribute /* pragma-neverc-attribute */, 7397,
           /* DiagArray369 */ 896, 0, R"()")
DIAG_ENTRY(anonymous_83 /* pragma-once-outside-header */, 7421,
           /* DiagArray370 */ 898, 0, R"()")
DIAG_ENTRY(PragmaPack /* pragma-pack */, 7448, /* DiagArray371 */ 900, 0, R"()")
DIAG_ENTRY(anonymous_84 /* pragma-system-header-outside-header */, 7460,
           /* DiagArray372 */ 902, 0, R"()")
DIAG_ENTRY(Pragmas /* pragmas */, 7496, /* DiagArray373 */ 904,
           /* DiagSubGroup373 */ 316, R"()")
DIAG_ENTRY(CPre23Compat /* pre-c23-compat */, 7504, /* DiagArray374 */ 906, 0,
           R"()")
DIAG_ENTRY(anonymous_11 /* pre-c2x-compat */, 7519, 0,
           /* DiagSubGroup375 */ 321, R"()")
DIAG_ENTRY(anonymous_103 /* predefined-identifier-outside-function */, 7534,
           /* DiagArray376 */ 920, 0, R"()")
DIAG_ENTRY(PrivateExtern /* private-extern */, 7573, /* DiagArray377 */ 922, 0,
           R"()")
DIAG_ENTRY(anonymous_67 /* psabi */, 7588, /* DiagArray378 */ 924, 0, R"()")
DIAG_ENTRY(anonymous_136 /* qualified-void-return-type */, 7594,
           /* DiagArray379 */ 926, 0, R"()")
DIAG_ENTRY(FrameworkHdrQuotedInclude /* quoted-include-in-framework-header */,
           7621, /* DiagArray380 */ 928, 0, R"()")
DIAG_ENTRY(ReadOnlyPlacementChecks /* read-only-types */, 7656,
           /* DiagArray381 */ 930, 0, R"()")
DIAG_ENTRY(anonymous_22 /* redundant-decls */, 7672, 0, 0, R"()")
DIAG_ENTRY(RemarkBackendPlugin /* remark-backend-plugin */, 7688,
           /* DiagArray383 */ 932, 0, R"()")
DIAG_ENTRY(ReservedIdAsMacroAlias /* reserved-id-macro */, 7710, 0,
           /* DiagSubGroup384 */ 323, R"()")
DIAG_ENTRY(ReservedIdentifier /* reserved-identifier */, 7728,
           /* DiagArray385 */ 934, /* DiagSubGroup385 */ 325, R"()")
DIAG_ENTRY(ReservedIdAsMacro /* reserved-macro-identifier */, 7748,
           /* DiagArray386 */ 936, 0, R"()")
DIAG_ENTRY(RestrictExpansionMacro /* restrict-expansion */, 7774,
           /* DiagArray387 */ 938, 0, R"()")
DIAG_ENTRY(anonymous_12 /* return-local-addr */, 7793, 0,
           /* DiagSubGroup388 */ 327, R"()")
DIAG_ENTRY(ReturnStackAddress /* return-stack-address */, 7811,
           /* DiagArray389 */ 940, 0, R"()")
DIAG_ENTRY(ReturnType /* return-type */, 7832, /* DiagArray390 */ 944, 0, R"()")
DIAG_ENTRY(UsedSearchPath /* search-path-usage */, 7844, /* DiagArray391 */ 948,
           0, R"()")
DIAG_ENTRY(Section /* section */, 7862, /* DiagArray392 */ 950, 0, R"()")
DIAG_ENTRY(SelfAssignment /* self-assign */, 7870, /* DiagArray393 */ 954,
           /* DiagSubGroup393 */ 329, R"()")
DIAG_ENTRY(SelfAssignmentField /* self-assign-field */, 7882,
           /* DiagArray394 */ 956, 0, R"()")
DIAG_ENTRY(Sentinel /* sentinel */, 7900, /* DiagArray395 */ 958, 0, R"()")
DIAG_ENTRY(anonymous_25 /* sequence-point */, 7909, 0,
           /* DiagSubGroup396 */ 331, R"()")
DIAG_ENTRY(Shadow /* shadow */, 7924, /* DiagArray397 */ 961, 0, R"()")
DIAG_ENTRY(ShadowAll /* shadow-all */, 7931, 0, /* DiagSubGroup398 */ 333,
           R"()")
DIAG_ENTRY(ShadowField /* shadow-field */, 7942, 0, 0, R"()")
DIAG_ENTRY(ShadowUncapturedLocal /* shadow-uncaptured-local */, 7955, 0, 0,
           R"()")
DIAG_ENTRY(anonymous_140 /* shift-count-negative */, 7979,
           /* DiagArray401 */ 963, 0, R"()")
DIAG_ENTRY(anonymous_141 /* shift-count-overflow */, 8000,
           /* DiagArray402 */ 965, 0, R"()")
DIAG_ENTRY(anonymous_139 /* shift-negative-value */, 8021,
           /* DiagArray403 */ 967, 0, R"()")
DIAG_ENTRY(ShiftOpParentheses /* shift-op-parentheses */, 8042,
           /* DiagArray404 */ 969, 0, R"()")
DIAG_ENTRY(anonymous_142 /* shift-overflow */, 8063, /* DiagArray405 */ 971, 0,
           R"()")
DIAG_ENTRY(anonymous_143 /* shift-sign-overflow */, 8078,
           /* DiagArray406 */ 973, 0, R"()")
DIAG_ENTRY(Shorten64To32 /* shorten-64-to-32 */, 8098, /* DiagArray407 */ 975,
           0, R"()")
DIAG_ENTRY(SignCompare /* sign-compare */, 8115, /* DiagArray408 */ 977, 0,
           R"()")
DIAG_ENTRY(SignConversion /* sign-conversion */, 8128, /* DiagArray409 */ 979,
           0, R"()")
DIAG_ENTRY(anonymous_23 /* sign-promo */, 8144, 0, 0, R"()")
DIAG_ENTRY(SignedEnumBitfield /* signed-enum-bitfield */, 8155, 0, 0, R"()")
DIAG_ENTRY(anonymous_161 /* signed-unsigned-wchar */, 8176,
           /* DiagArray412 */ 983, 0, R"()")
DIAG_ENTRY(SingleBitBitFieldConstantConversion /* single-bit-bitfield-constant-conversion
                                                */
           ,
           8198, /* DiagArray413 */ 985, 0, R"()")
DIAG_ENTRY(SizeofArrayArgument /* sizeof-array-argument */, 8238,
           /* DiagArray414 */ 987, 0, R"()")
DIAG_ENTRY(SizeofArrayDecay /* sizeof-array-decay */, 8260,
           /* DiagArray415 */ 989, 0, R"()")
DIAG_ENTRY(anonymous_124 /* sizeof-array-div */, 8279, /* DiagArray416 */ 991,
           0, R"()")
DIAG_ENTRY(anonymous_123 /* sizeof-pointer-div */, 8296, /* DiagArray417 */ 993,
           0, R"()")
DIAG_ENTRY(SizeofPointerMemaccess /* sizeof-pointer-memaccess */, 8315,
           /* DiagArray418 */ 995, 0, R"()")
DIAG_ENTRY(anonymous_63 /* slash-u-filename */, 8340, /* DiagArray419 */ 998, 0,
           R"()")
DIAG_ENTRY(anonymous_55 /* slh-asm-goto */, 8357, /* DiagArray420 */ 1000, 0,
           R"()")
DIAG_ENTRY(anonymous_57 /* sloc-usage */, 8370, /* DiagArray421 */ 1002, 0,
           R"()")
DIAG_ENTRY(UninitializedSometimes /* sometimes-uninitialized */, 8381, 0, 0,
           R"()")
DIAG_ENTRY(BackendSourceMgr /* source-mgr */, 8405, /* DiagArray423 */ 1004, 0,
           R"()")
DIAG_ENTRY(anonymous_53 /* stack-exhausted */, 8416, /* DiagArray424 */ 1006, 0,
           R"()")
DIAG_ENTRY(anonymous_54 /* stack-protector */, 8432, /* DiagArray425 */ 1008, 0,
           R"()")
DIAG_ENTRY(StaticFloatInit /* static-float-init */, 8448, 0,
           /* DiagSubGroup426 */ 337, R"()")
DIAG_ENTRY(StaticInInline /* static-in-inline */, 8466, /* DiagArray427 */ 1010,
           0, R"()")
DIAG_ENTRY(StaticLocalInInline /* static-local-in-inline */, 8483,
           /* DiagArray428 */ 1013, 0, R"()")
DIAG_ENTRY(UninitializedStaticSelfInit /* static-self-init */, 8506, 0, 0,
           R"()")
DIAG_ENTRY(anonymous_29 /* strict-aliasing */, 8523, 0, 0, R"()")
DIAG_ENTRY(anonymous_26 /* strict-aliasing=0 */, 8539, 0, 0, R"()")
DIAG_ENTRY(anonymous_27 /* strict-aliasing=1 */, 8557, 0, 0, R"()")
DIAG_ENTRY(anonymous_28 /* strict-aliasing=2 */, 8575, 0, 0, R"()")
DIAG_ENTRY(anonymous_36 /* strict-overflow */, 8593, 0, 0, R"()")
DIAG_ENTRY(anonymous_30 /* strict-overflow=0 */, 8609, 0, 0, R"()")
DIAG_ENTRY(anonymous_31 /* strict-overflow=1 */, 8627, 0, 0, R"()")
DIAG_ENTRY(anonymous_32 /* strict-overflow=2 */, 8645, 0, 0, R"()")
DIAG_ENTRY(anonymous_33 /* strict-overflow=3 */, 8663, 0, 0, R"()")
DIAG_ENTRY(anonymous_34 /* strict-overflow=4 */, 8681, 0, 0, R"()")
DIAG_ENTRY(anonymous_35 /* strict-overflow=5 */, 8699, 0, 0, R"()")
DIAG_ENTRY(StrictPrototypes /* strict-prototypes */, 8717,
           /* DiagArray441 */ 1015, /* DiagSubGroup441 */ 339, R"()")
DIAG_ENTRY(StringCompare /* string-compare */, 8735, /* DiagArray442 */ 1017, 0,
           R"()")
DIAG_ENTRY(StringConcatation /* string-concatenation */, 8750,
           /* DiagArray443 */ 1019, 0, R"()")
DIAG_ENTRY(StringConversion /* string-conversion */, 8771,
           /* DiagArray444 */ 1021, 0, R"()")
DIAG_ENTRY(StringPlusChar /* string-plus-char */, 8789, /* DiagArray445 */ 1023,
           0, R"()")
DIAG_ENTRY(StringPlusInt /* string-plus-int */, 8806, /* DiagArray446 */ 1025,
           0, R"()")
DIAG_ENTRY(anonymous_109 /* strlcpy-strlcat-size */, 8822,
           /* DiagArray447 */ 1027, 0, R"()")
DIAG_ENTRY(StrncatSize /* strncat-size */, 8843, /* DiagArray448 */ 1029, 0,
           R"()")
DIAG_ENTRY(SuspiciousBzero /* suspicious-bzero */, 8856,
           /* DiagArray449 */ 1033, 0, R"()")
DIAG_ENTRY(SuspiciousMemaccess /* suspicious-memaccess */, 8873, 0,
           /* DiagSubGroup450 */ 341, R"()")
DIAG_ENTRY(Switch /* switch */, 8894, /* DiagArray451 */ 1035, 0, R"()")
DIAG_ENTRY(SwitchBool /* switch-bool */, 8901, /* DiagArray452 */ 1039, 0,
           R"()")
DIAG_ENTRY(SwitchDefault /* switch-default */, 8913, /* DiagArray453 */ 1041, 0,
           R"()")
DIAG_ENTRY(SwitchEnum /* switch-enum */, 8928, /* DiagArray454 */ 1043, 0,
           R"()")
DIAG_ENTRY(SyncAlignment /* sync-alignment */, 8940, /* DiagArray455 */ 1045, 0,
           R"()")
DIAG_ENTRY(anonymous_160 /* sync-fetch-and-nand-semantics-changed */, 8955,
           /* DiagArray456 */ 1047, 0, R"()")
DIAG_ENTRY(anonymous_24 /* synth */, 8993, 0, 0, R"()")
DIAG_ENTRY(TargetClonesMixedSpecifiers /* target-clones-mixed-specifiers */,
           8999, /* DiagArray458 */ 1049, 0, R"()")
DIAG_ENTRY(TautologicalBitwiseCompare /* tautological-bitwise-compare */, 9030,
           0, 0, R"()")
DIAG_ENTRY(TautologicalCompare /* tautological-compare */, 9059,
           /* DiagArray460 */ 1051, /* DiagSubGroup460 */ 346, R"()")
DIAG_ENTRY(TautologicalConstantCompare /* tautological-constant-compare */,
           9080, /* DiagArray461 */ 1054, /* DiagSubGroup461 */ 353, R"()")
DIAG_ENTRY(
    TautologicalInRangeCompare /* tautological-constant-in-range-compare */,
    9110, 0, /* DiagSubGroup462 */ 355, R"()")
DIAG_ENTRY(TautologicalOutOfRangeCompare /* tautological-constant-out-of-range-compare
                                          */
           ,
           9149, /* DiagArray463 */ 1058, 0, R"()")
DIAG_ENTRY(TautologicalNegationCompare /* tautological-negation-compare */,
           9192, 0, 0, R"()")
DIAG_ENTRY(TautologicalOverlapCompare /* tautological-overlap-compare */, 9222,
           0, 0, R"()")
DIAG_ENTRY(TautologicalPointerCompare /* tautological-pointer-compare */, 9251,
           /* DiagArray466 */ 1060, 0, R"()")
DIAG_ENTRY(TautologicalTypeLimitCompare /* tautological-type-limit-compare */,
           9280, /* DiagArray467 */ 1063, 0, R"()")
DIAG_ENTRY(TautologicalUndefinedCompare /* tautological-undefined-compare */,
           9312, 0, 0, R"()")
DIAG_ENTRY(TautologicalUnsignedCharZeroCompare /* tautological-unsigned-char-zero-compare
                                                */
           ,
           9343, /* DiagArray469 */ 1065, 0, R"()")
DIAG_ENTRY(TautologicalUnsignedEnumZeroCompare /* tautological-unsigned-enum-zero-compare
                                                */
           ,
           9383, /* DiagArray470 */ 1067, 0, R"()")
DIAG_ENTRY(
    TautologicalUnsignedZeroCompare /* tautological-unsigned-zero-compare */,
    9423, /* DiagArray471 */ 1069, 0, R"()")
DIAG_ENTRY(TautologicalValueRangeCompare /* tautological-value-range-compare */,
           9458, /* DiagArray472 */ 1071, 0, R"()")
DIAG_ENTRY(anonymous_163 /* tcb-enforcement */, 9491, /* DiagArray473 */ 1073,
           0, R"()")
DIAG_ENTRY(anonymous_145 /* tentative-definition-incomplete-type */, 9507,
           /* DiagArray474 */ 1075, 0, R"()")
DIAG_ENTRY(Trigraphs /* trigraphs */, 9544, /* DiagArray475 */ 1077, 0, R"()")
DIAG_ENTRY(TypeLimits /* type-limits */, 9554, 0, /* DiagSubGroup476 */ 358,
           R"()")
DIAG_ENTRY(TypeSafety /* type-safety */, 9566, /* DiagArray477 */ 1082, 0,
           R"()")
DIAG_ENTRY(anonymous_131 /* typedef-redefinition */, 9578,
           /* DiagArray478 */ 1086, 0, R"()")
DIAG_ENTRY(anonymous_66 /* unable-to-open-stats-file */, 9599,
           /* DiagArray479 */ 1088, 0, R"()")
DIAG_ENTRY(UnalignedAccess /* unaligned-access */, 9625,
           /* DiagArray480 */ 1090, 0, R"()")
DIAG_ENTRY(anonymous_162 /* unaligned-qualifier-implicit-cast */, 9642,
           /* DiagArray481 */ 1092, 0, R"()")
DIAG_ENTRY(anonymous_87 /* undef */, 9676, /* DiagArray482 */ 1094, 0, R"()")
DIAG_ENTRY(anonymous_88 /* undef-prefix */, 9682, /* DiagArray483 */ 1096, 0,
           R"()")
DIAG_ENTRY(anonymous_113 /* undefined-arm-streaming */, 9695,
           /* DiagArray484 */ 1098, 0, R"()")
DIAG_ENTRY(anonymous_114 /* undefined-arm-za */, 9719, /* DiagArray485 */ 1100,
           0, R"()")
DIAG_ENTRY(UndefinedBoolConversion /* undefined-bool-conversion */, 9736, 0, 0,
           R"()")
DIAG_ENTRY(anonymous_130 /* undefined-inline */, 9762, /* DiagArray487 */ 1102,
           0, R"()")
DIAG_ENTRY(anonymous_129 /* undefined-internal */, 9779,
           /* DiagArray488 */ 1104, 0, R"()")
DIAG_ENTRY(UnevaluatedExpression /* unevaluated-expression */, 9798,
           /* DiagArray489 */ 1106, /* DiagSubGroup489 */ 363, R"()")
DIAG_ENTRY(UnguardedAvailability /* unguarded-availability */, 9821,
           /* DiagArray490 */ 1108, /* DiagSubGroup490 */ 365, R"()")
DIAG_ENTRY(UnguardedAvailabilityNew /* unguarded-availability-new */, 9844,
           /* DiagArray491 */ 1110, 0, R"()")
DIAG_ENTRY(Unicode /* unicode */, 9871, /* DiagArray492 */ 1112, 0, R"()")
DIAG_ENTRY(anonymous_73 /* unicode-homoglyph */, 9879, /* DiagArray493 */ 1119,
           0, R"()")
DIAG_ENTRY(anonymous_72 /* unicode-whitespace */, 9897, /* DiagArray494 */ 1121,
           0, R"()")
DIAG_ENTRY(anonymous_74 /* unicode-zero-width */, 9916, /* DiagArray495 */ 1123,
           0, R"()")
DIAG_ENTRY(Uninitialized /* uninitialized */, 9935, 0,
           /* DiagSubGroup496 */ 367, R"()")
DIAG_ENTRY(UninitializedConstReference /* uninitialized-const-reference */,
           9949, 0, 0, R"()")
DIAG_ENTRY(UnknownArgument /* unknown-argument */, 9979,
           /* DiagArray498 */ 1125, 0, R"()")
DIAG_ENTRY(UnknownAssumption /* unknown-assumption */, 9996,
           /* DiagArray499 */ 1127, 0, R"()")
DIAG_ENTRY(UnknownAttributes /* unknown-attributes */, 10015,
           /* DiagArray500 */ 1129, 0, R"()")
DIAG_ENTRY(anonymous_91 /* unknown-directives */, 10034,
           /* DiagArray501 */ 1131, 0, R"()")
DIAG_ENTRY(anonymous_77 /* unknown-escape-sequence */, 10053,
           /* DiagArray502 */ 1133, 0, R"()")
DIAG_ENTRY(UnknownPragmas /* unknown-pragmas */, 10077, /* DiagArray503 */ 1135,
           0, R"()")
DIAG_ENTRY(UnknownWarningOption /* unknown-warning-option */, 10093,
           /* DiagArray504 */ 1156, 0, R"()")
DIAG_ENTRY(UnneededInternalDecl /* unneeded-internal-declaration */, 10116,
           /* DiagArray505 */ 1160, 0, R"()")
DIAG_ENTRY(UnreachableCode /* unreachable-code */, 10146, 0,
           /* DiagSubGroup506 */ 371, R"()")
DIAG_ENTRY(UnreachableCodeAggressive /* unreachable-code-aggressive */, 10163,
           0, /* DiagSubGroup507 */ 375, R"()")
DIAG_ENTRY(UnreachableCodeBreak /* unreachable-code-break */, 10191, 0, 0,
           R"()")
DIAG_ENTRY(UnreachableCodeFallthrough /* unreachable-code-fallthrough */, 10214,
           0, 0, R"()")
DIAG_ENTRY(UnreachableCodeGenericAssoc /* unreachable-code-generic-assoc */,
           10243, /* DiagArray510 */ 1163, 0, R"()")
DIAG_ENTRY(UnreachableCodeLoopIncrement /* unreachable-code-loop-increment */,
           10274, 0, 0, R"()")
DIAG_ENTRY(UnreachableCodeReturn /* unreachable-code-return */, 10306, 0, 0,
           R"()")
DIAG_ENTRY(Unsequenced /* unsequenced */, 10330, /* DiagArray513 */ 1165, 0,
           R"()")
DIAG_ENTRY(UnsupportedABI /* unsupported-abi */, 10342, 0, 0, R"()")
DIAG_ENTRY(UnsupportedFPOpt /* unsupported-floating-point-opt */, 10358,
           /* DiagArray515 */ 1168, 0, R"()")
DIAG_ENTRY(UnsupportedTargetOpt /* unsupported-target-opt */, 10389,
           /* DiagArray516 */ 1171, 0, R"()")
DIAG_ENTRY(anonymous_126 /* unsupported-visibility */, 10412,
           /* DiagArray517 */ 1174, 0, R"()")
DIAG_ENTRY(Unused /* unused */, 10435, 0, /* DiagSubGroup518 */ 379, R"()")
DIAG_ENTRY(UnusedArgument /* unused-argument */, 10442, 0, 0, R"()")
DIAG_ENTRY(UnusedButSetParameter /* unused-but-set-parameter */, 10458,
           /* DiagArray520 */ 1176, 0, R"()")
DIAG_ENTRY(UnusedButSetVariable /* unused-but-set-variable */, 10483,
           /* DiagArray521 */ 1178, 0, R"()")
DIAG_ENTRY(UnusedCommandLineArgument /* unused-command-line-argument */, 10507,
           /* DiagArray522 */ 1180, 0, R"()")
DIAG_ENTRY(UnusedComparison /* unused-comparison */, 10536,
           /* DiagArray523 */ 1190, 0, R"()")
DIAG_ENTRY(UnusedConstVariable /* unused-const-variable */, 10554,
           /* DiagArray524 */ 1192, 0, R"()")
DIAG_ENTRY(UnusedFunction /* unused-function */, 10576, /* DiagArray525 */ 1194,
           /* DiagSubGroup525 */ 387, R"()")
DIAG_ENTRY(UnusedLabel /* unused-label */, 10592, /* DiagArray526 */ 1196, 0,
           R"()")
DIAG_ENTRY(UnusedLocalTypedef /* unused-local-typedef */, 10605,
           /* DiagArray527 */ 1198, 0, R"()")
DIAG_ENTRY(anonymous_47 /* unused-local-typedefs */, 10626, 0,
           /* DiagSubGroup528 */ 389, R"()")
DIAG_ENTRY(anonymous_86 /* unused-macros */, 10648, /* DiagArray529 */ 1200, 0,
           R"()")
DIAG_ENTRY(UnusedParameter /* unused-parameter */, 10662,
           /* DiagArray530 */ 1202, 0, R"()")
DIAG_ENTRY(UnusedResult /* unused-result */, 10679, /* DiagArray531 */ 1204, 0,
           R"()")
DIAG_ENTRY(UnusedValue /* unused-value */, 10693, /* DiagArray532 */ 1207,
           /* DiagSubGroup532 */ 391, R"()")
DIAG_ENTRY(UnusedVariable /* unused-variable */, 10706, /* DiagArray533 */ 1212,
           /* DiagSubGroup533 */ 395, R"()")
DIAG_ENTRY(anonymous_157 /* unused-volatile-lvalue */, 10722,
           /* DiagArray534 */ 1214, 0, R"()")
DIAG_ENTRY(UsedButMarkedUnused /* used-but-marked-unused */, 10745,
           /* DiagArray535 */ 1216, 0, R"()")
DIAG_ENTRY(UserDefinedWarnings /* user-defined-warnings */, 10768,
           /* DiagArray536 */ 1218, 0, R"()")
DIAG_ENTRY(Varargs /* varargs */, 10790, /* DiagArray537 */ 1220, 0, R"()")
DIAG_ENTRY(VariadicMacros /* variadic-macros */, 10798, /* DiagArray538 */ 1224,
           0, R"()")
DIAG_ENTRY(anonymous_115 /* vec-elem-size */, 10814, /* DiagArray539 */ 1228, 0,
           R"()")
DIAG_ENTRY(VectorConversion /* vector-conversion */, 10828,
           /* DiagArray540 */ 1230, 0, R"()")
DIAG_ENTRY(anonymous_46 /* vector-conversions */, 10846, 0,
           /* DiagSubGroup541 */ 397, R"()")
DIAG_ENTRY(Visibility /* visibility */, 10865, /* DiagArray542 */ 1232, 0,
           R"()")
DIAG_ENTRY(VLA /* vla */, 10876, /* DiagArray543 */ 1235,
           /* DiagSubGroup543 */ 399, R"()")
DIAG_ENTRY(VLAExtension /* vla-extension */, 10880, /* DiagArray544 */ 1237,
           /* DiagSubGroup544 */ 401, R"()")
DIAG_ENTRY(VLAUseStaticAssert /* vla-extension-static-assert */, 10894, 0, 0,
           R"()")
DIAG_ENTRY(VoidPointerToEnumCast /* void-pointer-to-enum-cast */, 10922,
           /* DiagArray546 */ 1239, 0, R"()")
DIAG_ENTRY(VoidPointerToIntCast /* void-pointer-to-int-cast */, 10948,
           /* DiagArray547 */ 1241, /* DiagSubGroup547 */ 403, R"()")
DIAG_ENTRY(VoidPointerDeref /* void-ptr-dereference */, 10973,
           /* DiagArray548 */ 1243, 0, R"()")
DIAG_ENTRY(VolatileRegisterVar /* volatile-register-var */, 10994, 0, 0, R"()")
DIAG_ENTRY(WritableStrings /* writable-strings */, 11016, 0,
           /* DiagSubGroup550 */ 405, R"()")
DIAG_ENTRY(
    GCCWriteStrings /* write-strings */, 11033, 0, /* DiagSubGroup551 */ 407,
    R"(**Note:** enabling this warning in C will change the semantic behavior of the
program by treating all string literals as having type ``const char *``
instead of ``char *``. This can cause unexpected behaviors with type-sensitive
constructs like ``_Generic``.)")
DIAG_ENTRY(XorUsedAsPow /* xor-used-as-pow */, 11047, /* DiagArray552 */ 1245,
           0, R"()")
DIAG_ENTRY(ZeroLengthArray /* zero-length-array */, 11063,
           /* DiagArray553 */ 1249, 0, R"()")
#endif // DIAG_ENTRY

#ifdef GET_CATEGORY_TABLE
CATEGORY("", DiagCat_None)
CATEGORY("Lexical or Preprocessor Issue", DiagCat_Lexical_or_Preprocessor_Issue)
CATEGORY("Semantic Issue", DiagCat_Semantic_Issue)
CATEGORY("Parse Issue", DiagCat_Parse_Issue)
CATEGORY("Inline Assembly Issue", DiagCat_Inline_Assembly_Issue)
CATEGORY("Backend Issue", DiagCat_Backend_Issue)
CATEGORY("SourceMgr Reported Issue", DiagCat_SourceMgr_Reported_Issue)
CATEGORY("Nullability Issue", DiagCat_Nullability_Issue)
CATEGORY("User-Defined Issue", DiagCat_User_Defined_Issue)
CATEGORY("Value Conversion Issue", DiagCat_Value_Conversion_Issue)
CATEGORY("Deprecations", DiagCat_Deprecations)
CATEGORY("Format String Issue", DiagCat_Format_String_Issue)
CATEGORY("#pragma message Directive", DiagCat__pragma_message_Directive)
CATEGORY("Unused Entity Issue", DiagCat_Unused_Entity_Issue)
#endif // GET_CATEGORY_TABLE

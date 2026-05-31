// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (c) 1996-2015, International Business Machines Corporation and others.
* All Rights Reserved.
*******************************************************************************
*/

#ifndef UCOL_H
#define UCOL_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/parseerr.h"
#include "unicode/uloc.h"
#include "unicode/uscript.h"

#if U_SHOW_CPLUSPLUS_API
#include "unicode/localpointer.h"
#endif   // U_SHOW_CPLUSPLUS_API

/**
 * @addtogroup icu4c ICU4C
 * @{
 * \file
 * \brief C API: Collator 
 *
 * <h2> Collator C API </h2>
 *
 * The C API for Collator performs locale-sensitive
 * string comparison. You use this service to build
 * searching and sorting routines for natural language text.
 * <p>
 * For more information about the collation service see 
 * <a href="https://unicode-org.github.io/icu/userguide/collation">the User Guide</a>.
 * <p>
 * Collation service provides correct sorting orders for most locales supported in ICU. 
 * If specific data for a locale is not available, the orders eventually falls back
 * to the <a href="http://www.unicode.org/reports/tr35/tr35-collation.html#Root_Collation">CLDR root sort order</a>. 
 * <p>
 * Sort ordering may be customized by providing your own set of rules. For more on
 * this subject see the <a href="https://unicode-org.github.io/icu/userguide/collation/customization">
 * Collation Customization</a> section of the User Guide.
 * <p>
 * @see         UCollationResult
 * @see         UNormalizationMode
 * @see         UCollationStrength
 * @see         UCollationElements
 */

/** A collator.
*  For usage in C programs.
*/
struct UCollator;
/** structure representing a collator object instance 
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
typedef struct UCollator UCollator;


/**
 * UCOL_LESS is returned if source string is compared to be less than target
 * string in the ucol_strcoll() method.
 * UCOL_EQUAL is returned if source string is compared to be equal to target
 * string in the ucol_strcoll() method.
 * UCOL_GREATER is returned if source string is compared to be greater than
 * target string in the ucol_strcoll() method.
 * @see ucol_strcoll()
 * <p>
 * Possible values for a comparison result 
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
typedef enum UCollationResult : int32_t {
  /** string a == string b */
  UCOL_EQUAL    = 0,
  /** string a > string b */
  UCOL_GREATER    = 1,
  /** string a < string b */
  UCOL_LESS    = -1
} UCollationResult ;


/** Enum containing attribute values for controlling collation behavior.
 * Here are all the allowable values. Not every attribute can take every value. The only
 * universal value is UCOL_DEFAULT, which resets the attribute value to the predefined  
 * value for that locale 
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
typedef enum UColAttributeValue : int32_t {
  /** accepted by most attributes */
  UCOL_DEFAULT = -1,

  /** Primary collation strength */
  UCOL_PRIMARY = 0,
  /** Secondary collation strength */
  UCOL_SECONDARY = 1,
  /** Tertiary collation strength */
  UCOL_TERTIARY = 2,
  /** Default collation strength */
  UCOL_DEFAULT_STRENGTH = UCOL_TERTIARY,
  UCOL_CE_STRENGTH_LIMIT,
  /** Quaternary collation strength */
  UCOL_QUATERNARY=3,
  /** Identical collation strength */
  UCOL_IDENTICAL=15,
  UCOL_STRENGTH_LIMIT,

  /** Turn the feature off - works for UCOL_FRENCH_COLLATION, 
      UCOL_CASE_LEVEL, UCOL_HIRAGANA_QUATERNARY_MODE
      & UCOL_DECOMPOSITION_MODE*/
  UCOL_OFF = 16,
  /** Turn the feature on - works for UCOL_FRENCH_COLLATION, 
      UCOL_CASE_LEVEL, UCOL_HIRAGANA_QUATERNARY_MODE
      & UCOL_DECOMPOSITION_MODE*/
  UCOL_ON = 17,
  
  /** Valid for UCOL_ALTERNATE_HANDLING. Alternate handling will be shifted */
  UCOL_SHIFTED = 20,
  /** Valid for UCOL_ALTERNATE_HANDLING. Alternate handling will be non ignorable */
  UCOL_NON_IGNORABLE = 21,

  /** Valid for UCOL_CASE_FIRST - 
      lower case sorts before upper case */
  UCOL_LOWER_FIRST = 24,
  /** upper case sorts before lower case */
  UCOL_UPPER_FIRST = 25,

#ifndef U_HIDE_DEPRECATED_API
    /**
     * One more than the highest normal UColAttributeValue value.
     * \xrefitem deprecated "Deprecated" "Deprecated List" ICU 58 The numeric value may change over time, see ICU ticket #12420.
     */
  UCOL_ATTRIBUTE_VALUE_COUNT
#endif  /* U_HIDE_DEPRECATED_API */
} UColAttributeValue;

/**
 * Enum containing the codes for reordering segments of the collation table that are not script
 * codes. These reordering codes are to be used in conjunction with the script codes.
 * @see ucol_getReorderCodes
 * @see ucol_setReorderCodes
 * @see ucol_getEquivalentReorderCodes
 * @see UScriptCode
 * \xrefitem stable "Stable" "Stable List" ICU 4.8
 */
 typedef enum UColReorderCode : int32_t {
   /**
    * A special reordering code that is used to specify the default
    * reordering codes for a locale.
    * \xrefitem stable "Stable" "Stable List" ICU 4.8
    */   
    UCOL_REORDER_CODE_DEFAULT       = -1,
   /**
    * A special reordering code that is used to specify no reordering codes.
    * \xrefitem stable "Stable" "Stable List" ICU 4.8
    */   
    UCOL_REORDER_CODE_NONE          = USCRIPT_UNKNOWN,
   /**
    * A special reordering code that is used to specify all other codes used for
    * reordering except for the codes lised as UColReorderCode values and those
    * listed explicitly in a reordering.
    * \xrefitem stable "Stable" "Stable List" ICU 4.8
    */   
    UCOL_REORDER_CODE_OTHERS        = USCRIPT_UNKNOWN,
   /**
    * Characters with the space property.
    * This is equivalent to the rule value "space".
    * \xrefitem stable "Stable" "Stable List" ICU 4.8
    */    
    UCOL_REORDER_CODE_SPACE         = 0x1000,
   /**
    * The first entry in the enumeration of reordering groups. This is intended for use in
    * range checking and enumeration of the reorder codes.
    * \xrefitem stable "Stable" "Stable List" ICU 4.8
    */    
    UCOL_REORDER_CODE_FIRST         = UCOL_REORDER_CODE_SPACE,
   /**
    * Characters with the punctuation property.
    * This is equivalent to the rule value "punct".
    * \xrefitem stable "Stable" "Stable List" ICU 4.8
    */    
    UCOL_REORDER_CODE_PUNCTUATION   = 0x1001,
   /**
    * Characters with the symbol property.
    * This is equivalent to the rule value "symbol".
    * \xrefitem stable "Stable" "Stable List" ICU 4.8
    */    
    UCOL_REORDER_CODE_SYMBOL        = 0x1002,
   /**
    * Characters with the currency property.
    * This is equivalent to the rule value "currency".
    * \xrefitem stable "Stable" "Stable List" ICU 4.8
    */    
    UCOL_REORDER_CODE_CURRENCY      = 0x1003,
   /**
    * Characters with the digit property.
    * This is equivalent to the rule value "digit".
    * \xrefitem stable "Stable" "Stable List" ICU 4.8
    */    
    UCOL_REORDER_CODE_DIGIT         = 0x1004,
#ifndef U_HIDE_DEPRECATED_API
    /**
     * One more than the highest normal UColReorderCode value.
     * \xrefitem deprecated "Deprecated" "Deprecated List" ICU 58 The numeric value may change over time, see ICU ticket #12420.
     */
    UCOL_REORDER_CODE_LIMIT         = 0x1005
#endif  /* U_HIDE_DEPRECATED_API */
} UColReorderCode;

/**
 * Base letter represents a primary difference.  Set comparison
 * level to UCOL_PRIMARY to ignore secondary and tertiary differences.
 * Use this to set the strength of a Collator object.
 * Example of primary difference, "abc" &lt; "abd"
 * 
 * Diacritical differences on the same base letter represent a secondary
 * difference.  Set comparison level to UCOL_SECONDARY to ignore tertiary
 * differences. Use this to set the strength of a Collator object.
 * Example of secondary difference, "&auml;" >> "a".
 *
 * Uppercase and lowercase versions of the same character represents a
 * tertiary difference.  Set comparison level to UCOL_TERTIARY to include
 * all comparison differences. Use this to set the strength of a Collator
 * object.
 * Example of tertiary difference, "abc" &lt;&lt;&lt; "ABC".
 *
 * Two characters are considered "identical" when they have the same
 * unicode spellings.  UCOL_IDENTICAL.
 * For example, "&auml;" == "&auml;".
 *
 * UCollationStrength is also used to determine the strength of sort keys 
 * generated from UCollator objects
 * These values can be now found in the UColAttributeValue enum.
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 **/
typedef UColAttributeValue UCollationStrength;

/** Attributes that collation service understands. All the attributes can take UCOL_DEFAULT
 * value, as well as the values specific to each one. 
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
typedef enum UColAttribute : int32_t {
     /** Attribute for direction of secondary weights - used in Canadian French.
      * Acceptable values are UCOL_ON, which results in secondary weights
      * being considered backwards and UCOL_OFF which treats secondary
      * weights in the order they appear.
      * \xrefitem stable "Stable" "Stable List" ICU 2.0
      */
     UCOL_FRENCH_COLLATION, 
     /** Attribute for handling variable elements.
      * Acceptable values are UCOL_NON_IGNORABLE (default)
      * which treats all the codepoints with non-ignorable 
      * primary weights in the same way,
      * and UCOL_SHIFTED which causes codepoints with primary 
      * weights that are equal or below the variable top value
      * to be ignored on primary level and moved to the quaternary 
      * level.
      * \xrefitem stable "Stable" "Stable List" ICU 2.0
      */
     UCOL_ALTERNATE_HANDLING, 
     /** Controls the ordering of upper and lower case letters.
      * Acceptable values are UCOL_OFF (default), which orders
      * upper and lower case letters in accordance to their tertiary
      * weights, UCOL_UPPER_FIRST which forces upper case letters to 
      * sort before lower case letters, and UCOL_LOWER_FIRST which does 
      * the opposite.
      * \xrefitem stable "Stable" "Stable List" ICU 2.0
      */
     UCOL_CASE_FIRST, 
     /** Controls whether an extra case level (positioned before the third
      * level) is generated or not. Acceptable values are UCOL_OFF (default), 
      * when case level is not generated, and UCOL_ON which causes the case
      * level to be generated. Contents of the case level are affected by
      * the value of UCOL_CASE_FIRST attribute. A simple way to ignore 
      * accent differences in a string is to set the strength to UCOL_PRIMARY
      * and enable case level.
      * \xrefitem stable "Stable" "Stable List" ICU 2.0
      */
     UCOL_CASE_LEVEL,
     /** Controls whether the normalization check and necessary normalizations
      * are performed. When set to UCOL_OFF (default) no normalization check
      * is performed. The correctness of the result is guaranteed only if the 
      * input data is in so-called FCD form (see users manual for more info).
      * When set to UCOL_ON, an incremental check is performed to see whether
      * the input data is in the FCD form. If the data is not in the FCD form,
      * incremental NFD normalization is performed.
      * \xrefitem stable "Stable" "Stable List" ICU 2.0
      */
     UCOL_NORMALIZATION_MODE, 
     /** An alias for UCOL_NORMALIZATION_MODE attribute.
      * \xrefitem stable "Stable" "Stable List" ICU 2.0
      */
     UCOL_DECOMPOSITION_MODE = UCOL_NORMALIZATION_MODE,
     /** The strength attribute. Can be either UCOL_PRIMARY, UCOL_SECONDARY,
      * UCOL_TERTIARY, UCOL_QUATERNARY or UCOL_IDENTICAL. The usual strength
      * for most locales (except Japanese) is tertiary.
      *
      * Quaternary strength 
      * is useful when combined with shifted setting for alternate handling
      * attribute and for JIS X 4061 collation, when it is used to distinguish
      * between Katakana and Hiragana.
      * Otherwise, quaternary level
      * is affected only by the number of non-ignorable code points in
      * the string.
      *
      * Identical strength is rarely useful, as it amounts 
      * to codepoints of the NFD form of the string.
      * \xrefitem stable "Stable" "Stable List" ICU 2.0
      */
     UCOL_STRENGTH,  
#ifndef U_HIDE_DEPRECATED_API
     /** When turned on, this attribute positions Hiragana before all  
      * non-ignorables on quaternary level This is a sneaky way to produce JIS
      * sort order.
      *
      * This attribute was an implementation detail of the CLDR Japanese tailoring.
      * Since ICU 50, this attribute is not settable any more via API functions.
      * Since CLDR 25/ICU 53, explicit quaternary relations are used
      * to achieve the same Japanese sort order.
      *
      * \xrefitem deprecated "Deprecated" "Deprecated List" ICU 50 Implementation detail, cannot be set via API, was removed from implementation.
      */
     UCOL_HIRAGANA_QUATERNARY_MODE = UCOL_STRENGTH + 1,
#endif  /* U_HIDE_DEPRECATED_API */
     /**
      * When turned on, this attribute makes
      * substrings of digits sort according to their numeric values.
      *
      * This is a way to get '100' to sort AFTER '2'. Note that the longest
      * digit substring that can be treated as a single unit is
      * 254 digits (not counting leading zeros). If a digit substring is
      * longer than that, the digits beyond the limit will be treated as a
      * separate digit substring.
      *
      * A "digit" in this sense is a code point with General_Category=Nd,
      * which does not include circled numbers, roman numerals, etc.
      * Only a contiguous digit substring is considered, that is,
      * non-negative integers without separators.
      * There is no support for plus/minus signs, decimals, exponents, etc.
      *
      * \xrefitem stable "Stable" "Stable List" ICU 2.8
      */
     UCOL_NUMERIC_COLLATION = UCOL_STRENGTH + 2, 

    /* Do not conditionalize the following with #ifndef U_HIDE_DEPRECATED_API,
     * it is needed for layout of RuleBasedCollator object. */
#ifndef U_FORCE_HIDE_DEPRECATED_API
    /**
     * One more than the highest normal UColAttribute value.
     * \xrefitem deprecated "Deprecated" "Deprecated List" ICU 58 The numeric value may change over time, see ICU ticket #12420.
     */
     UCOL_ATTRIBUTE_COUNT
#endif  // U_FORCE_HIDE_DEPRECATED_API
} UColAttribute;


/**
 * Open a UCollator for comparing strings.
 *
 * For some languages, multiple collation types are available;
 * for example, "de@collation=phonebook".
 * Starting with ICU 54, collation attributes can be specified via locale keywords as well,
 * in the old locale extension syntax ("el@colCaseFirst=upper")
 * or in language tag syntax ("el-u-kf-upper").
 * See <a href="https://unicode-org.github.io/icu/userguide/collation/api">User Guide: Collation API</a>.
 *
 * The UCollator pointer is used in all the calls to the Collation 
 * service. After finished, collator must be disposed of by calling
 * {@link #ucol_close }.
 * @param loc The locale containing the required collation rules. 
 *            Special values for locales can be passed in - 
 *            if NULL is passed for the locale, the default locale
 *            collation rules will be used. If empty string ("") or
 *            "root" are passed, the root collator will be returned.
 * @param status A pointer to a UErrorCode to receive any errors
 * @return A pointer to a UCollator, or 0 if an error occurred.
 * @see ucol_openRules
 * @see ucol_clone
 * @see ucol_close
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI UCollator* U_EXPORT2 
ucol_open(const char *loc, UErrorCode *status) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Produce a UCollator instance according to the rules supplied.
 * The rules are used to change the default ordering, defined in the
 * UCA in a process called tailoring. The resulting UCollator pointer
 * can be used in the same way as the one obtained by {@link #ucol_strcoll }.
 * @param rules A string describing the collation rules. For the syntax
 *              of the rules please see users guide.
 * @param rulesLength The length of rules, or -1 if null-terminated.
 * @param normalizationMode The normalization mode: One of
 *             UCOL_OFF     (expect the text to not need normalization),
 *             UCOL_ON      (normalize), or
 *             UCOL_DEFAULT (set the mode according to the rules)
 * @param strength The default collation strength; one of UCOL_PRIMARY, UCOL_SECONDARY,
 * UCOL_TERTIARY, UCOL_IDENTICAL,UCOL_DEFAULT_STRENGTH - can be also set in the rules.
 * @param parseError  A pointer to UParseError to receive information about errors
 *                    occurred during parsing. This argument can currently be set
 *                    to NULL, but at users own risk. Please provide a real structure.
 * @param status A pointer to a UErrorCode to receive any errors
 * @return A pointer to a UCollator. It is not guaranteed that NULL be returned in case
 *         of error - please use status argument to check for errors.
 * @see ucol_open
 * @see ucol_clone
 * @see ucol_close
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI UCollator* U_EXPORT2 
ucol_openRules( const UChar        *rules,
                int32_t            rulesLength,
                UColAttributeValue normalizationMode,
                UCollationStrength strength,
                UParseError        *parseError,
                UErrorCode         *status) __INTRODUCED_IN(__ANDROID_API_T__);



#ifndef U_HIDE_DEPRECATED_API

#endif  /* U_HIDE_DEPRECATED_API */

#ifndef U_HIDE_DEPRECATED_API

#endif  /* U_HIDE_DEPRECATED_API */



/** 
 * Close a UCollator.
 * Once closed, a UCollator should not be used. Every open collator should
 * be closed. Otherwise, a memory leak will result.
 * @param coll The UCollator to close.
 * @see ucol_open
 * @see ucol_openRules
 * @see ucol_clone
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI void U_EXPORT2 
ucol_close(UCollator *coll) __INTRODUCED_IN(__ANDROID_API_T__);



#if U_SHOW_CPLUSPLUS_API

U_NAMESPACE_BEGIN

/**
 * \class LocalUCollatorPointer
 * "Smart pointer" class, closes a UCollator via ucol_close().
 * For most methods see the LocalPointerBase base class.
 *
 * @see LocalPointerBase
 * @see LocalPointer
 * \xrefitem stable "Stable" "Stable List" ICU 4.4
 */
U_DEFINE_LOCAL_OPEN_POINTER(LocalUCollatorPointer, UCollator, ucol_close);

U_NAMESPACE_END

#endif

/**
 * Compare two strings.
 * The strings will be compared using the options already specified.
 * @param coll The UCollator containing the comparison rules.
 * @param source The source string.
 * @param sourceLength The length of source, or -1 if null-terminated.
 * @param target The target string.
 * @param targetLength The length of target, or -1 if null-terminated.
 * @return The result of comparing the strings; one of UCOL_EQUAL,
 * UCOL_GREATER, UCOL_LESS
 * @see ucol_greater
 * @see ucol_greaterOrEqual
 * @see ucol_equal
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI UCollationResult U_EXPORT2 
ucol_strcoll(    const    UCollator    *coll,
        const    UChar        *source,
        int32_t            sourceLength,
        const    UChar        *target,
        int32_t            targetLength) __INTRODUCED_IN(__ANDROID_API_T__);



/** 
* Compare two strings in UTF-8. 
* The strings will be compared using the options already specified. 
* Note: When input string contains malformed a UTF-8 byte sequence, 
* this function treats these bytes as REPLACEMENT CHARACTER (U+FFFD).
* @param coll The UCollator containing the comparison rules. 
* @param source The source UTF-8 string. 
* @param sourceLength The length of source, or -1 if null-terminated. 
* @param target The target UTF-8 string. 
* @param targetLength The length of target, or -1 if null-terminated. 
* @param status A pointer to a UErrorCode to receive any errors 
* @return The result of comparing the strings; one of UCOL_EQUAL, 
* UCOL_GREATER, UCOL_LESS 
* @see ucol_greater 
* @see ucol_greaterOrEqual 
* @see ucol_equal 
* \xrefitem stable "Stable" "Stable List" ICU 50 
*/ 
U_CAPI UCollationResult U_EXPORT2
ucol_strcollUTF8(
        const UCollator *coll,
        const char      *source,
        int32_t         sourceLength,
        const char      *target,
        int32_t         targetLength,
        UErrorCode      *status) __INTRODUCED_IN(__ANDROID_API_T__);











/**
 * Get the collation strength used in a UCollator.
 * The strength influences how strings are compared.
 * @param coll The UCollator to query.
 * @return The collation strength; one of UCOL_PRIMARY, UCOL_SECONDARY,
 * UCOL_TERTIARY, UCOL_QUATERNARY, UCOL_IDENTICAL
 * @see ucol_setStrength
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI UCollationStrength U_EXPORT2 
ucol_getStrength(const UCollator *coll) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Set the collation strength used in a UCollator.
 * The strength influences how strings are compared.
 * @param coll The UCollator to set.
 * @param strength The desired collation strength; one of UCOL_PRIMARY, 
 * UCOL_SECONDARY, UCOL_TERTIARY, UCOL_QUATERNARY, UCOL_IDENTICAL, UCOL_DEFAULT
 * @see ucol_getStrength
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI void U_EXPORT2 
ucol_setStrength(UCollator *coll,
                 UCollationStrength strength) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Retrieves the reordering codes for this collator.
 * These reordering codes are a combination of UScript codes and UColReorderCode entries.
 * @param coll The UCollator to query.
 * @param dest The array to fill with the script ordering.
 * @param destCapacity The length of dest. If it is 0, then dest may be NULL and the function 
 * will only return the length of the result without writing any codes (pre-flighting).
 * @param pErrorCode Must be a valid pointer to an error code value, which must not indicate a 
 * failure before the function call.
 * @return The number of reordering codes written to the dest array.
 * @see ucol_setReorderCodes
 * @see ucol_getEquivalentReorderCodes
 * @see UScriptCode
 * @see UColReorderCode
 * \xrefitem stable "Stable" "Stable List" ICU 4.8
 */
U_CAPI int32_t U_EXPORT2 
ucol_getReorderCodes(const UCollator* coll,
                    int32_t* dest,
                    int32_t destCapacity,
                    UErrorCode *pErrorCode) __INTRODUCED_IN(__ANDROID_API_T__);


/** 
 * Sets the reordering codes for this collator.
 * Collation reordering allows scripts and some other groups of characters
 * to be moved relative to each other. This reordering is done on top of
 * the DUCET/CLDR standard collation order. Reordering can specify groups to be placed 
 * at the start and/or the end of the collation order. These groups are specified using
 * UScript codes and UColReorderCode entries.
 *
 * <p>By default, reordering codes specified for the start of the order are placed in the 
 * order given after several special non-script blocks. These special groups of characters
 * are space, punctuation, symbol, currency, and digit. These special groups are represented with
 * UColReorderCode entries. Script groups can be intermingled with 
 * these special non-script groups if those special groups are explicitly specified in the reordering.
 *
 * <p>The special code OTHERS stands for any script that is not explicitly 
 * mentioned in the list of reordering codes given. Anything that is after OTHERS
 * will go at the very end of the reordering in the order given.
 *
 * <p>The special reorder code DEFAULT will reset the reordering for this collator
 * to the default for this collator. The default reordering may be the DUCET/CLDR order or may be a reordering that
 * was specified when this collator was created from resource data or from rules. The 
 * DEFAULT code <b>must</b> be the sole code supplied when it is used.
 * If not, then U_ILLEGAL_ARGUMENT_ERROR will be set.
 *
 * <p>The special reorder code NONE will remove any reordering for this collator.
 * The result of setting no reordering will be to have the DUCET/CLDR ordering used. The 
 * NONE code <b>must</b> be the sole code supplied when it is used.
 *
 * @param coll The UCollator to set.
 * @param reorderCodes An array of script codes in the new order. This can be NULL if the 
 * length is also set to 0. An empty array will clear any reordering codes on the collator.
 * @param reorderCodesLength The length of reorderCodes.
 * @param pErrorCode Must be a valid pointer to an error code value, which must not indicate a
 * failure before the function call.
 * @see ucol_getReorderCodes
 * @see ucol_getEquivalentReorderCodes
 * @see UScriptCode
 * @see UColReorderCode
 * \xrefitem stable "Stable" "Stable List" ICU 4.8
 */ 
U_CAPI void U_EXPORT2 
ucol_setReorderCodes(UCollator* coll,
                    const int32_t* reorderCodes,
                    int32_t reorderCodesLength,
                    UErrorCode *pErrorCode) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Retrieves the reorder codes that are grouped with the given reorder code. Some reorder
 * codes will be grouped and must reorder together.
 * Beginning with ICU 55, scripts only reorder together if they are primary-equal,
 * for example Hiragana and Katakana.
 *
 * @param reorderCode The reorder code to determine equivalence for.
 * @param dest The array to fill with the script ordering.
 * @param destCapacity The length of dest. If it is 0, then dest may be NULL and the function
 * will only return the length of the result without writing any codes (pre-flighting).
 * @param pErrorCode Must be a valid pointer to an error code value, which must not indicate 
 * a failure before the function call.
 * @return The number of reordering codes written to the dest array.
 * @see ucol_setReorderCodes
 * @see ucol_getReorderCodes
 * @see UScriptCode
 * @see UColReorderCode
 * \xrefitem stable "Stable" "Stable List" ICU 4.8
 */
U_CAPI int32_t U_EXPORT2 
ucol_getEquivalentReorderCodes(int32_t reorderCode,
                    int32_t* dest,
                    int32_t destCapacity,
                    UErrorCode *pErrorCode) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Get the display name for a UCollator.
 * The display name is suitable for presentation to a user.
 * @param objLoc The locale of the collator in question.
 * @param dispLoc The locale for display.
 * @param result A pointer to a buffer to receive the attribute.
 * @param resultLength The maximum size of result.
 * @param status A pointer to a UErrorCode to receive any errors
 * @return The total buffer size needed; if greater than resultLength,
 * the output was truncated.
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI int32_t U_EXPORT2 
ucol_getDisplayName(    const    char        *objLoc,
            const    char        *dispLoc,
            UChar             *result,
            int32_t         resultLength,
            UErrorCode        *status) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Get a locale for which collation rules are available.
 * A UCollator in a locale returned by this function will perform the correct
 * collation for the locale.
 * @param localeIndex The index of the desired locale.
 * @return A locale for which collation rules are available, or 0 if none.
 * @see ucol_countAvailable
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI const char* U_EXPORT2 
ucol_getAvailable(int32_t localeIndex) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Determine how many locales have collation rules available.
 * This function is most useful as determining the loop ending condition for
 * calls to {@link #ucol_getAvailable }.
 * @return The number of locales for which collation rules are available.
 * @see ucol_getAvailable
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI int32_t U_EXPORT2 
ucol_countAvailable(void) __INTRODUCED_IN(__ANDROID_API_T__);



#if !UCONFIG_NO_SERVICE
/**
 * Create a string enumerator of all locales for which a valid
 * collator may be opened.
 * @param status input-output error code
 * @return a string enumeration over locale strings. The caller is
 * responsible for closing the result.
 * \xrefitem stable "Stable" "Stable List" ICU 3.0
 */
U_CAPI UEnumeration* U_EXPORT2
ucol_openAvailableLocales(UErrorCode *status) __INTRODUCED_IN(__ANDROID_API_T__);


#endif

/**
 * Create a string enumerator of all possible keywords that are relevant to
 * collation. At this point, the only recognized keyword for this
 * service is "collation".
 * @param status input-output error code
 * @return a string enumeration over locale strings. The caller is
 * responsible for closing the result.
 * \xrefitem stable "Stable" "Stable List" ICU 3.0
 */
U_CAPI UEnumeration* U_EXPORT2
ucol_getKeywords(UErrorCode *status) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Given a keyword, create a string enumeration of all values
 * for that keyword that are currently in use.
 * @param keyword a particular keyword as enumerated by
 * ucol_getKeywords. If any other keyword is passed in, *status is set
 * to U_ILLEGAL_ARGUMENT_ERROR.
 * @param status input-output error code
 * @return a string enumeration over collation keyword values, or NULL
 * upon error. The caller is responsible for closing the result.
 * \xrefitem stable "Stable" "Stable List" ICU 3.0
 */
U_CAPI UEnumeration* U_EXPORT2
ucol_getKeywordValues(const char *keyword, UErrorCode *status) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Given a key and a locale, returns an array of string values in a preferred
 * order that would make a difference. These are all and only those values where
 * the open (creation) of the service with the locale formed from the input locale
 * plus input keyword and that value has different behavior than creation with the
 * input locale alone.
 * @param key           one of the keys supported by this service.  For now, only
 *                      "collation" is supported.
 * @param locale        the locale
 * @param commonlyUsed  if set to true it will return only commonly used values
 *                      with the given locale in preferred order.  Otherwise,
 *                      it will return all the available values for the locale.
 * @param status error status
 * @return a string enumeration over keyword values for the given key and the locale.
 * \xrefitem stable "Stable" "Stable List" ICU 4.2
 */
U_CAPI UEnumeration* U_EXPORT2
ucol_getKeywordValuesForLocale(const char* key,
                               const char* locale,
                               UBool commonlyUsed,
                               UErrorCode* status) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Return the functionally equivalent locale for the specified
 * input locale, with respect to given keyword, for the
 * collation service. If two different input locale + keyword
 * combinations produce the same result locale, then collators
 * instantiated for these two different input locales will behave
 * equivalently. The converse is not always true; two collators
 * may in fact be equivalent, but return different results, due to
 * internal details. The return result has no other meaning than
 * that stated above, and implies nothing as to the relationship
 * between the two locales. This is intended for use by
 * applications who wish to cache collators, or otherwise reuse
 * collators when possible. The functional equivalent may change
 * over time. For more information, please see the <a
 * href="https://unicode-org.github.io/icu/userguide/locale#locales-and-services">
 * Locales and Services</a> section of the ICU User Guide.
 * @param result fillin for the functionally equivalent result locale
 * @param resultCapacity capacity of the fillin buffer
 * @param keyword a particular keyword as enumerated by
 * ucol_getKeywords.
 * @param locale the specified input locale
 * @param isAvailable if non-NULL, pointer to a fillin parameter that
 * on return indicates whether the specified input locale was 'available'
 * to the collation service. A locale is defined as 'available' if it
 * physically exists within the collation locale data.
 * @param status pointer to input-output error code
 * @return the actual buffer size needed for the locale. If greater
 * than resultCapacity, the returned full name will be truncated and
 * an error code will be returned.
 * \xrefitem stable "Stable" "Stable List" ICU 3.0
 */
U_CAPI int32_t U_EXPORT2
ucol_getFunctionalEquivalent(char* result, int32_t resultCapacity,
                             const char* keyword, const char* locale,
                             UBool* isAvailable, UErrorCode* status) __INTRODUCED_IN(__ANDROID_API_T__);





#ifndef U_HIDE_DEPRECATED_API



#endif  /* U_HIDE_DEPRECATED_API */


/**
 * Get a sort key for a string from a UCollator.
 * Sort keys may be compared using <TT>strcmp</TT>.
 *
 * Note that sort keys are often less efficient than simply doing comparison.  
 * For more details, see the ICU User Guide.
 *
 * Like ICU functions that write to an output buffer, the buffer contents
 * is undefined if the buffer capacity (resultLength parameter) is too small.
 * Unlike ICU functions that write a string to an output buffer,
 * the terminating zero byte is counted in the sort key length.
 * @param coll The UCollator containing the collation rules.
 * @param source The string to transform.
 * @param sourceLength The length of source, or -1 if null-terminated.
 * @param result A pointer to a buffer to receive the attribute.
 * @param resultLength The maximum size of result.
 * @return The size needed to fully store the sort key.
 *      If there was an internal error generating the sort key,
 *      a zero value is returned.
 * @see ucol_keyHashCode
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI int32_t U_EXPORT2 
ucol_getSortKey(const    UCollator    *coll,
        const    UChar        *source,
        int32_t        sourceLength,
        uint8_t        *result,
        int32_t        resultLength) __INTRODUCED_IN(__ANDROID_API_T__);






/** enum that is taken by ucol_getBound API 
 * See below for explanation                
 * do not change the values assigned to the 
 * members of this enum. Underlying code    
 * depends on them having these numbers     
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
typedef enum UColBoundMode : int32_t {
  /** lower bound */
  UCOL_BOUND_LOWER = 0,
  /** upper bound that will match strings of exact size */
  UCOL_BOUND_UPPER = 1,
  /** upper bound that will match all the strings that have the same initial substring as the given string */
  UCOL_BOUND_UPPER_LONG = 2,
#ifndef U_HIDE_DEPRECATED_API
    /**
     * One more than the highest normal UColBoundMode value.
     * \xrefitem deprecated "Deprecated" "Deprecated List" ICU 58 The numeric value may change over time, see ICU ticket #12420.
     */
    UCOL_BOUND_VALUE_COUNT
#endif  /* U_HIDE_DEPRECATED_API */
} UColBoundMode;


        




/**
 * Merges two sort keys. The levels are merged with their corresponding counterparts
 * (primaries with primaries, secondaries with secondaries etc.). Between the values
 * from the same level a separator is inserted.
 *
 * This is useful, for example, for combining sort keys from first and last names
 * to sort such pairs.
 * See http://www.unicode.org/reports/tr10/#Merging_Sort_Keys
 *
 * The recommended way to achieve "merged" sorting is by
 * concatenating strings with U+FFFE between them.
 * The concatenation has the same sort order as the merged sort keys,
 * but merge(getSortKey(str1), getSortKey(str2)) may differ from getSortKey(str1 + '\\uFFFE' + str2).
 * Using strings with U+FFFE may yield shorter sort keys.
 *
 * For details about Sort Key Features see
 * https://unicode-org.github.io/icu/userguide/collation/api#sort-key-features
 *
 * It is possible to merge multiple sort keys by consecutively merging
 * another one with the intermediate result.
 *
 * The length of the merge result is the sum of the lengths of the input sort keys.
 *
 * Example (uncompressed):
 * <pre>191B1D 01 050505 01 910505 00
 * 1F2123 01 050505 01 910505 00</pre>
 * will be merged as 
 * <pre>191B1D 02 1F2123 01 050505 02 050505 01 910505 02 910505 00</pre>
 *
 * If the destination buffer is not big enough, then its contents are undefined.
 * If any of source lengths are zero or any of the source pointers are NULL/undefined,
 * the result is of size zero.
 *
 * @param src1 the first sort key
 * @param src1Length the length of the first sort key, including the zero byte at the end;
 *        can be -1 if the function is to find the length
 * @param src2 the second sort key
 * @param src2Length the length of the second sort key, including the zero byte at the end;
 *        can be -1 if the function is to find the length
 * @param dest the buffer where the merged sort key is written,
 *        can be NULL if destCapacity==0
 * @param destCapacity the number of bytes in the dest buffer
 * @return the length of the merged sort key, src1Length+src2Length;
 *         can be larger than destCapacity, or 0 if an error occurs (only for illegal arguments),
 *         in which cases the contents of dest is undefined
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI int32_t U_EXPORT2 
ucol_mergeSortkeys(const uint8_t *src1, int32_t src1Length,
                   const uint8_t *src2, int32_t src2Length,
                   uint8_t *dest, int32_t destCapacity) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Universal attribute setter
 * @param coll collator which attributes are to be changed
 * @param attr attribute type 
 * @param value attribute value
 * @param status to indicate whether the operation went on smoothly or there were errors
 * @see UColAttribute
 * @see UColAttributeValue
 * @see ucol_getAttribute
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI void U_EXPORT2 
ucol_setAttribute(UCollator *coll, UColAttribute attr, UColAttributeValue value, UErrorCode *status) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Universal attribute getter
 * @param coll collator which attributes are to be changed
 * @param attr attribute type
 * @return attribute value
 * @param status to indicate whether the operation went on smoothly or there were errors
 * @see UColAttribute
 * @see UColAttributeValue
 * @see ucol_setAttribute
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI UColAttributeValue  U_EXPORT2 
ucol_getAttribute(const UCollator *coll, UColAttribute attr, UErrorCode *status) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Sets the variable top to the top of the specified reordering group.
 * The variable top determines the highest-sorting character
 * which is affected by UCOL_ALTERNATE_HANDLING.
 * If that attribute is set to UCOL_NON_IGNORABLE, then the variable top has no effect.
 * @param coll the collator
 * @param group one of UCOL_REORDER_CODE_SPACE, UCOL_REORDER_CODE_PUNCTUATION,
 *              UCOL_REORDER_CODE_SYMBOL, UCOL_REORDER_CODE_CURRENCY;
 *              or UCOL_REORDER_CODE_DEFAULT to restore the default max variable group
 * @param pErrorCode Standard ICU error code. Its input value must
 *                   pass the U_SUCCESS() test, or else the function returns
 *                   immediately. Check for U_FAILURE() on output or use with
 *                   function chaining. (See User Guide for details.)
 * @see ucol_getMaxVariable
 * \xrefitem stable "Stable" "Stable List" ICU 53
 */
U_CAPI void U_EXPORT2
ucol_setMaxVariable(UCollator *coll, UColReorderCode group, UErrorCode *pErrorCode) __INTRODUCED_IN(__ANDROID_API_T__);



/**
 * Returns the maximum reordering group whose characters are affected by UCOL_ALTERNATE_HANDLING.
 * @param coll the collator
 * @return the maximum variable reordering group.
 * @see ucol_setMaxVariable
 * \xrefitem stable "Stable" "Stable List" ICU 53
 */
U_CAPI UColReorderCode U_EXPORT2
ucol_getMaxVariable(const UCollator *coll) __INTRODUCED_IN(__ANDROID_API_T__);



#ifndef U_HIDE_DEPRECATED_API

#endif  /* U_HIDE_DEPRECATED_API */



#ifndef U_HIDE_DEPRECATED_API

#endif  /* U_HIDE_DEPRECATED_API */

/**
 * Thread safe cloning operation. The result is a clone of a given collator.
 * @param coll collator to be cloned
 * @param status to indicate whether the operation went on smoothly or there were errors
 * @return pointer to the new clone
 * @see ucol_open
 * @see ucol_openRules
 * @see ucol_close
 * \xrefitem stable "Stable" "Stable List" ICU 71
 */
U_CAPI UCollator* U_EXPORT2 ucol_clone(const UCollator *coll, UErrorCode *status) __INTRODUCED_IN(__ANDROID_API_T__);



#ifndef U_HIDE_DEPRECATED_API




/** default memory size for the new clone.
 * \xrefitem deprecated "Deprecated" "Deprecated List" ICU 52. Do not rely on ucol_safeClone() cloning into any provided buffer.
 */
#define U_COL_SAFECLONE_BUFFERSIZE 1

#endif /* U_HIDE_DEPRECATED_API */



#ifndef U_HIDE_DEPRECATED_API

#endif  /* U_HIDE_DEPRECATED_API */





#ifndef U_HIDE_INTERNAL_API



#endif  /* U_HIDE_INTERNAL_API */






#endif /* #if !UCONFIG_NO_COLLATION */

#endif

/** @} */ // addtogroup

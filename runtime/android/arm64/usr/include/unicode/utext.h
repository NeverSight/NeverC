// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 2004-2012, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  utext.h
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2004oct06
*   created by: Markus W. Scherer
*/

#ifndef __UTEXT_H__
#define __UTEXT_H__

/**
 * @addtogroup icu4c ICU4C
 * @{
 * \file
 * \brief C API: Abstract Unicode Text API
 *
 * The Text Access API provides a means to allow text that is stored in alternative
 * formats to work with ICU services.  ICU normally operates on text that is
 * stored in UTF-16 format, in (UChar *) arrays for the C APIs or as type
 * UnicodeString for C++ APIs.
 *
 * ICU Text Access allows other formats, such as UTF-8 or non-contiguous
 * UTF-16 strings, to be placed in a UText wrapper and then passed to ICU services.
 *
 * There are three general classes of usage for UText:
 *
 *     Application Level Use.  This is the simplest usage - applications would
 *     use one of the utext_open() functions on their input text, and pass
 *     the resulting UText to the desired ICU service.
 *
 *     Second is usage in ICU Services, such as break iteration, that will need to
 *     operate on input presented to them as a UText.  These implementations
 *     will need to use the iteration and related UText functions to gain
 *     access to the actual text.
 *
 *     The third class of UText users are "text providers."  These are the
 *     UText implementations for the various text storage formats.  An application
 *     or system with a unique text storage format can implement a set of
 *     UText provider functions for that format, which will then allow
 *     ICU services to operate on that format.
 *
 *
 * <em>Iterating over text</em>
 *
 * Here is sample code for a forward iteration over the contents of a UText
 *
 * \code
 *    UChar32  c;
 *    UText    *ut = whatever();
 *
 *    for (c=utext_next32From(ut, 0); c>=0; c=utext_next32(ut)) {
 *       // do whatever with the codepoint c here.
 *    }
 * \endcode
 *
 * And here is similar code to iterate in the reverse direction, from the end
 * of the text towards the beginning.
 *
 * \code
 *    UChar32  c;
 *    UText    *ut = whatever();
 *    int      textLength = utext_nativeLength(ut);
 *    for (c=utext_previous32From(ut, textLength); c>=0; c=utext_previous32(ut)) {
 *       // do whatever with the codepoint c here.
 *    }
 * \endcode
 *
 * <em>Characters and Indexing</em>
 *
 * Indexing into text by UText functions is nearly always in terms of the native
 * indexing of the underlying text storage.  The storage format could be UTF-8
 * or UTF-32, for example.  When coding to the UText access API, no assumptions
 * can be made regarding the size of characters, or how far an index
 * may move when iterating between characters.
 *
 * All indices supplied to UText functions are pinned to the length of the
 * text.  An out-of-bounds index is not considered to be an error, but is
 * adjusted to be in the range  0 <= index <= length of input text.
 *
 *
 * When an index position is returned from a UText function, it will be
 * a native index to the underlying text.  In the case of multi-unit characters,
 * it will  always refer to the first position of the character,
 * never to the interior.  This is essentially the same thing as saying that
 * a returned index will always point to a boundary between characters.
 *
 * When a native index is supplied to a UText function, all indices that
 * refer to any part of a multi-unit character representation are considered
 * to be equivalent.  In the case of multi-unit characters, an incoming index
 * will be logically normalized to refer to the start of the character.
 * 
 * It is possible to test whether a native index is on a code point boundary
 * by doing a utext_setNativeIndex() followed by a utext_getNativeIndex().
 * If the index is returned unchanged, it was on a code point boundary.  If
 * an adjusted index is returned, the original index referred to the
 * interior of a character.
 *
 * <em>Conventions for calling UText functions</em>
 *
 * Most UText access functions have as their first parameter a (UText *) pointer,
 * which specifies the UText to be used.  Unless otherwise noted, the
 * pointer must refer to a valid, open UText.  Attempting to
 * use a closed UText or passing a NULL pointer is a programming error and
 * will produce undefined results or NULL pointer exceptions.
 * 
 * The UText_Open family of functions can either open an existing (closed)
 * UText, or heap allocate a new UText.  Here is sample code for creating
 * a stack-allocated UText.
 *
 * \code
 *    char     *s = whatever();  // A utf-8 string 
 *    U_ErrorCode status = U_ZERO_ERROR;
 *    UText    ut = UTEXT_INITIALIZER;
 *    utext_openUTF8(ut, s, -1, &status);
 *    if (U_FAILURE(status)) {
 *        // error handling
 *    } else {
 *        // work with the UText
 *    }
 * \endcode
 *
 * Any existing UText passed to an open function _must_ have been initialized, 
 * either by the UTEXT_INITIALIZER, or by having been originally heap-allocated
 * by an open function.  Passing NULL will cause the open function to
 * heap-allocate and fully initialize a new UText.
 *
 */



#include "unicode/utypes.h"
#include "unicode/uchar.h"
#if U_SHOW_CPLUSPLUS_API
#include "unicode/localpointer.h"
#include "unicode/rep.h"
#include "unicode/unistr.h"
#include "unicode/chariter.h"
#endif


U_CDECL_BEGIN

struct UText;
typedef struct UText UText; /**< C typedef for struct UText. \xrefitem stable "Stable" "Stable List" ICU 3.6 */


/***************************************************************************************
 *
 *   C Functions for creating UText wrappers around various kinds of text strings.
 *
 ****************************************************************************************/


/**
  * Close function for UText instances.
  * Cleans up, releases any resources being held by an open UText.
  * <p>
  *   If the UText was originally allocated by one of the utext_open functions,
  *   the storage associated with the utext will also be freed.
  *   If the UText storage originated with the application, as it would with
  *   a local or static instance, the storage will not be deleted.
  *
  *   An open UText can be reset to refer to new string by using one of the utext_open()
  *   functions without first closing the UText.  
  *
  * @param ut  The UText to be closed.
  * @return    NULL if the UText struct was deleted by the close.  If the UText struct
  *            was originally provided by the caller to the open function, it is
  *            returned by this function, and may be safely used again in
  *            a subsequent utext_open.
  *
  * \xrefitem stable "Stable" "Stable List" ICU 3.4
  */
U_CAPI UText * U_EXPORT2
utext_close(UText *ut) __INTRODUCED_IN(31);



/**
 * Open a read-only UText implementation for UTF-8 strings.
 * 
 * \htmlonly
 * Any invalid UTF-8 in the input will be handled in this way:
 * a sequence of bytes that has the form of a truncated, but otherwise valid,
 * UTF-8 sequence will be replaced by a single unicode replacement character, \uFFFD. 
 * Any other illegal bytes will each be replaced by a \uFFFD.
 * \endhtmlonly
 * 
 * @param ut     Pointer to a UText struct.  If NULL, a new UText will be created.
 *               If non-NULL, must refer to an initialized UText struct, which will then
 *               be reset to reference the specified UTF-8 string.
 * @param s      A UTF-8 string.  Must not be NULL.
 * @param length The length of the UTF-8 string in bytes, or -1 if the string is
 *               zero terminated.
 * @param status Errors are returned here.
 * @return       A pointer to the UText.  If a pre-allocated UText was provided, it
 *               will always be used and returned.
 * \xrefitem stable "Stable" "Stable List" ICU 3.4
 */
U_CAPI UText * U_EXPORT2
utext_openUTF8(UText *ut, const char *s, int64_t length, UErrorCode *status) __INTRODUCED_IN(31);




/**
 * Open a read-only UText for UChar * string.
 * 
 * @param ut     Pointer to a UText struct.  If NULL, a new UText will be created.
 *               If non-NULL, must refer to an initialized UText struct, which will then
 *               be reset to reference the specified UChar string.
 * @param s      A UChar (UTF-16) string
 * @param length The number of UChars in the input string, or -1 if the string is
 *               zero terminated.
 * @param status Errors are returned here.
 * @return       A pointer to the UText.  If a pre-allocated UText was provided, it
 *               will always be used and returned.
 * \xrefitem stable "Stable" "Stable List" ICU 3.4
 */
U_CAPI UText * U_EXPORT2
utext_openUChars(UText *ut, const UChar *s, int64_t length, UErrorCode *status) __INTRODUCED_IN(31);




#if U_SHOW_CPLUSPLUS_API










#endif


/**
  *  Clone a UText.  This is much like opening a UText where the source text is itself
  *  another UText.
  *
  *  A deep clone will copy both the UText data structures and the underlying text.
  *  The original and cloned UText will operate completely independently; modifications
  *  made to the text in one will not affect the other.  Text providers are not
  *  required to support deep clones.  The user of clone() must check the status return
  *  and be prepared to handle failures.
  *
  *  The standard UText implementations for UTF8, UChar *, UnicodeString and
  *  Replaceable all support deep cloning.
  *
  *  The UText returned from a deep clone will be writable, assuming that the text
  *  provider is able to support writing, even if the source UText had been made
  *  non-writable by means of UText_freeze().
  *
  *  A shallow clone replicates only the UText data structures; it does not make
  *  a copy of the underlying text.  Shallow clones can be used as an efficient way to 
  *  have multiple iterators active in a single text string that is not being
  *  modified.
  *
  *  A shallow clone operation will not fail, barring truly exceptional conditions such
  *  as memory allocation failures.
  *
  *  Shallow UText clones should be avoided if the UText functions that modify the
  *  text are expected to be used, either on the original or the cloned UText.
  *  Any such modifications  can cause unpredictable behavior.  Read Only
  *  shallow clones provide some protection against errors of this type by
  *  disabling text modification via the cloned UText.
  *
  *  A shallow clone made with the readOnly parameter == false will preserve the 
  *  utext_isWritable() state of the source object.  Note, however, that
  *  write operations must be avoided while more than one UText exists that refer
  *  to the same underlying text.
  *
  *  A UText and its clone may be safely concurrently accessed by separate threads.
  *  This is true for read access only with shallow clones, and for both read and
  *  write access with deep clones.
  *  It is the responsibility of the Text Provider to ensure that this thread safety
  *  constraint is met.
  *
  *  @param dest   A UText struct to be filled in with the result of the clone operation,
  *                or NULL if the clone function should heap-allocate a new UText struct.
  *                If non-NULL, must refer to an already existing UText, which will then
  *                be reset to become the clone.
  *  @param src    The UText to be cloned.
  *  @param deep   true to request a deep clone, false for a shallow clone.
  *  @param readOnly true to request that the cloned UText have read only access to the 
  *                underlying text.  

  *  @param status Errors are returned here.  For deep clones, U_UNSUPPORTED_ERROR
  *                will be returned if the text provider is unable to clone the
  *                original text.
  *  @return       The newly created clone, or NULL if the clone operation failed.
  *  \xrefitem stable "Stable" "Stable List" ICU 3.4
  */
U_CAPI UText * U_EXPORT2
utext_clone(UText *dest, const UText *src, UBool deep, UBool readOnly, UErrorCode *status) __INTRODUCED_IN(31);




/**
  *  Compare two UText objects for equality.
  *  UTexts are equal if they are iterating over the same text, and
  *    have the same iteration position within the text.
  *    If either or both of the parameters are NULL, the comparison is false.
  *
  *  @param a   The first of the two UTexts to compare.
  *  @param b   The other UText to be compared.
  *  @return    true if the two UTexts are equal.
  *  \xrefitem stable "Stable" "Stable List" ICU 3.6
  */
U_CAPI UBool U_EXPORT2
utext_equals(const UText *a, const UText *b) __INTRODUCED_IN(31);




/*****************************************************************************
 *
 *   Functions to work with the text represented by a UText wrapper
 *
 *****************************************************************************/

/**
  * Get the length of the text.  Depending on the characteristics
  * of the underlying text representation, this may be expensive.  
  * @see  utext_isLengthExpensive()
  *
  *
  * @param ut  the text to be accessed.
  * @return the length of the text, expressed in native units.
  *
  * \xrefitem stable "Stable" "Stable List" ICU 3.4
  */
U_CAPI int64_t U_EXPORT2
utext_nativeLength(UText *ut) __INTRODUCED_IN(31);





/**
 * Returns the code point at the requested index,
 * or U_SENTINEL (-1) if it is out of bounds.
 *
 * If the specified index points to the interior of a multi-unit
 * character - one of the trail bytes of a UTF-8 sequence, for example -
 * the complete code point will be returned.
 *
 * The iteration position will be set to the start of the returned code point.
 *
 * This function is roughly equivalent to the sequence
 *    utext_setNativeIndex(index);
 *    utext_current32();
 * (There is a subtle difference if the index is out of bounds by being less than zero - 
 * utext_setNativeIndex(negative value) sets the index to zero, after which utext_current()
 * will return the char at zero.  utext_char32At(negative index), on the other hand, will
 * return the U_SENTINEL value of -1.)
 * 
 * @param ut the text to be accessed
 * @param nativeIndex the native index of the character to be accessed.  If the index points
 *        to other than the first unit of a multi-unit character, it will be adjusted
 *        to the start of the character.
 * @return the code point at the specified index.
 * \xrefitem stable "Stable" "Stable List" ICU 3.4
 */
U_CAPI UChar32 U_EXPORT2
utext_char32At(UText *ut, int64_t nativeIndex) __INTRODUCED_IN(31);




/**
 *
 * Get the code point at the current iteration position,
 * or U_SENTINEL (-1) if the iteration has reached the end of
 * the input text.
 *
 * @param ut the text to be accessed.
 * @return the Unicode code point at the current iterator position.
 * \xrefitem stable "Stable" "Stable List" ICU 3.4
 */
U_CAPI UChar32 U_EXPORT2
utext_current32(UText *ut) __INTRODUCED_IN(31);




/**
 * Get the code point at the current iteration position of the UText, and
 * advance the position to the first index following the character.
 *
 * If the position is at the end of the text (the index following
 * the last character, which is also the length of the text), 
 * return U_SENTINEL (-1) and do not advance the index. 
 *
 * This is a post-increment operation.
 *
 * An inline macro version of this function, UTEXT_NEXT32(), 
 * is available for performance critical use.
 *
 * @param ut the text to be accessed.
 * @return the Unicode code point at the iteration position.
 * @see UTEXT_NEXT32
 * \xrefitem stable "Stable" "Stable List" ICU 3.4
 */
U_CAPI UChar32 U_EXPORT2
utext_next32(UText *ut) __INTRODUCED_IN(31);




/**
 *  Move the iterator position to the character (code point) whose
 *  index precedes the current position, and return that character.
 *  This is a pre-decrement operation.
 *
 *  If the initial position is at the start of the text (index of 0) 
 *  return U_SENTINEL (-1), and leave the position unchanged.
 *
 *  An inline macro version of this function, UTEXT_PREVIOUS32(), 
 *  is available for performance critical use.
 *
 *  @param ut the text to be accessed.
 *  @return the previous UChar32 code point, or U_SENTINEL (-1) 
 *          if the iteration has reached the start of the text.
 *  @see UTEXT_PREVIOUS32
 *  \xrefitem stable "Stable" "Stable List" ICU 3.4
 */
U_CAPI UChar32 U_EXPORT2
utext_previous32(UText *ut) __INTRODUCED_IN(31);




/**
  * Set the iteration index and return the code point at that index. 
  * Leave the iteration index at the start of the following code point.
  *
  * This function is the most efficient and convenient way to
  * begin a forward iteration.  The results are identical to the those
  * from the sequence
  * \code
  *    utext_setIndex();
  *    utext_next32();
  * \endcode
  *
  *  @param ut the text to be accessed.
  *  @param nativeIndex Iteration index, in the native units of the text provider.
  *  @return Code point which starts at or before index,
  *         or U_SENTINEL (-1) if it is out of bounds.
  * \xrefitem stable "Stable" "Stable List" ICU 3.4
  */
U_CAPI UChar32 U_EXPORT2
utext_next32From(UText *ut, int64_t nativeIndex) __INTRODUCED_IN(31);





/**
  * Set the iteration index, and return the code point preceding the
  * one specified by the initial index.  Leave the iteration position
  * at the start of the returned code point.
  *
  * This function is the most efficient and convenient way to
  * begin a backwards iteration.
  *
  * @param ut the text to be accessed.
  * @param nativeIndex Iteration index in the native units of the text provider.
  * @return Code point preceding the one at the initial index,
  *         or U_SENTINEL (-1) if it is out of bounds.
  *
  * \xrefitem stable "Stable" "Stable List" ICU 3.4
  */
U_CAPI UChar32 U_EXPORT2
utext_previous32From(UText *ut, int64_t nativeIndex) __INTRODUCED_IN(31);



/**
  * Get the current iterator position, which can range from 0 to 
  * the length of the text.
  * The position is a native index into the input text, in whatever format it
  * may have (possibly UTF-8 for example), and may not always be the same as
  * the corresponding UChar (UTF-16) index.
  * The returned position will always be aligned to a code point boundary. 
  *
  * @param ut the text to be accessed.
  * @return the current index position, in the native units of the text provider.
  * \xrefitem stable "Stable" "Stable List" ICU 3.4
  */
U_CAPI int64_t U_EXPORT2
utext_getNativeIndex(const UText *ut) __INTRODUCED_IN(31);



/**
 * Set the current iteration position to the nearest code point
 * boundary at or preceding the specified index.
 * The index is in the native units of the original input text.
 * If the index is out of range, it will be pinned to be within
 * the range of the input text.
 * <p>
 * It will usually be more efficient to begin an iteration
 * using the functions utext_next32From() or utext_previous32From()
 * rather than setIndex().
 * <p>
 * Moving the index position to an adjacent character is best done
 * with utext_next32(), utext_previous32() or utext_moveIndex32().
 * Attempting to do direct arithmetic on the index position is
 * complicated by the fact that the size (in native units) of a
 * character depends on the underlying representation of the character
 * (UTF-8, UTF-16, UTF-32, arbitrary codepage), and is not
 * easily knowable.
 *
 * @param ut the text to be accessed.
 * @param nativeIndex the native unit index of the new iteration position.
 * \xrefitem stable "Stable" "Stable List" ICU 3.4
 */
U_CAPI void U_EXPORT2
utext_setNativeIndex(UText *ut, int64_t nativeIndex) __INTRODUCED_IN(31);



/**
 * Move the iterator position by delta code points.  The number of code points
 * is a signed number; a negative delta will move the iterator backwards,
 * towards the start of the text.
 * <p>
 * The index is moved by <code>delta</code> code points
 * forward or backward, but no further backward than to 0 and
 * no further forward than to utext_nativeLength().
 * The resulting index value will be in between 0 and length, inclusive.
 *
 * @param ut the text to be accessed.
 * @param delta the signed number of code points to move the iteration position.
 * @return true if the position could be moved the requested number of positions while
 *              staying within the range [0 - text length].
 * \xrefitem stable "Stable" "Stable List" ICU 3.4
 */
U_CAPI UBool U_EXPORT2
utext_moveIndex32(UText *ut, int32_t delta) __INTRODUCED_IN(31);



/**
 * Get the native index of the character preceding the current position.
 * If the iteration position is already at the start of the text, zero
 * is returned.
 * The value returned is the same as that obtained from the following sequence,
 * but without the side effect of changing the iteration position.
 *   
 * \code
 *    UText  *ut = whatever;
 *      ...
 *    utext_previous(ut)
 *    utext_getNativeIndex(ut);
 * \endcode
 *
 * This function is most useful during forwards iteration, where it will get the
 *   native index of the character most recently returned from utext_next().
 *
 * @param ut the text to be accessed
 * @return the native index of the character preceding the current index position,
 *         or zero if the current position is at the start of the text.
 * \xrefitem stable "Stable" "Stable List" ICU 3.6
 */
U_CAPI int64_t U_EXPORT2
utext_getPreviousNativeIndex(UText *ut); 


/**
 *
 * Extract text from a UText into a UChar buffer.  The range of text to be extracted
 * is specified in the native indices of the UText provider.  These may not necessarily
 * be UTF-16 indices.
 * <p>
 * The size (number of 16 bit UChars) of the data to be extracted is returned.  The
 * full number of UChars is returned, even when the extracted text is truncated
 * because the specified buffer size is too small.
 * <p>
 * The extracted string will (if you are a user) / must (if you are a text provider)
 * be NUL-terminated if there is sufficient space in the destination buffer.  This
 * terminating NUL is not included in the returned length.
 * <p>
 * The iteration index is left at the position following the last extracted character.
 *
 * @param  ut    the UText from which to extract data.
 * @param  nativeStart the native index of the first character to extract.\
 *               If the specified index is out of range,
 *               it will be pinned to be within 0 <= index <= textLength
 * @param  nativeLimit the native string index of the position following the last
 *               character to extract.  If the specified index is out of range,
 *               it will be pinned to be within 0 <= index <= textLength.
 *               nativeLimit must be >= nativeStart.
 * @param  dest  the UChar (UTF-16) buffer into which the extracted text is placed
 * @param  destCapacity  The size, in UChars, of the destination buffer.  May be zero
 *               for precomputing the required size.
 * @param  status receives any error status.
 *         U_BUFFER_OVERFLOW_ERROR: the extracted text was truncated because the 
 *         buffer was too small.  Returns number of UChars for preflighting.
 * @return Number of UChars in the data to be extracted.  Does not include a trailing NUL.
 *
 * \xrefitem stable "Stable" "Stable List" ICU 3.4
 */
U_CAPI int32_t U_EXPORT2
utext_extract(UText *ut,
             int64_t nativeStart, int64_t nativeLimit,
             UChar *dest, int32_t destCapacity,
             UErrorCode *status) __INTRODUCED_IN(31);







U_CDECL_END


#if U_SHOW_CPLUSPLUS_API

U_NAMESPACE_BEGIN

/**
 * \class LocalUTextPointer
 * "Smart pointer" class, closes a UText via utext_close().
 * For most methods see the LocalPointerBase base class.
 *
 * @see LocalPointerBase
 * @see LocalPointer
 * \xrefitem stable "Stable" "Stable List" ICU 4.4
 */
U_DEFINE_LOCAL_OPEN_POINTER(LocalUTextPointer, UText, utext_close);

U_NAMESPACE_END

#endif


#endif

/** @} */ // addtogroup

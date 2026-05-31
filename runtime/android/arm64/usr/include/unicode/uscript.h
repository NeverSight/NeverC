// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 **********************************************************************
 *   Copyright (C) 1997-2016, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 **********************************************************************
 *
 * File USCRIPT.H
 *
 * Modification History:
 *
 *   Date        Name        Description
 *   07/06/2001    Ram         Creation.
 ******************************************************************************
 */

#ifndef USCRIPT_H
#define USCRIPT_H
#include "unicode/utypes.h"

/**
 * @addtogroup icu4c ICU4C
 * @{
 * \file
 * \brief C API: Unicode Script Information
 */

/**
 * Constants for ISO 15924 script codes.
 *
 * The current set of script code constants supports at least all scripts
 * that are encoded in the version of Unicode which ICU currently supports.
 * The names of the constants are usually derived from the
 * Unicode script property value aliases.
 * See UAX #24 Unicode Script Property (http://www.unicode.org/reports/tr24/)
 * and http://www.unicode.org/Public/UCD/latest/ucd/PropertyValueAliases.txt .
 *
 * In addition, constants for many ISO 15924 script codes
 * are included, for use with language tags, CLDR data, and similar.
 * Some of those codes are not used in the Unicode Character Database (UCD).
 * For example, there are no characters that have a UCD script property value of
 * Hans or Hant. All Han ideographs have the Hani script property value in Unicode.
 *
 * Private-use codes Qaaa..Qabx are not included, except as used in the UCD or in CLDR.
 *
 * Starting with ICU 55, script codes are only added when their scripts
 * have been or will certainly be encoded in Unicode,
 * and have been assigned Unicode script property value aliases,
 * to ensure that their script names are stable and match the names of the constants.
 * Script codes like Latf and Aran that are not subject to separate encoding
 * may be added at any time.
 *
 * \xrefitem stable "Stable" "Stable List" ICU 2.2
 */
typedef enum UScriptCode {
    /*
     * Note: UScriptCode constants and their ISO script code comments
     * are parsed by preparseucd.py.
     * It matches lines like
     *     USCRIPT_<Unicode Script value name> = <integer>,  / * <ISO script code> * /
     */

      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_INVALID_CODE = -1,
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_COMMON       =  0,  /* Zyyy */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_INHERITED    =  1,  /* Zinh */ /* "Code for inherited script", for non-spacing combining marks; also Qaai */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_ARABIC       =  2,  /* Arab */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_ARMENIAN     =  3,  /* Armn */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_BENGALI      =  4,  /* Beng */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_BOPOMOFO     =  5,  /* Bopo */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_CHEROKEE     =  6,  /* Cher */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_COPTIC       =  7,  /* Copt */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_CYRILLIC     =  8,  /* Cyrl */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_DESERET      =  9,  /* Dsrt */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_DEVANAGARI   = 10,  /* Deva */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_ETHIOPIC     = 11,  /* Ethi */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_GEORGIAN     = 12,  /* Geor */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_GOTHIC       = 13,  /* Goth */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_GREEK        = 14,  /* Grek */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_GUJARATI     = 15,  /* Gujr */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_GURMUKHI     = 16,  /* Guru */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_HAN          = 17,  /* Hani */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_HANGUL       = 18,  /* Hang */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_HEBREW       = 19,  /* Hebr */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_HIRAGANA     = 20,  /* Hira */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_KANNADA      = 21,  /* Knda */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_KATAKANA     = 22,  /* Kana */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_KHMER        = 23,  /* Khmr */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_LAO          = 24,  /* Laoo */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_LATIN        = 25,  /* Latn */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_MALAYALAM    = 26,  /* Mlym */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_MONGOLIAN    = 27,  /* Mong */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_MYANMAR      = 28,  /* Mymr */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_OGHAM        = 29,  /* Ogam */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_OLD_ITALIC   = 30,  /* Ital */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_ORIYA        = 31,  /* Orya */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_RUNIC        = 32,  /* Runr */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_SINHALA      = 33,  /* Sinh */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_SYRIAC       = 34,  /* Syrc */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_TAMIL        = 35,  /* Taml */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_TELUGU       = 36,  /* Telu */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_THAANA       = 37,  /* Thaa */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_THAI         = 38,  /* Thai */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_TIBETAN      = 39,  /* Tibt */
      /** Canadian_Aboriginal script. \xrefitem stable "Stable" "Stable List" ICU 2.6 */
      USCRIPT_CANADIAN_ABORIGINAL = 40,  /* Cans */
      /** Canadian_Aboriginal script (alias). \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_UCAS         = USCRIPT_CANADIAN_ABORIGINAL,
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_YI           = 41,  /* Yiii */
      /* New scripts in Unicode 3.2 */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_TAGALOG      = 42,  /* Tglg */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_HANUNOO      = 43,  /* Hano */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_BUHID        = 44,  /* Buhd */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.2 */
      USCRIPT_TAGBANWA     = 45,  /* Tagb */

      /* New scripts in Unicode 4 */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.6 */
      USCRIPT_BRAILLE      = 46,  /* Brai */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.6 */
      USCRIPT_CYPRIOT      = 47,  /* Cprt */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.6 */
      USCRIPT_LIMBU        = 48,  /* Limb */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.6 */
      USCRIPT_LINEAR_B     = 49,  /* Linb */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.6 */
      USCRIPT_OSMANYA      = 50,  /* Osma */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.6 */
      USCRIPT_SHAVIAN      = 51,  /* Shaw */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.6 */
      USCRIPT_TAI_LE       = 52,  /* Tale */
      /** \xrefitem stable "Stable" "Stable List" ICU 2.6 */
      USCRIPT_UGARITIC     = 53,  /* Ugar */

      /** New script code in Unicode 4.0.1 \xrefitem stable "Stable" "Stable List" ICU 3.0 */
      USCRIPT_KATAKANA_OR_HIRAGANA = 54,/*Hrkt */

      /* New scripts in Unicode 4.1 */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.4 */
      USCRIPT_BUGINESE      = 55, /* Bugi */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.4 */
      USCRIPT_GLAGOLITIC    = 56, /* Glag */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.4 */
      USCRIPT_KHAROSHTHI    = 57, /* Khar */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.4 */
      USCRIPT_SYLOTI_NAGRI  = 58, /* Sylo */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.4 */
      USCRIPT_NEW_TAI_LUE   = 59, /* Talu */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.4 */
      USCRIPT_TIFINAGH      = 60, /* Tfng */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.4 */
      USCRIPT_OLD_PERSIAN   = 61, /* Xpeo */

      /* New script codes from Unicode and ISO 15924 */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_BALINESE                      = 62, /* Bali */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_BATAK                         = 63, /* Batk */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_BLISSYMBOLS                   = 64, /* Blis */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_BRAHMI                        = 65, /* Brah */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_CHAM                          = 66, /* Cham */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_CIRTH                         = 67, /* Cirt */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_OLD_CHURCH_SLAVONIC_CYRILLIC  = 68, /* Cyrs */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_DEMOTIC_EGYPTIAN              = 69, /* Egyd */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_HIERATIC_EGYPTIAN             = 70, /* Egyh */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_EGYPTIAN_HIEROGLYPHS          = 71, /* Egyp */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_KHUTSURI                      = 72, /* Geok */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_SIMPLIFIED_HAN                = 73, /* Hans */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_TRADITIONAL_HAN               = 74, /* Hant */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_PAHAWH_HMONG                  = 75, /* Hmng */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_OLD_HUNGARIAN                 = 76, /* Hung */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_HARAPPAN_INDUS                = 77, /* Inds */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_JAVANESE                      = 78, /* Java */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_KAYAH_LI                      = 79, /* Kali */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_LATIN_FRAKTUR                 = 80, /* Latf */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_LATIN_GAELIC                  = 81, /* Latg */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_LEPCHA                        = 82, /* Lepc */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_LINEAR_A                      = 83, /* Lina */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_MANDAIC                       = 84, /* Mand */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_MANDAEAN                      = USCRIPT_MANDAIC,
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_MAYAN_HIEROGLYPHS             = 85, /* Maya */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_MEROITIC_HIEROGLYPHS          = 86, /* Mero */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_MEROITIC                      = USCRIPT_MEROITIC_HIEROGLYPHS,
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_NKO                           = 87, /* Nkoo */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_ORKHON                        = 88, /* Orkh */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_OLD_PERMIC                    = 89, /* Perm */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_PHAGS_PA                      = 90, /* Phag */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_PHOENICIAN                    = 91, /* Phnx */
      /** \xrefitem stable "Stable" "Stable List" ICU 52 */
      USCRIPT_MIAO                          = 92, /* Plrd */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_PHONETIC_POLLARD              = USCRIPT_MIAO,
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_RONGORONGO                    = 93, /* Roro */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_SARATI                        = 94, /* Sara */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_ESTRANGELO_SYRIAC             = 95, /* Syre */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_WESTERN_SYRIAC                = 96, /* Syrj */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_EASTERN_SYRIAC                = 97, /* Syrn */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_TENGWAR                       = 98, /* Teng */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_VAI                           = 99, /* Vaii */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_VISIBLE_SPEECH                = 100,/* Visp */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_CUNEIFORM                     = 101,/* Xsux */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_UNWRITTEN_LANGUAGES           = 102,/* Zxxx */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.6 */
      USCRIPT_UNKNOWN                       = 103,/* Zzzz */ /* Unknown="Code for uncoded script", for unassigned code points */

      /** \xrefitem stable "Stable" "Stable List" ICU 3.8 */
      USCRIPT_CARIAN                        = 104,/* Cari */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.8 */
      USCRIPT_JAPANESE                      = 105,/* Jpan */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.8 */
      USCRIPT_LANNA                         = 106,/* Lana */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.8 */
      USCRIPT_LYCIAN                        = 107,/* Lyci */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.8 */
      USCRIPT_LYDIAN                        = 108,/* Lydi */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.8 */
      USCRIPT_OL_CHIKI                      = 109,/* Olck */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.8 */
      USCRIPT_REJANG                        = 110,/* Rjng */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.8 */
      USCRIPT_SAURASHTRA                    = 111,/* Saur */
      /** Sutton SignWriting \xrefitem stable "Stable" "Stable List" ICU 3.8 */
      USCRIPT_SIGN_WRITING                  = 112,/* Sgnw */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.8 */
      USCRIPT_SUNDANESE                     = 113,/* Sund */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.8 */
      USCRIPT_MOON                          = 114,/* Moon */
      /** \xrefitem stable "Stable" "Stable List" ICU 3.8 */
      USCRIPT_MEITEI_MAYEK                  = 115,/* Mtei */

      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_IMPERIAL_ARAMAIC              = 116,/* Armi */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_AVESTAN                       = 117,/* Avst */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_CHAKMA                        = 118,/* Cakm */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_KOREAN                        = 119,/* Kore */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_KAITHI                        = 120,/* Kthi */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_MANICHAEAN                    = 121,/* Mani */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_INSCRIPTIONAL_PAHLAVI         = 122,/* Phli */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_PSALTER_PAHLAVI               = 123,/* Phlp */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_BOOK_PAHLAVI                  = 124,/* Phlv */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_INSCRIPTIONAL_PARTHIAN        = 125,/* Prti */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_SAMARITAN                     = 126,/* Samr */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_TAI_VIET                      = 127,/* Tavt */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_MATHEMATICAL_NOTATION         = 128,/* Zmth */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.0 */
      USCRIPT_SYMBOLS                       = 129,/* Zsym */

      /** \xrefitem stable "Stable" "Stable List" ICU 4.4 */
      USCRIPT_BAMUM                         = 130,/* Bamu */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.4 */
      USCRIPT_LISU                          = 131,/* Lisu */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.4 */
      USCRIPT_NAKHI_GEBA                    = 132,/* Nkgb */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.4 */
      USCRIPT_OLD_SOUTH_ARABIAN             = 133,/* Sarb */

      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_BASSA_VAH                     = 134,/* Bass */
      /** \xrefitem stable "Stable" "Stable List" ICU 54 */
      USCRIPT_DUPLOYAN                      = 135,/* Dupl */
#ifndef U_HIDE_DEPRECATED_API
      /** \xrefitem deprecated "Deprecated" "Deprecated List" ICU 54 Typo, use USCRIPT_DUPLOYAN */
      USCRIPT_DUPLOYAN_SHORTAND             = USCRIPT_DUPLOYAN,
#endif  /* U_HIDE_DEPRECATED_API */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_ELBASAN                       = 136,/* Elba */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_GRANTHA                       = 137,/* Gran */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_KPELLE                        = 138,/* Kpel */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_LOMA                          = 139,/* Loma */
      /** Mende Kikakui \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_MENDE                         = 140,/* Mend */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_MEROITIC_CURSIVE              = 141,/* Merc */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_OLD_NORTH_ARABIAN             = 142,/* Narb */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_NABATAEAN                     = 143,/* Nbat */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_PALMYRENE                     = 144,/* Palm */
      /** \xrefitem stable "Stable" "Stable List" ICU 54 */
      USCRIPT_KHUDAWADI                     = 145,/* Sind */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_SINDHI                        = USCRIPT_KHUDAWADI,
      /** \xrefitem stable "Stable" "Stable List" ICU 4.6 */
      USCRIPT_WARANG_CITI                   = 146,/* Wara */

      /** \xrefitem stable "Stable" "Stable List" ICU 4.8 */
      USCRIPT_AFAKA                         = 147,/* Afak */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.8 */
      USCRIPT_JURCHEN                       = 148,/* Jurc */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.8 */
      USCRIPT_MRO                           = 149,/* Mroo */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.8 */
      USCRIPT_NUSHU                         = 150,/* Nshu */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.8 */
      USCRIPT_SHARADA                       = 151,/* Shrd */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.8 */
      USCRIPT_SORA_SOMPENG                  = 152,/* Sora */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.8 */
      USCRIPT_TAKRI                         = 153,/* Takr */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.8 */
      USCRIPT_TANGUT                        = 154,/* Tang */
      /** \xrefitem stable "Stable" "Stable List" ICU 4.8 */
      USCRIPT_WOLEAI                        = 155,/* Wole */

      /** \xrefitem stable "Stable" "Stable List" ICU 49 */
      USCRIPT_ANATOLIAN_HIEROGLYPHS         = 156,/* Hluw */
      /** \xrefitem stable "Stable" "Stable List" ICU 49 */
      USCRIPT_KHOJKI                        = 157,/* Khoj */
      /** \xrefitem stable "Stable" "Stable List" ICU 49 */
      USCRIPT_TIRHUTA                       = 158,/* Tirh */

      /** \xrefitem stable "Stable" "Stable List" ICU 52 */
      USCRIPT_CAUCASIAN_ALBANIAN            = 159,/* Aghb */
      /** \xrefitem stable "Stable" "Stable List" ICU 52 */
      USCRIPT_MAHAJANI                      = 160,/* Mahj */

      /** \xrefitem stable "Stable" "Stable List" ICU 54 */
      USCRIPT_AHOM                          = 161,/* Ahom */
      /** \xrefitem stable "Stable" "Stable List" ICU 54 */
      USCRIPT_HATRAN                        = 162,/* Hatr */
      /** \xrefitem stable "Stable" "Stable List" ICU 54 */
      USCRIPT_MODI                          = 163,/* Modi */
      /** \xrefitem stable "Stable" "Stable List" ICU 54 */
      USCRIPT_MULTANI                       = 164,/* Mult */
      /** \xrefitem stable "Stable" "Stable List" ICU 54 */
      USCRIPT_PAU_CIN_HAU                   = 165,/* Pauc */
      /** \xrefitem stable "Stable" "Stable List" ICU 54 */
      USCRIPT_SIDDHAM                       = 166,/* Sidd */

      /** \xrefitem stable "Stable" "Stable List" ICU 58 */
      USCRIPT_ADLAM                         = 167,/* Adlm */
      /** \xrefitem stable "Stable" "Stable List" ICU 58 */
      USCRIPT_BHAIKSUKI                     = 168,/* Bhks */
      /** \xrefitem stable "Stable" "Stable List" ICU 58 */
      USCRIPT_MARCHEN                       = 169,/* Marc */
      /** \xrefitem stable "Stable" "Stable List" ICU 58 */
      USCRIPT_NEWA                          = 170,/* Newa */
      /** \xrefitem stable "Stable" "Stable List" ICU 58 */
      USCRIPT_OSAGE                         = 171,/* Osge */

      /** \xrefitem stable "Stable" "Stable List" ICU 58 */
      USCRIPT_HAN_WITH_BOPOMOFO             = 172,/* Hanb */
      /** \xrefitem stable "Stable" "Stable List" ICU 58 */
      USCRIPT_JAMO                          = 173,/* Jamo */
      /** \xrefitem stable "Stable" "Stable List" ICU 58 */
      USCRIPT_SYMBOLS_EMOJI                 = 174,/* Zsye */

      /** \xrefitem stable "Stable" "Stable List" ICU 60 */
      USCRIPT_MASARAM_GONDI                 = 175,/* Gonm */
      /** \xrefitem stable "Stable" "Stable List" ICU 60 */
      USCRIPT_SOYOMBO                       = 176,/* Soyo */
      /** \xrefitem stable "Stable" "Stable List" ICU 60 */
      USCRIPT_ZANABAZAR_SQUARE              = 177,/* Zanb */

      /** \xrefitem stable "Stable" "Stable List" ICU 62 */
      USCRIPT_DOGRA                         = 178,/* Dogr */
      /** \xrefitem stable "Stable" "Stable List" ICU 62 */
      USCRIPT_GUNJALA_GONDI                 = 179,/* Gong */
      /** \xrefitem stable "Stable" "Stable List" ICU 62 */
      USCRIPT_MAKASAR                       = 180,/* Maka */
      /** \xrefitem stable "Stable" "Stable List" ICU 62 */
      USCRIPT_MEDEFAIDRIN                   = 181,/* Medf */
      /** \xrefitem stable "Stable" "Stable List" ICU 62 */
      USCRIPT_HANIFI_ROHINGYA               = 182,/* Rohg */
      /** \xrefitem stable "Stable" "Stable List" ICU 62 */
      USCRIPT_SOGDIAN                       = 183,/* Sogd */
      /** \xrefitem stable "Stable" "Stable List" ICU 62 */
      USCRIPT_OLD_SOGDIAN                   = 184,/* Sogo */

      /** \xrefitem stable "Stable" "Stable List" ICU 64 */
      USCRIPT_ELYMAIC                       = 185,/* Elym */
      /** \xrefitem stable "Stable" "Stable List" ICU 64 */
      USCRIPT_NYIAKENG_PUACHUE_HMONG        = 186,/* Hmnp */
      /** \xrefitem stable "Stable" "Stable List" ICU 64 */
      USCRIPT_NANDINAGARI                   = 187,/* Nand */
      /** \xrefitem stable "Stable" "Stable List" ICU 64 */
      USCRIPT_WANCHO                        = 188,/* Wcho */

      /** \xrefitem stable "Stable" "Stable List" ICU 66 */
      USCRIPT_CHORASMIAN                    = 189,/* Chrs */
      /** \xrefitem stable "Stable" "Stable List" ICU 66 */
      USCRIPT_DIVES_AKURU                   = 190,/* Diak */
      /** \xrefitem stable "Stable" "Stable List" ICU 66 */
      USCRIPT_KHITAN_SMALL_SCRIPT           = 191,/* Kits */
      /** \xrefitem stable "Stable" "Stable List" ICU 66 */
      USCRIPT_YEZIDI                        = 192,/* Yezi */

      /** \xrefitem stable "Stable" "Stable List" ICU 70 */
      USCRIPT_CYPRO_MINOAN                  = 193,/* Cpmn */
      /** \xrefitem stable "Stable" "Stable List" ICU 70 */
      USCRIPT_OLD_UYGHUR                    = 194,/* Ougr */
      /** \xrefitem stable "Stable" "Stable List" ICU 70 */
      USCRIPT_TANGSA                        = 195,/* Tnsa */
      /** \xrefitem stable "Stable" "Stable List" ICU 70 */
      USCRIPT_TOTO                          = 196,/* Toto */
      /** \xrefitem stable "Stable" "Stable List" ICU 70 */
      USCRIPT_VITHKUQI                      = 197,/* Vith */

      /** \xrefitem stable "Stable" "Stable List" ICU 72 */
      USCRIPT_KAWI                          = 198,/* Kawi */
      /** \xrefitem stable "Stable" "Stable List" ICU 72 */
      USCRIPT_NAG_MUNDARI                   = 199,/* Nagm */

#ifndef U_HIDE_DEPRECATED_API
    /**
     * One more than the highest normal UScriptCode value.
     * The highest value is available via u_getIntPropertyMaxValue(UCHAR_SCRIPT).
     *
     * \xrefitem deprecated "Deprecated" "Deprecated List" ICU 58 The numeric value may change over time, see ICU ticket #12420.
     */
    USCRIPT_CODE_LIMIT    = 200
#endif  // U_HIDE_DEPRECATED_API
} UScriptCode;

/**
 * Gets the script codes associated with the given locale or ISO 15924 abbreviation or name.
 * Fills in USCRIPT_MALAYALAM given "Malayam" OR "Mlym".
 * Fills in USCRIPT_LATIN given "en" OR "en_US"
 * If the required capacity is greater than the capacity of the destination buffer,
 * then the error code is set to U_BUFFER_OVERFLOW_ERROR and the required capacity is returned.
 *
 * <p>Note: To search by short or long script alias only, use
 * u_getPropertyValueEnum(UCHAR_SCRIPT, alias) instead.  That does
 * a fast lookup with no access of the locale data.
 *
 * @param nameOrAbbrOrLocale name of the script, as given in
 * PropertyValueAliases.txt, or ISO 15924 code or locale
 * @param fillIn the UScriptCode buffer to fill in the script code
 * @param capacity the capacity (size) of UScriptCode buffer passed in.
 * @param err the error status code.
 * @return The number of script codes filled in the buffer passed in
 * \xrefitem stable "Stable" "Stable List" ICU 2.4
 */
U_CAPI int32_t  U_EXPORT2
uscript_getCode(const char* nameOrAbbrOrLocale,UScriptCode* fillIn,int32_t capacity,UErrorCode *err) __INTRODUCED_IN(31);



/**
 * Returns the long Unicode script name, if there is one.
 * Otherwise returns the 4-letter ISO 15924 script code.
 * Returns "Malayam" given USCRIPT_MALAYALAM.
 *
 * @param scriptCode UScriptCode enum
 * @return long script name as given in PropertyValueAliases.txt, or the 4-letter code,
 * or NULL if scriptCode is invalid
 * \xrefitem stable "Stable" "Stable List" ICU 2.4
 */
U_CAPI const char*  U_EXPORT2
uscript_getName(UScriptCode scriptCode) __INTRODUCED_IN(31);



/**
 * Returns the 4-letter ISO 15924 script code,
 * which is the same as the short Unicode script name if Unicode has names for the script.
 * Returns "Mlym" given USCRIPT_MALAYALAM.
 *
 * @param scriptCode UScriptCode enum
 * @return short script name (4-letter code), or NULL if scriptCode is invalid
 * \xrefitem stable "Stable" "Stable List" ICU 2.4
 */
U_CAPI const char*  U_EXPORT2
uscript_getShortName(UScriptCode scriptCode) __INTRODUCED_IN(31);



/**
 * Gets the script code associated with the given codepoint.
 * Returns USCRIPT_MALAYALAM given 0x0D02
 * @param codepoint UChar32 codepoint
 * @param err the error status code.
 * @return The UScriptCode, or 0 if codepoint is invalid
 * \xrefitem stable "Stable" "Stable List" ICU 2.4
 */
U_CAPI UScriptCode  U_EXPORT2
uscript_getScript(UChar32 codepoint, UErrorCode *err) __INTRODUCED_IN(31);



/**
 * Do the Script_Extensions of code point c contain script sc?
 * If c does not have explicit Script_Extensions, then this tests whether
 * c has the Script property value sc.
 *
 * Some characters are commonly used in multiple scripts.
 * For more information, see UAX #24: http://www.unicode.org/reports/tr24/.
 * @param c code point
 * @param sc script code
 * @return true if sc is in Script_Extensions(c)
 * \xrefitem stable "Stable" "Stable List" ICU 49
 */
U_CAPI UBool U_EXPORT2
uscript_hasScript(UChar32 c, UScriptCode sc) __INTRODUCED_IN(31);



/**
 * Writes code point c's Script_Extensions as a list of UScriptCode values
 * to the output scripts array and returns the number of script codes.
 * - If c does have Script_Extensions, then the Script property value
 *   (normally Common or Inherited) is not included.
 * - If c does not have Script_Extensions, then the one Script code is written to the output array.
 * - If c is not a valid code point, then the one USCRIPT_UNKNOWN code is written.
 * In other words, if the return value is 1,
 * then the output array contains exactly c's single Script code.
 * If the return value is n>=2, then the output array contains c's n Script_Extensions script codes.
 *
 * Some characters are commonly used in multiple scripts.
 * For more information, see UAX #24: http://www.unicode.org/reports/tr24/.
 *
 * If there are more than capacity script codes to be written, then
 * U_BUFFER_OVERFLOW_ERROR is set and the number of Script_Extensions is returned.
 * (Usual ICU buffer handling behavior.)
 *
 * @param c code point
 * @param scripts output script code array
 * @param capacity capacity of the scripts array
 * @param errorCode Standard ICU error code. Its input value must
 *                  pass the U_SUCCESS() test, or else the function returns
 *                  immediately. Check for U_FAILURE() on output or use with
 *                  function chaining. (See User Guide for details.)
 * @return number of script codes in c's Script_Extensions, or 1 for the single Script value,
 *         written to scripts unless U_BUFFER_OVERFLOW_ERROR indicates insufficient capacity
 * \xrefitem stable "Stable" "Stable List" ICU 49
 */
U_CAPI int32_t U_EXPORT2
uscript_getScriptExtensions(UChar32 c,
                            UScriptCode *scripts, int32_t capacity,
                            UErrorCode *errorCode) __INTRODUCED_IN(31);



/**
 * Script usage constants.
 * See UAX #31 Unicode Identifier and Pattern Syntax.
 * http://www.unicode.org/reports/tr31/#Table_Candidate_Characters_for_Exclusion_from_Identifiers
 *
 * \xrefitem stable "Stable" "Stable List" ICU 51
 */
typedef enum UScriptUsage {
    /** Not encoded in Unicode. \xrefitem stable "Stable" "Stable List" ICU 51 */
    USCRIPT_USAGE_NOT_ENCODED,
    /** Unknown script usage. \xrefitem stable "Stable" "Stable List" ICU 51 */
    USCRIPT_USAGE_UNKNOWN,
    /** Candidate for Exclusion from Identifiers. \xrefitem stable "Stable" "Stable List" ICU 51 */
    USCRIPT_USAGE_EXCLUDED,
    /** Limited Use script. \xrefitem stable "Stable" "Stable List" ICU 51 */
    USCRIPT_USAGE_LIMITED_USE,
    /** Aspirational Use script. \xrefitem stable "Stable" "Stable List" ICU 51 */
    USCRIPT_USAGE_ASPIRATIONAL,
    /** Recommended script. \xrefitem stable "Stable" "Stable List" ICU 51 */
    USCRIPT_USAGE_RECOMMENDED
} UScriptUsage;

/**
 * Writes the script sample character string.
 * This string normally consists of one code point but might be longer.
 * The string is empty if the script is not encoded.
 *
 * @param script script code
 * @param dest output string array
 * @param capacity number of UChars in the dest array
 * @param pErrorCode standard ICU in/out error code, must pass U_SUCCESS() on input
 * @return the string length, even if U_BUFFER_OVERFLOW_ERROR
 * \xrefitem stable "Stable" "Stable List" ICU 51
 */
U_CAPI int32_t U_EXPORT2
uscript_getSampleString(UScriptCode script, UChar *dest, int32_t capacity, UErrorCode *pErrorCode) __INTRODUCED_IN(31);



#if U_SHOW_CPLUSPLUS_API

U_NAMESPACE_BEGIN
class UnicodeString;
U_NAMESPACE_END

/**
 * Returns the script sample character string.
 * This string normally consists of one code point but might be longer.
 * The string is empty if the script is not encoded.
 *
 * @param script script code
 * @return the sample character string
 * \xrefitem stable "Stable" "Stable List" ICU 51
 */
U_COMMON_API icu::UnicodeString U_EXPORT2
uscript_getSampleUnicodeString(UScriptCode script);

#endif

/**
 * Returns the script usage according to UAX #31 Unicode Identifier and Pattern Syntax.
 * Returns USCRIPT_USAGE_NOT_ENCODED if the script is not encoded in Unicode.
 *
 * @param script script code
 * @return script usage
 * @see UScriptUsage
 * \xrefitem stable "Stable" "Stable List" ICU 51
 */
U_CAPI UScriptUsage U_EXPORT2
uscript_getUsage(UScriptCode script) __INTRODUCED_IN(31);



/**
 * Returns true if the script is written right-to-left.
 * For example, Arab and Hebr.
 *
 * @param script script code
 * @return true if the script is right-to-left
 * \xrefitem stable "Stable" "Stable List" ICU 51
 */
U_CAPI UBool U_EXPORT2
uscript_isRightToLeft(UScriptCode script) __INTRODUCED_IN(31);



/**
 * Returns true if the script allows line breaks between letters (excluding hyphenation).
 * Such a script typically requires dictionary-based line breaking.
 * For example, Hani and Thai.
 *
 * @param script script code
 * @return true if the script allows line breaks between letters
 * \xrefitem stable "Stable" "Stable List" ICU 51
 */
U_CAPI UBool U_EXPORT2
uscript_breaksBetweenLetters(UScriptCode script) __INTRODUCED_IN(31);



/**
 * Returns true if in modern (or most recent) usage of the script case distinctions are customary.
 * For example, Latn and Cyrl.
 *
 * @param script script code
 * @return true if the script is cased
 * \xrefitem stable "Stable" "Stable List" ICU 51
 */
U_CAPI UBool U_EXPORT2
uscript_isCased(UScriptCode script) __INTRODUCED_IN(31);



#endif

/** @} */ // addtogroup

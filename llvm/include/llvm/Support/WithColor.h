//===- WithColor.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_WITHCOLOR_H
#define LLVM_SUPPORT_WITHCOLOR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class Error;
class StringRef;

namespace cl {
class OptionCategory;
}

// Symbolic names for various syntax elements.
enum class HighlightColor {
  Address,
  String,
  Tag,
  Attribute,
  Enumerator,
  Macro,
  Error,
  Warning,
  Note,
  Remark
};

enum class ColorMode {
  /// Determine whether to use color based on the command line argument and the
  /// raw_ostream.
  Auto,
  /// Enable colors. Because raw_ostream is the one implementing colors, this
  /// has no effect if the stream does not support colors or has colors
  /// disabled.
  Enable,
  /// Disable colors.
  Disable,
};

/// An RAII object that temporarily switches an output stream to a specific
/// color.
class WithColor {
public:
  using AutoDetectFunctionType = bool (*)(const raw_ostream &OS);

  /// To be used like this: WithColor(OS, HighlightColor::String) << "text";
  /// @param OS The output stream
  /// @param S Symbolic name for syntax element to color
  /// @param Mode Enable, disable or compute whether to use colors.
  LLVM_CTOR_NODISCARD WithColor(raw_ostream &OS, HighlightColor S,
                                ColorMode Mode = ColorMode::Auto);
  /// To be used like this: WithColor(OS, raw_ostream::BLACK) << "text";
  /// @param OS The output stream
  /// @param Color ANSI color to use, the special SAVEDCOLOR can be used to
  /// change only the bold attribute, and keep colors untouched
  /// @param Bold Bold/brighter text, default false
  /// @param BG If true, change the background, default: change foreground
  /// @param Mode Enable, disable or compute whether to use colors.
  LLVM_CTOR_NODISCARD WithColor(
      raw_ostream &OS, raw_ostream::Colors Color = raw_ostream::SAVEDCOLOR,
      bool Bold = false, bool BG = false, ColorMode Mode = ColorMode::Auto)
      : OS(OS), Mode(Mode) {
    changeColor(Color, Bold, BG);
  }
  ~WithColor();

  raw_ostream &get() { return OS; }
  operator raw_ostream &() { return OS; }
  template <typename T> WithColor &operator<<(T &O) {
    OS << O;
    return *this;
  }
  template <typename T> WithColor &operator<<(const T &O) {
    OS << O;
    return *this;
  }

  /// Convenience method for printing "error: " to stderr.
  static raw_ostream &error();
  /// Convenience method for printing "warning: " to stderr.
  static raw_ostream &warning();
  /// Convenience method for printing "note: " to stderr.
  static raw_ostream &note();
  /// Convenience method for printing "remark: " to stderr.
  static raw_ostream &remark();

  /// Convenience method for printing "error: " to the given stream.
  static raw_ostream &error(raw_ostream &OS, StringRef Prefix = "",
                            bool DisableColors = false);
  /// Convenience method for printing "warning: " to the given stream.
  static raw_ostream &warning(raw_ostream &OS, StringRef Prefix = "",
                              bool DisableColors = false);
  /// Convenience method for printing "note: " to the given stream.
  static raw_ostream &note(raw_ostream &OS, StringRef Prefix = "",
                           bool DisableColors = false);
  /// Convenience method for printing "remark: " to the given stream.
  static raw_ostream &remark(raw_ostream &OS, StringRef Prefix = "",
                             bool DisableColors = false);

  /// Determine whether colors are displayed.
  bool colorsEnabled();

  /// Change the color of text that will be output from this point forward.
  /// @param Color ANSI color to use, the special SAVEDCOLOR can be used to
  /// change only the bold attribute, and keep colors untouched
  /// @param Bold Bold/brighter text, default false
  /// @param BG If true, change the background, default: change foreground
  WithColor &changeColor(raw_ostream::Colors Color, bool Bold = false,
                         bool BG = false);

  /// Reset the colors to terminal defaults. Call this when you are done
  /// outputting colored text, or before program exit.
  WithColor &resetColor();

  /// Implement default handling for Error.
  /// Print "error: " to stderr.
  static void defaultErrorHandler(Error Err);

  /// Implement default handling for Warning.
  /// Print "warning: " to stderr.
  static void defaultWarningHandler(Error Warning);

  /// Retrieve the default color auto detection function.
  static AutoDetectFunctionType defaultAutoDetectFunction();

  /// Change the global auto detection function.
  static void
  setAutoDetectFunction(AutoDetectFunctionType NewAutoDetectFunction);

private:
  raw_ostream &OS;
  ColorMode Mode;

  static AutoDetectFunctionType AutoDetectFunction;
};

namespace cl {
class OptionCategory;
}

cl::OptionCategory &getColorCategory();

void initWithColorOptions();

namespace detail {
bool withColorDefaultAutoDetect(const raw_ostream &OS);
} // namespace detail

inline WithColor::AutoDetectFunctionType WithColor::AutoDetectFunction =
    detail::withColorDefaultAutoDetect;

inline WithColor::WithColor(raw_ostream &OS, HighlightColor Color,
                            ColorMode Mode)
    : OS(OS), Mode(Mode) {
  if (colorsEnabled()) {
    switch (Color) {
    case HighlightColor::Address:
      OS.changeColor(raw_ostream::YELLOW);
      break;
    case HighlightColor::String:
      OS.changeColor(raw_ostream::GREEN);
      break;
    case HighlightColor::Tag:
      OS.changeColor(raw_ostream::BLUE);
      break;
    case HighlightColor::Attribute:
      OS.changeColor(raw_ostream::CYAN);
      break;
    case HighlightColor::Enumerator:
      OS.changeColor(raw_ostream::MAGENTA);
      break;
    case HighlightColor::Macro:
      OS.changeColor(raw_ostream::RED);
      break;
    case HighlightColor::Error:
      OS.changeColor(raw_ostream::RED, true);
      break;
    case HighlightColor::Warning:
      OS.changeColor(raw_ostream::MAGENTA, true);
      break;
    case HighlightColor::Note:
      OS.changeColor(raw_ostream::BLACK, true);
      break;
    case HighlightColor::Remark:
      OS.changeColor(raw_ostream::BLUE, true);
      break;
    }
  }
}
inline raw_ostream &WithColor::error() { return error(errs()); }
inline raw_ostream &WithColor::warning() { return warning(errs()); }
inline raw_ostream &WithColor::note() { return note(errs()); }
inline raw_ostream &WithColor::remark() { return remark(errs()); }
inline raw_ostream &WithColor::error(raw_ostream &OS, StringRef Prefix,
                                     bool DisableColors) {
  if (!Prefix.empty())
    OS << Prefix << ": ";
  return WithColor(OS, HighlightColor::Error,
                   DisableColors ? ColorMode::Disable : ColorMode::Auto)
             .get()
         << "error: ";
}
inline raw_ostream &WithColor::warning(raw_ostream &OS, StringRef Prefix,
                                       bool DisableColors) {
  if (!Prefix.empty())
    OS << Prefix << ": ";
  return WithColor(OS, HighlightColor::Warning,
                   DisableColors ? ColorMode::Disable : ColorMode::Auto)
             .get()
         << "warning: ";
}
inline raw_ostream &WithColor::note(raw_ostream &OS, StringRef Prefix,
                                    bool DisableColors) {
  if (!Prefix.empty())
    OS << Prefix << ": ";
  return WithColor(OS, HighlightColor::Note,
                   DisableColors ? ColorMode::Disable : ColorMode::Auto)
             .get()
         << "note: ";
}
inline raw_ostream &WithColor::remark(raw_ostream &OS, StringRef Prefix,
                                      bool DisableColors) {
  if (!Prefix.empty())
    OS << Prefix << ": ";
  return WithColor(OS, HighlightColor::Remark,
                   DisableColors ? ColorMode::Disable : ColorMode::Auto)
             .get()
         << "remark: ";
}
inline bool WithColor::colorsEnabled() {
  switch (Mode) {
  case ColorMode::Enable:
    return true;
  case ColorMode::Disable:
    return false;
  case ColorMode::Auto:
    return AutoDetectFunction(OS);
  }
  llvm_unreachable("All cases handled above.");
}
inline WithColor &WithColor::changeColor(raw_ostream::Colors Color, bool Bold,
                                         bool BG) {
  if (colorsEnabled())
    OS.changeColor(Color, Bold, BG);
  return *this;
}
inline WithColor &WithColor::resetColor() {
  if (colorsEnabled())
    OS.resetColor();
  return *this;
}
inline WithColor::~WithColor() { resetColor(); }
inline void WithColor::defaultErrorHandler(Error Err) {
  handleAllErrors(std::move(Err), [](ErrorInfoBase &Info) {
    WithColor::error() << Info.message() << '\n';
  });
}
inline void WithColor::defaultWarningHandler(Error Warning) {
  handleAllErrors(std::move(Warning), [](ErrorInfoBase &Info) {
    WithColor::warning() << Info.message() << '\n';
  });
}
inline WithColor::AutoDetectFunctionType
WithColor::defaultAutoDetectFunction() {
  return detail::withColorDefaultAutoDetect;
}
inline void
WithColor::setAutoDetectFunction(AutoDetectFunctionType NewAutoDetectFunction) {
  AutoDetectFunction = NewAutoDetectFunction;
}

} // end namespace llvm

#endif // LLVM_SUPPORT_WITHCOLOR_H

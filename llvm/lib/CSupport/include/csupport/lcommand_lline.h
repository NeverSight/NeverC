#ifndef CSUPPORT_LCOMMAND_LLINE_H
#define CSUPPORT_LCOMMAND_LLINE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_cl_is_whitespace(char c);
int csupport_cl_is_whitespace_or_null(char c);
int csupport_cl_is_quote(char c);
int csupport_cl_has_utf8_bom(const char *data, size_t len);

size_t csupport_cl_parse_backslash(const char *src, size_t src_len, size_t pos,
                                   char *out, size_t out_cap, size_t *out_pos);

int csupport_cl_is_windows_special_char(char c);
int csupport_cl_is_windows_special_char_in_cmd_name(char c);

int csupport_cl_tokenize_command_line(const char *source, size_t source_len,
                                      const char **argv, int max_args);

size_t csupport_cl_tokenize_gnu(const char *src, size_t src_len,
                                char *token_buf, size_t token_buf_cap,
                                const char **tokens, size_t max_tokens,
                                size_t *token_count, int mark_eols);

size_t csupport_cl_tokenize_windows(const char *src, size_t src_len,
                                    char *token_buf, size_t token_buf_cap,
                                    const char **tokens, size_t max_tokens,
                                    size_t *token_count, int initial_cmd_name);

int csupport_cl_tokenize_config_line(const char *src, size_t src_len,
                                     char *line_buf, size_t line_buf_cap,
                                     size_t *line_len, size_t *consumed);

int csupport_cl_has_utf16_bom(const char *data, size_t len);

int csupport_cl_expand_response_file_line(const char *line, size_t line_len,
                                          char *out, size_t out_cap,
                                          size_t *out_len);
size_t csupport_cl_find_option_prefix(const char *arg, size_t arg_len);
int csupport_cl_split_option_value(const char *arg, size_t arg_len,
                                   const char **key, size_t *key_len,
                                   const char **val, size_t *val_len);

int csupport_cl_edit_distance(const char *a, size_t a_len, const char *b,
                              size_t b_len, int allow_replacements,
                              unsigned max_dist);

int csupport_cl_edit_distance_insensitive(const char *a, size_t a_len,
                                          const char *b, size_t b_len,
                                          int allow_replacements,
                                          unsigned max_dist);

int csupport_cl_tokenize_gnu_to_buf(const char *src, size_t src_len,
                                    char *tokens_buf, size_t tokens_cap,
                                    size_t *token_offsets, size_t *token_lens,
                                    size_t max_tokens, size_t *num_tokens);
size_t csupport_cl_format_option_help(char *buf, size_t buflen,
                                      const char *prefix, size_t prefix_len,
                                      const char *name, size_t name_len,
                                      const char *help, size_t help_len,
                                      unsigned max_width);

size_t csupport_cl_arg_plus_prefixes_size(const char *arg_name, size_t arg_len,
                                          size_t pad);
size_t csupport_cl_format_arg_prefix(char *buf, size_t buflen,
                                     const char *arg_name, size_t arg_len,
                                     size_t pad);
int csupport_cl_string_distance(const char *a, size_t a_len, const char *b,
                                size_t b_len, int allow_replacements,
                                unsigned max_dist);
int csupport_cl_looks_like_option(const char *arg, size_t len);

int csupport_cl_skip_comment_line(const char *line, size_t len);
size_t csupport_cl_trim_trailing_whitespace(const char *str, size_t len);
int csupport_cl_parse_numeric_option(const char *str, size_t len,
                                     long long *out_val);
int csupport_cl_match_prefix(const char *arg, size_t arg_len,
                             const char *prefix, size_t prefix_len);

size_t csupport_cl_strip_leading_dashes(const char *arg, size_t len);
int csupport_cl_split_key_value(const char *arg, size_t len, const char **key,
                                size_t *key_len, const char **val,
                                size_t *val_len);
int csupport_cl_is_positional_arg(const char *arg, size_t len);
size_t csupport_cl_format_option_usage(char *buf, size_t buflen,
                                       const char *name, size_t name_len,
                                       const char *meta, size_t meta_len,
                                       int is_required);
int csupport_cl_parse_bool(const char *str, size_t len, int *out);
size_t csupport_cl_word_wrap(const char *text, size_t text_len,
                             unsigned start_col, unsigned max_col, char *buf,
                             size_t buflen);

/* Format a help line: prefix + name + padding + "- " + desc */
size_t csupport_cl_format_option_help_line(char *buf, size_t buflen,
                                           const char *prefix,
                                           size_t prefix_len, const char *name,
                                           size_t name_len, const char *desc,
                                           size_t desc_len, unsigned help_col);

/* Split comma-separated value into tokens. Returns number of tokens found. */
int csupport_cl_comma_separate(const char *val, size_t val_len,
                               const char **tokens, size_t *token_lens,
                               size_t max_tokens);

int csupport_cl_parse_int(const char *str, size_t len, int *out);
int csupport_cl_parse_unsigned(const char *str, size_t len, unsigned *out);
int csupport_cl_parse_uint64(const char *str, size_t len, uint64_t *out);
size_t csupport_cl_format_usage_line(char *buf, size_t buflen, const char *prog,
                                     size_t prog_len, const char *args,
                                     size_t args_len);
int csupport_cl_is_arg_equal(const char *arg, size_t arg_len, const char *name,
                             size_t name_len);
int csupport_cl_starts_with_dash(const char *arg, size_t len);
int csupport_cl_is_double_dash(const char *arg, size_t len);
int csupport_cl_is_response_file(const char *arg, size_t len);
size_t csupport_cl_extract_option_name(const char *arg, size_t arg_len,
                                       char *name_buf, size_t name_buflen);
size_t csupport_cl_extract_option_value(const char *arg, size_t arg_len,
                                        char *val_buf, size_t val_buflen);
int csupport_cl_option_has_value(const char *arg, size_t arg_len);
int csupport_cl_compare_options(const char *a, size_t a_len, const char *b,
                                size_t b_len);

int csupport_cl_comma_split(const char *value, size_t value_len,
                            char *out_token, size_t out_cap, size_t *consumed);
size_t csupport_cl_format_help_text(const char *desc, size_t desc_len,
                                    unsigned start_col, unsigned max_col,
                                    char *out, size_t out_cap);
size_t csupport_cl_format_option_name(const char *name, size_t name_len,
                                      int is_long, char *out, size_t out_cap);

int csupport_cl_validate_option_name(const char *name, size_t name_len);

size_t csupport_cl_format_env_var(const char *prefix, size_t prefix_len,
                                  const char *name, size_t name_len, char *out,
                                  size_t out_cap);

int csupport_cl_is_numeric(const char *str, size_t len);

size_t csupport_cl_tokenize_simple(const char *src, size_t src_len,
                                   const char **tokens, size_t max_tokens);
int csupport_cl_parse_key_value(const char *arg, size_t arg_len,
                                const char **key_start, size_t *key_len,
                                const char **val_start, size_t *val_len);
size_t csupport_cl_format_value_list(const char *const *values,
                                     size_t num_values, const char *separator,
                                     char *out, size_t out_cap);

int csupport_cl_expand_tilde(const char *path, size_t path_len, char *out,
                             size_t out_cap);
size_t csupport_cl_count_tokens(const char *str, size_t str_len);
int csupport_cl_compare_options_by_name(const char *a, size_t a_len,
                                        const char *b, size_t b_len);
size_t csupport_cl_format_wrapped_text(const char *text, size_t text_len,
                                       unsigned start_col, unsigned max_col,
                                       char *out, size_t out_cap);

typedef void (*csupport_cl_token_cb)(const char *tok, size_t tok_len,
                                     void *ctx);
typedef void (*csupport_cl_eol_cb)(void *ctx);

void csupport_cl_tokenize_windows_impl(const char *src, size_t src_len,
                                       csupport_cl_token_cb add_token,
                                       csupport_cl_eol_cb mark_eol, void *ctx,
                                       int initial_command_name);

void csupport_cl_tokenize_gnu_impl(const char *src, size_t src_len,
                                   csupport_cl_token_cb add_token,
                                   csupport_cl_eol_cb mark_eol, void *ctx);

int csupport_cl_parse_double(const char *str, size_t len, double *out_val);

int csupport_cl_should_print_option(const char *name, size_t name_len,
                                    const char *desc, size_t desc_len,
                                    int value_expected_optional);

size_t csupport_cl_get_option_prefixes_size(void);

int csupport_cl_comma_separate_values(const char *value, size_t value_len,
                                      void (*callback)(const char *, size_t,
                                                       void *),
                                      void *ctx);

size_t csupport_cl_extract_config_line(const char *src, size_t src_len,
                                       size_t *offset, char *out,
                                       size_t out_cap);

int csupport_cl_is_valid_option_name(const char *name, size_t len);
size_t csupport_cl_format_env_var_name(const char *prefix, size_t prefix_len,
                                       const char *name, size_t name_len,
                                       char *out, size_t out_cap);

int csupport_cl_is_flag_option(const char *name, size_t len);
int csupport_cl_strip_negation(const char *name, size_t name_len, char *out,
                               size_t out_cap);
size_t csupport_cl_format_version(int major, int minor, int patch, char *out,
                                  size_t out_cap);

size_t csupport_cl_format_opt_width(const char *name, size_t name_len,
                                    const char *value_str, size_t value_len,
                                    char *out, size_t out_cap);

size_t csupport_cl_format_size_suffix(uint64_t bytes, char *out,
                                      size_t out_cap);

/* Expand <CFGDIR> tokens in a string, replacing with base_path.
 * Returns output length. Separators between components use sep char. */
size_t csupport_cl_expand_cfgdir(const char *arg, size_t arg_len,
                                 const char *base_path, size_t base_len,
                                 char *out, size_t out_cap);

/* Compare two option names lexicographically (for sorting). */
int csupport_cl_opt_name_compare(const char *a, const char *b);

/* Check if string starts with '-' or '--' prefix, return offset past prefix. */
size_t csupport_cl_skip_dash_prefix(const char *arg, size_t len);

/* Format an argument with its dash prefix into buf.
 * Returns output length. */
size_t csupport_cl_format_arg_with_prefix(const char *name, size_t name_len,
                                          size_t pad, char *out,
                                          size_t out_cap);

/* Calculate the display width of an option argument. */
size_t csupport_cl_arg_plus_prefixes_size(const char *name, size_t name_len,
                                          size_t pad);

/* Find nearest option by edit distance. Returns distance or -1 if none found.
 */
int csupport_cl_find_nearest_option(const char *arg, size_t arg_len,
                                    const char *const *names, size_t num_names,
                                    size_t *best_idx, size_t max_distance);

/* Format a value description "=<desc>" into buffer. */
size_t csupport_cl_format_value_desc(const char *desc, size_t desc_len,
                                     char *out, size_t out_cap);

/* Parse string as int64. Returns 1 on success, 0 on failure. */
int csupport_cl_parse_int64(const char *str, size_t len, int64_t *out);

/* Parse string as double. Returns 1 on success, 0 on failure. */
int csupport_cl_parse_double_ex(const char *str, size_t len, double *out);

/* Format help string with padding. Returns bytes written. */
size_t csupport_cl_format_help_str(const char *help, size_t help_len,
                                   size_t global_width, size_t option_width,
                                   char *buf, size_t cap);

/* Parse boolean value string. Returns 1=true, 0=false, -1=invalid. */
int csupport_cl_parse_bool_value(const char *str, size_t len);

/* Format option info line (prefix+name+value_desc). Returns bytes written. */
size_t csupport_cl_format_option_info(const char *prefix, const char *name,
                                      const char *value_desc, int has_value,
                                      int value_optional,
                                      int positional_eats_args, char *buf,
                                      size_t cap);

/* Format option with prefix (pad+prefix+name). Returns bytes written. */
int csupport_cl_format_opt_with_prefix2(const char *name, size_t name_len,
                                        size_t pad, char *buf, size_t cap);

/* Sort comparison function for string arrays (qsort compatible). */
int csupport_cl_sort_cmp(const void *a, const void *b);

/* Format version printer output. Returns bytes written. */
size_t csupport_cl_format_version_printer(const char *program_name,
                                          const char *version, char *buf,
                                          size_t cap);

#ifdef __cplusplus
}

/* ===== CommandLine C++ inline implementations (from cpp_bridge.cpp) ===== */

#include "cpp_compat_stl.h"
#include <algorithm>
#include <utility>
#ifndef CMOVE
#define CMOVE(x) std::move(x)
#endif
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"

//===----------------------------------------------------------------------===//
// Template instantiations and anchors.
//
namespace llvm {
namespace cl {
template class basic_parser<bool>;
template class basic_parser<boolOrDefault>;
template class basic_parser<int>;
template class basic_parser<long>;
template class basic_parser<long long>;
template class basic_parser<unsigned>;
template class basic_parser<unsigned long>;
template class basic_parser<unsigned long long>;
template class basic_parser<double>;
template class basic_parser<float>;
template class basic_parser<string_t>;
template class basic_parser<char>;

template class opt<unsigned>;
template class opt<int>;
template class opt<string_t>;
template class opt<char>;
template class opt<bool>;

// Pin the vtables to this file.
inline void GenericOptionValue::anchor() {}
inline void OptionValue<boolOrDefault>::anchor() {}
inline void OptionValue<string_t>::anchor() {}
inline void Option::anchor() {}
inline void basic_parser_impl::anchor() {}
inline void parser<bool>::anchor() {}
inline void parser<boolOrDefault>::anchor() {}
inline void parser<int>::anchor() {}
inline void parser<long>::anchor() {}
inline void parser<long long>::anchor() {}
inline void parser<unsigned>::anchor() {}
inline void parser<unsigned long>::anchor() {}
inline void parser<unsigned long long>::anchor() {}
inline void parser<double>::anchor() {}
inline void parser<float>::anchor() {}
inline void parser<string_t>::anchor() {}
inline void parser<char>::anchor() {}

} // namespace cl
} // namespace llvm

//===----------------------------------------------------------------------===//

namespace llvm {
namespace cl {

using llvm::ArrayRef;
using llvm::function_ref;
using llvm::raw_ostream;
using llvm::SmallPtrSet;
using llvm::SmallString;
using llvm::SmallVector;
using llvm::StringRef;

inline const size_t DefaultPad = 2;

inline StringRef ArgHelpPrefix = " - ";

inline size_t argPlusPrefixesSize(StringRef ArgName, size_t Pad = DefaultPad) {
  return csupport_cl_arg_plus_prefixes_size(ArgName.data(), ArgName.size(),
                                            Pad);
}

inline SmallString<8> argPrefix(StringRef ArgName, size_t Pad = DefaultPad) {
  char buf[32];
  size_t n = csupport_cl_format_arg_prefix(buf, sizeof(buf), ArgName.data(),
                                           ArgName.size(), Pad);
  return SmallString<8>(StringRef(buf, n));
}

// Option predicates...
static inline bool isGrouping(const Option *O) {
  return O->getMiscFlags() & cl::Grouping;
}
static inline bool isPrefixedOrGrouping(const Option *O) {
  return isGrouping(O) || O->getFormattingFlag() == cl::Prefix ||
         O->getFormattingFlag() == cl::AlwaysPrefix;
}

namespace cl_detail {
class PrintArg {
  StringRef ArgName;
  size_t Pad;

public:
  PrintArg(StringRef ArgName, size_t Pad = DefaultPad)
      : ArgName(ArgName), Pad(Pad) {}
  friend raw_ostream &operator<<(raw_ostream &OS, const PrintArg &);
};

inline raw_ostream &operator<<(raw_ostream &OS, const PrintArg &Arg) {
  OS << argPrefix(Arg.ArgName, Arg.Pad) << Arg.ArgName;
  return OS;
}

class CommandLineParser {
public:
  // Globals for name and overview of program.  Program name is not a string to
  // avoid static ctor/dtor issues.
  SmallString<64> ProgramName;
  StringRef ProgramOverview;

  // This collects additional help to be printed.
  SmallVector<StringRef, 4> MoreHelp;

  // This collects Options added with the cl::DefaultOption flag. Since they can
  // be overridden, they are not added to the appropriate SubCommands until
  // ParseCommandLineOptions actually runs.
  SmallVector<Option *, 4> DefaultOptions;

  // This collects the different option categories that have been registered.
  SmallPtrSet<OptionCategory *, 16> RegisteredOptionCategories;

  // This collects the different subcommands that have been registered.
  SmallPtrSet<SubCommand *, 4> RegisteredSubCommands;

  CommandLineParser() {
    registerSubCommand(&SubCommand::getTopLevel());
    registerSubCommand(&SubCommand::getAll());
  }

  void ResetAllOptionOccurrences();

  bool ParseCommandLineOptions(int argc, const char *const *argv,
                               StringRef Overview, raw_ostream *Errs = 0,
                               bool LongOptionsUseDoubleDash = false);

  void forEachSubCommand(Option &Opt, function_ref<void(SubCommand &)> Action) {
    if (Opt.Subs.empty()) {
      Action(SubCommand::getTopLevel());
      return;
    }
    if (Opt.Subs.size() == 1 && *Opt.Subs.begin() == &SubCommand::getAll()) {
      for (auto *SC : RegisteredSubCommands)
        Action(*SC);
      return;
    }
    for (auto *SC : Opt.Subs) {
      assert(SC != &SubCommand::getAll() &&
             "SubCommand::getAll() should not be used with other subcommands");
      Action(*SC);
    }
  }

  void addLiteralOption(Option &Opt, SubCommand *SC, StringRef Name) {
    if (Opt.hasArgStr())
      return;
    if (!SC->OptionsMap.insert({Name, &Opt}).second) {
      errs() << ProgramName << ": CommandLine Error: Option '" << Name
             << "' registered more than once!\n";
      report_fatal_error("inconsistency in registered CommandLine options");
    }
  }

  void addLiteralOption(Option &Opt, StringRef Name) {
    forEachSubCommand(
        Opt, [&](SubCommand &SC) { addLiteralOption(Opt, &SC, Name); });
  }

  void addOption(Option *O, SubCommand *SC) {
    bool HadErrors = false;
    if (O->hasArgStr()) {
      // If it's a DefaultOption, check to make sure it isn't already there.
      if (O->isDefaultOption() && SC->OptionsMap.contains(O->ArgStr))
        return;

      // Add argument to the argument map!
      if (!SC->OptionsMap.insert({O->ArgStr, O}).second) {
        errs() << ProgramName << ": CommandLine Error: Option '" << O->ArgStr
               << "' registered more than once!\n";
        HadErrors = true;
      }
    }

    // Remember information about positional options.
    if (O->getFormattingFlag() == cl::Positional)
      SC->PositionalOpts.push_back(O);
    else if (O->getMiscFlags() & cl::Sink) // Remember sink options
      SC->SinkOpts.push_back(O);
    else if (O->getNumOccurrencesFlag() == cl::ConsumeAfter) {
      if (SC->ConsumeAfterOpt) {
        O->error("Cannot specify more than one option with cl::ConsumeAfter!");
        HadErrors = true;
      }
      SC->ConsumeAfterOpt = O;
    }

    // Fail hard if there were errors. These are strictly unrecoverable and
    // indicate serious issues such as conflicting option names or an
    // incorrectly
    // linked LLVM distribution.
    if (HadErrors)
      report_fatal_error("inconsistency in registered CommandLine options");
  }

  void addOption(Option *O, bool ProcessDefaultOption = false) {
    if (!ProcessDefaultOption && O->isDefaultOption()) {
      DefaultOptions.push_back(O);
      return;
    }
    forEachSubCommand(*O, [&](SubCommand &SC) { addOption(O, &SC); });
  }

  void removeOption(Option *O, SubCommand *SC) {
    SmallVector<StringRef, 16> OptionNames;
    O->getExtraOptionNames(OptionNames);
    if (O->hasArgStr())
      OptionNames.push_back(O->ArgStr);

    SubCommand &Sub = *SC;
    auto End = Sub.OptionsMap.end();
    for (auto Name : OptionNames) {
      auto I = Sub.OptionsMap.find(Name);
      if (I != End && I->getValue() == O)
        Sub.OptionsMap.erase(I);
    }

    if (O->getFormattingFlag() == cl::Positional)
      for (auto *Opt = Sub.PositionalOpts.begin();
           Opt != Sub.PositionalOpts.end(); ++Opt) {
        if (*Opt == O) {
          Sub.PositionalOpts.erase(Opt);
          break;
        }
      }
    else if (O->getMiscFlags() & cl::Sink)
      for (auto *Opt = Sub.SinkOpts.begin(); Opt != Sub.SinkOpts.end(); ++Opt) {
        if (*Opt == O) {
          Sub.SinkOpts.erase(Opt);
          break;
        }
      }
    else if (O == Sub.ConsumeAfterOpt)
      Sub.ConsumeAfterOpt = 0;
  }

  void removeOption(Option *O) {
    forEachSubCommand(*O, [&](SubCommand &SC) { removeOption(O, &SC); });
  }

  bool hasOptions(const SubCommand &Sub) const {
    return (!Sub.OptionsMap.empty() || !Sub.PositionalOpts.empty() ||
            0 != Sub.ConsumeAfterOpt);
  }

  bool hasOptions() const {
    for (const auto *S : RegisteredSubCommands) {
      if (hasOptions(*S))
        return true;
    }
    return false;
  }

  bool hasNamedSubCommands() const {
    for (const auto *S : RegisteredSubCommands)
      if (!S->getName().empty())
        return true;
    return false;
  }

  SubCommand *getActiveSubCommand() { return ActiveSubCommand; }

  void updateArgStr(Option *O, StringRef NewName, SubCommand *SC) {
    SubCommand &Sub = *SC;
    if (!Sub.OptionsMap.insert({NewName, O}).second) {
      errs() << ProgramName << ": CommandLine Error: Option '" << O->ArgStr
             << "' registered more than once!\n";
      report_fatal_error("inconsistency in registered CommandLine options");
    }
    Sub.OptionsMap.erase(O->ArgStr);
  }

  void updateArgStr(Option *O, StringRef NewName) {
    forEachSubCommand(*O,
                      [&](SubCommand &SC) { updateArgStr(O, NewName, &SC); });
  }

  void printOptionValues();

  void registerCategory(OptionCategory *cat) {
    assert(count_if(RegisteredOptionCategories,
                    [cat](const OptionCategory *Category) {
                      return cat->getName() == Category->getName();
                    }) == 0 &&
           "Duplicate option categories");

    RegisteredOptionCategories.insert(cat);
  }

  void registerSubCommand(SubCommand *sub) {
    assert(count_if(RegisteredSubCommands,
                    [sub](const SubCommand *Sub) {
                      return (!sub->getName().empty()) &&
                             (Sub->getName() == sub->getName());
                    }) == 0 &&
           "Duplicate subcommands");
    RegisteredSubCommands.insert(sub);

    // For all options that have been registered for all subcommands, add the
    // option to this subcommand now.
    if (sub != &SubCommand::getAll()) {
      for (auto &E : SubCommand::getAll().OptionsMap) {
        Option *O = E.second;
        if ((O->isPositional() || O->isSink() || O->isConsumeAfter()) ||
            O->hasArgStr())
          addOption(O, sub);
        else
          addLiteralOption(*O, sub, E.first());
      }
    }
  }

  void unregisterSubCommand(SubCommand *sub) {
    RegisteredSubCommands.erase(sub);
  }

  iterator_range<typename SmallPtrSet<SubCommand *, 4>::iterator>
  getRegisteredSubcommands() {
    return make_range(RegisteredSubCommands.begin(),
                      RegisteredSubCommands.end());
  }

  void reset() {
    ActiveSubCommand = 0;
    ProgramName.clear();
    ProgramOverview = StringRef();

    MoreHelp.clear();
    RegisteredOptionCategories.clear();

    ResetAllOptionOccurrences();
    RegisteredSubCommands.clear();

    SubCommand::getTopLevel().reset();
    SubCommand::getAll().reset();
    registerSubCommand(&SubCommand::getTopLevel());
    registerSubCommand(&SubCommand::getAll());

    DefaultOptions.clear();
  }

private:
  SubCommand *ActiveSubCommand = 0;

  Option *LookupOption(SubCommand &Sub, StringRef &Arg, StringRef &Value);
  Option *LookupLongOption(SubCommand &Sub, StringRef &Arg, StringRef &Value,
                           bool LongOptionsUseDoubleDash, bool HaveDoubleDash) {
    Option *Opt = LookupOption(Sub, Arg, Value);
    if (Opt && LongOptionsUseDoubleDash && !HaveDoubleDash && !isGrouping(Opt))
      return 0;
    return Opt;
  }
  SubCommand *LookupSubCommand(StringRef Name, SmallString<256> &NearestString);
};

} // namespace cl_detail

using cl_detail::PrintArg;

inline ManagedStatic<cl_detail::CommandLineParser> GlobalParser;

inline void AddLiteralOption(Option &O, StringRef Name) {
  GlobalParser->addLiteralOption(O, Name);
}

inline extrahelp::extrahelp(StringRef Help) : morehelp(Help) {
  GlobalParser->MoreHelp.push_back(Help);
}

inline void Option::addArgument() {
  GlobalParser->addOption(this);
  FullyInitialized = true;
}

inline void Option::removeArgument() { GlobalParser->removeOption(this); }

inline void Option::setArgStr(StringRef S) {
  if (FullyInitialized)
    GlobalParser->updateArgStr(this, S);
  assert((S.empty() || S[0] != '-') && "Option can't start with '-");
  ArgStr = S;
  if (ArgStr.size() == 1)
    setMiscFlag(Grouping);
}

inline void Option::addCategory(OptionCategory &C) {
  assert(!Categories.empty() && "Categories cannot be empty.");
  // Maintain backward compatibility by replacing the default GeneralCategory
  // if it's still set.  Otherwise, just add the new one.  The GeneralCategory
  // must be explicitly added if you want multiple categories that include it.
  if (&C != &getGeneralCategory() && Categories[0] == &getGeneralCategory())
    Categories[0] = &C;
  else if (!is_contained(Categories, &C))
    Categories.push_back(&C);
}

inline void Option::reset() {
  NumOccurrences = 0;
  setDefault();
  if (isDefaultOption())
    removeArgument();
}

inline void OptionCategory::registerCategory() {
  GlobalParser->registerCategory(this);
}

// A special subcommand representing no subcommand. It is particularly important
// that this ManagedStatic uses constant initailization and not dynamic
// initialization because it is referenced from cl::opt constructors, which run
// dynamically in an arbitrary order.
LLVM_REQUIRE_CONSTANT_INITIALIZATION
inline ManagedStatic<SubCommand> TopLevelSubCommand;

// A special subcommand that can be used to put an option into all subcommands.
LLVM_REQUIRE_CONSTANT_INITIALIZATION
inline ManagedStatic<SubCommand> AllSubCommands;

inline SubCommand &SubCommand::getTopLevel() { return *TopLevelSubCommand; }

inline SubCommand &SubCommand::getAll() { return *AllSubCommands; }

inline void SubCommand::registerSubCommand() {
  GlobalParser->registerSubCommand(this);
}

inline void SubCommand::unregisterSubCommand() {
  GlobalParser->unregisterSubCommand(this);
}

inline void SubCommand::reset() {
  PositionalOpts.clear();
  SinkOpts.clear();
  OptionsMap.clear();

  ConsumeAfterOpt = 0;
}

inline SubCommand::operator bool() const {
  return (GlobalParser->getActiveSubCommand() == this);
}

//===----------------------------------------------------------------------===//
// Basic, shared command line option processing machinery.
//

/// LookupOption - Lookup the option specified by the specified option on the
/// command line.  If there is a value specified (after an equal sign) return
/// that as well.  This assumes that leading dashes have already been stripped.
inline Option *cl_detail::CommandLineParser::LookupOption(SubCommand &Sub,
                                                          StringRef &Arg,
                                                          StringRef &Value) {
  // Reject all dashes.
  if (Arg.empty())
    return 0;
  assert(&Sub != &SubCommand::getAll());

  size_t EqualPos = Arg.find('=');

  // If we have an equals sign, remember the value.
  if (EqualPos == StringRef::npos) {
    // Look up the option.
    return Sub.OptionsMap.lookup(Arg);
  }

  // If the argument before the = is a valid option name and the option allows
  // non-prefix form (ie is not AlwaysPrefix), we match.  If not, signal match
  // failure by returning 0.
  auto I = Sub.OptionsMap.find(Arg.substr(0, EqualPos));
  if (I == Sub.OptionsMap.end())
    return 0;

  auto *O = I->second;
  if (O->getFormattingFlag() == cl::AlwaysPrefix)
    return 0;

  Value = Arg.substr(EqualPos + 1);
  Arg = Arg.substr(0, EqualPos);
  return I->second;
}

inline SubCommand *cl_detail::CommandLineParser::LookupSubCommand(
    StringRef Name, SmallString<256> &NearestString) {
  if (Name.empty())
    return &SubCommand::getTopLevel();
  // Find a subcommand with the edit distance == 1.
  SubCommand *NearestMatch = 0;
  for (auto *S : RegisteredSubCommands) {
    if (S == &SubCommand::getAll())
      continue;
    if (S->getName().empty())
      continue;

    if (StringRef(S->getName()) == StringRef(Name))
      return S;

    if (!NearestMatch && S->getName().edit_distance(Name) < 2)
      NearestMatch = S;
  }

  if (NearestMatch)
    NearestString = NearestMatch->getName();

  return &SubCommand::getTopLevel();
}

/// LookupNearestOption - Lookup the closest match to the option specified by
/// the specified option on the command line.  If there is a value specified
/// (after an equal sign) return that as well.  This assumes that leading dashes
/// have already been stripped.
inline Option *LookupNearestOption(StringRef Arg,
                                   const StringMap<Option *> &OptionsMap,
                                   SmallString<256> &NearestString) {
  // Reject all dashes.
  if (Arg.empty())
    return 0;

  // Split on any equal sign.
  auto SplitArg = Arg.split('=');
  StringRef &LHS = SplitArg.first; // LHS == Arg when no '=' is present.
  StringRef &RHS = SplitArg.second;

  // Find the closest match.
  Option *Best = 0;
  unsigned BestDistance = 0;
  for (StringMap<Option *>::const_iterator it = OptionsMap.begin(),
                                           ie = OptionsMap.end();
       it != ie; ++it) {
    Option *O = it->second;
    // Do not suggest really hidden options (not shown in any help).
    if (O->getOptionHiddenFlag() == ReallyHidden)
      continue;

    SmallVector<StringRef, 16> OptionNames;
    O->getExtraOptionNames(OptionNames);
    if (O->hasArgStr())
      OptionNames.push_back(O->ArgStr);

    bool PermitValue = O->getValueExpectedFlag() != cl::ValueDisallowed;
    StringRef Flag = PermitValue ? LHS : Arg;
    for (const auto &Name : OptionNames) {
      unsigned Distance = StringRef(Name).edit_distance(
          Flag, /*AllowReplacements=*/true, /*MaxEditDistance=*/BestDistance);
      if (!Best || Distance < BestDistance) {
        Best = O;
        BestDistance = Distance;
        if (RHS.empty() || !PermitValue)
          NearestString = Name;
        else
          NearestString = (Twine(Name) + "=" + RHS).str();
      }
    }
  }

  return Best;
}

/// CommaSeparateAndAddOccurrence - A wrapper around Handler->addOccurrence()
/// that does special handling of cl::CommaSeparated options.
inline bool CommaSeparateAndAddOccurrence(Option *Handler, unsigned pos,
                                          StringRef ArgName, StringRef Value,
                                          bool MultiArg = false) {
  // Check to see if this option accepts a comma separated list of values.  If
  // it does, we have to split up the value into multiple values.
  if (Handler->getMiscFlags() & CommaSeparated) {
    StringRef Val(Value);
    StringRef::size_type Pos = Val.find(',');

    while (Pos != StringRef::npos) {
      // Process the portion before the comma.
      if (Handler->addOccurrence(pos, ArgName, Val.substr(0, Pos), MultiArg))
        return true;
      // Erase the portion before the comma, AND the comma.
      Val = Val.substr(Pos + 1);
      // Check for another comma.
      Pos = Val.find(',');
    }

    Value = Val;
  }

  return Handler->addOccurrence(pos, ArgName, Value, MultiArg);
}

/// ProvideOption - For Value, this differentiates between an empty value ("")
/// and a null value (StringRef()).  The later is accepted for arguments that
/// don't allow a value (-foo) the former is rejected (-foo=).
static inline bool ProvideOption(Option *Handler, StringRef ArgName,
                                 StringRef Value, int argc,
                                 const char *const *argv, int &i) {
  // Is this a multi-argument option?
  unsigned NumAdditionalVals = Handler->getNumAdditionalVals();

  // Enforce value requirements
  switch (Handler->getValueExpectedFlag()) {
  case ValueRequired:
    if (!Value.data()) { // No value specified?
      // If no other argument or the option only supports prefix form, we
      // cannot look at the next argument.
      if (i + 1 >= argc || Handler->getFormattingFlag() == cl::AlwaysPrefix)
        return Handler->error("requires a value!");
      // Steal the next argument, like for '-o filename'
      assert(argv && "null check");
      Value = StringRef(argv[++i]);
    }
    break;
  case ValueDisallowed:
    if (NumAdditionalVals > 0)
      return Handler->error("multi-valued option specified"
                            " with ValueDisallowed modifier!");

    if (Value.data())
      return Handler->error("does not allow a value! '" + Twine(Value) +
                            "' specified.");
    break;
  case ValueOptional:
    break;
  }

  // If this isn't a multi-arg option, just run the handler.
  if (NumAdditionalVals == 0)
    return CommaSeparateAndAddOccurrence(Handler, i, ArgName, Value);

  // If it is, run the handle several times.
  bool MultiArg = false;

  if (Value.data()) {
    if (CommaSeparateAndAddOccurrence(Handler, i, ArgName, Value, MultiArg))
      return true;
    --NumAdditionalVals;
    MultiArg = true;
  }

  while (NumAdditionalVals > 0) {
    if (i + 1 >= argc)
      return Handler->error("not enough values!");
    assert(argv && "null check");
    Value = StringRef(argv[++i]);

    if (CommaSeparateAndAddOccurrence(Handler, i, ArgName, Value, MultiArg))
      return true;
    MultiArg = true;
    --NumAdditionalVals;
  }
  return false;
}

inline bool ProvidePositionalOption(Option *Handler, StringRef Arg, int i) {
  int Dummy = i;
  return ProvideOption(Handler, Handler->ArgStr, Arg, 0, 0, Dummy);
}

// getOptionPred - Check to see if there are any options that satisfy the
// specified predicate with names that are the prefixes in Name.  This is
// checked by progressively stripping characters off of the name, checking to
// see if there options that satisfy the predicate.  If we find one, return it,
// otherwise return null.
//
inline Option *getOptionPred(StringRef Name, size_t &Length,
                             bool (*Pred)(const Option *),
                             const StringMap<Option *> &OptionsMap) {
  StringMap<Option *>::const_iterator OMI = OptionsMap.find(Name);
  if (OMI != OptionsMap.end() && !Pred(OMI->getValue()))
    OMI = OptionsMap.end();

  // Loop while we haven't found an option and Name still has at least two
  // characters in it (so that the next iteration will not be the empty
  // string.
  while (OMI == OptionsMap.end() && Name.size() > 1) {
    Name = Name.substr(0, Name.size() - 1); // Chop off the last character.
    OMI = OptionsMap.find(Name);
    if (OMI != OptionsMap.end() && !Pred(OMI->getValue()))
      OMI = OptionsMap.end();
  }

  if (OMI != OptionsMap.end() && Pred(OMI->second)) {
    Length = Name.size();
    return OMI->second; // Found one!
  }
  return 0; // No option found!
}

/// HandlePrefixedOrGroupedOption - The specified argument string (which started
/// with at least one '-') does not fully match an available option.  Check to
/// see if this is a prefix or grouped option.  If so, split arg into output an
/// Arg/Value pair and return the Option to parse it with.
inline Option *
HandlePrefixedOrGroupedOption(StringRef &Arg, StringRef &Value,
                              bool &ErrorParsing,
                              const StringMap<Option *> &OptionsMap) {
  if (Arg.size() == 1)
    return 0;

  // Do the lookup!
  size_t Length = 0;
  Option *PGOpt = getOptionPred(Arg, Length, isPrefixedOrGrouping, OptionsMap);
  if (!PGOpt)
    return 0;

  do {
    StringRef MaybeValue =
        (Length < Arg.size()) ? Arg.substr(Length) : StringRef();
    Arg = Arg.substr(0, Length);
    assert(OptionsMap.count(Arg) && OptionsMap.find(Arg)->second == PGOpt);

    // cl::Prefix options do not preserve '=' when used separately.
    // The behavior for them with grouped options should be the same.
    if (MaybeValue.empty() || PGOpt->getFormattingFlag() == cl::AlwaysPrefix ||
        (PGOpt->getFormattingFlag() == cl::Prefix && MaybeValue[0] != '=')) {
      Value = MaybeValue;
      return PGOpt;
    }

    if (MaybeValue[0] == '=') {
      Value = MaybeValue.substr(1);
      return PGOpt;
    }

    // This must be a grouped option.
    assert(isGrouping(PGOpt) && "Broken getOptionPred!");

    // Grouping options inside a group can't have values.
    if (PGOpt->getValueExpectedFlag() == cl::ValueRequired) {
      ErrorParsing |= PGOpt->error("may not occur within a group!");
      return 0;
    }

    // Because the value for the option is not required, we don't need to pass
    // argc/argv in.
    int Dummy = 0;
    ErrorParsing |= ProvideOption(PGOpt, Arg, StringRef(), 0, 0, Dummy);

    // Get the next grouping option.
    Arg = MaybeValue;
    PGOpt = getOptionPred(Arg, Length, isGrouping, OptionsMap);
  } while (PGOpt);

  // We could not find a grouping option in the remainder of Arg.
  return 0;
}

#define RequiresValue(O)                                                       \
  ((O)->getNumOccurrencesFlag() == cl::Required ||                             \
   (O)->getNumOccurrencesFlag() == cl::OneOrMore)
#define EatsUnboundedNumberOfValues(O)                                         \
  ((O)->getNumOccurrencesFlag() == cl::ZeroOrMore ||                           \
   (O)->getNumOccurrencesFlag() == cl::OneOrMore)

#define isWhitespace(C) csupport_cl_is_whitespace(C)
#define isWhitespaceOrNull(C) csupport_cl_is_whitespace_or_null(C)
#define isQuote(C) csupport_cl_is_quote(C)

struct GnuTokenCtx {
  StringSaver *Saver;
  SmallVectorImpl<const char *> *NewArgv;
  bool MarkEOLs;
};

inline void gnuTokenCb(const char *tok, size_t tok_len, void *ctx) {
  GnuTokenCtx *c = (GnuTokenCtx *)ctx;
  StringRef S(tok, tok_len);
  c->NewArgv->push_back(c->Saver->save(S).data());
}
inline void gnuEolCb(void *ctx) {
  GnuTokenCtx *c = (GnuTokenCtx *)ctx;
  if (c->MarkEOLs)
    c->NewArgv->push_back(0);
}

inline void TokenizeGNUCommandLine(StringRef Src, StringSaver &Saver,
                                   SmallVectorImpl<const char *> &NewArgv,
                                   bool MarkEOLs) {
  GnuTokenCtx ctx = {&Saver, &NewArgv, MarkEOLs};
  csupport_cl_tokenize_gnu_impl(Src.data(), Src.size(), gnuTokenCb, gnuEolCb,
                                &ctx);
}

/// Backslashes are interpreted in a rather complicated way in the Windows-style
/// command line, because backslashes are used both to separate path and to
/// escape double quote. This method consumes runs of backslashes as well as the
/// following double quote if it's escaped.
///
///  * If an even number of backslashes is followed by a double quote, one
///    backslash is output for every pair of backslashes, and the last double
///    quote remains unconsumed. The double quote will later be interpreted as
///    the start or end of a quoted string in the main loop outside of this
///    function.
///
///  * If an odd number of backslashes is followed by a double quote, one
///    backslash is output for every pair of backslashes, and a double quote is
///    output for the last pair of backslash-double quote. The double quote is
///    consumed in this case.
///
///  * Otherwise, backslashes are interpreted literally.
/* parseBackslash eliminated -- csupport_cl_parse_backslash called directly */

// Windows treats whitespace, double quotes, and backslashes specially, except
// when parsing the first token of a full command line, in which case
// backslashes are not special.
#define isWindowsSpecialChar(C) csupport_cl_is_windows_special_char(C)
#define isWindowsSpecialCharInCommandName(C)                                   \
  csupport_cl_is_windows_special_char_in_cmd_name(C)

struct WinTokenCtx {
  StringSaver *Saver;
  function_ref<void(StringRef)> *AddToken;
  function_ref<void()> *MarkEOL;
  bool AlwaysCopy;
};

inline void winTokenCb(const char *tok, size_t tok_len, void *ctx) {
  WinTokenCtx *c = (WinTokenCtx *)ctx;
  StringRef S(tok, tok_len);
  if (c->AlwaysCopy)
    S = c->Saver->save(S);
  (*c->AddToken)(S);
}
inline void winEolCb(void *ctx) {
  WinTokenCtx *c = (WinTokenCtx *)ctx;
  (*c->MarkEOL)();
}

static inline void tokenizeWindowsCommandLineImpl(
    StringRef Src, StringSaver &Saver, function_ref<void(StringRef)> AddToken,
    bool AlwaysCopy, function_ref<void()> MarkEOL, bool InitialCommandName) {
  WinTokenCtx ctx = {&Saver, &AddToken, &MarkEOL, AlwaysCopy};
  csupport_cl_tokenize_windows_impl(Src.data(), Src.size(), winTokenCb,
                                    winEolCb, &ctx, InitialCommandName ? 1 : 0);
}

inline void TokenizeWindowsCommandLine(StringRef Src, StringSaver &Saver,
                                       SmallVectorImpl<const char *> &NewArgv,
                                       bool MarkEOLs) {
  auto AddToken = [&](StringRef Tok) { NewArgv.push_back(Tok.data()); };
  auto OnEOL = [&]() {
    if (MarkEOLs)
      NewArgv.push_back(0);
  };
  tokenizeWindowsCommandLineImpl(Src, Saver, AddToken,
                                 /*AlwaysCopy=*/true, OnEOL, false);
}

inline void
TokenizeWindowsCommandLineNoCopy(StringRef Src, StringSaver &Saver,
                                 SmallVectorImpl<StringRef> &NewArgv) {
  auto AddToken = [&](StringRef Tok) { NewArgv.push_back(Tok); };
  auto OnEOL = []() {};
  tokenizeWindowsCommandLineImpl(Src, Saver, AddToken, /*AlwaysCopy=*/false,
                                 OnEOL, false);
}

inline void
TokenizeWindowsCommandLineFull(StringRef Src, StringSaver &Saver,
                               SmallVectorImpl<const char *> &NewArgv,
                               bool MarkEOLs) {
  auto AddToken = [&](StringRef Tok) { NewArgv.push_back(Tok.data()); };
  auto OnEOL = [&]() {
    if (MarkEOLs)
      NewArgv.push_back(0);
  };
  tokenizeWindowsCommandLineImpl(Src, Saver, AddToken,
                                 /*AlwaysCopy=*/true, OnEOL, true);
}

inline void tokenizeConfigFile(StringRef Source, StringSaver &Saver,
                               SmallVectorImpl<const char *> &NewArgv,
                               bool MarkEOLs) {
  size_t offset = 0;
  char buf[4096];
  while (offset < Source.size()) {
    size_t len = csupport_cl_extract_config_line(Source.data(), Source.size(),
                                                 &offset, buf, sizeof(buf) - 1);
    if (len == 0)
      continue;
    if (len >= sizeof(buf))
      len = sizeof(buf) - 1;
    buf[len] = '\0';
    cl::TokenizeGNUCommandLine(StringRef(buf, len), Saver, NewArgv, MarkEOLs);
  }
}

// It is called byte order marker but the UTF-8 BOM is actually not affected
// by the host system's endianness.
#define hasUTF8ByteOrderMark(S) csupport_cl_has_utf8_bom((S).data(), (S).size())

// Substitute <CFGDIR> with the file's base path.
inline void ExpandBasePaths(StringRef BasePath, StringSaver &Saver,
                            const char *&Arg) {
  assert(sys::path::is_absolute(BasePath));
  const StringLiteral Token("<CFGDIR>");
  const StringRef ArgString(Arg);

  SmallString<128> ResponseFile;
  StringRef::size_type StartPos = 0;
  for (StringRef::size_type TokenPos = ArgString.find(Token);
       TokenPos != StringRef::npos;
       TokenPos = ArgString.find(Token, StartPos)) {
    // Token may appear more than once per arg (e.g. comma-separated linker
    // args). Support by using path-append on any subsequent appearances.
    const StringRef LHS = ArgString.substr(StartPos, TokenPos - StartPos);
    if (ResponseFile.empty())
      ResponseFile = LHS;
    else
      llvm::sys::path::append(ResponseFile, LHS);
    ResponseFile.append(BasePath);
    StartPos = TokenPos + Token.size();
  }

  if (!ResponseFile.empty()) {
    // Path-append the remaining arg substring if at least one token appeared.
    const StringRef Remaining = ArgString.substr(StartPos);
    if (!Remaining.empty())
      llvm::sys::path::append(ResponseFile, Remaining);
    Arg = Saver.save(ResponseFile.str()).data();
  }
}

// FName must be an absolute path.
inline Error
ExpansionContext::expandResponseFile(StringRef FName,
                                     SmallVectorImpl<const char *> &NewArgv) {
  assert(sys::path::is_absolute(FName));
  llvm::ErrorOr<uptr_t<MemoryBuffer>> MemBufOrErr = FS->getBufferForFile(FName);
  if (!MemBufOrErr) {
    errc_t EC = MemBufOrErr.getError();
    return llvm::createStringError(EC, Twine("cannot not open file '") + FName +
                                           "': " + EC.message());
  }
  MemoryBuffer &MemBuf = *MemBufOrErr.get();
  StringRef Str(MemBuf.getBufferStart(), MemBuf.getBufferSize());

  // If we have a UTF-16 byte order mark, convert to UTF-8 for parsing.
  ArrayRef<char> BufRef(MemBuf.getBufferStart(), MemBuf.getBufferEnd());
  SmallString<4096> UTF8Buf;
  if (hasUTF16ByteOrderMark(BufRef)) {
    if (!convertUTF16ToUTF8String(BufRef, UTF8Buf))
      return llvm::createStringError(llvm::errc::illegal_byte_sequence,
                                     "Could not convert UTF16 to UTF8");
    Str = StringRef(UTF8Buf);
  }
  // If we see UTF-8 BOM sequence at the beginning of a file, we shall remove
  // these bytes before parsing.
  // Reference: http://en.wikipedia.org/wiki/UTF-8#Byte_order_mark
  else if (hasUTF8ByteOrderMark(BufRef))
    Str = StringRef(BufRef.data() + 3, BufRef.size() - 3);

  // Tokenize the contents into NewArgv.
  Tokenizer(Str, Saver, NewArgv, MarkEOLs);

  // Expanded file content may require additional transformations, like using
  // absolute paths instead of relative in '@file' constructs or expanding
  // macros.
  if (!RelativeNames && !InConfigFile)
    return Error::success();

  StringRef BasePath = llvm::sys::path::parent_path(FName);
  for (const char *&Arg : NewArgv) {
    if (!Arg)
      continue;

    // Substitute <CFGDIR> with the file's base path.
    if (InConfigFile)
      ExpandBasePaths(BasePath, Saver, Arg);

    // Discover the case, when argument should be transformed into '@file' and
    // evaluate 'file' for it.
    StringRef ArgStr(Arg);
    StringRef FileName;
    bool ConfigInclusion = false;
    if (ArgStr.consume_front("@")) {
      FileName = ArgStr;
      if (!llvm::sys::path::is_relative(FileName))
        continue;
    } else if (ArgStr.consume_front("--config=")) {
      FileName = ArgStr;
      ConfigInclusion = true;
    } else {
      continue;
    }

    // Update expansion construct.
    SmallString<128> ResponseFile;
    ResponseFile.push_back('@');
    if (ConfigInclusion && !llvm::sys::path::has_parent_path(FileName)) {
      SmallString<128> FilePath;
      if (!findConfigFile(FileName, FilePath))
        return createStringError(
            make_error_code(llvm::errc::no_such_file_or_directory),
            "cannot not find configuration file: " + FileName);
      ResponseFile.append(FilePath);
    } else {
      ResponseFile.append(BasePath);
      llvm::sys::path::append(ResponseFile, FileName);
    }
    Arg = Saver.save(ResponseFile.str()).data();
  }
  return Error::success();
}

/// Expand response files on a command line recursively using the given
/// StringSaver and tokenization strategy.
inline Error
ExpansionContext::expandResponseFiles(SmallVectorImpl<const char *> &Argv) {
  struct ResponseFileRecord {
    SmallString<256> File;
    size_t End;
  };

  // To detect recursive response files, we maintain a stack of files and the
  // position of the last argument in the file. This position is updated
  // dynamically as we recursively expand files.
  SmallVector<ResponseFileRecord, 3> FileStack;

  // Push a dummy entry that represents the initial command line, removing
  // the need to check for an empty list.
  FileStack.push_back({SmallString<256>(""), Argv.size()});

  // Don't cache Argv.size() because it can change.
  for (unsigned I = 0; I != Argv.size();) {
    while (I == FileStack.back().End) {
      // Passing the end of a file's argument list, so we can remove it from the
      // stack.
      FileStack.pop_back();
    }

    const char *Arg = Argv[I];
    // Check if it is an EOL marker
    if (Arg == 0) {
      ++I;
      continue;
    }

    if (Arg[0] != '@') {
      ++I;
      continue;
    }

    const char *FName = Arg + 1;
    // Note that CurrentDir is only used for top-level rsp files, the rest will
    // always have an absolute path deduced from the containing file.
    SmallString<128> CurrDir;
    if (llvm::sys::path::is_relative(FName)) {
      if (CurrentDir.empty()) {
        if (auto CWD = FS->getCurrentWorkingDirectory()) {
          CurrDir = *CWD;
        } else {
          return createStringError(
              CWD.getError(), Twine("cannot get absolute path for: ") + FName);
        }
      } else {
        CurrDir = CurrentDir;
      }
      llvm::sys::path::append(CurrDir, FName);
      FName = CurrDir.c_str();
    }

    ErrorOr<llvm::vfs::Status> Res = FS->status(FName);
    if (!Res || !Res->exists()) {
      errc_t EC = Res.getError();
      if (!InConfigFile) {
        // If the specified file does not exist, leave '@file' unexpanded, as
        // libiberty does.
        if (!EC || EC == llvm::errc::no_such_file_or_directory) {
          ++I;
          continue;
        }
      }
      if (!EC)
        EC = llvm::errc::no_such_file_or_directory;
      return createStringError(EC, Twine("cannot not open file '") + FName +
                                       "': " + EC.message());
    }
    const llvm::vfs::Status &FileStatus = Res.get();

    auto IsEquivalent =
        [FileStatus, this](const ResponseFileRecord &RFile) -> ErrorOr<bool> {
      ErrorOr<llvm::vfs::Status> RHS = FS->status(RFile.File);
      if (!RHS)
        return RHS.getError();
      return FileStatus.equivalent(*RHS);
    };

    // Check for recursive response files.
    for (const auto &F : drop_begin(FileStack)) {
      if (ErrorOr<bool> R = IsEquivalent(F)) {
        if (R.get())
          return createStringError(
              R.getError(), Twine("recursive expansion of: '") + F.File + "'");
      } else {
        return createStringError(R.getError(),
                                 Twine("cannot open file: ") + F.File);
      }
    }

    // Replace this response file argument with the tokenization of its
    // contents.  Nested response files are expanded in subsequent iterations.
    SmallVector<const char *, 0> ExpandedArgv;
    if (Error Err = expandResponseFile(FName, ExpandedArgv))
      return Err;

    for (ResponseFileRecord &Record : FileStack) {
      // Increase the end of all active records by the number of newly expanded
      // arguments, minus the response file itself.
      Record.End += ExpandedArgv.size() - 1;
    }

    FileStack.push_back({SmallString<256>(FName), I + ExpandedArgv.size()});
    Argv.erase(Argv.begin() + I);
    Argv.insert(Argv.begin() + I, ExpandedArgv.begin(), ExpandedArgv.end());
  }

  // If successful, the top of the file stack will mark the end of the Argv
  // stream. A failure here indicates a bug in the stack popping logic above.
  // Note that FileStack may have more than one element at this point because we
  // don't have a chance to pop the stack when encountering recursive files at
  // the end of the stream, so seeing that doesn't indicate a bug.
  assert(FileStack.size() > 0 && Argv.size() == FileStack.back().End);
  return Error::success();
}

inline bool expandResponseFiles(int Argc, const char *const *Argv,
                                const char *EnvVar, StringSaver &Saver,
                                SmallVectorImpl<const char *> &NewArgv) {
#ifdef _WIN32
  auto Tokenize = cl::TokenizeWindowsCommandLine;
#else
  auto Tokenize = cl::TokenizeGNUCommandLine;
#endif
  // The environment variable specifies initial options.
  if (EnvVar)
    if (auto EnvValue = sys::Process::GetEnv(EnvVar))
      Tokenize(*EnvValue, Saver, NewArgv, /*MarkEOLs=*/false);

  // Command line options can override the environment variable.
  NewArgv.append(Argv + 1, Argv + Argc);
  ExpansionContext ECtx(Saver.getAllocator(), Tokenize);
  if (Error Err = ECtx.expandResponseFiles(NewArgv)) {
    errs() << toString(CMOVE(Err)) << '\n';
    return false;
  }
  return true;
}

inline bool ExpandResponseFiles(StringSaver &Saver, TokenizerCallback Tokenizer,
                                SmallVectorImpl<const char *> &Argv) {
  ExpansionContext ECtx(Saver.getAllocator(), Tokenizer);
  if (Error Err = ECtx.expandResponseFiles(Argv)) {
    errs() << toString(CMOVE(Err)) << '\n';
    return false;
  }
  return true;
}

inline ExpansionContext::ExpansionContext(BumpPtrAllocator &A,
                                          TokenizerCallback T)
    : Saver(A), Tokenizer(T), FS(vfs::getRealFileSystem().get()) {}

inline bool ExpansionContext::findConfigFile(StringRef FileName,
                                             SmallVectorImpl<char> &FilePath) {
  SmallString<128> CfgFilePath;
  const auto FileExists = [this](SmallString<128> Path) -> bool {
    auto Status = FS->status(Path);
    return Status &&
           Status->getType() == llvm::sys::fs::file_type::regular_file;
  };

  // If file name contains directory separator, treat it as a path to
  // configuration file.
  if (llvm::sys::path::has_parent_path(FileName)) {
    CfgFilePath = FileName;
    if (llvm::sys::path::is_relative(FileName) && FS->makeAbsolute(CfgFilePath))
      return false;
    if (!FileExists(CfgFilePath))
      return false;
    FilePath.assign(CfgFilePath.begin(), CfgFilePath.end());
    return true;
  }

  // Look for the file in search directories.
  for (const StringRef &Dir : SearchDirs) {
    if (Dir.empty())
      continue;
    CfgFilePath.assign(Dir);
    llvm::sys::path::append(CfgFilePath, FileName);
    llvm::sys::path::native(CfgFilePath);
    if (FileExists(CfgFilePath)) {
      FilePath.assign(CfgFilePath.begin(), CfgFilePath.end());
      return true;
    }
  }

  return false;
}

inline Error
ExpansionContext::readConfigFile(StringRef CfgFile,
                                 SmallVectorImpl<const char *> &Argv) {
  SmallString<128> AbsPath;
  if (sys::path::is_relative(CfgFile)) {
    AbsPath.assign(CfgFile);
    if (errc_t EC = FS->makeAbsolute(AbsPath))
      return make_error<StringError>(
          EC, Twine("cannot get absolute path for " + CfgFile));
    CfgFile = AbsPath.str();
  }
  InConfigFile = true;
  RelativeNames = true;
  if (Error Err = expandResponseFile(CfgFile, Argv))
    return Err;
  return expandResponseFiles(Argv);
}

void initCommonOptions();
inline bool ParseCommandLineOptions(int argc, const char *const *argv,
                                    StringRef Overview, raw_ostream *Errs,
                                    const char *EnvVar,
                                    bool LongOptionsUseDoubleDash) {
  initCommonOptions();
  SmallVector<const char *, 20> NewArgv;
  BumpPtrAllocator A;
  StringSaver Saver(A);
  NewArgv.push_back(argv[0]);

  // Parse options from environment variable.
  if (EnvVar) {
    if (auto EnvValue = sys::Process::GetEnv(StringRef(EnvVar)))
      TokenizeGNUCommandLine(*EnvValue, Saver, NewArgv);
  }

  // Append options from command line.
  for (int I = 1; I < argc; ++I)
    NewArgv.push_back(argv[I]);
  int NewArgc = (int)(NewArgv.size());

  // Parse all options.
  return GlobalParser->ParseCommandLineOptions(NewArgc, &NewArgv[0], Overview,
                                               Errs, LongOptionsUseDoubleDash);
}

/// Reset all options at least once, so that we can parse different options.
inline void cl_detail::CommandLineParser::ResetAllOptionOccurrences() {
  // Reset all option values to look like they have never been seen before.
  // Options might be reset twice (they can be reference in both OptionsMap
  // and one of the other members), but that does not harm.
  for (auto *SC : RegisteredSubCommands) {
    for (auto &O : SC->OptionsMap)
      O.second->reset();
    for (Option *O : SC->PositionalOpts)
      O->reset();
    for (Option *O : SC->SinkOpts)
      O->reset();
    if (SC->ConsumeAfterOpt)
      SC->ConsumeAfterOpt->reset();
  }
}

inline bool cl_detail::CommandLineParser::ParseCommandLineOptions(
    int argc, const char *const *argv, StringRef Overview, raw_ostream *Errs,
    bool LongOptionsUseDoubleDash) {
  assert(hasOptions() && "No options specified!");

  ProgramOverview = Overview;
  bool IgnoreErrors = Errs;
  if (!Errs)
    Errs = &errs();
  bool ErrorParsing = false;

  // Expand response files.
  SmallVector<const char *, 20> newArgv(argv, argv + argc);
  BumpPtrAllocator A;
#ifdef _WIN32
  auto Tokenize = cl::TokenizeWindowsCommandLine;
#else
  auto Tokenize = cl::TokenizeGNUCommandLine;
#endif
  ExpansionContext ECtx(A, Tokenize);
  if (Error Err = ECtx.expandResponseFiles(newArgv)) {
    *Errs << toString(CMOVE(Err)) << '\n';
    return false;
  }
  argv = &newArgv[0];
  argc = (int)(newArgv.size());

  // Copy the program name into ProgName, making sure not to overflow it.
  ProgramName = sys::path::filename(StringRef(argv[0]));

  // Check out the positional arguments to collect information about them.
  unsigned NumPositionalRequired = 0;

  // Determine whether or not there are an unlimited number of positionals
  bool HasUnlimitedPositionals = false;

  int FirstArg = 1;
  SubCommand *ChosenSubCommand = &SubCommand::getTopLevel();
  SmallString<256> NearestSubCommandString;
  bool MaybeNamedSubCommand =
      argc >= 2 && argv[FirstArg][0] != '-' && hasNamedSubCommands();
  if (MaybeNamedSubCommand) {
    // If the first argument specifies a valid subcommand, start processing
    // options from the second argument.
    ChosenSubCommand =
        LookupSubCommand(StringRef(argv[FirstArg]), NearestSubCommandString);
    if (ChosenSubCommand != &SubCommand::getTopLevel())
      FirstArg = 2;
  }
  GlobalParser->ActiveSubCommand = ChosenSubCommand;

  assert(ChosenSubCommand);
  auto &ConsumeAfterOpt = ChosenSubCommand->ConsumeAfterOpt;
  auto &PositionalOpts = ChosenSubCommand->PositionalOpts;
  auto &SinkOpts = ChosenSubCommand->SinkOpts;
  auto &OptionsMap = ChosenSubCommand->OptionsMap;

  for (auto *O : DefaultOptions) {
    addOption(O, true);
  }

  if (ConsumeAfterOpt) {
    assert(PositionalOpts.size() > 0 &&
           "Cannot specify cl::ConsumeAfter without a positional argument!");
  }
  if (!PositionalOpts.empty()) {

    // Calculate how many positional values are _required_.
    bool UnboundedFound = false;
    for (size_t i = 0, e = PositionalOpts.size(); i != e; ++i) {
      Option *Opt = PositionalOpts[i];
      if (RequiresValue(Opt))
        ++NumPositionalRequired;
      else if (ConsumeAfterOpt) {
        // ConsumeAfter cannot be combined with "optional" positional options
        // unless there is only one positional argument...
        if (PositionalOpts.size() > 1) {
          if (!IgnoreErrors)
            Opt->error("error - this positional option will never be matched, "
                       "because it does not Require a value, and a "
                       "cl::ConsumeAfter option is active!");
          ErrorParsing = true;
        }
      } else if (UnboundedFound && !Opt->hasArgStr()) {
        // This option does not "require" a value...  Make sure this option is
        // not specified after an option that eats all extra arguments, or this
        // one will never get any!
        //
        if (!IgnoreErrors)
          Opt->error("error - option can never match, because "
                     "another positional argument will match an "
                     "unbounded number of values, and this option"
                     " does not require a value!");
        *Errs << ProgramName << ": CommandLine Error: Option '" << Opt->ArgStr
              << "' is all messed up!\n";
        *Errs << PositionalOpts.size();
        ErrorParsing = true;
      }
      UnboundedFound |= EatsUnboundedNumberOfValues(Opt);
    }
    HasUnlimitedPositionals = UnboundedFound || ConsumeAfterOpt;
  }

  // PositionalVals - A vector of "positional" arguments we accumulate into
  // the process at the end.
  //
  struct StrWithIndex {
    StringRef first;
    unsigned second;
  };
  SmallVector<StrWithIndex, 4> PositionalVals;

  // If the program has named positional arguments, and the name has been run
  // across, keep track of which positional argument was named.  Otherwise put
  // the positional args into the PositionalVals list...
  Option *ActivePositionalArg = 0;

  // Loop over all of the arguments... processing them.
  bool DashDashFound = false; // Have we read '--'?
  for (int i = FirstArg; i < argc; ++i) {
    Option *Handler = 0;
    SmallString<256> NearestHandlerString;
    StringRef Value;
    StringRef ArgName = "";
    bool HaveDoubleDash = false;

    // Check to see if this is a positional argument.  This argument is
    // considered to be positional if it doesn't start with '-', if it is "-"
    // itself, or if we have seen "--" already.
    //
    if (argv[i][0] != '-' || argv[i][1] == 0 || DashDashFound) {
      // Positional argument!
      if (ActivePositionalArg) {
        ProvidePositionalOption(ActivePositionalArg, StringRef(argv[i]), i);
        continue; // We are done!
      }

      if (!PositionalOpts.empty()) {
        PositionalVals.push_back({StringRef(argv[i]), (unsigned)i});

        // All of the positional arguments have been fulfulled, give the rest to
        // the consume after option... if it's specified...
        //
        if (PositionalVals.size() >= NumPositionalRequired && ConsumeAfterOpt) {
          for (++i; i < argc; ++i)
            PositionalVals.push_back({StringRef(argv[i]), (unsigned)i});
          break; // Handle outside of the argument processing loop...
        }

        // Delay processing positional arguments until the end...
        continue;
      }
    } else if (argv[i][0] == '-' && argv[i][1] == '-' && argv[i][2] == 0 &&
               !DashDashFound) {
      DashDashFound = true; // This is the mythical "--"?
      continue;             // Don't try to process it as an argument itself.
    } else if (ActivePositionalArg &&
               (ActivePositionalArg->getMiscFlags() & PositionalEatsArgs)) {
      // If there is a positional argument eating options, check to see if this
      // option is another positional argument.  If so, treat it as an argument,
      // otherwise feed it to the eating positional.
      ArgName = StringRef(argv[i] + 1);
      // Eat second dash.
      if (!ArgName.empty() && ArgName[0] == '-') {
        HaveDoubleDash = true;
        ArgName = ArgName.substr(1);
      }

      Handler = LookupLongOption(*ChosenSubCommand, ArgName, Value,
                                 LongOptionsUseDoubleDash, HaveDoubleDash);
      if (!Handler || Handler->getFormattingFlag() != cl::Positional) {
        ProvidePositionalOption(ActivePositionalArg, StringRef(argv[i]), i);
        continue; // We are done!
      }
    } else { // We start with a '-', must be an argument.
      ArgName = StringRef(argv[i] + 1);
      // Eat second dash.
      if (!ArgName.empty() && ArgName[0] == '-') {
        HaveDoubleDash = true;
        ArgName = ArgName.substr(1);
      }

      Handler = LookupLongOption(*ChosenSubCommand, ArgName, Value,
                                 LongOptionsUseDoubleDash, HaveDoubleDash);

      // If Handler is not found in a specialized subcommand, look up handler
      // in the top-level subcommand.
      // cl::opt without cl::sub belongs to top-level subcommand.
      if (!Handler && ChosenSubCommand != &SubCommand::getTopLevel())
        Handler = LookupLongOption(SubCommand::getTopLevel(), ArgName, Value,
                                   LongOptionsUseDoubleDash, HaveDoubleDash);

      // Check to see if this "option" is really a prefixed or grouped argument.
      if (!Handler && !(LongOptionsUseDoubleDash && HaveDoubleDash))
        Handler = HandlePrefixedOrGroupedOption(ArgName, Value, ErrorParsing,
                                                OptionsMap);

      // Otherwise, look for the closest available option to report to the user
      // in the upcoming error.
      if (!Handler && SinkOpts.empty())
        LookupNearestOption(ArgName, OptionsMap, NearestHandlerString);
    }

    if (!Handler) {
      if (!SinkOpts.empty()) {
        for (Option *SinkOpt : SinkOpts)
          SinkOpt->addOccurrence(i, "", StringRef(argv[i]));
        continue;
      }

      auto ReportUnknownArgument = [&](bool IsArg,
                                       StringRef NearestArgumentName) {
        *Errs << ProgramName << ": Unknown "
              << (IsArg ? "command line argument" : "subcommand") << " '"
              << argv[i] << "'.  Try: '" << argv[0] << " --help'\n";

        if (NearestArgumentName.empty())
          return;

        *Errs << ProgramName << ": Did you mean '";
        if (IsArg)
          *Errs << PrintArg(NearestArgumentName, 0);
        else
          *Errs << NearestArgumentName;
        *Errs << "'?\n";
      };

      if (i > 1 || !MaybeNamedSubCommand)
        ReportUnknownArgument(/*IsArg=*/true, NearestHandlerString);
      else
        ReportUnknownArgument(/*IsArg=*/false, NearestSubCommandString);

      ErrorParsing = true;
      continue;
    }

    // If this is a named positional argument, just remember that it is the
    // active one...
    if (Handler->getFormattingFlag() == cl::Positional) {
      if ((Handler->getMiscFlags() & PositionalEatsArgs) && !Value.empty()) {
        Handler->error("This argument does not take a value.\n"
                       "\tInstead, it consumes any positional arguments until "
                       "the next recognized option.",
                       *Errs);
        ErrorParsing = true;
      }
      ActivePositionalArg = Handler;
    } else
      ErrorParsing |= ProvideOption(Handler, ArgName, Value, argc, argv, i);
  }

  // Check and handle positional arguments now...
  if (NumPositionalRequired > PositionalVals.size()) {
    *Errs << ProgramName
          << ": Not enough positional command line arguments specified!\n"
          << "Must specify at least " << NumPositionalRequired
          << " positional argument" << (NumPositionalRequired > 1 ? "s" : "")
          << ": See: " << argv[0] << " --help\n";

    ErrorParsing = true;
  } else if (!HasUnlimitedPositionals &&
             PositionalVals.size() > PositionalOpts.size()) {
    *Errs << ProgramName << ": Too many positional arguments specified!\n"
          << "Can specify at most " << PositionalOpts.size()
          << " positional arguments: See: " << argv[0] << " --help\n";
    ErrorParsing = true;

  } else if (!ConsumeAfterOpt) {
    // Positional args have already been handled if ConsumeAfter is specified.
    unsigned ValNo = 0, NumVals = (unsigned)(PositionalVals.size());
    for (size_t i = 0, e = PositionalOpts.size(); i != e; ++i) {
      if (RequiresValue(PositionalOpts[i])) {
        ProvidePositionalOption(PositionalOpts[i], PositionalVals[ValNo].first,
                                PositionalVals[ValNo].second);
        ValNo++;
        --NumPositionalRequired; // We fulfilled our duty...
      }

      // If we _can_ give this option more arguments, do so now, as long as we
      // do not give it values that others need.  'Done' controls whether the
      // option even _WANTS_ any more.
      //
      bool Done = PositionalOpts[i]->getNumOccurrencesFlag() == cl::Required;
      while (NumVals - ValNo > NumPositionalRequired && !Done) {
        switch (PositionalOpts[i]->getNumOccurrencesFlag()) {
        case cl::Optional:
          Done = true; // Optional arguments want _at most_ one value
          __attribute__((fallthrough));
        case cl::ZeroOrMore: // Zero or more will take all they can get...
        case cl::OneOrMore:  // One or more will take all they can get...
          ProvidePositionalOption(PositionalOpts[i],
                                  PositionalVals[ValNo].first,
                                  PositionalVals[ValNo].second);
          ValNo++;
          break;
        default:
          llvm_unreachable("Internal error, unexpected NumOccurrences flag in "
                           "positional argument processing!");
        }
      }
    }
  } else {
    assert(ConsumeAfterOpt && NumPositionalRequired <= PositionalVals.size());
    unsigned ValNo = 0;
    for (size_t J = 0, E = PositionalOpts.size(); J != E; ++J)
      if (RequiresValue(PositionalOpts[J])) {
        ErrorParsing |= ProvidePositionalOption(PositionalOpts[J],
                                                PositionalVals[ValNo].first,
                                                PositionalVals[ValNo].second);
        ValNo++;
      }

    // Handle the case where there is just one positional option, and it's
    // optional.  In this case, we want to give JUST THE FIRST option to the
    // positional option and keep the rest for the consume after.  The above
    // loop would have assigned no values to positional options in this case.
    //
    if (PositionalOpts.size() == 1 && ValNo == 0 && !PositionalVals.empty()) {
      ErrorParsing |= ProvidePositionalOption(PositionalOpts[0],
                                              PositionalVals[ValNo].first,
                                              PositionalVals[ValNo].second);
      ValNo++;
    }

    // Handle over all of the rest of the arguments to the
    // cl::ConsumeAfter command line option...
    for (; ValNo != PositionalVals.size(); ++ValNo)
      ErrorParsing |=
          ProvidePositionalOption(ConsumeAfterOpt, PositionalVals[ValNo].first,
                                  PositionalVals[ValNo].second);
  }

  // Loop over args and make sure all required args are specified!
  for (const auto &Opt : OptionsMap) {
    switch (Opt.second->getNumOccurrencesFlag()) {
    case Required:
    case OneOrMore:
      if (Opt.second->getNumOccurrences() == 0) {
        Opt.second->error("must be specified at least once!");
        ErrorParsing = true;
      }
      __attribute__((fallthrough));
    default:
      break;
    }
  }

  // Now that we know if -debug is specified, we can use it.
  // Note that if ReadResponseFiles == true, this must be done before the
  // memory allocated for the expanded command line is free()d below.
#ifdef LLVM_DEBUG
  LLVM_DEBUG(dbgs() << "Args: ";
             for (int i = 0; i < argc; ++i) dbgs() << argv[i] << ' ';
             dbgs() << '\n';);
#endif

  // Free all of the memory allocated to the map.  Command line options may only
  // be processed once!
  MoreHelp.clear();

  // If we had an error processing our arguments, don't let the program execute
  if (ErrorParsing) {
    if (!IgnoreErrors)
      exit(1);
    return false;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// Option Base class implementation
//

inline bool Option::error(const Twine &Message, StringRef ArgName,
                          raw_ostream &Errs) {
  if (!ArgName.data())
    ArgName = ArgStr;
  if (ArgName.empty())
    Errs << HelpStr; // Be nice for positional arguments
  else
    Errs << GlobalParser->ProgramName << ": for the " << PrintArg(ArgName, 0);

  Errs << " option: " << Message << "\n";
  return true;
}

inline bool Option::addOccurrence(unsigned pos, StringRef ArgName,
                                  StringRef Value, bool MultiArg) {
  if (!MultiArg)
    NumOccurrences++; // Increment the number of times we have been seen

  return handleOccurrence(pos, ArgName, Value);
}

// getValueStr - Get the value description string, using "DefaultMsg" if nothing
// has been specified yet.
//
inline StringRef getValueStr(const Option &O, StringRef DefaultMsg) {
  if (O.ValueStr.empty())
    return DefaultMsg;
  return O.ValueStr;
}

//===----------------------------------------------------------------------===//
// cl::alias class implementation
//

// Return the width of the option tag for printing...
inline size_t alias::getOptionWidth() const {
  return argPlusPrefixesSize(ArgStr);
}

inline void Option::printHelpStr(StringRef HelpStr, size_t Indent,
                                 size_t FirstLineIndentedBy) {
  assert(Indent >= FirstLineIndentedBy);
  auto Split = HelpStr.split('\n');
  outs().indent(Indent - FirstLineIndentedBy)
      << ArgHelpPrefix << Split.first << "\n";
  while (!Split.second.empty()) {
    Split = Split.second.split('\n');
    outs().indent(Indent) << Split.first << "\n";
  }
}

inline void Option::printEnumValHelpStr(StringRef HelpStr, size_t BaseIndent,
                                        size_t FirstLineIndentedBy) {
  const StringRef ValHelpPrefix = "  ";
  assert(BaseIndent >= FirstLineIndentedBy);
  auto Split = HelpStr.split('\n');
  outs().indent(BaseIndent - FirstLineIndentedBy)
      << ArgHelpPrefix << ValHelpPrefix << Split.first << "\n";
  while (!Split.second.empty()) {
    Split = Split.second.split('\n');
    outs().indent(BaseIndent + ValHelpPrefix.size()) << Split.first << "\n";
  }
}

// Print out the option for the alias.
inline void alias::printOptionInfo(size_t GlobalWidth) const {
  outs() << PrintArg(ArgStr);
  printHelpStr(HelpStr, GlobalWidth, argPlusPrefixesSize(ArgStr));
}

//===----------------------------------------------------------------------===//
// Parser Implementation code...
//

// basic_parser implementation
//

// Return the width of the option tag for printing...
inline size_t basic_parser_impl::getOptionWidth(const Option &O) const {
  size_t Len = argPlusPrefixesSize(O.ArgStr);
  auto ValName = getValueName();
  if (!ValName.empty()) {
    size_t FormattingLen = 3;
    if (O.getMiscFlags() & PositionalEatsArgs)
      FormattingLen = 6;
    Len += getValueStr(O, ValName).size() + FormattingLen;
  }

  return Len;
}

// printOptionInfo - Print out information about this option.  The
// to-be-maintained width is specified.
//
inline void basic_parser_impl::printOptionInfo(const Option &O,
                                               size_t GlobalWidth) const {
  outs() << PrintArg(O.ArgStr);

  auto ValName = getValueName();
  if (!ValName.empty()) {
    if (O.getMiscFlags() & PositionalEatsArgs) {
      outs() << " <" << getValueStr(O, ValName) << ">...";
    } else if (O.getValueExpectedFlag() == ValueOptional)
      outs() << "[=<" << getValueStr(O, ValName) << ">]";
    else {
      outs() << (O.ArgStr.size() == 1 ? " <" : "=<") << getValueStr(O, ValName)
             << '>';
    }
  }

  Option::printHelpStr(O.HelpStr, GlobalWidth, getOptionWidth(O));
}

inline void basic_parser_impl::printOptionName(const Option &O,
                                               size_t GlobalWidth) const {
  outs() << PrintArg(O.ArgStr);
  outs().indent(GlobalWidth - O.ArgStr.size());
}

// parser<bool> implementation
//
inline bool parser<bool>::parse(Option &O, StringRef ArgName, StringRef Arg,
                                bool &Value) {
  if (Arg == "" || Arg == "true" || Arg == "TRUE" || Arg == "True" ||
      Arg == "1") {
    Value = true;
    return false;
  }

  if (Arg == "false" || Arg == "FALSE" || Arg == "False" || Arg == "0") {
    Value = false;
    return false;
  }
  return O.error("'" + Arg +
                 "' is invalid value for boolean argument! Try 0 or 1");
}

// parser<boolOrDefault> implementation
//
inline bool parser<boolOrDefault>::parse(Option &O, StringRef ArgName,
                                         StringRef Arg, boolOrDefault &Value) {
  if (Arg == "" || Arg == "true" || Arg == "TRUE" || Arg == "True" ||
      Arg == "1") {
    Value = BOU_TRUE;
    return false;
  }
  if (Arg == "false" || Arg == "FALSE" || Arg == "False" || Arg == "0") {
    Value = BOU_FALSE;
    return false;
  }

  return O.error("'" + Arg +
                 "' is invalid value for boolean argument! Try 0 or 1");
}

// parser<int> implementation
//
inline bool parser<int>::parse(Option &O, StringRef ArgName, StringRef Arg,
                               int &Value) {
  if (Arg.getAsInteger(0, Value))
    return O.error("'" + Arg + "' value invalid for integer argument!");
  return false;
}

// parser<long> implementation
//
inline bool parser<long>::parse(Option &O, StringRef ArgName, StringRef Arg,
                                long &Value) {
  if (Arg.getAsInteger(0, Value))
    return O.error("'" + Arg + "' value invalid for long argument!");
  return false;
}

// parser<long long> implementation
//
inline bool parser<long long>::parse(Option &O, StringRef ArgName,
                                     StringRef Arg, long long &Value) {
  if (Arg.getAsInteger(0, Value))
    return O.error("'" + Arg + "' value invalid for llong argument!");
  return false;
}

// parser<unsigned> implementation
//
inline bool parser<unsigned>::parse(Option &O, StringRef ArgName, StringRef Arg,
                                    unsigned &Value) {

  if (Arg.getAsInteger(0, Value))
    return O.error("'" + Arg + "' value invalid for uint argument!");
  return false;
}

// parser<unsigned long> implementation
//
inline bool parser<unsigned long>::parse(Option &O, StringRef ArgName,
                                         StringRef Arg, unsigned long &Value) {

  if (Arg.getAsInteger(0, Value))
    return O.error("'" + Arg + "' value invalid for ulong argument!");
  return false;
}

// parser<unsigned long long> implementation
//
inline bool parser<unsigned long long>::parse(Option &O, StringRef ArgName,
                                              StringRef Arg,
                                              unsigned long long &Value) {

  if (Arg.getAsInteger(0, Value))
    return O.error("'" + Arg + "' value invalid for ullong argument!");
  return false;
}

// parser<double>/parser<float> implementation
//
inline bool parseDouble(Option &O, StringRef Arg, double &Value) {
  if (to_float(Arg, Value))
    return false;
  return O.error("'" + Arg + "' value invalid for floating point argument!");
}

inline bool parser<double>::parse(Option &O, StringRef ArgName, StringRef Arg,
                                  double &Val) {
  return parseDouble(O, Arg, Val);
}

inline bool parser<float>::parse(Option &O, StringRef ArgName, StringRef Arg,
                                 float &Val) {
  double dVal;
  if (parseDouble(O, Arg, dVal))
    return true;
  Val = (float)dVal;
  return false;
}

// generic_parser_base implementation
//

// findOption - Return the option number corresponding to the specified
// argument string.  If the option is not found, getNumOptions() is returned.
//
inline unsigned generic_parser_base::findOption(StringRef Name) {
  unsigned e = getNumOptions();

  for (unsigned i = 0; i != e; ++i) {
    if (getOption(i) == Name)
      return i;
  }
  return e;
}

inline StringRef EqValue = "=<value>";
inline StringRef EmptyOption = "<empty>";
inline StringRef OptionPrefix = "    =";
inline size_t getOptionPrefixesSize() {
  return OptionPrefix.size() + ArgHelpPrefix.size();
}

inline bool shouldPrintOption(StringRef Name, StringRef Description,
                              const Option &O) {
  return O.getValueExpectedFlag() != ValueOptional || !Name.empty() ||
         !Description.empty();
}

// Return the width of the option tag for printing...
inline size_t generic_parser_base::getOptionWidth(const Option &O) const {
  if (O.hasArgStr()) {
    size_t Size = argPlusPrefixesSize(O.ArgStr) + EqValue.size();
    for (unsigned i = 0, e = getNumOptions(); i != e; ++i) {
      StringRef Name = getOption(i);
      if (!shouldPrintOption(Name, getDescription(i), O))
        continue;
      size_t NameSize = Name.empty() ? EmptyOption.size() : Name.size();
      Size = std::max(Size, NameSize + getOptionPrefixesSize());
    }
    return Size;
  } else {
    size_t BaseSize = 0;
    for (unsigned i = 0, e = getNumOptions(); i != e; ++i)
      BaseSize = std::max(BaseSize, getOption(i).size() + 8);
    return BaseSize;
  }
}

// printOptionInfo - Print out information about this option.  The
// to-be-maintained width is specified.
//
inline void generic_parser_base::printOptionInfo(const Option &O,
                                                 size_t GlobalWidth) const {
  if (O.hasArgStr()) {
    // When the value is optional, first print a line just describing the
    // option without values.
    if (O.getValueExpectedFlag() == ValueOptional) {
      for (unsigned i = 0, e = getNumOptions(); i != e; ++i) {
        if (getOption(i).empty()) {
          outs() << PrintArg(O.ArgStr);
          Option::printHelpStr(O.HelpStr, GlobalWidth,
                               argPlusPrefixesSize(O.ArgStr));
          break;
        }
      }
    }

    outs() << PrintArg(O.ArgStr) << EqValue;
    Option::printHelpStr(O.HelpStr, GlobalWidth,
                         EqValue.size() + argPlusPrefixesSize(O.ArgStr));
    for (unsigned i = 0, e = getNumOptions(); i != e; ++i) {
      StringRef OptionName = getOption(i);
      StringRef Description = getDescription(i);
      if (!shouldPrintOption(OptionName, Description, O))
        continue;
      size_t FirstLineIndent = OptionName.size() + getOptionPrefixesSize();
      outs() << OptionPrefix << OptionName;
      if (OptionName.empty()) {
        outs() << EmptyOption;
        assert(FirstLineIndent >= EmptyOption.size());
        FirstLineIndent += EmptyOption.size();
      }
      if (!Description.empty())
        Option::printEnumValHelpStr(Description, GlobalWidth, FirstLineIndent);
      else
        outs() << '\n';
    }
  } else {
    if (!O.HelpStr.empty())
      outs() << "  " << O.HelpStr << '\n';
    for (unsigned i = 0, e = getNumOptions(); i != e; ++i) {
      StringRef Option = getOption(i);
      outs() << "    " << PrintArg(Option);
      Option::printHelpStr(getDescription(i), GlobalWidth, Option.size() + 8);
    }
  }
}

static const size_t MaxOptWidth = 8; // arbitrary spacing for printOptionDiff

// printGenericOptionDiff - Print the value of this option and it's default.
//
// "Generic" options have each value mapped to a name.
inline void generic_parser_base::printGenericOptionDiff(
    const Option &O, const GenericOptionValue &Value,
    const GenericOptionValue &Default, size_t GlobalWidth) const {
  outs() << "  " << PrintArg(O.ArgStr);
  outs().indent(GlobalWidth - O.ArgStr.size());

  unsigned NumOpts = getNumOptions();
  for (unsigned i = 0; i != NumOpts; ++i) {
    if (!Value.compare(getOptionValue(i)))
      continue;

    outs() << "= " << getOption(i);
    size_t L = getOption(i).size();
    size_t NumSpaces = MaxOptWidth > L ? MaxOptWidth - L : 0;
    outs().indent(NumSpaces) << " (default: ";
    for (unsigned j = 0; j != NumOpts; ++j) {
      if (!Default.compare(getOptionValue(j)))
        continue;
      outs() << getOption(j);
      break;
    }
    outs() << ")\n";
    return;
  }
  outs() << "= *unknown option value*\n";
}

// printOptionDiff - Specializations for printing basic value types.
//
#define PRINT_OPT_DIFF(T)                                                      \
  inline void parser<T>::printOptionDiff(                                      \
      const Option &O, T V, OptionValue<T> D, size_t GlobalWidth) const {      \
    printOptionName(O, GlobalWidth);                                           \
    SmallString<64> Str;                                                       \
    {                                                                          \
      raw_svector_ostream SS(Str);                                             \
      SS << V;                                                                 \
    }                                                                          \
    outs() << "= " << Str;                                                     \
    size_t NumSpaces =                                                         \
        MaxOptWidth > Str.size() ? MaxOptWidth - Str.size() : 0;               \
    outs().indent(NumSpaces) << " (default: ";                                 \
    if (D.hasValue())                                                          \
      outs() << D.getValue();                                                  \
    else                                                                       \
      outs() << "*no default*";                                                \
    outs() << ")\n";                                                           \
  }

PRINT_OPT_DIFF(bool)
PRINT_OPT_DIFF(boolOrDefault)
PRINT_OPT_DIFF(int)
PRINT_OPT_DIFF(long)
PRINT_OPT_DIFF(long long)
PRINT_OPT_DIFF(unsigned)
PRINT_OPT_DIFF(unsigned long)
PRINT_OPT_DIFF(unsigned long long)
PRINT_OPT_DIFF(double)
PRINT_OPT_DIFF(float)
PRINT_OPT_DIFF(char)

inline void parser<string_t>::printOptionDiff(const Option &O, StringRef V,
                                              const OptionValue<string_t> &D,
                                              size_t GlobalWidth) const {
  printOptionName(O, GlobalWidth);
  outs() << "= " << V;
  size_t NumSpaces = MaxOptWidth > V.size() ? MaxOptWidth - V.size() : 0;
  outs().indent(NumSpaces) << " (default: ";
  if (D.hasValue())
    outs() << D.getValue();
  else
    outs() << "*no default*";
  outs() << ")\n";
}

// Print a placeholder for options that don't yet support printOptionDiff().
inline void basic_parser_impl::printOptionNoValue(const Option &O,
                                                  size_t GlobalWidth) const {
  printOptionName(O, GlobalWidth);
  outs() << "= *cannot print option value*\n";
}

//===----------------------------------------------------------------------===//
// -help and -help-hidden option implementation
//

struct NamedOption {
  const char *first;
  cl::Option *second;
};
struct NamedSubCommand {
  const char *first;
  cl::SubCommand *second;
};

inline int OptNameCompare(const NamedOption *LHS, const NamedOption *RHS) {
  return csupport_cl_opt_name_compare(LHS->first, RHS->first);
}

inline int SubNameCompare(const NamedSubCommand *LHS,
                          const NamedSubCommand *RHS) {
  return csupport_cl_opt_name_compare(LHS->first, RHS->first);
}

// Copy Options into a vector so we can sort them as we like.
inline void sortOpts(StringMap<Option *> &OptMap,
                     SmallVectorImpl<NamedOption> &Opts, bool ShowHidden) {
  SmallPtrSet<Option *, 32> OptionSet; // Duplicate option detection.

  for (StringMap<Option *>::iterator I = OptMap.begin(), E = OptMap.end();
       I != E; ++I) {
    // Ignore really-hidden options.
    if (I->second->getOptionHiddenFlag() == ReallyHidden)
      continue;

    // Unless showhidden is set, ignore hidden flags.
    if (I->second->getOptionHiddenFlag() == Hidden && !ShowHidden)
      continue;

    // If we've already seen this option, don't add it to the list again.
    if (!OptionSet.insert(I->second).second)
      continue;

    Opts.push_back(NamedOption{I->getKey().data(), I->second});
  }

  // Sort the options list alphabetically.
  array_pod_sort(Opts.begin(), Opts.end(), OptNameCompare);
}

static void sortSubCommands(const SmallPtrSetImpl<SubCommand *> &SubMap,
                            SmallVectorImpl<NamedSubCommand> &Subs) {
  for (auto *S : SubMap) {
    if (S->getName().empty())
      continue;
    Subs.push_back({S->getName().data(), S});
  }
  array_pod_sort(Subs.begin(), Subs.end(), SubNameCompare);
}

namespace cl_detail {
class HelpPrinter {
protected:
  const bool ShowHidden;
  typedef SmallVector<NamedOption, 128> StrOptionPairVector;
  typedef SmallVector<NamedSubCommand, 128> StrSubCommandPairVector;
  // Print the options. Opts is assumed to be alphabetically sorted.
  virtual void printOptions(StrOptionPairVector &Opts, size_t MaxArgLen) {
    for (size_t i = 0, e = Opts.size(); i != e; ++i)
      Opts[i].second->printOptionInfo(MaxArgLen);
  }

  void printSubCommands(StrSubCommandPairVector &Subs, size_t MaxSubLen) {
    for (const auto &S : Subs) {
      outs() << "  " << S.first;
      if (!S.second->getDescription().empty()) {
        outs().indent(MaxSubLen - strlen(S.first));
        outs() << " - " << S.second->getDescription();
      }
      outs() << "\n";
    }
  }

public:
  explicit HelpPrinter(bool showHidden) : ShowHidden(showHidden) {}
  virtual ~HelpPrinter() = default;

  // Invoke the printer.
  void operator=(bool Value) {
    if (!Value)
      return;
    printHelp();

    // Halt the program since help information was printed
    exit(0);
  }

  void printHelp() {
    SubCommand *Sub = GlobalParser->getActiveSubCommand();
    auto &OptionsMap = Sub->OptionsMap;
    auto &PositionalOpts = Sub->PositionalOpts;
    auto &ConsumeAfterOpt = Sub->ConsumeAfterOpt;

    StrOptionPairVector Opts;
    sortOpts(OptionsMap, Opts, ShowHidden);

    StrSubCommandPairVector Subs;
    sortSubCommands(GlobalParser->RegisteredSubCommands, Subs);

    if (!GlobalParser->ProgramOverview.empty())
      outs() << "OVERVIEW: " << GlobalParser->ProgramOverview << "\n";

    if (Sub == &SubCommand::getTopLevel()) {
      outs() << "USAGE: " << GlobalParser->ProgramName;
      if (!Subs.empty())
        outs() << " [subcommand]";
      outs() << " [options]";
    } else {
      if (!Sub->getDescription().empty()) {
        outs() << "SUBCOMMAND '" << Sub->getName()
               << "': " << Sub->getDescription() << "\n\n";
      }
      outs() << "USAGE: " << GlobalParser->ProgramName << " " << Sub->getName()
             << " [options]";
    }

    for (auto *Opt : PositionalOpts) {
      if (Opt->hasArgStr())
        outs() << " --" << Opt->ArgStr;
      outs() << " " << Opt->HelpStr;
    }

    // Print the consume after option info if it exists...
    if (ConsumeAfterOpt)
      outs() << " " << ConsumeAfterOpt->HelpStr;

    if (Sub == &SubCommand::getTopLevel() && !Subs.empty()) {
      // Compute the maximum subcommand length...
      size_t MaxSubLen = 0;
      for (size_t i = 0, e = Subs.size(); i != e; ++i)
        MaxSubLen = std::max(MaxSubLen, strlen(Subs[i].first));

      outs() << "\n\n";
      outs() << "SUBCOMMANDS:\n\n";
      printSubCommands(Subs, MaxSubLen);
      outs() << "\n";
      outs() << "  Type \"" << GlobalParser->ProgramName
             << " <subcommand> --help\" to get more help on a specific "
                "subcommand";
    }

    outs() << "\n\n";

    // Compute the maximum argument length...
    size_t MaxArgLen = 0;
    for (size_t i = 0, e = Opts.size(); i != e; ++i)
      MaxArgLen = std::max(MaxArgLen, Opts[i].second->getOptionWidth());

    outs() << "OPTIONS:\n";
    printOptions(Opts, MaxArgLen);

    // Print any extra help the user has declared.
    for (const auto &I : GlobalParser->MoreHelp)
      outs() << I;
    GlobalParser->MoreHelp.clear();
  }
};

class CategorizedHelpPrinter : public HelpPrinter {
public:
  explicit CategorizedHelpPrinter(bool showHidden) : HelpPrinter(showHidden) {}

  // Helper function for printOptions().
  // It shall return a negative value if A's name should be lexicographically
  // ordered before B's name. It returns a value greater than zero if B's name
  // should be ordered before A's name, and it returns 0 otherwise.
  static int OptionCategoryCompare(OptionCategory *const *A,
                                   OptionCategory *const *B) {
    return (*A)->getName().compare((*B)->getName());
  }

  // Make sure we inherit our base class's operator=()
  using HelpPrinter::operator=;

protected:
  void printOptions(StrOptionPairVector &Opts, size_t MaxArgLen) override {
    SmallVector<OptionCategory *, 8> SortedCategories;
    DenseMap<OptionCategory *, SmallVector<Option *, 8>> CategorizedOptions;

    // Collect registered option categories into vector in preparation for
    // sorting.
    for (OptionCategory *Category : GlobalParser->RegisteredOptionCategories)
      SortedCategories.push_back(Category);

    // Sort the different option categories alphabetically.
    assert(SortedCategories.size() > 0 && "No option categories registered!");
    array_pod_sort(SortedCategories.begin(), SortedCategories.end(),
                   OptionCategoryCompare);

    // Walk through pre-sorted options and assign into categories.
    // Because the options are already alphabetically sorted the
    // options within categories will also be alphabetically sorted.
    for (size_t I = 0, E = Opts.size(); I != E; ++I) {
      Option *Opt = Opts[I].second;
      for (auto &Cat : Opt->Categories) {
        assert(llvm::is_contained(SortedCategories, Cat) &&
               "Option has an unregistered category");
        CategorizedOptions[Cat].push_back(Opt);
      }
    }

    // Now do printing.
    for (OptionCategory *Category : SortedCategories) {
      // Hide empty categories for --help, but show for --help-hidden.
      const auto &CategoryOptions = CategorizedOptions[Category];
      bool IsEmptyCategory = CategoryOptions.empty();
      if (!ShowHidden && IsEmptyCategory)
        continue;

      // Print category information.
      outs() << "\n";
      outs() << Category->getName() << ":\n";

      // Check if description is set.
      if (!Category->getDescription().empty())
        outs() << Category->getDescription() << "\n\n";
      else
        outs() << "\n";

      // When using --help-hidden explicitly state if the category has no
      // options associated with it.
      if (IsEmptyCategory) {
        outs() << "  This option category has no options.\n";
        continue;
      }
      // Loop over the options in the category and print.
      for (const Option *Opt : CategoryOptions)
        Opt->printOptionInfo(MaxArgLen);
    }
  }
};

// This wraps the Uncategorizing and Categorizing printers and decides
// at run time which should be invoked.
class HelpPrinterWrapper {
private:
  HelpPrinter &UncategorizedPrinter;
  CategorizedHelpPrinter &CategorizedPrinter;

public:
  explicit HelpPrinterWrapper(HelpPrinter &UncategorizedPrinter,
                              CategorizedHelpPrinter &CategorizedPrinter)
      : UncategorizedPrinter(UncategorizedPrinter),
        CategorizedPrinter(CategorizedPrinter) {}

  // Invoke the printer.
  void operator=(bool Value);
};

} // namespace cl_detail

#if defined(__GNUC__)
// GCC and GCC-compatible compilers define __OPTIMIZE__ when optimizations are
// enabled.
#if defined(__OPTIMIZE__)
#define LLVM_IS_DEBUG_BUILD 0
#else
#define LLVM_IS_DEBUG_BUILD 1
#endif
#elif defined(_MSC_VER)
// MSVC doesn't have a predefined macro indicating if optimizations are enabled.
// Use _DEBUG instead. This macro actually corresponds to the choice between
// debug and release CRTs, but it is a reasonable proxy.
#if defined(_DEBUG)
#define LLVM_IS_DEBUG_BUILD 1
#else
#define LLVM_IS_DEBUG_BUILD 0
#endif
#else
// Otherwise, for an unknown compiler, assume this is an optimized build.
#define LLVM_IS_DEBUG_BUILD 0
#endif

namespace cl_detail {
class VersionPrinter {
public:
  void print(SmallVector<VersionPrinterTy, 2> ExtraPrinters = {}) {
    raw_ostream &OS = outs();
#ifdef PACKAGE_VENDOR
    OS << PACKAGE_VENDOR << " ";
#else
    OS << "LLVM (http://llvm.org/):\n  ";
#endif
    OS << PACKAGE_NAME << " version " << PACKAGE_VERSION << "\n  ";
#if LLVM_IS_DEBUG_BUILD
    OS << "DEBUG build";
#else
    OS << "Optimized build";
#endif
#ifndef NDEBUG
    OS << " with assertions";
#endif
    OS << ".\n";

    // Iterate over any registered extra printers and call them to add further
    // information.
    if (!ExtraPrinters.empty()) {
      for (const auto &I : ExtraPrinters)
        I(outs());
    }
  }
  void operator=(bool OptionWasSpecified);
};

struct CommandLineCommonOptions {
  // Declare the four HelpPrinter instances that are used to print out help, or
  // help-hidden as an uncategorized list or in categories.
  HelpPrinter UncategorizedNormalPrinter{false};
  HelpPrinter UncategorizedHiddenPrinter{true};
  CategorizedHelpPrinter CategorizedNormalPrinter{false};
  CategorizedHelpPrinter CategorizedHiddenPrinter{true};
  // Declare HelpPrinter wrappers that will decide whether or not to invoke
  // a categorizing help printer
  HelpPrinterWrapper WrappedNormalPrinter{UncategorizedNormalPrinter,
                                          CategorizedNormalPrinter};
  HelpPrinterWrapper WrappedHiddenPrinter{UncategorizedHiddenPrinter,
                                          CategorizedHiddenPrinter};
  // Define a category for generic options that all tools should have.
  cl::OptionCategory GenericCategory{"Generic Options"};

  // Define uncategorized help printers.
  // --help-list is hidden by default because if Option categories are being
  // used then --help behaves the same as --help-list.
  cl::opt<HelpPrinter, true, parser<bool>> HLOp{
      "help-list",
      cl::desc(
          "Display list of available options (--help-list-hidden for more)"),
      cl::location(UncategorizedNormalPrinter),
      cl::Hidden,
      cl::ValueDisallowed,
      cl::cat(GenericCategory),
      cl::sub(SubCommand::getAll())};

  cl::opt<HelpPrinter, true, parser<bool>> HLHOp{
      "help-list-hidden",
      cl::desc("Display list of all available options"),
      cl::location(UncategorizedHiddenPrinter),
      cl::Hidden,
      cl::ValueDisallowed,
      cl::cat(GenericCategory),
      cl::sub(SubCommand::getAll())};

  // Define uncategorized/categorized help printers. These printers change their
  // behaviour at runtime depending on whether one or more Option categories
  // have been declared.
  cl::opt<HelpPrinterWrapper, true, parser<bool>> HOp{
      "help",
      cl::desc("Display available options (--help-hidden for more)"),
      cl::location(WrappedNormalPrinter),
      cl::ValueDisallowed,
      cl::cat(GenericCategory),
      cl::sub(SubCommand::getAll())};

  cl::alias HOpA{"h", cl::desc("Alias for --help"), cl::aliasopt(HOp),
                 cl::DefaultOption};

  cl::opt<HelpPrinterWrapper, true, parser<bool>> HHOp{
      "help-hidden",
      cl::desc("Display all available options"),
      cl::location(WrappedHiddenPrinter),
      cl::Hidden,
      cl::ValueDisallowed,
      cl::cat(GenericCategory),
      cl::sub(SubCommand::getAll())};

  cl::opt<bool> PrintOptions{
      "print-options",
      cl::desc("Print non-default options after command line parsing"),
      cl::Hidden,
      cl::init(false),
      cl::cat(GenericCategory),
      cl::sub(SubCommand::getAll())};

  cl::opt<bool> PrintAllOptions{
      "print-all-options",
      cl::desc("Print all option values after command line parsing"),
      cl::Hidden,
      cl::init(false),
      cl::cat(GenericCategory),
      cl::sub(SubCommand::getAll())};

  VersionPrinterTy OverrideVersionPrinter = 0;

  SmallVector<VersionPrinterTy, 2> ExtraVersionPrinters;

  // Define the --version option that prints out the LLVM version for the tool
  VersionPrinter VersionPrinterInstance;

  cl::opt<VersionPrinter, true, parser<bool>> VersOp{
      "version", cl::desc("Display the version of this program"),
      cl::location(VersionPrinterInstance), cl::ValueDisallowed,
      cl::cat(GenericCategory)};
};
} // namespace cl_detail

// Lazy-initialized global instance of options controlling the command-line
// parser and general handling.
inline ManagedStatic<cl_detail::CommandLineCommonOptions> CommonOptions;

inline OptionCategory &getGeneralCategory() {
  // Initialise the general option category.
  static OptionCategory GeneralCategory{"General options"};
  return GeneralCategory;
}

inline void cl_detail::VersionPrinter::operator=(bool OptionWasSpecified) {
  if (!OptionWasSpecified)
    return;

  if (CommonOptions->OverrideVersionPrinter != 0) {
    CommonOptions->OverrideVersionPrinter(outs());
    exit(0);
  }
  print(CommonOptions->ExtraVersionPrinters);

  exit(0);
}

inline void cl_detail::HelpPrinterWrapper::operator=(bool Value) {
  if (!Value)
    return;

  // Decide which printer to invoke. If more than one option category is
  // registered then it is useful to show the categorized help instead of
  // uncategorized help.
  if (GlobalParser->RegisteredOptionCategories.size() > 1) {
    // unhide --help-list option so user can have uncategorized output if they
    // want it.
    CommonOptions->HLOp.setHiddenFlag(NotHidden);

    CategorizedPrinter = true; // Invoke categorized printer
  } else
    UncategorizedPrinter = true; // Invoke uncategorized printer
}

// Print the value of each option.
inline void PrintOptionValues() { GlobalParser->printOptionValues(); }

inline void cl_detail::CommandLineParser::printOptionValues() {
  if (!CommonOptions->PrintOptions && !CommonOptions->PrintAllOptions)
    return;

  SmallVector<NamedOption, 128> Opts;
  sortOpts(ActiveSubCommand->OptionsMap, Opts, /*ShowHidden*/ true);

  // Compute the maximum argument length...
  size_t MaxArgLen = 0;
  for (size_t i = 0, e = Opts.size(); i != e; ++i)
    MaxArgLen = std::max(MaxArgLen, Opts[i].second->getOptionWidth());

  for (size_t i = 0, e = Opts.size(); i != e; ++i)
    Opts[i].second->printOptionValue(MaxArgLen, CommonOptions->PrintAllOptions);
}

// Utility function for printing the help message.
inline void PrintHelpMessage(bool Hidden, bool Categorized) {
  if (!Hidden && !Categorized)
    CommonOptions->UncategorizedNormalPrinter.printHelp();
  else if (!Hidden && Categorized)
    CommonOptions->CategorizedNormalPrinter.printHelp();
  else if (Hidden && !Categorized)
    CommonOptions->UncategorizedHiddenPrinter.printHelp();
  else
    CommonOptions->CategorizedHiddenPrinter.printHelp();
}

/// Utility function for printing version number.
inline void PrintVersionMessage() {
  CommonOptions->VersionPrinterInstance.print(
      CommonOptions->ExtraVersionPrinters);
}

inline void SetVersionPrinter(VersionPrinterTy func) {
  CommonOptions->OverrideVersionPrinter = func;
}

inline void AddExtraVersionPrinter(VersionPrinterTy func) {
  CommonOptions->ExtraVersionPrinters.push_back(func);
}

inline StringMap<Option *> &getRegisteredOptions(SubCommand &Sub) {
  initCommonOptions();
  auto &Subs = GlobalParser->RegisteredSubCommands;
  (void)Subs;
  assert(Subs.contains(&Sub));
  return Sub.OptionsMap;
}

inline iterator_range<typename SmallPtrSet<SubCommand *, 4>::iterator>
getRegisteredSubcommands() {
  return GlobalParser->getRegisteredSubcommands();
}

inline void HideUnrelatedOptions(OptionCategory &Category, SubCommand &Sub) {
  initCommonOptions();
  for (auto &I : Sub.OptionsMap) {
    bool Unrelated = true;
    for (auto &Cat : I.second->Categories) {
      if (Cat == &Category || Cat == &CommonOptions->GenericCategory)
        Unrelated = false;
    }
    if (Unrelated)
      I.second->setHiddenFlag(cl::ReallyHidden);
  }
}

inline void HideUnrelatedOptions(ArrayRef<const OptionCategory *> Categories,
                                 SubCommand &Sub) {
  initCommonOptions();
  for (auto &I : Sub.OptionsMap) {
    bool Unrelated = true;
    for (auto &Cat : I.second->Categories) {
      if (is_contained(Categories, Cat) ||
          Cat == &CommonOptions->GenericCategory)
        Unrelated = false;
    }
    if (Unrelated)
      I.second->setHiddenFlag(cl::ReallyHidden);
  }
}

inline void ResetCommandLineParser() { GlobalParser->reset(); }
inline void ResetAllOptionOccurrences() {
  GlobalParser->ResetAllOptionOccurrences();
}

inline void LLVMParseCommandLineOptions(int argc, const char *const *argv,
                                        const char *Overview) {
  llvm::cl::ParseCommandLineOptions(argc, argv, StringRef(Overview),
                                    &llvm::nulls());
}

} // namespace cl
} // namespace llvm

/* Avoid polluting subsequent headers: isWhitespace etc. break
 * clang::isWhitespace. */
#undef isWhitespace
#undef isWhitespaceOrNull
#undef isQuote
#undef isWindowsSpecialChar
#undef isWindowsSpecialCharInCommandName
#undef RequiresValue
#undef EatsUnboundedNumberOfValues
#undef hasUTF8ByteOrderMark

#endif /* __cplusplus */
#endif /* CSUPPORT_LCOMMAND_LLINE_H */

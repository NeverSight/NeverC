//===- YAMLParser.h - Simple YAML parser ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This is a YAML 1.2 parser.
//
//  See http://www.yaml.org/spec/1.2/spec.html for the full standard.
//
//  This currently does not implement the following:
//    * Tag resolution.
//    * UTF-16.
//    * BOMs anywhere other than the first Unicode scalar value in the file.
//
//  The most important class here is Stream. This represents a YAML stream with
//  0, 1, or many documents.
//
//  SourceMgr sm;
//  StringRef input = getInput();
//  yaml::Stream stream(input, sm);
//
//  for (yaml::document_iterator di = stream.begin(), de = stream.end();
//       di != de; ++di) {
//    yaml::Node *n = di->getRoot();
//    if (n) {
//      // Do something with n...
//    } else
//      break;
//  }
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_YAMLPARSER_H
#define LLVM_SUPPORT_YAMLPARSER_H

#include "csupport/lstring_lref.h"
#include "csupport/ly_la_lm_ll_lparser.h"
#include "llvm/ADT/AllocatorList.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <assert.h>
#include <errno.h>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <stddef.h>
#include <string>

namespace llvm {

class raw_ostream;

namespace yaml {

class Document;
class document_iterator;
class Node;
class Scanner;
struct Token;

/// Dump all the tokens in this stream to OS.
/// \returns true if there was an error, false otherwise.
bool dumpTokens(StringRef Input, raw_ostream &);

/// Scans all tokens in input without outputting anything. This is used
///        for benchmarking the tokenizer.
/// \returns true if there was an error, false otherwise.
bool scanTokens(StringRef Input);

/// Escape \a Input for a double quoted scalar; if \p EscapePrintable
/// is true, all UTF8 sequences will be escaped, if \p EscapePrintable is
/// false, those UTF8 sequences encoding printable unicode scalars will not be
/// escaped, but emitted verbatim.
SmallString<256> escape(StringRef Input, bool EscapePrintable = true);

/// Parse \p S as a bool according to https://yaml.org/type/bool.html.
/// Returns -1 if not a bool, 0 for false, 1 for true.
int parseBool(StringRef S);

/// This class represents a YAML stream potentially containing multiple
///        documents.
class Stream {
public:
  /// This keeps a reference to the string referenced by \p Input.
  Stream(StringRef Input, SourceMgr &, bool ShowColors = true,
         int *EC = nullptr);

  Stream(MemoryBufferRef InputBuffer, SourceMgr &, bool ShowColors = true,
         int *EC = nullptr);
  ~Stream();

  document_iterator begin();
  document_iterator end();
  void skip();
  bool failed();

  bool validate() {
    skip();
    return !failed();
  }

  void printError(Node *N, const Twine &Msg,
                  SourceMgr::DiagKind Kind = SourceMgr::DK_Error);
  void printError(const SMRange &Range, const Twine &Msg,
                  SourceMgr::DiagKind Kind = SourceMgr::DK_Error);

private:
  friend class Document;

  std::unique_ptr<Scanner> scanner;
  std::unique_ptr<Document> CurrentDoc;
};

/// Abstract base class for all Nodes.
class Node {
  virtual void anchor();

public:
  enum NodeKind {
    NK_Null,
    NK_Scalar,
    NK_BlockScalar,
    NK_KeyValue,
    NK_Mapping,
    NK_Sequence,
    NK_Alias
  };

  Node(unsigned int Type, std::unique_ptr<Document> &, StringRef Anchor,
       StringRef Tag);

  // It's not safe to copy YAML nodes; the document is streamed and the position
  // is part of the state.
  Node(const Node &) = delete;
  void operator=(const Node &) = delete;

  void *operator new(size_t Size, BumpPtrAllocator &Alloc,
                     size_t Alignment = 16) noexcept {
    return Alloc.Allocate(Size, Alignment);
  }

  void operator delete(void *Ptr, BumpPtrAllocator &Alloc,
                       size_t Size) noexcept {
    Alloc.Deallocate(Ptr, Size, 0);
  }

  void operator delete(void *) noexcept = delete;

  /// Get the value of the anchor attached to this node. If it does not
  ///        have one, getAnchor().size() will be 0.
  StringRef getAnchor() const { return Anchor; }

  /// Get the tag as it was written in the document. This does not
  ///   perform tag resolution.
  StringRef getRawTag() const { return Tag; }

  /// Get the verbatium tag for a given Node. This performs tag resoluton
  ///   and substitution.
  SmallString<256> getVerbatimTag() const;

  SMRange getSourceRange() const { return SourceRange; }
  void setSourceRange(SMRange SR) { SourceRange = SR; }

  // These functions forward to Document and Scanner.
  Token &peekNext();
  Token getNext();
  Node *parseBlockNode();
  BumpPtrAllocator &getAllocator();
  void setError(const Twine &Message, Token &Location) const;
  bool failed() const;

  virtual void skip() {}

  unsigned int getType() const { return TypeID; }

protected:
  std::unique_ptr<Document> &Doc;
  SMRange SourceRange;

  ~Node() = default;

private:
  unsigned int TypeID;
  StringRef Anchor;
  /// The tag as typed in the document.
  StringRef Tag;
};

/// A null value.
///
/// Example:
///   !!null null
class NullNode final : public Node {
  void anchor() override;

public:
  NullNode(std::unique_ptr<Document> &D)
      : Node(NK_Null, D, StringRef(), StringRef()) {}

  static bool classof(const Node *N) { return N->getType() == NK_Null; }
};

/// A scalar node is an opaque datum that can be presented as a
///        series of zero or more Unicode scalar values.
///
/// Example:
///   Adena
class ScalarNode final : public Node {
  void anchor() override;

public:
  ScalarNode(std::unique_ptr<Document> &D, StringRef Anchor, StringRef Tag,
             StringRef Val)
      : Node(NK_Scalar, D, Anchor, Tag), Value(Val) {
    SMLoc Start = SMLoc::getFromPointer(Val.begin());
    SMLoc End = SMLoc::getFromPointer(Val.end());
    SourceRange = SMRange(Start, End);
  }

  // Return Value without any escaping or folding or other fun YAML stuff. This
  // is the exact bytes that are contained in the file (after conversion to
  // utf8).
  StringRef getRawValue() const { return Value; }

  /// Gets the value of this node as a StringRef.
  ///
  /// \param Storage is used to store the content of the returned StringRef if
  ///        it requires any modification from how it appeared in the source.
  ///        This happens with escaped characters and multi-line literals.
  StringRef getValue(SmallVectorImpl<char> &Storage) const;

  static bool classof(const Node *N) { return N->getType() == NK_Scalar; }

private:
  StringRef Value;

  StringRef getDoubleQuotedValue(StringRef UnquotedValue,
                                 SmallVectorImpl<char> &Storage) const;

  static StringRef getSingleQuotedValue(StringRef RawValue,
                                        SmallVectorImpl<char> &Storage);

  static StringRef getPlainValue(StringRef RawValue,
                                 SmallVectorImpl<char> &Storage);
};

/// A block scalar node is an opaque datum that can be presented as a
///        series of zero or more Unicode scalar values.
///
/// Example:
///   |
///     Hello
///     World
class BlockScalarNode final : public Node {
  void anchor() override;

public:
  BlockScalarNode(std::unique_ptr<Document> &D, StringRef Anchor, StringRef Tag,
                  StringRef Value, StringRef RawVal)
      : Node(NK_BlockScalar, D, Anchor, Tag), Value(Value) {
    SMLoc Start = SMLoc::getFromPointer(RawVal.begin());
    SMLoc End = SMLoc::getFromPointer(RawVal.end());
    SourceRange = SMRange(Start, End);
  }

  /// Gets the value of this node as a StringRef.
  StringRef getValue() const { return Value; }

  static bool classof(const Node *N) { return N->getType() == NK_BlockScalar; }

private:
  StringRef Value;
};

/// A key and value pair. While not technically a Node under the YAML
///        representation graph, it is easier to treat them this way.
///
/// TODO: Consider making this not a child of Node.
///
/// Example:
///   Section: .text
class KeyValueNode final : public Node {
  void anchor() override;

public:
  KeyValueNode(std::unique_ptr<Document> &D)
      : Node(NK_KeyValue, D, StringRef(), StringRef()) {}

  /// Parse and return the key.
  ///
  /// This may be called multiple times.
  ///
  /// \returns The key, or nullptr if failed() == true.
  Node *getKey();

  /// Parse and return the value.
  ///
  /// This may be called multiple times.
  ///
  /// \returns The value, or nullptr if failed() == true.
  Node *getValue();

  void skip() override {
    if (Node *Key = getKey()) {
      Key->skip();
      if (Node *Val = getValue())
        Val->skip();
    }
  }

  static bool classof(const Node *N) { return N->getType() == NK_KeyValue; }

private:
  Node *Key = nullptr;
  Node *Value = nullptr;
};

/// This is an iterator abstraction over YAML collections shared by both
///        sequences and maps.
///
/// BaseT must have a ValueT* member named CurrentEntry and a member function
/// increment() which must set CurrentEntry to 0 to create an end iterator.
template <class BaseT, class ValueT> class basic_collection_iterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type = ValueT;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type *;
  using reference = value_type &;

  basic_collection_iterator() = default;
  basic_collection_iterator(BaseT *B) : Base(B) {}

  ValueT *operator->() const {
    assert(Base && Base->CurrentEntry && "Attempted to access end iterator!");
    return Base->CurrentEntry;
  }

  ValueT &operator*() const {
    assert(Base && Base->CurrentEntry &&
           "Attempted to dereference end iterator!");
    return *Base->CurrentEntry;
  }

  operator ValueT *() const {
    assert(Base && Base->CurrentEntry && "Attempted to access end iterator!");
    return Base->CurrentEntry;
  }

  /// Note on EqualityComparable:
  ///
  /// The iterator is not re-entrant,
  /// it is meant to be used for parsing YAML on-demand
  /// Once iteration started - it can point only to one entry at a time
  /// hence Base.CurrentEntry and Other.Base.CurrentEntry are equal
  /// iff Base and Other.Base are equal.
  bool operator==(const basic_collection_iterator &Other) const {
    if (Base && (Base == Other.Base)) {
      assert((Base->CurrentEntry == Other.Base->CurrentEntry) &&
             "Equal Bases expected to point to equal Entries");
    }

    return Base == Other.Base;
  }

  bool operator!=(const basic_collection_iterator &Other) const {
    return !(Base == Other.Base);
  }

  basic_collection_iterator &operator++() {
    assert(Base && "Attempted to advance iterator past end!");
    Base->increment();
    // Create an end iterator.
    if (!Base->CurrentEntry)
      Base = nullptr;
    return *this;
  }

private:
  BaseT *Base = nullptr;
};

// The following two templates are used for both MappingNode and Sequence Node.
// Named collectionBegin/collectionSkip (not begin/skip) so ADL does not pick
// them up for unrelated llvm types (e.g. SmallVector<SimpleKey>).
template <class CollectionType>
typename CollectionType::iterator collectionBegin(CollectionType &C) {
  assert(C.IsAtBeginning && "You may only iterate over a collection once!");
  C.IsAtBeginning = false;
  typename CollectionType::iterator ret(&C);
  ++ret;
  return ret;
}

template <class CollectionType> void collectionSkip(CollectionType &C) {
  // TODO: support skipping from the middle of a parsed collection ;/
  assert((C.IsAtBeginning || C.IsAtEnd) && "Cannot skip mid parse!");
  if (C.IsAtBeginning)
    for (typename CollectionType::iterator i = collectionBegin(C), e = C.end();
         i != e; ++i)
      i->skip();
}

/// Represents a YAML map created from either a block map for a flow map.
///
/// This parses the YAML stream as increment() is called.
///
/// Example:
///   Name: _main
///   Scope: Global
class MappingNode final : public Node {
  void anchor() override;

public:
  enum MappingType {
    MT_Block,
    MT_Flow,
    MT_Inline ///< An inline mapping node is used for "[key: value]".
  };

  MappingNode(std::unique_ptr<Document> &D, StringRef Anchor, StringRef Tag,
              MappingType MT)
      : Node(NK_Mapping, D, Anchor, Tag), Type(MT) {}

  friend class basic_collection_iterator<MappingNode, KeyValueNode>;

  using iterator = basic_collection_iterator<MappingNode, KeyValueNode>;

  template <class T> friend typename T::iterator yaml::collectionBegin(T &);
  template <class T> friend void yaml::collectionSkip(T &);

  iterator begin() { return collectionBegin(*this); }

  iterator end() { return iterator(); }

  void skip() override { collectionSkip(*this); }

  static bool classof(const Node *N) { return N->getType() == NK_Mapping; }

private:
  MappingType Type;
  bool IsAtBeginning = true;
  bool IsAtEnd = false;
  KeyValueNode *CurrentEntry = nullptr;

  void increment();
};

/// Represents a YAML sequence created from either a block sequence for a
///        flow sequence.
///
/// This parses the YAML stream as increment() is called.
///
/// Example:
///   - Hello
///   - World
class SequenceNode final : public Node {
  void anchor() override;

public:
  enum SequenceType {
    ST_Block,
    ST_Flow,
    // Use for:
    //
    // key:
    // - val1
    // - val2
    //
    // As a BlockMappingEntry and BlockEnd are not created in this case.
    ST_Indentless
  };

  SequenceNode(std::unique_ptr<Document> &D, StringRef Anchor, StringRef Tag,
               SequenceType ST)
      : Node(NK_Sequence, D, Anchor, Tag), SeqType(ST) {}

  friend class basic_collection_iterator<SequenceNode, Node>;

  using iterator = basic_collection_iterator<SequenceNode, Node>;

  template <class T> friend typename T::iterator yaml::collectionBegin(T &);
  template <class T> friend void yaml::collectionSkip(T &);

  void increment();

  iterator begin() { return collectionBegin(*this); }

  iterator end() { return iterator(); }

  void skip() override { collectionSkip(*this); }

  static bool classof(const Node *N) { return N->getType() == NK_Sequence; }

private:
  SequenceType SeqType;
  bool IsAtBeginning = true;
  bool IsAtEnd = false;
  bool WasPreviousTokenFlowEntry = true; // Start with an imaginary ','.
  Node *CurrentEntry = nullptr;
};

/// Represents an alias to a Node with an anchor.
///
/// Example:
///   *AnchorName
class AliasNode final : public Node {
  void anchor() override;

public:
  AliasNode(std::unique_ptr<Document> &D, StringRef Val)
      : Node(NK_Alias, D, StringRef(), StringRef()), Name(Val) {}

  StringRef getName() const { return Name; }

  static bool classof(const Node *N) { return N->getType() == NK_Alias; }

private:
  StringRef Name;
};

/// A YAML Stream is a sequence of Documents. A document contains a root
///        node.
class Document {
public:
  Document(Stream &ParentStream);

  /// Root for parsing a node. Returns a single node.
  Node *parseBlockNode();

  /// Finish parsing the current document and return true if there are
  ///        more. Return false otherwise.
  bool skip();

  /// Parse and return the root level node.
  Node *getRoot() {
    if (Root)
      return Root;
    return Root = parseBlockNode();
  }

  const std::map<StringRef, StringRef> &getTagMap() const { return TagMap; }

private:
  friend class Node;
  friend class document_iterator;

  /// Stream to read tokens from.
  Stream &stream;

  /// Used to allocate nodes to. All are destroyed without calling their
  ///        destructor when the document is destroyed.
  BumpPtrAllocator NodeAllocator;

  /// The root node. Used to support skipping a partially parsed
  ///        document.
  Node *Root;

  /// Maps tag prefixes to their expansion.
  std::map<StringRef, StringRef> TagMap;

  Token &peekNext();
  Token getNext();
  void setError(const Twine &Message, Token &Location) const;
  bool failed() const;

  /// Parse %BLAH directives and return true if any were encountered.
  bool parseDirectives();

  /// Parse %YAML
  void parseYAMLDirective();

  /// Parse %TAG
  void parseTAGDirective();

  /// Consume the next token and error if it is not \a TK.
  bool expectToken(int TK);
};

/// Iterator abstraction for Documents over a Stream.
class document_iterator {
public:
  document_iterator() = default;
  document_iterator(std::unique_ptr<Document> &D) : Doc(&D) {}

  bool operator==(const document_iterator &Other) const {
    if (isAtEnd() || Other.isAtEnd())
      return isAtEnd() && Other.isAtEnd();

    return Doc == Other.Doc;
  }
  bool operator!=(const document_iterator &Other) const {
    return !(*this == Other);
  }

  document_iterator operator++() {
    assert(Doc && "incrementing iterator past the end.");
    if (!(*Doc)->skip()) {
      Doc->reset(nullptr);
    } else {
      Stream &S = (*Doc)->stream;
      Doc->reset(new Document(S));
    }
    return *this;
  }

  Document &operator*() { return **Doc; }

  std::unique_ptr<Document> &operator->() { return *Doc; }

private:
  bool isAtEnd() const { return !Doc || !*Doc; }

  std::unique_ptr<Document> *Doc = nullptr;
};

} // end namespace yaml

} // end namespace llvm

//===----------------------------------------------------------------------===//
// Implementation - Token, Scanner, and inline method definitions
//===----------------------------------------------------------------------===//

namespace llvm {
namespace yaml {

/// Token - A single YAML token.
struct Token {
  enum TokenKind {
    TK_Error, // Uninitialized token.
    TK_StreamStart,
    TK_StreamEnd,
    TK_VersionDirective,
    TK_TagDirective,
    TK_DocumentStart,
    TK_DocumentEnd,
    TK_BlockEntry,
    TK_BlockEnd,
    TK_BlockSequenceStart,
    TK_BlockMappingStart,
    TK_FlowEntry,
    TK_FlowSequenceStart,
    TK_FlowSequenceEnd,
    TK_FlowMappingStart,
    TK_FlowMappingEnd,
    TK_Key,
    TK_Value,
    TK_Scalar,
    TK_BlockScalar,
    TK_Alias,
    TK_Anchor,
    TK_Tag
  } Kind = TK_Error;

  StringRef Range;
  SmallString<256> Value;
  Token() = default;
};

using TokenQueueT = BumpPtrList<Token>;

struct SimpleKey {
  TokenQueueT::iterator Tok;
  unsigned Column = 0;
  unsigned Line = 0;
  unsigned FlowLevel = 0;
  bool IsRequired = false;
  bool operator==(const SimpleKey &Other) { return Tok == Other.Tok; }
};

namespace detail {

enum UnicodeEncodingForm {
  UEF_UTF32_LE,
  UEF_UTF32_BE,
  UEF_UTF16_LE,
  UEF_UTF16_BE,
  UEF_UTF8,
  UEF_Unknown
};

struct EncodingInfo {
  UnicodeEncodingForm first;
  unsigned second;
};

inline EncodingInfo getUnicodeEncoding(StringRef Input) {
  unsigned bom_len;
  int enc = csupport_detect_unicode_encoding(
      (const unsigned char *)(Input.data()), Input.size(), &bom_len);
  static const UnicodeEncodingForm map[] = {UEF_Unknown,  UEF_UTF8,
                                            UEF_UTF16_LE, UEF_UTF16_BE,
                                            UEF_UTF32_BE, UEF_UTF32_LE};
  return {map[enc], bom_len};
}

struct UTF8Decoded {
  uint32_t first;
  unsigned second;
};

inline UTF8Decoded decodeUTF8(StringRef Range) {
  unsigned len;
  uint32_t cp = csupport_yaml_decode_utf8(Range.data(), Range.size(), &len);
  return {cp, len};
}

inline void encodeUTF8(uint32_t UnicodeScalarValue,
                       SmallVectorImpl<char> &Result) {
  char buf[4];
  int n = csupport_yaml_encode_utf8(UnicodeScalarValue, buf, sizeof(buf));
  Result.append(buf, buf + n);
}

LLVM_ATTRIBUTE_NOINLINE inline bool wasEscaped(StringRef::iterator First,
                                               StringRef::iterator Position) {
  assert(Position - 1 >= First);
  return csupport_yaml_was_escaped(First, Position) != 0;
}

inline StringRef
parseScalarValue(StringRef UnquotedValue, SmallVectorImpl<char> &Storage,
                 StringRef LookupChars,
                 function_ref<StringRef(StringRef, SmallVectorImpl<char> &)>
                     UnescapeCallback) {
  size_t I = UnquotedValue.find_first_of(LookupChars);
  if (I == StringRef::npos)
    return UnquotedValue;

  Storage.clear();
  Storage.reserve(UnquotedValue.size());
  char LastNewLineAddedAs = '\0';
  for (; I != StringRef::npos; I = UnquotedValue.find_first_of(LookupChars)) {
    if (UnquotedValue[I] != '\r' && UnquotedValue[I] != '\n') {
      llvm::append_range(Storage, UnquotedValue.take_front(I));
      UnquotedValue = UnescapeCallback(UnquotedValue.drop_front(I), Storage);
      LastNewLineAddedAs = '\0';
      continue;
    }
    if (size_t LastNonSWhite = UnquotedValue.find_last_not_of(" \t", I);
        LastNonSWhite != StringRef::npos) {
      llvm::append_range(Storage, UnquotedValue.take_front(LastNonSWhite + 1));
      Storage.push_back(' ');
      LastNewLineAddedAs = ' ';
    } else {
      switch (LastNewLineAddedAs) {
      case ' ':
        assert(!Storage.empty() && Storage.back() == ' ');
        Storage.back() = '\n';
        LastNewLineAddedAs = '\n';
        break;
      case '\n':
        assert(!Storage.empty() && Storage.back() == '\n');
        Storage.push_back('\n');
        break;
      default:
        Storage.push_back(' ');
        LastNewLineAddedAs = ' ';
        break;
      }
    }
    if (UnquotedValue.substr(I, 2) == "\r\n")
      I++;
    UnquotedValue = UnquotedValue.drop_front(I + 1).ltrim(" \t");
  }
  llvm::append_range(Storage, UnquotedValue);
  return StringRef(Storage.begin(), Storage.size());
}

} // namespace detail

/// Scans YAML tokens from a MemoryBuffer.
class Scanner {
public:
  Scanner(StringRef Input, SourceMgr &SM, bool ShowColors = true, int *EC = 0);
  Scanner(MemoryBufferRef Buffer, SourceMgr &SM_, bool ShowColors = true,
          int *EC = 0);

  Token &peekNext();
  Token getNext();

  void printError(SMLoc Loc, SourceMgr::DiagKind Kind, const Twine &Message,
                  ArrayRef<SMRange> Ranges = {}) {
    SM.PrintMessage(Loc, Kind, Message, Ranges, /* FixIts= */ {}, ShowColors);
  }

  void setError(const Twine &Message, StringRef::iterator Position) {
    if (Position >= End)
      Position = End - 1;
    if (EC)
      *EC = EINVAL;
    if (!Failed)
      printError(SMLoc::getFromPointer(Position), SourceMgr::DK_Error, Message);
    Failed = true;
  }

  bool failed() { return Failed; }

private:
  void init(MemoryBufferRef Buffer);

  StringRef currentInput() { return StringRef(Current, End - Current); }

  detail::UTF8Decoded decodeUTF8(StringRef::iterator Position) {
    return detail::decodeUTF8(StringRef(Position, End - Position));
  }

  StringRef::iterator skip_nb_char(StringRef::iterator Position);
  StringRef::iterator skip_b_break(StringRef::iterator Position);
  StringRef::iterator skip_s_space(StringRef::iterator Position);
  StringRef::iterator skip_s_white(StringRef::iterator Position);
  StringRef::iterator skip_ns_char(StringRef::iterator Position);

  using SkipWhileFunc = StringRef::iterator (Scanner::*)(StringRef::iterator);

  StringRef::iterator skip_while(SkipWhileFunc Func,
                                 StringRef::iterator Position);
  void advanceWhile(SkipWhileFunc Func);
  void advanceWhile_nb();
  void advanceWhile_s_space();
  void scan_ns_uri_char();
  bool consume(uint32_t Expected);
  void skip(uint32_t Distance);
  bool isBlankOrBreak(StringRef::iterator Position);
  bool isPlainSafeNonBlank(StringRef::iterator Position);
  bool isLineEmpty(StringRef Line);
  bool consumeLineBreakIfPresent();
  void saveSimpleKeyCandidate(TokenQueueT::iterator Tok, unsigned AtColumn,
                              bool IsRequired);
  void removeStaleSimpleKeyCandidates();
  void removeSimpleKeyCandidatesOnFlowLevel(unsigned Level);
  bool unrollIndent(int ToColumn);
  bool rollIndent(int ToColumn, Token::TokenKind Kind,
                  TokenQueueT::iterator InsertPoint);
  void skipComment();
  void scanToNextToken();
  bool scanStreamStart();
  bool scanStreamEnd();
  bool scanDirective();
  bool scanDocumentIndicator(bool IsStart);
  bool scanFlowCollectionStart(bool IsSequence);
  bool scanFlowCollectionEnd(bool IsSequence);
  bool scanFlowEntry();
  bool scanBlockEntry();
  bool scanKey();
  bool scanValue();
  bool scanFlowScalar(bool IsDoubleQuoted);
  bool scanPlainScalar();
  bool scanAliasOrAnchor(bool IsAlias);
  bool scanBlockScalar(bool IsLiteral);
  bool scanBlockScalarIndicators(char &StyleIndicator, char &ChompingIndicator,
                                 unsigned &IndentIndicator, bool &IsDone);
  char scanBlockStyleIndicator();
  char scanBlockChompingIndicator();
  unsigned scanBlockIndentationIndicator();
  bool scanBlockScalarHeader(char &ChompingIndicator, unsigned &IndentIndicator,
                             bool &IsDone);
  bool findBlockScalarIndent(unsigned &BlockIndent, unsigned BlockExitIndent,
                             unsigned &LineBreaks, bool &IsDone);
  bool scanBlockScalarIndent(unsigned BlockIndent, unsigned BlockExitIndent,
                             bool &IsDone);
  bool scanTag();
  bool fetchMoreTokens();

  SourceMgr &SM;
  MemoryBufferRef InputBuffer;
  StringRef::iterator Current;
  StringRef::iterator End;
  int Indent;
  unsigned Column;
  unsigned Line;
  unsigned FlowLevel;
  bool IsStartOfStream;
  bool IsSimpleKeyAllowed;
  bool IsAdjacentValueAllowedInFlow;
  bool Failed;
  bool ShowColors;
  TokenQueueT TokenQueue;
  SmallVector<int, 4> Indents;
  SmallVector<SimpleKey, 4> SimpleKeys;
  int *EC;
};

//===----------------------------------------------------------------------===//
// Inline implementations - vtable anchors
//===----------------------------------------------------------------------===//

inline void Node::anchor() {}
inline void NullNode::anchor() {}
inline void ScalarNode::anchor() {}
inline void BlockScalarNode::anchor() {}
inline void KeyValueNode::anchor() {}
inline void MappingNode::anchor() {}
inline void SequenceNode::anchor() {}
inline void AliasNode::anchor() {}

//===----------------------------------------------------------------------===//
// Inline implementations - free functions
//===----------------------------------------------------------------------===//

inline bool dumpTokens(StringRef Input, raw_ostream &OS) {
  SourceMgr SM;
  Scanner scanner(Input, SM);
  while (true) {
    Token T = scanner.getNext();
    switch (T.Kind) {
    case Token::TK_StreamStart:
      OS << "Stream-Start: ";
      break;
    case Token::TK_StreamEnd:
      OS << "Stream-End: ";
      break;
    case Token::TK_VersionDirective:
      OS << "Version-Directive: ";
      break;
    case Token::TK_TagDirective:
      OS << "Tag-Directive: ";
      break;
    case Token::TK_DocumentStart:
      OS << "Document-Start: ";
      break;
    case Token::TK_DocumentEnd:
      OS << "Document-End: ";
      break;
    case Token::TK_BlockEntry:
      OS << "Block-Entry: ";
      break;
    case Token::TK_BlockEnd:
      OS << "Block-End: ";
      break;
    case Token::TK_BlockSequenceStart:
      OS << "Block-Sequence-Start: ";
      break;
    case Token::TK_BlockMappingStart:
      OS << "Block-Mapping-Start: ";
      break;
    case Token::TK_FlowEntry:
      OS << "Flow-Entry: ";
      break;
    case Token::TK_FlowSequenceStart:
      OS << "Flow-Sequence-Start: ";
      break;
    case Token::TK_FlowSequenceEnd:
      OS << "Flow-Sequence-End: ";
      break;
    case Token::TK_FlowMappingStart:
      OS << "Flow-Mapping-Start: ";
      break;
    case Token::TK_FlowMappingEnd:
      OS << "Flow-Mapping-End: ";
      break;
    case Token::TK_Key:
      OS << "Key: ";
      break;
    case Token::TK_Value:
      OS << "Value: ";
      break;
    case Token::TK_Scalar:
      OS << "Scalar: ";
      break;
    case Token::TK_BlockScalar:
      OS << "Block Scalar: ";
      break;
    case Token::TK_Alias:
      OS << "Alias: ";
      break;
    case Token::TK_Anchor:
      OS << "Anchor: ";
      break;
    case Token::TK_Tag:
      OS << "Tag: ";
      break;
    case Token::TK_Error:
      break;
    }
    OS << T.Range << "\n";
    if (T.Kind == Token::TK_StreamEnd)
      break;
    else if (T.Kind == Token::TK_Error)
      return false;
  }
  return true;
}

inline bool scanTokens(StringRef Input) {
  SourceMgr SM;
  Scanner scanner(Input, SM);
  while (true) {
    Token T = scanner.getNext();
    if (T.Kind == Token::TK_StreamEnd)
      break;
    else if (T.Kind == Token::TK_Error)
      return false;
  }
  return true;
}

inline SmallString<256> escape(StringRef Input, bool EscapePrintable) {
  char buf[8192];
  size_t n = csupport_yaml_escape(Input.data(), Input.size(),
                                  EscapePrintable ? 1 : 0, buf, sizeof(buf));
  return SmallString<256>(StringRef(buf, n));
}

inline int parseBool(StringRef S) {
  return csupport_yaml_parse_bool(S.data(), S.size());
}

//===----------------------------------------------------------------------===//
// Inline implementations - Scanner
//===----------------------------------------------------------------------===//

inline Scanner::Scanner(StringRef Input, SourceMgr &sm, bool ShowColors,
                        int *EC)
    : SM(sm), ShowColors(ShowColors), EC(EC) {
  init(MemoryBufferRef(Input, "YAML"));
}

inline Scanner::Scanner(MemoryBufferRef Buffer, SourceMgr &SM_, bool ShowColors,
                        int *EC)
    : SM(SM_), ShowColors(ShowColors), EC(EC) {
  init(Buffer);
}

inline void Scanner::init(MemoryBufferRef Buffer) {
  InputBuffer = Buffer;
  Current = InputBuffer.getBufferStart();
  End = InputBuffer.getBufferEnd();
  Indent = -1;
  Column = 0;
  Line = 0;
  FlowLevel = 0;
  IsStartOfStream = true;
  IsSimpleKeyAllowed = true;
  IsAdjacentValueAllowedInFlow = false;
  Failed = false;
  std::unique_ptr<MemoryBuffer> InputBufferOwner =
      MemoryBuffer::getMemBuffer(Buffer, /*RequiresNullTerminator=*/false);
  SM.AddNewSourceBuffer(std::move(InputBufferOwner), SMLoc());
}

inline Token &Scanner::peekNext() {
  bool NeedMore = false;
  while (true) {
    if (TokenQueue.empty() || NeedMore) {
      if (!fetchMoreTokens()) {
        TokenQueue.clear();
        SimpleKeys.clear();
        TokenQueue.push_back(Token());
        return TokenQueue.front();
      }
    }
    assert(!TokenQueue.empty() && "fetchMoreTokens lied about getting tokens!");

    removeStaleSimpleKeyCandidates();
    SimpleKey SK;
    SK.Tok = TokenQueue.begin();
    if (!is_contained(SimpleKeys, SK))
      break;
    else
      NeedMore = true;
  }
  return TokenQueue.front();
}

inline Token Scanner::getNext() {
  Token Ret = peekNext();
  if (!TokenQueue.empty())
    TokenQueue.pop_front();
  if (TokenQueue.empty())
    TokenQueue.resetAlloc();
  return Ret;
}

inline StringRef::iterator Scanner::skip_nb_char(StringRef::iterator Position) {
  if (Position == End)
    return Position;
  int n = csupport_yaml_is_nb_char(Position, End);
  if (n > 0)
    return Position + n;
  return Position;
}

inline StringRef::iterator Scanner::skip_b_break(StringRef::iterator Position) {
  if (Position == End)
    return Position;
  int n = csupport_yaml_is_b_break(Position, End);
  if (n > 0)
    return Position + n;
  return Position;
}

inline StringRef::iterator Scanner::skip_s_space(StringRef::iterator Position) {
  if (Position == End)
    return Position;
  if (*Position == ' ')
    return Position + 1;
  return Position;
}

inline StringRef::iterator Scanner::skip_s_white(StringRef::iterator Position) {
  if (Position == End)
    return Position;
  if (*Position == ' ' || *Position == '\t')
    return Position + 1;
  return Position;
}

inline StringRef::iterator Scanner::skip_ns_char(StringRef::iterator Position) {
  if (Position == End)
    return Position;
  if (*Position == ' ' || *Position == '\t')
    return Position;
  return skip_nb_char(Position);
}

inline StringRef::iterator Scanner::skip_while(SkipWhileFunc Func,
                                               StringRef::iterator Position) {
  while (true) {
    StringRef::iterator i = (this->*Func)(Position);
    if (i == Position)
      break;
    Position = i;
  }
  return Position;
}

inline void Scanner::advanceWhile(SkipWhileFunc Func) {
  auto Final = skip_while(Func, Current);
  Column += (unsigned)(Final - Current);
  Current = Final;
}

inline void Scanner::advanceWhile_nb() {
  size_t n = csupport_yaml_skip_while_nb_char(Current, End);
  Column += (unsigned)n;
  Current += n;
}

inline void Scanner::advanceWhile_s_space() {
  size_t n = csupport_yaml_skip_while_s_space(Current, End);
  Column += (unsigned)n;
  Current += n;
}

inline void Scanner::scan_ns_uri_char() {
  while (Current != End) {
    if (csupport_yaml_is_ns_uri_char(*Current, Current + 1, End)) {
      if (*Current == '%') {
        Current += 3;
        Column += 3;
      } else {
        ++Current;
        ++Column;
      }
    } else
      break;
  }
}

inline bool Scanner::consume(uint32_t Expected) {
  if (Expected >= 0x80) {
    setError("Cannot consume non-ascii characters", Current);
    return false;
  }
  if (Current == End)
    return false;
  if (uint8_t(*Current) >= 0x80) {
    setError("Cannot consume non-ascii characters", Current);
    return false;
  }
  if (uint8_t(*Current) == Expected) {
    ++Current;
    ++Column;
    return true;
  }
  return false;
}

inline void Scanner::skip(uint32_t Distance) {
  Current += Distance;
  Column += Distance;
  assert(Current <= End && "Skipped past the end");
}

inline bool Scanner::isBlankOrBreak(StringRef::iterator Position) {
  if (Position == End)
    return false;
  return csupport_yaml_is_blank_or_break(*Position);
}

inline bool Scanner::isPlainSafeNonBlank(StringRef::iterator Position) {
  if (Position == End)
    return false;
  return csupport_yaml_is_plain_safe_non_blank(*Position, FlowLevel) != 0;
}

inline bool Scanner::isLineEmpty(StringRef Line) {
  return csupport_yaml_is_line_empty(Line.data(), Line.size()) != 0;
}

inline bool Scanner::consumeLineBreakIfPresent() {
  auto Next = skip_b_break(Current);
  if (Next == Current)
    return false;
  Column = 0;
  ++Line;
  Current = Next;
  return true;
}

inline void Scanner::saveSimpleKeyCandidate(TokenQueueT::iterator Tok,
                                            unsigned AtColumn,
                                            bool IsRequired) {
  if (IsSimpleKeyAllowed) {
    SimpleKey SK;
    SK.Tok = Tok;
    SK.Line = Line;
    SK.Column = AtColumn;
    SK.IsRequired = IsRequired;
    SK.FlowLevel = FlowLevel;
    SimpleKeys.push_back(SK);
  }
}

inline void Scanner::removeStaleSimpleKeyCandidates() {
  for (SmallVectorImpl<SimpleKey>::iterator i = SimpleKeys.begin();
       i != SimpleKeys.end();) {
    if (i->Line != Line || i->Column + 1024 < Column) {
      if (i->IsRequired)
        setError("Could not find expected : for simple key",
                 i->Tok->Range.begin());
      i = SimpleKeys.erase(i);
    } else
      ++i;
  }
}

inline void Scanner::removeSimpleKeyCandidatesOnFlowLevel(unsigned Level) {
  if (!SimpleKeys.empty() && (SimpleKeys.end() - 1)->FlowLevel == Level)
    SimpleKeys.pop_back();
}

inline bool Scanner::unrollIndent(int ToColumn) {
  Token T;
  if (FlowLevel != 0)
    return true;
  while (Indent > ToColumn) {
    T.Kind = Token::TK_BlockEnd;
    T.Range = StringRef(Current, 1);
    TokenQueue.push_back(T);
    Indent = Indents.pop_back_val();
  }
  return true;
}

inline bool Scanner::rollIndent(int ToColumn, Token::TokenKind Kind,
                                TokenQueueT::iterator InsertPoint) {
  if (FlowLevel)
    return true;
  if (Indent < ToColumn) {
    Indents.push_back(Indent);
    Indent = ToColumn;
    Token T;
    T.Kind = Kind;
    T.Range = StringRef(Current, 0);
    TokenQueue.insert(InsertPoint, T);
  }
  return true;
}

inline void Scanner::skipComment() {
  if (Current == End || *Current != '#')
    return;
  while (true) {
    StringRef::iterator I = skip_nb_char(Current);
    if (I == Current)
      break;
    Current = I;
    ++Column;
  }
}

inline void Scanner::scanToNextToken() {
  while (true) {
    while (Current != End && (*Current == ' ' || *Current == '\t')) {
      skip(1);
    }
    skipComment();
    StringRef::iterator i = skip_b_break(Current);
    if (i == Current)
      break;
    Current = i;
    ++Line;
    Column = 0;
    if (!FlowLevel)
      IsSimpleKeyAllowed = true;
  }
}

inline bool Scanner::scanStreamStart() {
  IsStartOfStream = false;
  detail::EncodingInfo EI = detail::getUnicodeEncoding(currentInput());
  Token T;
  T.Kind = Token::TK_StreamStart;
  T.Range = StringRef(Current, EI.second);
  TokenQueue.push_back(T);
  Current += EI.second;
  return true;
}

inline bool Scanner::scanStreamEnd() {
  if (Column != 0) {
    Column = 0;
    ++Line;
  }
  unrollIndent(-1);
  SimpleKeys.clear();
  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = false;
  Token T;
  T.Kind = Token::TK_StreamEnd;
  T.Range = StringRef(Current, 0);
  TokenQueue.push_back(T);
  return true;
}

inline bool Scanner::scanDirective() {
  unrollIndent(-1);
  SimpleKeys.clear();
  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = false;
  StringRef::iterator Start = Current;
  consume('%');
  StringRef::iterator NameStart = Current;
  Current += csupport_yaml_skip_while_ns_char(Current, End);
  StringRef Name(NameStart, Current - NameStart);
  Current += csupport_yaml_skip_while_s_white(Current, End);
  Token T;
  if (Name == "YAML") {
    Current += csupport_yaml_skip_while_ns_char(Current, End);
    T.Kind = Token::TK_VersionDirective;
    T.Range = StringRef(Start, Current - Start);
    TokenQueue.push_back(T);
    return true;
  } else if (Name == "TAG") {
    Current += csupport_yaml_skip_while_ns_char(Current, End);
    Current += csupport_yaml_skip_while_s_white(Current, End);
    Current += csupport_yaml_skip_while_ns_char(Current, End);
    T.Kind = Token::TK_TagDirective;
    T.Range = StringRef(Start, Current - Start);
    TokenQueue.push_back(T);
    return true;
  }
  return false;
}

inline bool Scanner::scanDocumentIndicator(bool IsStart) {
  unrollIndent(-1);
  SimpleKeys.clear();
  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = false;
  Token T;
  T.Kind = IsStart ? Token::TK_DocumentStart : Token::TK_DocumentEnd;
  T.Range = StringRef(Current, 3);
  skip(3);
  TokenQueue.push_back(T);
  return true;
}

inline bool Scanner::scanFlowCollectionStart(bool IsSequence) {
  Token T;
  T.Kind =
      IsSequence ? Token::TK_FlowSequenceStart : Token::TK_FlowMappingStart;
  T.Range = StringRef(Current, 1);
  skip(1);
  TokenQueue.push_back(T);
  saveSimpleKeyCandidate(--TokenQueue.end(), Column - 1, false);
  IsSimpleKeyAllowed = true;
  IsAdjacentValueAllowedInFlow = false;
  ++FlowLevel;
  return true;
}

inline bool Scanner::scanFlowCollectionEnd(bool IsSequence) {
  removeSimpleKeyCandidatesOnFlowLevel(FlowLevel);
  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = true;
  Token T;
  T.Kind = IsSequence ? Token::TK_FlowSequenceEnd : Token::TK_FlowMappingEnd;
  T.Range = StringRef(Current, 1);
  skip(1);
  TokenQueue.push_back(T);
  if (FlowLevel)
    --FlowLevel;
  return true;
}

inline bool Scanner::scanFlowEntry() {
  removeSimpleKeyCandidatesOnFlowLevel(FlowLevel);
  IsSimpleKeyAllowed = true;
  IsAdjacentValueAllowedInFlow = false;
  Token T;
  T.Kind = Token::TK_FlowEntry;
  T.Range = StringRef(Current, 1);
  skip(1);
  TokenQueue.push_back(T);
  return true;
}

inline bool Scanner::scanBlockEntry() {
  rollIndent(Column, Token::TK_BlockSequenceStart, TokenQueue.end());
  removeSimpleKeyCandidatesOnFlowLevel(FlowLevel);
  IsSimpleKeyAllowed = true;
  IsAdjacentValueAllowedInFlow = false;
  Token T;
  T.Kind = Token::TK_BlockEntry;
  T.Range = StringRef(Current, 1);
  skip(1);
  TokenQueue.push_back(T);
  return true;
}

inline bool Scanner::scanKey() {
  if (!FlowLevel)
    rollIndent(Column, Token::TK_BlockMappingStart, TokenQueue.end());
  removeSimpleKeyCandidatesOnFlowLevel(FlowLevel);
  IsSimpleKeyAllowed = !FlowLevel;
  IsAdjacentValueAllowedInFlow = false;
  Token T;
  T.Kind = Token::TK_Key;
  T.Range = StringRef(Current, 1);
  skip(1);
  TokenQueue.push_back(T);
  return true;
}

inline bool Scanner::scanValue() {
  if (!SimpleKeys.empty()) {
    SimpleKey SK = SimpleKeys.pop_back_val();
    Token T;
    T.Kind = Token::TK_Key;
    T.Range = SK.Tok->Range;
    TokenQueueT::iterator i, e;
    for (i = TokenQueue.begin(), e = TokenQueue.end(); i != e; ++i) {
      if (i == SK.Tok)
        break;
    }
    if (i == e) {
      Failed = true;
      return false;
    }
    i = TokenQueue.insert(i, T);
    rollIndent(SK.Column, Token::TK_BlockMappingStart, i);
    IsSimpleKeyAllowed = false;
  } else {
    if (!FlowLevel)
      rollIndent(Column, Token::TK_BlockMappingStart, TokenQueue.end());
    IsSimpleKeyAllowed = !FlowLevel;
  }
  IsAdjacentValueAllowedInFlow = false;
  Token T;
  T.Kind = Token::TK_Value;
  T.Range = StringRef(Current, 1);
  skip(1);
  TokenQueue.push_back(T);
  return true;
}

inline bool Scanner::scanFlowScalar(bool IsDoubleQuoted) {
  StringRef::iterator Start = Current;
  unsigned ColStart = Column;
  if (IsDoubleQuoted) {
    do {
      ++Current;
      while (Current != End && *Current != '"')
        ++Current;
    } while (Current != End && *(Current - 1) == '\\' &&
             detail::wasEscaped(Start + 1, Current));
  } else {
    skip(1);
    while (Current != End) {
      if (Current + 1 < End && *Current == '\'' && *(Current + 1) == '\'') {
        skip(2);
        continue;
      } else if (*Current == '\'')
        break;
      StringRef::iterator i = skip_nb_char(Current);
      if (i == Current) {
        i = skip_b_break(Current);
        if (i == Current)
          break;
        Current = i;
        Column = 0;
        ++Line;
      } else {
        if (i == End)
          break;
        Current = i;
        ++Column;
      }
    }
  }

  if (Current == End) {
    setError("Expected quote at end of scalar", Current);
    return false;
  }

  skip(1);
  Token T;
  T.Kind = Token::TK_Scalar;
  T.Range = StringRef(Start, Current - Start);
  TokenQueue.push_back(T);
  saveSimpleKeyCandidate(--TokenQueue.end(), ColStart, false);
  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = true;
  return true;
}

inline bool Scanner::scanPlainScalar() {
  StringRef::iterator Start = Current;
  unsigned ColStart = Column;
  unsigned LeadingBlanks = 0;
  assert(Indent >= -1 && "Indent must be >= -1 !");
  unsigned indent = (unsigned)(Indent + 1);
  while (Current != End) {
    if (*Current == '#')
      break;
    while (Current != End &&
           ((*Current != ':' && isPlainSafeNonBlank(Current)) ||
            (*Current == ':' && isPlainSafeNonBlank(Current + 1)))) {
      StringRef::iterator i = skip_nb_char(Current);
      if (i == Current)
        break;
      Current = i;
      ++Column;
    }
    if (!isBlankOrBreak(Current))
      break;
    StringRef::iterator Tmp = Current;
    while (isBlankOrBreak(Tmp)) {
      StringRef::iterator i = skip_s_white(Tmp);
      if (i != Tmp) {
        if (LeadingBlanks && (Column < indent) && *Tmp == '\t') {
          setError("Found invalid tab character in indentation", Tmp);
          return false;
        }
        Tmp = i;
        ++Column;
      } else {
        i = skip_b_break(Tmp);
        if (!LeadingBlanks)
          LeadingBlanks = 1;
        Tmp = i;
        Column = 0;
        ++Line;
      }
    }
    if (!FlowLevel && Column < indent)
      break;
    Current = Tmp;
  }
  if (Start == Current) {
    setError("Got empty plain scalar", Start);
    return false;
  }
  Token T;
  T.Kind = Token::TK_Scalar;
  T.Range = StringRef(Start, Current - Start);
  TokenQueue.push_back(T);
  saveSimpleKeyCandidate(--TokenQueue.end(), ColStart, false);
  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = false;
  return true;
}

inline bool Scanner::scanAliasOrAnchor(bool IsAlias) {
  StringRef::iterator Start = Current;
  unsigned ColStart = Column;
  skip(1);
  while (Current != End) {
    if (*Current == '[' || *Current == ']' || *Current == '{' ||
        *Current == '}' || *Current == ',' || *Current == ':')
      break;
    StringRef::iterator i = skip_ns_char(Current);
    if (i == Current)
      break;
    Current = i;
    ++Column;
  }
  if (Start + 1 == Current) {
    setError("Got empty alias or anchor", Start);
    return false;
  }
  Token T;
  T.Kind = IsAlias ? Token::TK_Alias : Token::TK_Anchor;
  T.Range = StringRef(Start, Current - Start);
  TokenQueue.push_back(T);
  saveSimpleKeyCandidate(--TokenQueue.end(), ColStart, false);
  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = false;
  return true;
}

inline bool Scanner::scanBlockScalarIndicators(char &StyleIndicator,
                                               char &ChompingIndicator,
                                               unsigned &IndentIndicator,
                                               bool &IsDone) {
  StyleIndicator = scanBlockStyleIndicator();
  if (!scanBlockScalarHeader(ChompingIndicator, IndentIndicator, IsDone))
    return false;
  return true;
}

inline char Scanner::scanBlockStyleIndicator() {
  char Indicator = ' ';
  if (Current != End && (*Current == '>' || *Current == '|')) {
    Indicator = *Current;
    skip(1);
  }
  return Indicator;
}

inline char Scanner::scanBlockChompingIndicator() {
  char Indicator = ' ';
  if (Current != End && (*Current == '+' || *Current == '-')) {
    Indicator = *Current;
    skip(1);
  }
  return Indicator;
}

inline unsigned Scanner::scanBlockIndentationIndicator() {
  unsigned Indent = 0;
  if (Current != End && (*Current >= '1' && *Current <= '9')) {
    Indent = unsigned(*Current - '0');
    skip(1);
  }
  return Indent;
}

inline bool Scanner::scanBlockScalarHeader(char &ChompingIndicator,
                                           unsigned &IndentIndicator,
                                           bool &IsDone) {
  auto Start = Current;
  ChompingIndicator = scanBlockChompingIndicator();
  IndentIndicator = scanBlockIndentationIndicator();
  if (ChompingIndicator == ' ')
    ChompingIndicator = scanBlockChompingIndicator();
  Current += csupport_yaml_skip_while_s_white(Current, End);
  skipComment();
  if (Current == End) {
    Token T;
    T.Kind = Token::TK_BlockScalar;
    T.Range = StringRef(Start, Current - Start);
    TokenQueue.push_back(T);
    IsDone = true;
    return true;
  }
  if (!consumeLineBreakIfPresent()) {
    setError("Expected a line break after block scalar header", Current);
    return false;
  }
  return true;
}

inline bool Scanner::findBlockScalarIndent(unsigned &BlockIndent,
                                           unsigned BlockExitIndent,
                                           unsigned &LineBreaks, bool &IsDone) {
  unsigned MaxAllSpaceLineCharacters = 0;
  StringRef::iterator LongestAllSpaceLine;
  while (true) {
    advanceWhile_s_space();
    if (skip_nb_char(Current) != Current) {
      if (Column <= BlockExitIndent) {
        IsDone = true;
        return true;
      }
      BlockIndent = Column;
      if (MaxAllSpaceLineCharacters > BlockIndent) {
        setError(
            "Leading all-spaces line must be smaller than the block indent",
            LongestAllSpaceLine);
        return false;
      }
      return true;
    }
    if (skip_b_break(Current) != Current &&
        Column > MaxAllSpaceLineCharacters) {
      MaxAllSpaceLineCharacters = Column;
      LongestAllSpaceLine = Current;
    }
    if (Current == End) {
      IsDone = true;
      return true;
    }
    if (!consumeLineBreakIfPresent()) {
      IsDone = true;
      return true;
    }
    ++LineBreaks;
  }
  return true;
}

inline bool Scanner::scanBlockScalarIndent(unsigned BlockIndent,
                                           unsigned BlockExitIndent,
                                           bool &IsDone) {
  while (Column < BlockIndent) {
    auto I = skip_s_space(Current);
    if (I == Current)
      break;
    Current = I;
    ++Column;
  }
  if (skip_nb_char(Current) == Current)
    return true;
  if (Column <= BlockExitIndent) {
    IsDone = true;
    return true;
  }
  if (Column < BlockIndent) {
    if (Current != End && *Current == '#') {
      IsDone = true;
      return true;
    }
    setError("A text line is less indented than the block scalar", Current);
    return false;
  }
  return true;
}

inline bool Scanner::scanBlockScalar(bool IsLiteral) {
  assert(*Current == '|' || *Current == '>');
  char StyleIndicator;
  char ChompingIndicator;
  unsigned BlockIndent;
  bool IsDone = false;
  if (!scanBlockScalarIndicators(StyleIndicator, ChompingIndicator, BlockIndent,
                                 IsDone))
    return false;
  if (IsDone)
    return true;
  bool IsFolded = StyleIndicator == '>';
  const auto *Start = Current;
  unsigned BlockExitIndent = Indent < 0 ? 0 : (unsigned)Indent;
  unsigned LineBreaks = 0;
  if (BlockIndent == 0) {
    if (!findBlockScalarIndent(BlockIndent, BlockExitIndent, LineBreaks,
                               IsDone))
      return false;
  }
  SmallString<256> Str;
  while (!IsDone) {
    if (!scanBlockScalarIndent(BlockIndent, BlockExitIndent, IsDone))
      return false;
    if (IsDone)
      break;
    auto LineStart = Current;
    advanceWhile_nb();
    if (LineStart != Current) {
      if (LineBreaks && IsFolded && !Scanner::isLineEmpty(Str)) {
        if (LineBreaks == 1) {
          Str.append(LineBreaks,
                     isLineEmpty(StringRef(LineStart, Current - LineStart))
                         ? '\n'
                         : ' ');
        }
        LineBreaks--;
      }
      Str.append(LineBreaks, '\n');
      Str.append(StringRef(LineStart, Current - LineStart));
      LineBreaks = 0;
    }
    if (Current == End)
      break;
    if (!consumeLineBreakIfPresent())
      break;
    ++LineBreaks;
  }
  if (Current == End && !LineBreaks)
    LineBreaks = 1;
  Str.append(csupport_yaml_get_chomped_line_breaks(
                 ChompingIndicator, LineBreaks, Str.data(), Str.size()),
             '\n');
  if (!FlowLevel)
    IsSimpleKeyAllowed = true;
  IsAdjacentValueAllowedInFlow = false;
  Token T;
  T.Kind = Token::TK_BlockScalar;
  T.Range = StringRef(Start, Current - Start);
  T.Value = Str;
  TokenQueue.push_back(T);
  return true;
}

inline bool Scanner::scanTag() {
  StringRef::iterator Start = Current;
  unsigned ColStart = Column;
  skip(1);
  if (Current == End || isBlankOrBreak(Current))
    ;
  else if (*Current == '<') {
    skip(1);
    scan_ns_uri_char();
    if (!consume('>'))
      return false;
  } else {
    Current += csupport_yaml_skip_while_ns_char(Current, End);
  }
  Token T;
  T.Kind = Token::TK_Tag;
  T.Range = StringRef(Start, Current - Start);
  TokenQueue.push_back(T);
  saveSimpleKeyCandidate(--TokenQueue.end(), ColStart, false);
  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = false;
  return true;
}

inline bool Scanner::fetchMoreTokens() {
  if (IsStartOfStream)
    return scanStreamStart();
  scanToNextToken();
  if (Current == End)
    return scanStreamEnd();
  removeStaleSimpleKeyCandidates();
  unrollIndent(Column);
  if (Column == 0 && *Current == '%')
    return scanDirective();
  if (Column == 0 && Current + 4 <= End && *Current == '-' &&
      *(Current + 1) == '-' && *(Current + 2) == '-' &&
      (Current + 3 == End || isBlankOrBreak(Current + 3)))
    return scanDocumentIndicator(true);
  if (Column == 0 && Current + 4 <= End && *Current == '.' &&
      *(Current + 1) == '.' && *(Current + 2) == '.' &&
      (Current + 3 == End || isBlankOrBreak(Current + 3)))
    return scanDocumentIndicator(false);
  if (*Current == '[')
    return scanFlowCollectionStart(true);
  if (*Current == '{')
    return scanFlowCollectionStart(false);
  if (*Current == ']')
    return scanFlowCollectionEnd(true);
  if (*Current == '}')
    return scanFlowCollectionEnd(false);
  if (*Current == ',')
    return scanFlowEntry();
  if (*Current == '-' && (isBlankOrBreak(Current + 1) || Current + 1 == End))
    return scanBlockEntry();
  if (*Current == '?' && (Current + 1 == End || isBlankOrBreak(Current + 1)))
    return scanKey();
  if (*Current == ':' &&
      (!isPlainSafeNonBlank(Current + 1) || IsAdjacentValueAllowedInFlow))
    return scanValue();
  if (*Current == '*')
    return scanAliasOrAnchor(true);
  if (*Current == '&')
    return scanAliasOrAnchor(false);
  if (*Current == '!')
    return scanTag();
  if (*Current == '|' && !FlowLevel)
    return scanBlockScalar(true);
  if (*Current == '>' && !FlowLevel)
    return scanBlockScalar(false);
  if (*Current == '\'')
    return scanFlowScalar(false);
  if (*Current == '"')
    return scanFlowScalar(true);
  StringRef FirstChar(Current, 1);
  if ((!isBlankOrBreak(Current) &&
       FirstChar.find_first_of("-?:,[]{}#&*!|>'\"%@`") == StringRef::npos) ||
      (FirstChar.find_first_of("?:-") != StringRef::npos &&
       isPlainSafeNonBlank(Current + 1)))
    return scanPlainScalar();
  setError("Unrecognized character while tokenizing.", Current);
  return false;
}

//===----------------------------------------------------------------------===//
// Inline implementations - Stream
//===----------------------------------------------------------------------===//

inline Stream::Stream(StringRef Input, SourceMgr &SM, bool ShowColors, int *EC)
    : scanner(new Scanner(Input, SM, ShowColors, EC)) {}

inline Stream::Stream(MemoryBufferRef InputBuffer, SourceMgr &SM,
                      bool ShowColors, int *EC)
    : scanner(new Scanner(InputBuffer, SM, ShowColors, EC)) {}

inline Stream::~Stream() = default;

inline bool Stream::failed() { return scanner->failed(); }

inline void Stream::printError(Node *N, const Twine &Msg,
                               SourceMgr::DiagKind Kind) {
  printError(N ? N->getSourceRange() : SMRange(), Msg, Kind);
}

inline void Stream::printError(const SMRange &Range, const Twine &Msg,
                               SourceMgr::DiagKind Kind) {
  scanner->printError(Range.Start, Kind, Msg, Range);
}

inline document_iterator Stream::begin() {
  if (CurrentDoc)
    report_fatal_error("Can only iterate over the stream once");
  scanner->getNext();
  CurrentDoc.reset(new Document(*this));
  return document_iterator(CurrentDoc);
}

inline document_iterator Stream::end() { return document_iterator(); }

inline void Stream::skip() {
  for (Document &Doc : *this)
    Doc.skip();
}

//===----------------------------------------------------------------------===//
// Inline implementations - Node
//===----------------------------------------------------------------------===//

inline Node::Node(unsigned int Type, std::unique_ptr<Document> &D, StringRef A,
                  StringRef T)
    : Doc(D), TypeID(Type), Anchor(A), Tag(T) {
  SMLoc Start = SMLoc::getFromPointer(peekNext().Range.begin());
  SourceRange = SMRange(Start, Start);
}

inline SmallString<256> Node::getVerbatimTag() const {
  StringRef Raw = getRawTag();
  if (!Raw.empty() && Raw != "!") {
    SmallString<256> Ret;
    if (Raw.find_last_of('!') == 0) {
      Ret = Doc->getTagMap().find("!")->second;
      Ret += Raw.substr(1);
      return SmallString<256>(Ret.str());
    } else if (Raw.starts_with("!!")) {
      Ret = Doc->getTagMap().find("!!")->second;
      Ret += Raw.substr(2);
      return SmallString<256>(Ret.str());
    } else {
      StringRef TagHandle = Raw.substr(0, Raw.find_last_of('!') + 1);
      auto It = Doc->getTagMap().find(TagHandle);
      if (It != Doc->getTagMap().end())
        Ret = It->second;
      else {
        Token T;
        T.Kind = Token::TK_Tag;
        T.Range = TagHandle;
        setError(Twine("Unknown tag handle ") + TagHandle, T);
      }
      Ret += Raw.substr(Raw.find_last_of('!') + 1);
      return SmallString<256>(Ret.str());
    }
  }
  switch (getType()) {
  case NK_Null:
    return SmallString<256>(StringRef("tag:yaml.org,2002:null"));
  case NK_Scalar:
  case NK_BlockScalar:
    return SmallString<256>(StringRef("tag:yaml.org,2002:str"));
  case NK_Mapping:
    return SmallString<256>(StringRef("tag:yaml.org,2002:map"));
  case NK_Sequence:
    return SmallString<256>(StringRef("tag:yaml.org,2002:seq"));
  }
  return SmallString<256>();
}

inline Token &Node::peekNext() { return Doc->peekNext(); }
inline Token Node::getNext() { return Doc->getNext(); }
inline Node *Node::parseBlockNode() { return Doc->parseBlockNode(); }
inline BumpPtrAllocator &Node::getAllocator() { return Doc->NodeAllocator; }

inline void Node::setError(const Twine &Msg, Token &Tok) const {
  Doc->setError(Msg, Tok);
}

inline bool Node::failed() const { return Doc->failed(); }

//===----------------------------------------------------------------------===//
// Inline implementations - ScalarNode
//===----------------------------------------------------------------------===//

inline StringRef ScalarNode::getValue(SmallVectorImpl<char> &Storage) const {
  if (Value[0] == '"')
    return getDoubleQuotedValue(Value, Storage);
  if (Value[0] == '\'')
    return getSingleQuotedValue(Value, Storage);
  return getPlainValue(Value, Storage);
}

inline StringRef
ScalarNode::getDoubleQuotedValue(StringRef RawValue,
                                 SmallVectorImpl<char> &Storage) const {
  assert(RawValue.size() >= 2 && RawValue.front() == '"' &&
         RawValue.back() == '"');
  StringRef UnquotedValue = RawValue.substr(1, RawValue.size() - 2);

  auto UnescapeFunc = [this](StringRef UnquotedValue,
                             SmallVectorImpl<char> &Storage) {
    assert(UnquotedValue.take_front(1) == "\\");
    if (UnquotedValue.size() == 1) {
      Token T;
      T.Range = UnquotedValue;
      setError("Unrecognized escape code", T);
      Storage.clear();
      return StringRef();
    }
    UnquotedValue = UnquotedValue.drop_front(1);
    switch (UnquotedValue[0]) {
    default: {
      Token T;
      T.Range = UnquotedValue.take_front(1);
      setError("Unrecognized escape code", T);
      Storage.clear();
      return StringRef();
    }
    case '\r':
      if (UnquotedValue.size() >= 2 && UnquotedValue[1] == '\n')
        UnquotedValue = UnquotedValue.drop_front(1);
      __attribute__((fallthrough));
    case '\n':
      return UnquotedValue.drop_front(1).ltrim(" \t");
    case '0':
      Storage.push_back(0x00);
      break;
    case 'a':
      Storage.push_back(0x07);
      break;
    case 'b':
      Storage.push_back(0x08);
      break;
    case 't':
    case 0x09:
      Storage.push_back(0x09);
      break;
    case 'n':
      Storage.push_back(0x0A);
      break;
    case 'v':
      Storage.push_back(0x0B);
      break;
    case 'f':
      Storage.push_back(0x0C);
      break;
    case 'r':
      Storage.push_back(0x0D);
      break;
    case 'e':
      Storage.push_back(0x1B);
      break;
    case ' ':
      Storage.push_back(0x20);
      break;
    case '"':
      Storage.push_back(0x22);
      break;
    case '/':
      Storage.push_back(0x2F);
      break;
    case '\\':
      Storage.push_back(0x5C);
      break;
    case 'N':
      detail::encodeUTF8(0x85, Storage);
      break;
    case '_':
      detail::encodeUTF8(0xA0, Storage);
      break;
    case 'L':
      detail::encodeUTF8(0x2028, Storage);
      break;
    case 'P':
      detail::encodeUTF8(0x2029, Storage);
      break;
    case 'x': {
      if (UnquotedValue.size() < 3)
        break;
      unsigned int UnicodeScalarValue;
      if (UnquotedValue.substr(1, 2).getAsInteger(16, UnicodeScalarValue))
        UnicodeScalarValue = 0xFFFD;
      detail::encodeUTF8(UnicodeScalarValue, Storage);
      return UnquotedValue.drop_front(3);
    }
    case 'u': {
      if (UnquotedValue.size() < 5)
        break;
      unsigned int UnicodeScalarValue;
      if (UnquotedValue.substr(1, 4).getAsInteger(16, UnicodeScalarValue))
        UnicodeScalarValue = 0xFFFD;
      detail::encodeUTF8(UnicodeScalarValue, Storage);
      return UnquotedValue.drop_front(5);
    }
    case 'U': {
      if (UnquotedValue.size() < 9)
        break;
      unsigned int UnicodeScalarValue;
      if (UnquotedValue.substr(1, 8).getAsInteger(16, UnicodeScalarValue))
        UnicodeScalarValue = 0xFFFD;
      detail::encodeUTF8(UnicodeScalarValue, Storage);
      return UnquotedValue.drop_front(9);
    }
    }
    return UnquotedValue.drop_front(1);
  };

  return detail::parseScalarValue(UnquotedValue, Storage, "\\\r\n",
                                  UnescapeFunc);
}

inline StringRef
ScalarNode::getSingleQuotedValue(StringRef RawValue,
                                 SmallVectorImpl<char> &Storage) {
  assert(RawValue.size() >= 2 && RawValue.front() == '\'' &&
         RawValue.back() == '\'');
  StringRef UnquotedValue = RawValue.substr(1, RawValue.size() - 2);

  auto UnescapeFunc = [](StringRef UnquotedValue,
                         SmallVectorImpl<char> &Storage) {
    assert(UnquotedValue.take_front(2) == "''");
    Storage.push_back('\'');
    return UnquotedValue.drop_front(2);
  };

  return detail::parseScalarValue(UnquotedValue, Storage, "'\r\n",
                                  UnescapeFunc);
}

inline StringRef ScalarNode::getPlainValue(StringRef RawValue,
                                           SmallVectorImpl<char> &Storage) {
  RawValue = RawValue.rtrim("\r\n \t");
  return detail::parseScalarValue(RawValue, Storage, "\r\n", 0);
}

//===----------------------------------------------------------------------===//
// Inline implementations - KeyValueNode
//===----------------------------------------------------------------------===//

inline Node *KeyValueNode::getKey() {
  if (Key)
    return Key;
  {
    Token &t = peekNext();
    if (t.Kind == Token::TK_BlockEnd || t.Kind == Token::TK_Value ||
        t.Kind == Token::TK_Error) {
      return Key = new (getAllocator()) NullNode(Doc);
    }
    if (t.Kind == Token::TK_Key)
      getNext();
  }
  Token &t = peekNext();
  if (t.Kind == Token::TK_BlockEnd || t.Kind == Token::TK_Value) {
    return Key = new (getAllocator()) NullNode(Doc);
  }
  return Key = parseBlockNode();
}

inline Node *KeyValueNode::getValue() {
  if (Value)
    return Value;
  if (Node *Key = getKey())
    Key->skip();
  else {
    setError("Null key in Key Value.", peekNext());
    return Value = new (getAllocator()) NullNode(Doc);
  }
  if (failed())
    return Value = new (getAllocator()) NullNode(Doc);
  {
    Token &t = peekNext();
    if (t.Kind == Token::TK_BlockEnd || t.Kind == Token::TK_FlowMappingEnd ||
        t.Kind == Token::TK_Key || t.Kind == Token::TK_FlowEntry ||
        t.Kind == Token::TK_Error) {
      return Value = new (getAllocator()) NullNode(Doc);
    }
    if (t.Kind != Token::TK_Value) {
      setError("Unexpected token in Key Value.", t);
      return Value = new (getAllocator()) NullNode(Doc);
    }
    getNext();
  }
  Token &t = peekNext();
  if (t.Kind == Token::TK_BlockEnd || t.Kind == Token::TK_Key) {
    return Value = new (getAllocator()) NullNode(Doc);
  }
  return Value = parseBlockNode();
}

//===----------------------------------------------------------------------===//
// Inline implementations - MappingNode
//===----------------------------------------------------------------------===//

inline void MappingNode::increment() {
  if (failed()) {
    IsAtEnd = true;
    CurrentEntry = 0;
    return;
  }
  if (CurrentEntry) {
    CurrentEntry->skip();
    if (Type == MT_Inline) {
      IsAtEnd = true;
      CurrentEntry = 0;
      return;
    }
  }
  Token T = peekNext();
  if (T.Kind == Token::TK_Key || T.Kind == Token::TK_Scalar) {
    CurrentEntry = new (getAllocator()) KeyValueNode(Doc);
  } else if (Type == MT_Block) {
    switch (T.Kind) {
    case Token::TK_BlockEnd:
      getNext();
      IsAtEnd = true;
      CurrentEntry = 0;
      break;
    default:
      setError("Unexpected token. Expected Key or Block End", T);
      __attribute__((fallthrough));
    case Token::TK_Error:
      IsAtEnd = true;
      CurrentEntry = 0;
    }
  } else {
    switch (T.Kind) {
    case Token::TK_FlowEntry:
      getNext();
      return increment();
    case Token::TK_FlowMappingEnd:
      getNext();
      __attribute__((fallthrough));
    case Token::TK_Error:
      IsAtEnd = true;
      CurrentEntry = 0;
      break;
    default:
      setError("Unexpected token. Expected Key, Flow Entry, or Flow "
               "Mapping End.",
               T);
      IsAtEnd = true;
      CurrentEntry = 0;
    }
  }
}

//===----------------------------------------------------------------------===//
// Inline implementations - SequenceNode
//===----------------------------------------------------------------------===//

inline void SequenceNode::increment() {
  if (failed()) {
    IsAtEnd = true;
    CurrentEntry = 0;
    return;
  }
  if (CurrentEntry)
    CurrentEntry->skip();
  Token T = peekNext();
  if (SeqType == ST_Block) {
    switch (T.Kind) {
    case Token::TK_BlockEntry:
      getNext();
      CurrentEntry = parseBlockNode();
      if (!CurrentEntry) {
        IsAtEnd = true;
        CurrentEntry = 0;
      }
      break;
    case Token::TK_BlockEnd:
      getNext();
      IsAtEnd = true;
      CurrentEntry = 0;
      break;
    default:
      setError("Unexpected token. Expected Block Entry or Block End.", T);
      __attribute__((fallthrough));
    case Token::TK_Error:
      IsAtEnd = true;
      CurrentEntry = 0;
    }
  } else if (SeqType == ST_Indentless) {
    switch (T.Kind) {
    case Token::TK_BlockEntry:
      getNext();
      CurrentEntry = parseBlockNode();
      if (!CurrentEntry) {
        IsAtEnd = true;
        CurrentEntry = 0;
      }
      break;
    default:
    case Token::TK_Error:
      IsAtEnd = true;
      CurrentEntry = 0;
    }
  } else if (SeqType == ST_Flow) {
    switch (T.Kind) {
    case Token::TK_FlowEntry:
      getNext();
      WasPreviousTokenFlowEntry = true;
      return increment();
    case Token::TK_FlowSequenceEnd:
      getNext();
      __attribute__((fallthrough));
    case Token::TK_Error:
      IsAtEnd = true;
      CurrentEntry = 0;
      break;
    case Token::TK_StreamEnd:
    case Token::TK_DocumentEnd:
    case Token::TK_DocumentStart:
      setError("Could not find closing ]!", T);
      IsAtEnd = true;
      CurrentEntry = 0;
      break;
    default:
      if (!WasPreviousTokenFlowEntry) {
        setError("Expected , between entries!", T);
        IsAtEnd = true;
        CurrentEntry = 0;
        break;
      }
      CurrentEntry = parseBlockNode();
      if (!CurrentEntry) {
        IsAtEnd = true;
      }
      WasPreviousTokenFlowEntry = false;
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// Inline implementations - Document
//===----------------------------------------------------------------------===//

inline Document::Document(Stream &S) : stream(S), Root(0) {
  TagMap["!"] = "!";
  TagMap["!!"] = "tag:yaml.org,2002:";
  if (parseDirectives())
    expectToken(Token::TK_DocumentStart);
  Token &T = peekNext();
  if (T.Kind == Token::TK_DocumentStart)
    getNext();
}

inline bool Document::skip() {
  if (stream.scanner->failed())
    return false;
  if (!Root && !getRoot())
    return false;
  Root->skip();
  Token &T = peekNext();
  if (T.Kind == Token::TK_StreamEnd)
    return false;
  if (T.Kind == Token::TK_DocumentEnd) {
    getNext();
    return skip();
  }
  return true;
}

inline Token &Document::peekNext() { return stream.scanner->peekNext(); }
inline Token Document::getNext() { return stream.scanner->getNext(); }

inline void Document::setError(const Twine &Message, Token &Location) const {
  stream.scanner->setError(Message, Location.Range.begin());
}

inline bool Document::failed() const { return stream.scanner->failed(); }

inline Node *Document::parseBlockNode() {
  Token T = peekNext();
  Token AnchorInfo;
  Token TagInfo;
parse_property:
  switch (T.Kind) {
  case Token::TK_Alias:
    getNext();
    return new (NodeAllocator) AliasNode(stream.CurrentDoc, T.Range.substr(1));
  case Token::TK_Anchor:
    if (AnchorInfo.Kind == Token::TK_Anchor) {
      setError("Already encountered an anchor for this node!", T);
      return 0;
    }
    AnchorInfo = getNext();
    T = peekNext();
    goto parse_property;
  case Token::TK_Tag:
    if (TagInfo.Kind == Token::TK_Tag) {
      setError("Already encountered a tag for this node!", T);
      return 0;
    }
    TagInfo = getNext();
    T = peekNext();
    goto parse_property;
  default:
    break;
  }
  switch (T.Kind) {
  case Token::TK_BlockEntry:
    return new (NodeAllocator)
        SequenceNode(stream.CurrentDoc, AnchorInfo.Range.substr(1),
                     TagInfo.Range, SequenceNode::ST_Indentless);
  case Token::TK_BlockSequenceStart:
    getNext();
    return new (NodeAllocator)
        SequenceNode(stream.CurrentDoc, AnchorInfo.Range.substr(1),
                     TagInfo.Range, SequenceNode::ST_Block);
  case Token::TK_BlockMappingStart:
    getNext();
    return new (NodeAllocator)
        MappingNode(stream.CurrentDoc, AnchorInfo.Range.substr(1),
                    TagInfo.Range, MappingNode::MT_Block);
  case Token::TK_FlowSequenceStart:
    getNext();
    return new (NodeAllocator)
        SequenceNode(stream.CurrentDoc, AnchorInfo.Range.substr(1),
                     TagInfo.Range, SequenceNode::ST_Flow);
  case Token::TK_FlowMappingStart:
    getNext();
    return new (NodeAllocator)
        MappingNode(stream.CurrentDoc, AnchorInfo.Range.substr(1),
                    TagInfo.Range, MappingNode::MT_Flow);
  case Token::TK_Scalar:
    getNext();
    return new (NodeAllocator) ScalarNode(
        stream.CurrentDoc, AnchorInfo.Range.substr(1), TagInfo.Range, T.Range);
  case Token::TK_BlockScalar: {
    getNext();
    StringRef NullTerminatedStr(T.Value.c_str(), T.Value.size() + 1);
    StringRef StrCopy = NullTerminatedStr.copy(NodeAllocator).drop_back();
    return new (NodeAllocator)
        BlockScalarNode(stream.CurrentDoc, AnchorInfo.Range.substr(1),
                        TagInfo.Range, StrCopy, T.Range);
  }
  case Token::TK_Key:
    return new (NodeAllocator)
        MappingNode(stream.CurrentDoc, AnchorInfo.Range.substr(1),
                    TagInfo.Range, MappingNode::MT_Inline);
  case Token::TK_DocumentStart:
  case Token::TK_DocumentEnd:
  case Token::TK_StreamEnd:
  default:
    return new (NodeAllocator) NullNode(stream.CurrentDoc);
  case Token::TK_FlowMappingEnd:
  case Token::TK_FlowSequenceEnd:
  case Token::TK_FlowEntry: {
    if (Root && (isa<MappingNode>(Root) || isa<SequenceNode>(Root)))
      return new (NodeAllocator) NullNode(stream.CurrentDoc);
    setError("Unexpected token", T);
    return 0;
  }
  case Token::TK_Error:
    return 0;
  }
  llvm_unreachable("Control flow shouldn't reach here.");
  return 0;
}

inline bool Document::parseDirectives() {
  bool isDirective = false;
  while (true) {
    Token T = peekNext();
    if (T.Kind == Token::TK_TagDirective) {
      parseTAGDirective();
      isDirective = true;
    } else if (T.Kind == Token::TK_VersionDirective) {
      parseYAMLDirective();
      isDirective = true;
    } else
      break;
  }
  return isDirective;
}

inline void Document::parseYAMLDirective() { getNext(); }

inline void Document::parseTAGDirective() {
  Token Tag = getNext();
  StringRef T = Tag.Range;
  T = T.substr(T.find_first_of(" \t")).ltrim(" \t");
  size_t HandleEnd = T.find_first_of(" \t");
  StringRef TagHandle = T.substr(0, HandleEnd);
  StringRef TagPrefix = T.substr(HandleEnd).ltrim(" \t");
  TagMap[TagHandle] = TagPrefix;
}

inline bool Document::expectToken(int TK) {
  Token T = getNext();
  if (T.Kind != TK) {
    setError("Unexpected token", T);
    return false;
  }
  return true;
}

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_SUPPORT_YAMLPARSER_H

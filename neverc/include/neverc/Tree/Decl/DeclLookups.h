#ifndef NEVERC_TREE_DECLLOOKUPS_H
#define NEVERC_TREE_DECLLOOKUPS_H

#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/DeclBase.h"
#include "neverc/Tree/Decl/DeclContextInternals.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include <cstddef>
#include <iterator>

namespace neverc {

class DeclContext::all_lookups_iterator {
  StoredDeclsMap::iterator It, End;

public:
  using value_type = lookup_result;
  using reference = lookup_result;
  using pointer = lookup_result;
  using iterator_category = std::forward_iterator_tag;
  using difference_type = std::ptrdiff_t;

  all_lookups_iterator() = default;
  all_lookups_iterator(StoredDeclsMap::iterator It,
                       StoredDeclsMap::iterator End)
      : It(It), End(End) {}

  DeclarationName getLookupName() const { return It->first; }

  reference operator*() const { return It->second.getLookupResult(); }
  pointer operator->() const { return It->second.getLookupResult(); }

  all_lookups_iterator &operator++() {
    ++It;
    return *this;
  }

  all_lookups_iterator operator++(int) {
    all_lookups_iterator tmp(*this);
    ++(*this);
    return tmp;
  }

  friend bool operator==(all_lookups_iterator x, all_lookups_iterator y) {
    return x.It == y.It;
  }

  friend bool operator!=(all_lookups_iterator x, all_lookups_iterator y) {
    return x.It != y.It;
  }
};

inline DeclContext::lookups_range DeclContext::lookups() const {
  DeclContext *Primary = const_cast<DeclContext *>(this)->getPrimaryContext();
  if (StoredDeclsMap *Map = Primary->buildLookup())
    return lookups_range(all_lookups_iterator(Map->begin(), Map->end()),
                         all_lookups_iterator(Map->end(), Map->end()));

  // Synthesize an empty range. This requires that two default constructed
  // versions of these iterators form a valid empty range.
  return lookups_range(all_lookups_iterator(), all_lookups_iterator());
}

} // namespace neverc

#endif // NEVERC_TREE_DECLLOOKUPS_H

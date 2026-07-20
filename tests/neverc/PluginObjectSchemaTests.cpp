#include "neverc/Plugin/PluginObject.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <iterator>
#include <set>
#include <string>

namespace {

struct SchemaEntry {
  uint32_t StableID;
  const char *Name;
};

constexpr SchemaEntry SectionKinds[] = {
#define NEVERC_OBJECT_SCHEMA_SECTION_KIND(Symbol, ID, Name) {ID, Name},
#include "neverc/Plugin/Schema/PluginObjectSchema.inc"
#undef NEVERC_OBJECT_SCHEMA_SECTION_KIND
};

constexpr SchemaEntry SymbolTypes[] = {
#define NEVERC_OBJECT_SCHEMA_SYMBOL_TYPE(Symbol, ID, Name) {ID, Name},
#include "neverc/Plugin/Schema/PluginObjectSchema.inc"
#undef NEVERC_OBJECT_SCHEMA_SYMBOL_TYPE
};

constexpr SchemaEntry RelocationKinds[] = {
#define NEVERC_OBJECT_SCHEMA_RELOCATION_KIND(Symbol, ID, Name) {ID, Name},
#include "neverc/Plugin/Schema/PluginObjectSchema.inc"
#undef NEVERC_OBJECT_SCHEMA_RELOCATION_KIND
};

constexpr SchemaEntry ComdatSelections[] = {
#define NEVERC_OBJECT_SCHEMA_COMDAT_SELECTION(Symbol, ID, Name) {ID, Name},
#include "neverc/Plugin/Schema/PluginObjectSchema.inc"
#undef NEVERC_OBJECT_SCHEMA_COMDAT_SELECTION
};

TEST(PluginObjectSchemaTest, CoversStableRelocatableObjectModel) {
  static_assert(NEVERC_OBJECT_ENTITY_COUNT == 7);
  static_assert(std::size(SectionKinds) == NEVERC_OBJECT_SECTION_KIND_COUNT);
  static_assert(std::size(SymbolTypes) == NEVERC_OBJECT_SYMBOL_TYPE_COUNT);
  static_assert(std::size(RelocationKinds) ==
                NEVERC_OBJECT_RELOCATION_KIND_COUNT);
  static_assert(std::size(ComdatSelections) ==
                NEVERC_OBJECT_COMDAT_SELECTION_COUNT);

  EXPECT_EQ(NEVERC_OBJECT_ENTITY_GRAPH, UINT32_C(0x71000001));
  EXPECT_EQ(NEVERC_OBJECT_SECTION_KIND_TEXT, UINT32_C(0x72000001));
  EXPECT_EQ(NEVERC_OBJECT_SECTION_KIND_FORMAT_EXTENSION,
            UINT32_C(0x72000009));
  EXPECT_EQ(NEVERC_OBJECT_SYMBOL_TYPE_TLS, UINT32_C(0x73200005));
  EXPECT_EQ(NEVERC_OBJECT_RELOCATION_TARGET_EXTENSION,
            UINT32_C(0x74000008));
  EXPECT_EQ(NEVERC_OBJECT_COMDAT_ASSOCIATIVE, UINT32_C(0x75000006));
  EXPECT_EQ(std::char_traits<char>::length(NEVERC_OBJECT_SCHEMA_DIGEST),
            64U);
}

TEST(PluginObjectSchemaTest, KeepsStableIDsUniqueAndNamed) {
  std::set<uint32_t> IDs;
  const auto Check = [&IDs](const auto &Entries) {
    for (const SchemaEntry &Entry : Entries) {
      EXPECT_TRUE(IDs.insert(Entry.StableID).second);
      EXPECT_NE(Entry.StableID, 0U);
      EXPECT_NE(Entry.Name[0], '\0');
    }
  };
  Check(SectionKinds);
  Check(SymbolTypes);
  Check(RelocationKinds);
  Check(ComdatSelections);
}

} // namespace

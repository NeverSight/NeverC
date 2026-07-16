#include "neverc/Foundation/Attr/AttrKinds.h"
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Tree/Decl/DeclBase.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "neverc/Tree/Type/Type.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <set>
#include <string_view>

namespace {

struct DeclKindMapping {
  neverc::Decl::Kind Internal;
  NevercDeclKind Stable;
};

constexpr DeclKindMapping DeclKinds[] = {
#define NEVERC_AST_SCHEMA_INTERNAL_DECL(Internal, Symbol, ID)                  \
  {neverc::Decl::Internal, ID},
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_INTERNAL_DECL
};

struct StmtKindMapping {
  neverc::Stmt::StmtClass Internal;
  NevercStmtKind Stable;
};

constexpr StmtKindMapping StmtKinds[] = {
#define NEVERC_AST_SCHEMA_INTERNAL_STMT(Internal, Symbol, ID)                  \
  {neverc::Stmt::Internal, ID},
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_INTERNAL_STMT
};

struct TypeKindMapping {
  neverc::Type::TypeClass Internal;
  NevercTypeKind Stable;
};

constexpr TypeKindMapping TypeKinds[] = {
#define NEVERC_AST_SCHEMA_INTERNAL_TYPE(Internal, Symbol, ID)                  \
  {neverc::Type::Internal, ID},
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_INTERNAL_TYPE
};

struct AttrKindMapping {
  neverc::attr::Kind Internal;
  NevercAttrKind Stable;
};

constexpr AttrKindMapping AttrKinds[] = {
#define NEVERC_AST_SCHEMA_INTERNAL_ATTR(Internal, Symbol, ID)                  \
  {neverc::attr::Internal, ID},
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_INTERNAL_ATTR
};

struct NodeSchemaEntry {
  NevercASTSchemaDomain Domain;
  NevercASTNodeKind Kind;
  NevercASTNodeKind Parent;
  NevercBool Abstract;
  NevercASTSourceRangeMode SourceRange;
  const char *Name;
  const char *ClassName;
  uint32_t PropertyCount;
  uint32_t ChildSlotCount;
};

constexpr NodeSchemaEntry Nodes[] = {
#define NEVERC_AST_SCHEMA_NODE(Domain, Internal, Symbol, ID, ParentID,         \
                               Abstract, SourceRange, Name, ClassName,         \
                               PropertyCount, ChildSlotCount)                  \
  {NEVERC_AST_SCHEMA_DOMAIN_##Domain, ID, ParentID, Abstract, SourceRange,     \
   Name, ClassName, PropertyCount, ChildSlotCount},
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_NODE
};

struct PropertySchemaEntry {
  NevercASTPropertyID ID;
  NevercASTNodeKind Owner;
  NevercASTValueType ValueType;
  NevercASTAccessMode Access;
  NevercASTCardinality Cardinality;
  const char *Name;
};

constexpr PropertySchemaEntry Properties[] = {
#define NEVERC_AST_SCHEMA_PROPERTY(Symbol, ID, Owner, ValueType, Access,       \
                                   Cardinality, Name)                          \
  {ID, Owner, ValueType, Access, Cardinality, Name},
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_PROPERTY
};

struct ChildSlotSchemaEntry {
  NevercASTChildSlotID ID;
  NevercASTNodeKind Owner;
  NevercASTValueType ValueType;
  NevercASTAccessMode Access;
  NevercASTCardinality Cardinality;
  const char *Name;
};

constexpr ChildSlotSchemaEntry ChildSlots[] = {
#define NEVERC_AST_SCHEMA_CHILD_SLOT(Symbol, ID, Owner, ValueType, Access,     \
                                     Cardinality, Name)                        \
  {ID, Owner, ValueType, Access, Cardinality, Name},
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_CHILD_SLOT
};

const NodeSchemaEntry *findNode(NevercASTNodeKind Kind) {
  for (const NodeSchemaEntry &Node : Nodes)
    if (Node.Kind == Kind)
      return &Node;
  return nullptr;
}

const PropertySchemaEntry *findProperty(NevercASTPropertyID ID) {
  for (const PropertySchemaEntry &Property : Properties)
    if (Property.ID == ID)
      return &Property;
  return nullptr;
}

const ChildSlotSchemaEntry *findChildSlot(NevercASTChildSlotID ID) {
  for (const ChildSlotSchemaEntry &Slot : ChildSlots)
    if (Slot.ID == ID)
      return &Slot;
  return nullptr;
}

TEST(PluginASTSchemaTest, CoversEveryInternalConcreteKindWithStableIDs) {
  static_assert(std::size(DeclKinds) ==
                static_cast<size_t>(neverc::Decl::lastDecl) + 1);
  static_assert(std::size(StmtKinds) ==
                static_cast<size_t>(neverc::Stmt::lastStmtConstant));
  static_assert(std::size(TypeKinds) ==
                static_cast<size_t>(neverc::Type::TypeLast) + 1);
  static_assert(std::size(AttrKinds) ==
                static_cast<size_t>(neverc::attr::LastAttr) + 1);
  static_assert(std::size(DeclKinds) == NEVERC_DECL_KIND_COUNT);
  static_assert(std::size(StmtKinds) == NEVERC_STMT_KIND_COUNT);
  static_assert(std::size(TypeKinds) == NEVERC_TYPE_KIND_COUNT);
  static_assert(std::size(AttrKinds) == NEVERC_ATTR_KIND_COUNT);

  std::set<NevercASTNodeKind> StableIDs;
  const auto CheckMappings = [&StableIDs](const auto &Mappings,
                                         NevercASTSchemaDomain Domain) {
    for (const auto &Mapping : Mappings) {
      EXPECT_TRUE(StableIDs.insert(Mapping.Stable).second);
      const NodeSchemaEntry *Node = findNode(Mapping.Stable);
      ASSERT_NE(Node, nullptr);
      EXPECT_EQ(Node->Domain, Domain);
      EXPECT_EQ(Node->Abstract, NEVERC_FALSE);
    }
  };
  CheckMappings(DeclKinds, NEVERC_AST_SCHEMA_DOMAIN_DECL);
  CheckMappings(StmtKinds, NEVERC_AST_SCHEMA_DOMAIN_STMT);
  CheckMappings(TypeKinds, NEVERC_AST_SCHEMA_DOMAIN_TYPE);
  CheckMappings(AttrKinds, NEVERC_AST_SCHEMA_DOMAIN_ATTR);

  EXPECT_NE(NEVERC_DECL_KIND_EMPTY,
            static_cast<NevercDeclKind>(neverc::Decl::Empty));
  EXPECT_NE(NEVERC_STMT_KIND_BREAK_STMT,
            static_cast<NevercStmtKind>(neverc::Stmt::BreakStmtClass));
  EXPECT_NE(NEVERC_TYPE_KIND_BUILTIN,
            static_cast<NevercTypeKind>(neverc::Type::Builtin));
  EXPECT_NE(NEVERC_ATTR_KIND_ALIGNED,
            static_cast<NevercAttrKind>(neverc::attr::Aligned));
}

TEST(PluginASTSchemaTest, PublishesHierarchyPropertiesAndChildContracts) {
  static_assert(std::size(Nodes) == NEVERC_AST_SCHEMA_NODE_COUNT);
  static_assert(std::size(Properties) == NEVERC_AST_PROPERTY_COUNT);
  static_assert(std::size(ChildSlots) == NEVERC_AST_CHILD_SLOT_COUNT);

  const NodeSchemaEntry *Function = findNode(NEVERC_DECL_KIND_FUNCTION);
  ASSERT_NE(Function, nullptr);
  EXPECT_EQ(Function->Parent, NEVERC_AST_NODE_KIND_DECL_DECLARATOR);
  EXPECT_EQ(Function->SourceRange, NEVERC_AST_SOURCE_RANGE_NATIVE);
  EXPECT_GT(Function->PropertyCount, 0U);
  EXPECT_GT(Function->ChildSlotCount, 0U);

  const NodeSchemaEntry *Expr =
      findNode(NEVERC_AST_NODE_KIND_STMT_EXPR);
  ASSERT_NE(Expr, nullptr);
  EXPECT_EQ(Expr->Abstract, NEVERC_TRUE);
  EXPECT_EQ(Expr->Parent, NEVERC_AST_NODE_KIND_STMT_VALUE_STMT);

  const ChildSlotSchemaEntry *LHS =
      findChildSlot(NEVERC_AST_CHILD_SLOT_STMT_BINARY_OPERATOR_LHS);
  ASSERT_NE(LHS, nullptr);
  EXPECT_EQ(LHS->Owner, NEVERC_STMT_KIND_BINARY_OPERATOR);
  EXPECT_EQ(LHS->ValueType, NEVERC_AST_VALUE_EXPR);
  EXPECT_EQ(LHS->Access, NEVERC_AST_ACCESS_READ_WRITE);
  EXPECT_EQ(LHS->Cardinality, NEVERC_AST_CARDINALITY_REQUIRED);
  EXPECT_EQ(std::string_view(LHS->Name), "lhs");
}

TEST(PluginASTSchemaTest, AttrArgumentsUseExplicitPortableValueTypes) {
  const PropertySchemaEntry *Platform =
      findProperty(NEVERC_AST_PROPERTY_ATTR_AVAILABILITY_PLATFORM);
  ASSERT_NE(Platform, nullptr);
  EXPECT_EQ(Platform->Owner, NEVERC_ATTR_KIND_AVAILABILITY);
  EXPECT_EQ(Platform->ValueType, NEVERC_AST_VALUE_IDENTIFIER);
  EXPECT_EQ(Platform->Access, NEVERC_AST_ACCESS_READ_WRITE);
  EXPECT_EQ(Platform->Cardinality, NEVERC_AST_CARDINALITY_REQUIRED);

  const PropertySchemaEntry *Args =
      findProperty(NEVERC_AST_PROPERTY_ATTR_ANNOTATE_ARGS);
  ASSERT_NE(Args, nullptr);
  EXPECT_EQ(Args->Owner, NEVERC_ATTR_KIND_ANNOTATE);
  EXPECT_EQ(Args->ValueType, NEVERC_AST_VALUE_EXPR);
  EXPECT_EQ(Args->Cardinality, NEVERC_AST_CARDINALITY_MANY);

  EXPECT_EQ(std::char_traits<char>::length(NEVERC_AST_SCHEMA_DIGEST), 64U);
}

constexpr size_t DeclVisitorCount =
    0
#define NEVERC_AST_SCHEMA_VISIT_DECL(ClassName, Symbol, ID) +1
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_VISIT_DECL
    ;

constexpr size_t StmtVisitorCount =
    0
#define NEVERC_AST_SCHEMA_VISIT_STMT(ClassName, Symbol, ID) +1
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_VISIT_STMT
    ;

TEST(PluginASTSchemaTest, GeneratesVisitorDispatchSkeletons) {
  EXPECT_EQ(DeclVisitorCount, std::size(DeclKinds));
  EXPECT_EQ(StmtVisitorCount, std::size(StmtKinds));
}

} // namespace

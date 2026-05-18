// Convenience macros for obtaining offsets of members in structs.
//
// Usage:
//
//   #define FOR_EACH_FOO_FIELD(DO) \
//     DO(Ptr, bar)                 \
//     DO(uint32_t, baz)            \
//   CREATE_LAYOUT_CLASS(Foo, FOR_EACH_FOO_FIELD)
//   #undef FOR_EACH_FOO_FIELD
//
// This will generate
//
//   struct FooLayout {
//     uint32_t barOffset;
//     uint32_t bazOffset;
//     uint32_t totalSize;
//
//     FooLayout(size_t wordSize) {
//       assert(wordSize == 8);
//       init<uint64_t>();
//     }
//
//   private:
//     template <class Ptr> void init() {
//       FOR_EACH_FIELD(_INIT_OFFSET);
//       barOffset = offsetof(Layout<Ptr>, bar);
//       bazOffset = offsetof(Layout<Ptr>, baz);
//       totalSize = sizeof(Layout<Ptr>);
//     }
//     template <class Ptr> struct Layout {
//       Ptr bar;
//       uint32_t baz;
//     };
//   };

#define _OFFSET_FOR_FIELD(_, name) uint32_t name##Offset;
#define _INIT_OFFSET(type, name) name##Offset = offsetof(Layout<Ptr>, name);
#define _LAYOUT_ENTRY(type, name) type name;

#define CREATE_LAYOUT_CLASS(className, FOR_EACH_FIELD)                         \
  struct className##Layout {                                                   \
    FOR_EACH_FIELD(_OFFSET_FOR_FIELD)                                          \
    uint32_t totalSize;                                                        \
                                                                               \
    className##Layout(size_t wordSize) {                                       \
      assert(wordSize == 8);                                                   \
      init<uint64_t>();                                                        \
    }                                                                          \
                                                                               \
  private:                                                                     \
    template <class Ptr> void init() {                                         \
      FOR_EACH_FIELD(_INIT_OFFSET);                                            \
      totalSize = sizeof(Layout<Ptr>);                                         \
    }                                                                          \
    template <class Ptr> struct Layout {                                       \
      FOR_EACH_FIELD(_LAYOUT_ENTRY)                                            \
    };                                                                         \
  }

add_neverc_library(nevercStrHash
  StrHashFoldPass.cpp

  LINK_LIBS
  nevercFoundation

  DEPENDS
  NeverCDriverOptions
  )

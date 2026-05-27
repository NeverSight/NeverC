add_neverc_library(nevercXorStr
  EncryptCallStringsPass.cpp
  XorStrCleanupPass.cpp

  LINK_LIBS
  nevercFoundation

  DEPENDS
  NeverCDriverOptions
  )

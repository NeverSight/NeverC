# Install the conformance fixtures into the SDK so a downstream can build and run
# the conformance suite against its own installed host with the system compiler.
install(FILES
  ConformanceFixture.h
  MinimalPlugin.c
  InvalidDescriptorPlugins.c
  LifecyclePlugin.c
  DependencyPlugins.c
  DomainConformancePlugin.c
  ReplacementConformancePlugin.c
  DESTINATION pluginsdk/conformance/fixtures
  COMPONENT neverc-pluginsdk
)

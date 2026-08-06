from neverc_plugin import InterfaceRequirement, Plugin, PluginDependency


@Plugin(
    id="org.neverc.test.python-metadata",
    name="NeverC Python Metadata Plugin",
    version="4.5.6-rc.2+native-test",
    concurrency="thread_safe",
    reentrancy="allowed",
    required_interfaces=(InterfaceRequirement("IR_BUILDER"),),
    optional_interfaces=(InterfaceRequirement("MIR", minimum_minor=0),),
    dependencies=(
        PluginDependency(
            "org.neverc.test.python-minimal",
            minimum="1.2.0-beta.1",
            maximum="2.0.0",
            kind="after",
            allow_prerelease=True,
        ),
    ),
)
class MetadataPlugin:
    def register(self, context):
        pass

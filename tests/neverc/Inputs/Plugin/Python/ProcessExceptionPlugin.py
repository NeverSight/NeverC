from neverc_plugin import Plugin


@Plugin(
    id="org.neverc.test.python-process-exception",
    name="NeverC Python Process Exception Plugin",
    version="1.0.0",
)
class ProcessExceptionPlugin:
    def on_process_begin(self, ctx):
        raise RuntimeError("intentional Python process hook explosion")

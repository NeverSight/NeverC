from neverc_plugin import Plugin
from neverc_plugin.domains import driver


@Plugin(id="com.example.driver-trace", name="Python Driver Trace", version="1.0.0")
class DriverTracePlugin:
    def register(self, ctx):
        ctx.option(
            "--driver-trace-python",
            kind="flag",
            value_type="bool",
            help="Report the raw NeverC command line",
        )
        ctx.observer(driver.RAW_ARGUMENTS, fn=self.observe_arguments)

    def observe_arguments(self, frame):
        if frame.option_values("--driver-trace-python"):
            frame.emit_remark(
                f"Python observed {len(frame.arguments)} raw arguments",
                code=1001,
            )

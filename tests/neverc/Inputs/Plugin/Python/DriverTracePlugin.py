from neverc_plugin import Plugin
from neverc_plugin.domains import driver


@Plugin(
    id="org.neverc.test.python-driver-trace",
    name="NeverC Python Driver Trace Plugin",
    version="1.0.0",
)
class DriverTracePlugin:
    def __init__(self):
        self.retained_frame = None
        self.reported_stale_frame = False

    def register(self, ctx):
        ctx.option(
            "--python-driver-trace",
            kind="flag",
            value_type="bool",
            help="Trace NeverC raw arguments from Python",
        )
        ctx.option(
            "--python-driver-mode",
            kind="separate",
            value_type="enum",
            enum_values={"compact": 1, "verbose": 2},
            help="Select the Python trace presentation",
        )
        ctx.observer(
            driver.RAW_ARGUMENTS,
            when=("before", "after"),
            fn=self.observe_raw_arguments,
        )

    def observe_raw_arguments(self, frame):
        if not frame.option_values("--python-driver-trace"):
            return
        mode_values = frame.option_values("--python-driver-mode")
        if mode_values not in ((), ("compact",)):
            raise AssertionError(f"unexpected enum option value: {mode_values!r}")
        mode = mode_values[0] if mode_values else "default"
        frame.check_cancelled()
        arguments = frame.arguments
        self.retained_frame = frame
        frame.emit_remark(
            f"python raw arguments ({frame.when}): mode={mode} count={len(arguments)}",
            code=8101,
        )

    def on_task_end(self, ctx):
        if self.retained_frame is None or self.reported_stale_frame:
            return
        try:
            _ = self.retained_frame.arguments
        except RuntimeError:
            self.reported_stale_frame = True
            ctx.emit_remark("Python retained frame was rejected as stale", code=8102)
        else:
            raise AssertionError("a retained NeverC frame remained active")

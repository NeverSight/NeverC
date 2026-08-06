from neverc_plugin import Plugin
from neverc_plugin.domains import driver


@Plugin(
    id="org.neverc.test.python-exception",
    name="NeverC Python Exception Plugin",
    version="1.0.0",
)
class ExceptionPlugin:
    def register(self, ctx):
        ctx.observer(driver.RAW_ARGUMENTS, fn=self.raise_from_observer)

    def raise_from_observer(self, frame):
        raise RuntimeError("intentional Python observer explosion")

import os
import tempfile

from neverc_plugin import Plugin


def record(event):
    directory = tempfile.gettempdir() if os.name == "nt" else "/tmp"
    path = os.path.join(directory, f"neverc-python-plugin-{os.getpid()}.trace")
    with open(path, "a", encoding="utf-8") as trace:
        trace.write(event + "\n")


@Plugin(
    id="org.neverc.test.python-lifecycle",
    name="NeverC Python Lifecycle Plugin",
    version="1.0.0",
)
class LifecyclePlugin:
    def on_process_begin(self, ctx):
        record("process_begin")
        return "process-state"

    def register(self, ctx):
        record("register")

    def on_session_begin(self, ctx):
        record("session_begin")
        return "session-state"

    def on_session_end(self, ctx):
        assert ctx.state == "session-state"
        record("session_end")

    def on_task_begin(self, ctx):
        record(f"task_begin:{ctx.kind.name.lower()}")
        return "task-state"

    def on_task_end(self, ctx):
        assert ctx.state == "task-state"
        record(f"task_end:{ctx.kind.name.lower()}")

    def on_destroy(self, ctx):
        assert ctx.state == "process-state"
        record("destroy")

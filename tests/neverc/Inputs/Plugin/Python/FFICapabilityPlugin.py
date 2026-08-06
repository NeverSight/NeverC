from neverc_plugin import Plugin
from neverc_plugin.ffi import Capability


@Plugin(
    id="org.neverc.test.python-ffi-capability",
    name="NeverC Python FFI Capability Test",
    version="1.0.0",
)
class FFICapabilityPlugin:
    def on_process_begin(self, context):
        scope = context.ffi
        assert scope.capabilities == Capability.CORE
        assert scope.core.header.StructSize > 0
        self.process_scope = scope

    def register(self, context):
        scope = context.ffi
        assert scope.capabilities & Capability.CORE
        assert scope.capabilities & Capability.REGISTRAR
        assert scope.capabilities & Capability.REGISTRAR_CONTEXT
        assert scope.registrar.header.StructSize > 0
        assert scope.registrar_context_address != 0
        self.registration_scope = scope

    def on_session_begin(self, context):
        try:
            self.registration_scope.ensure_active()
        except RuntimeError as error:
            assert "no longer active" in str(error)
        else:
            raise AssertionError("registration capability remained active")
        scope = context.ffi
        assert scope.capabilities & Capability.SESSION
        assert scope.session == context.handle
        self.session_scope = scope

    def on_task_begin(self, context):
        scope = context.ffi
        assert scope.capabilities & Capability.TASK
        assert scope.task == context.handle
        assert scope.session == self.session_scope.session
        self.task_scope = scope

    def on_session_end(self, context):
        try:
            self.task_scope.core.header
        except RuntimeError as error:
            assert "no longer active" in str(error)
        else:
            raise AssertionError("task capability remained active")
        context.emit_remark("Python FFI capabilities and stale guards passed", code=8201)

    def on_destroy(self, context):
        try:
            self.session_scope.ensure_active()
        except RuntimeError as error:
            assert "no longer active" in str(error)
        else:
            raise AssertionError("session capability remained active")

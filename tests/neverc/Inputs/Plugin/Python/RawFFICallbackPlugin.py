import ctypes

from neverc_plugin import Plugin
from neverc_plugin import abi
from neverc_plugin.domains.driver import RAW_ARGUMENTS
from neverc_plugin.ffi import Capability, ContextKind, bind_callbacks, require_ok


@Plugin(
    id="org.neverc.test.python-raw-ffi-callback",
    name="NeverC Python Raw FFI Callback Test",
    version="1.0.0",
)
class RawFFICallbackPlugin:
    def register(self, context):
        self.raw_seen = False
        self.destroy_seen = False
        scope = context.ffi
        descriptor = abi.NevercObserverDescriptor()
        descriptor.Header.StructSize = ctypes.sizeof(descriptor)
        descriptor.Header.Major = abi.NEVERC_PLUGIN_ABI_MAJOR
        descriptor.Header.Minor = abi.NEVERC_PLUGIN_ABI_MINOR
        descriptor.Phase.High = RAW_ARGUMENTS.high
        descriptor.Phase.Low = RAW_ARGUMENTS.low
        descriptor.Points = abi.NEVERC_OBSERVER_BEFORE

        bound = bind_callbacks(
            scope,
            descriptor,
            {
                "Callback": self.raw_observer,
                "DestroyUserData": self.destroy_user_data,
            },
        )
        result = scope.registrar.function("RegisterObserver")(
            ctypes.c_void_p(scope.registrar_context_address), bound.pointer
        )
        require_ok(result, "NevercRegistrarAPI.RegisterObserver")
        bound.transfer()
        self.bound = bound
        context.observer(RAW_ARGUMENTS, when="before", fn=self.confirm_observer)

    def raw_observer(self, scope, frame_address, point):
        assert scope.kind == ContextKind.CALLBACK
        assert scope.capabilities & Capability.FRAME
        assert scope.frame.address == frame_address
        frame = scope.frame.copy()
        assert (frame.Task.Owner, frame.Task.Value) == scope.task
        assert point == abi.NEVERC_OBSERVER_BEFORE
        self.raw_seen = True
        return (abi.NEVERC_STATUS_OK, 0, 0)

    def confirm_observer(self, frame):
        assert self.raw_seen, "generated raw callback did not run first"
        frame.emit_remark("Python generated callback trampoline passed", code=8202)

    def destroy_user_data(self, scope):
        self.destroy_seen = True

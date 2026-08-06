from neverc_plugin import Plugin, abi
from neverc_plugin.ffi import bind_callbacks


def _noop(*_arguments):
    return None


@Plugin(
    id="org.neverc.test.python-all-ffi-callbacks",
    name="NeverC All Python FFI Callback Bindings",
    version="1.0.0",
)
class AllFFICallbackBindingsPlugin:
    def register(self, context):
        scope = context.ffi
        record_count = 0
        callback_count = 0

        for record_name in abi.PUBLIC_RECORDS:
            record_type = getattr(abi, record_name)
            fields = [name for name, _field_type in record_type._fields_]
            if "UserData" not in fields:
                continue
            callbacks = {
                field: _noop
                for field in fields
                if f"{record_name}.{field}" in abi.FUNCTION_SIGNATURES
            }
            if not callbacks:
                continue

            descriptor = record_type()
            binding = bind_callbacks(scope, descriptor, callbacks)
            binding.release()
            record_count += 1
            callback_count += len(callbacks)

        if record_count != 29 or callback_count != 75:
            raise RuntimeError(
                "generated callback inventory changed without updating the "
                f"runtime conformance contract: {record_count} records, "
                f"{callback_count} callbacks"
            )

# NeverC Protobuf Generator

`neverc-protoc.py` generates portable C/NeverC structs and bounded
encode/decode wrappers backed by `std/encoding/protobuf` descriptors.

```bash
python3 utils/protobuf/neverc-protoc.py schema.proto --out-dir generated
```

The minimal generator accepts proto3 messages containing scalar, string, and
bytes fields, including proto3 `optional` presence. It rejects imports,
services, enums, nested/message-valued fields, repeated fields, maps, oneofs,
extensions, and invalid/reserved field numbers. These features require richer
ownership and descriptor semantics and are deliberately fail-closed instead of
being emitted incorrectly.

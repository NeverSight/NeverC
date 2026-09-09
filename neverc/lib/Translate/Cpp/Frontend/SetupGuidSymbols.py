"""Exact Setup GUID and observed SDK template identities for native COFF.

This policy has no tool or filesystem access. The writer maps complete symbol
names; the independent archive audits reject the original names without using
the host inventory. Unknown spellings containing a selected identifier fail
closed instead of extending this policy to arbitrary SDK templates.
"""


GUID_RENAMES = {
    "_GUID_00000000_0000_0000_c000_000000000046": "neverc_cpp0000000000000000c000000000000046",
    "_GUID_177f0c4a_1cd3_4de7_a32c_71dbbb9fa36d": "neverc_cpp177f0c4a1cd34de7a32c71dbbb9fa36d",
    "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b": "neverc_cpp42843719db4c46c28e7c64f1816efd5b",
    "_GUID_26aab78c_4a60_49d6_af3b_3c35bc93365d": "neverc_cpp26aab78c4a6049d6af3b3c35bc93365d",
    "_GUID_42b21b78_6192_463e_87bf_d577838f1d5c": "neverc_cpp42b21b786192463e87bfd577838f1d5c",
}
for _old, _new in GUID_RENAMES.items():
    if (len(_old.encode("ascii")) != 42 or len(_new.encode("ascii")) != 42
            or _new != "neverc_cpp" + _old[6:].replace("_", "")):
        raise ValueError("Setup GUID data policy must preserve all 42 ASCII bytes")

# These exact interface/IID pairs and member signatures are the union of the
# c942 MSVC x64/ARM64 smart-pointer and linked-runtime COFF inventories (32 and
# 33 related external names, 37 distinct names). Do not infer more overloads
# from a prefix match. In particular, IUnknown and the CLSID have no observed
# related template names, and the enumerator's different IID is not selected.
_INTERFACES = (
    ("ISetupConfiguration", "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b"),
    ("ISetupConfiguration2", "_GUID_26aab78c_4a60_49d6_af3b_3c35bc93365d"),
    ("ISetupHelper", "_GUID_42b21b78_6192_463e_87bf_d577838f1d5c"),
)


def _observed_template_names(identifiers):
    names = []
    iiids, pointers = {}, {}
    for interface, old in _INTERFACES:
        # This is the observed MSVC address-NTTP spelling, not a generic
        # mangler or a text replacement within an unchecked C++ symbol.
        iiid = "?$_com_IIID@U" + interface + "@@$1?" + identifiers[old] + "@@3U__s_GUID@@B@@"
        pointer = "?$_com_ptr_t@V" + iiid + "@@"
        iiids[interface], pointers[interface] = iiid, pointer
        names.extend((
            "??0" + pointer + "QEAA@PEAU" + interface + "@@@Z",
            "??1" + pointer + "QEAA@XZ",
            "??C" + pointer + "QEBAPEAU" + interface + "@@XZ",
            "?GetIID@" + iiid + "SAAEBU_GUID@@XZ",
            "?GetIID@" + pointer + "SAAEBU_GUID@@XZ",
            "?_AddRef@" + pointer + "AEAAXXZ",
            "?_Release@" + pointer + "AEAAXXZ",
        ))
    query = pointers["ISetupConfiguration"]
    query_iid = iiids["ISetupConfiguration"]
    names.extend((
        "??0" + query + "QEAA@AEBV0@@Z",
        "??0" + query + "QEAA@XZ",
        "??B" + query + "QEBA_NXZ",
        "?CreateInstance@" + query + "QEAAJAEBU_GUID@@PEAUIUnknown@@K@Z",
    ))
    for interface in ("ISetupConfiguration2", "ISetupHelper"):
        pointer = pointers[interface]
        names.extend((
            "??4" + pointer + "QEAAAEAV0@$$QEAV0@@Z",
            "?Attach@" + pointer + "QEAAXPEAU" + interface + "@@@Z",
            "?Detach@" + pointer + "QEAAPEAU" + interface + "@@XZ",
            "?GetInterfacePtr@" + pointer + "QEAAAEAPEAU" + interface + "@@XZ",
            "??$?0V" + query_iid + "$0A@@" + pointer + "QEAA@AEBV" + query + "@Z",
            "??$_QueryInterface@V" + query + "@" + pointer + "AEAAJV" + query + "@Z",
        ))
    return tuple(names)


_OLD_TEMPLATES = _observed_template_names({name: name for name in GUID_RENAMES})
_NEW_TEMPLATES = _observed_template_names(GUID_RENAMES)
if len(_OLD_TEMPLATES) != 37 or len(set(_OLD_TEMPLATES)) != 37 or len(set(_NEW_TEMPLATES)) != 37:
    raise ValueError("Invalid observed Setup GUID template policy inventory")
_TEMPLATE_RENAMES = dict(zip(_OLD_TEMPLATES, _NEW_TEMPLATES))


def _observed_metadata_renames():
    # Filter the finite, already approved full-name catalog, never caller
    # input. The four prefixes are not a Cartesian product with that catalog:
    # c942 x64/ARM64 MSVC objects observed exactly 34/31/11/11 combinations.
    pdata = {name for name in _OLD_TEMPLATES
             if not name.startswith("?GetIID@?$_com_IIID@")}
    unwind = {name for name in pdata
              if not name.startswith("?GetInterfacePtr@")
              and not (name.startswith("??0?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@")
                       and name.endswith("QEAA@XZ"))}
    exceptions = {name for name in _OLD_TEMPLATES if name.startswith((
        "?_AddRef@", "?_Release@", "??4?$_com_ptr_t@", "??$_QueryInterface@", "?CreateInstance@"))}
    if (len(pdata), len(unwind), len(exceptions)) != (34, 31, 11):
        raise ValueError("Invalid observed Setup GUID metadata policy inventory")
    return {prefix + name: prefix + _TEMPLATE_RENAMES[name]
            for prefix, selected in (("$pdata$", pdata), ("$unwind$", unwind),
                                     ("$cppxdata$", exceptions), ("$ip2state$", exceptions))
            for name in sorted(selected)}


_METADATA_RENAMES = _observed_metadata_renames()
_PRIVATE_METADATA_NAMES = frozenset(_METADATA_RENAMES.values())
if len(_METADATA_RENAMES) != 87 or len(_PRIVATE_METADATA_NAMES) != 87:
    raise ValueError("Invalid observed Setup GUID metadata mapping")
_PRIVATE_NAMES = frozenset((*GUID_RENAMES.values(), *_NEW_TEMPLATES, *_PRIVATE_METADATA_NAMES))
if len(_PRIVATE_NAMES) != 129:
    raise ValueError("Setup GUID policy identities are not disjoint")


def contains_old_guid_name(name):
    """Detect residual selected identifiers, including unsupported grammar."""
    return any(old in name for old in GUID_RENAMES)


def is_private_guid_name(name):
    """Recognize complete new data, external templates and local metadata.

    This classifies names, not linkage. Callers must inspect COFF storage
    before treating a recognized name as an external closure obligation.
    """
    if name in _PRIVATE_NAMES:
        return True
    if any(new in name for new in GUID_RENAMES.values()):
        raise ValueError("Unsupported private Setup GUID symbol grammar: " + name)
    return False


def is_private_guid_metadata_name(name):
    """Identify the 87 exact new names requiring local metadata storage.

    Call is_private_guid_name first when validating an arbitrary name, so
    unknown spellings containing a selected private identifier still fail.
    """
    return name in _PRIVATE_METADATA_NAMES


def rewrite_name(name):
    """Return the complete replacement, or None for an unrelated/new name."""
    if name in GUID_RENAMES:
        return GUID_RENAMES[name]
    if name in _TEMPLATE_RENAMES:
        return _TEMPLATE_RENAMES[name]
    if name in _METADATA_RENAMES:
        return _METADATA_RENAMES[name]
    if contains_old_guid_name(name):
        raise ValueError("Unsupported original Setup GUID symbol grammar: " + name)
    # A partially changed or invented private spelling must not silently pass
    # preflight just because its complete old token has already disappeared.
    is_private_guid_name(name)
    return None


def build_rename_map(names):
    """Build an injective full-name map, without accepting existing targets.

    Repeated occurrences of one symbol across archive members are expected;
    only their common name is deduplicated, never the actual archive members.
    The transaction wrapper separately proves five-data-definition coverage.
    """
    inventory = set(names)
    for name in sorted(inventory):
        if rewrite_name(name) is None and is_private_guid_name(name):
            raise ValueError("Setup GUID target-name collision: existing private identity " + name)
    result, sources = {}, {}
    for name in sorted(inventory):
        target = rewrite_name(name)
        if target is None:
            continue
        if target in inventory:
            raise ValueError("Setup GUID target-name collision: " + name + " -> " + target)
        previous = sources.get(target)
        if previous is not None and previous != name:
            raise ValueError("Non-injective Setup GUID symbol mapping: " + previous +
                             " and " + name + " -> " + target)
        if contains_old_guid_name(target) or not is_private_guid_name(target):
            raise ValueError("Invalid Setup GUID replacement: " + name + " -> " + target)
        if len(name.encode("ascii")) != len(target.encode("ascii")):
            raise ValueError("Setup GUID symbol mapping changes byte length: " + name)
        sources[target], result[name] = name, target
    return result

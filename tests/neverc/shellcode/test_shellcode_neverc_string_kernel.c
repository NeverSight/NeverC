// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string in kernel-context shellcode mode.
 *
 * This is a compile-only guard for `-mshellcode-context=kernel`: the builtin
 * string runtime must use the shellcode-local arena and must not leave
 * malloc/free, mem*, or string-runtime externs for KernelImportPass to route
 * through the kernel resolver.
 */
int shellcode_entry(int seed) {
    string prefix = "kernel ";
    string joined = prefix + "string";
    string copied = joined;
    string cloned = joined.clone();

    if (!neverc_string_eq(joined, "kernel string"))
        return seed + 1;
    if (!neverc_string_eq(copied, "kernel string") || copied.data == joined.data)
        return seed + 2;
    if (!neverc_string_eq(cloned, "kernel string") || cloned.data == joined.data)
        return seed + 3;

    joined += "!";
    if (!neverc_string_eq(joined.substr(7), "string!"))
        return seed + 4;
    if (!joined.starts_with("kernel") || !joined.ends_with("!") ||
        !joined.contains("str"))
        return seed + 5;

    string mut = "kernel";
    mut = mut.append(" mode");
    mut = mut.push_back('?');
    mut = mut.pop_back();
    mut = mut.replace(7, 4, "arena");
    if (!neverc_string_eq(mut, "kernel arena"))
        return seed + 6;
    if (!neverc_string_empty(neverc_string_clear(mut)))
        return seed + 7;

    if (!neverc_string_eq(neverc_string_repeat("k", 3), "kkk"))
        return seed + 8;
    if (!neverc_string_eq("KeRnEl".to_lower(), "kernel"))
        return seed + 9;
    if (!neverc_string_eq("KeRnEl".to_upper(), "KERNEL"))
        return seed + 10;
    if (!neverc_string_eq(" \t kernel path \n".trim(), "kernel path"))
        return seed + 11;
    if (!neverc_string_eq(neverc_string_from_int(-64), "-64"))
        return seed + 12;
    if (neverc_string_to_uint("+4096") != 4096)
        return seed + 13;

    {
        string forged = {(const char *)1, 1, 1};
        if (forged.len != 1)
            return seed + 14;
    }

    const char *freed_block = 0;
    {
        string first = "kernel " + "reuse";
        freed_block = first.data;
    }
    string second = "kernel " + "reuse";
    if (second.data != freed_block)
        return seed + 15;
    if (!neverc_string_eq(second, "kernel reuse"))
        return seed + 16;

    for (int i = 0; i < 96; ++i) {
        string loop = "kernel";
        loop += " arena";
        if (!neverc_string_eq(loop, "kernel arena"))
            return seed + 17;
    }

    return seed;
}

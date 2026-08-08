#ifndef NEVERC_BUILD_BUILTINCOMMANDS_H
#define NEVERC_BUILD_BUILTINCOMMANDS_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace build {
namespace builtins {

/// Try to run a make-recipe command as a NeverC-owned portable builtin.
///
/// Returns true when the command was recognized and fully handled in-process
/// (ExitCode is always set in that case). Returns false when the host shell
/// should run the original command unchanged — for example unknown programs,
/// unsupported flags, unbalanced quotes, shell expansions, or unquoted shell
/// metacharacters.
///
/// When EchoCommand is true and the command name is a known builtin, the
/// original Command is printed to llvm::outs() under the same I/O lock as the
/// builtin body (except `sleep`, which prints under the lock then waits
/// without holding it). If the handler then returns false (unsupported form),
/// the line was still echoed — set Echoed when non-null so the caller does not
/// print it again before falling back to the host shell.
///
/// Command name → handler table lives in
/// `lib/Build/Platform/Builtins/BuiltinCommands.def` (X-macro + StringSwitch).
///
/// Supported portable subset used by NeverC example Makefiles and common
/// recipe utilities:
/// - `rm` with `-f` / `-r` / `-R` (including clustered forms) and globs
/// - `sleep <seconds>`
/// - `mkdir` / `mkdir -p` / `mkdir -m MODE` / clustered `-pm755`
/// - `touch` / `touch -c` / `touch -a` / `touch -m` / `touch -r REF`
///   (create or update timestamps; `-c` skips missing; `-a`/`-m` select
///   atime/mtime; `-r` copies timestamps from REF)
/// - `echo` / `echo -n` (unquoted globs expanded)
/// - `true` / `false` / `:`
/// - `pwd`
/// - `cat` (no flags; supports `--`)
/// - `cp` with `-f` / `-n` / `-u` / `-r` / `-R` / `-p` / `-a`
/// - `mv` with `-f` / `-n`
/// - `ln` / `ln -s` (and `-f` / `-n`; `ln -s` falls back on Windows)
/// - `link` (POSIX hard link; FILE1 FILE2)
/// - `chmod` / `chmod -R`/`-r` with octal modes
/// - `chown` / `chgrp` with `-R`/`-r` (Unix; `USER[:GROUP]` / `GROUP`)
/// - `dd if=SRC of=DST [bs=N] [count=N]` (absolute sizes; no stdin/stdout)
/// - `printf` with a small `%s` / `%d` / `%c` / `%%` and `\\` / `\\n` / `\\t`
///   subset (POSIX format reuse; unquoted arg globs expanded)
/// - `basename` / `basename -a` / `basename -s SUFFIX` / `dirname` (multi-arg)
/// - `test` / `[` with a common primary subset (including `-nt` / `-ot` /
///   `-ef` / `-r` / `-w` / `-x` and a single `-a` / `-o` connector)
/// - `install -d` / `install -D` / `install -t DIR` /
///   `install [-m MODE] SRC DEST` (and optional `-c`/`-p` no-ops; `-D`
///   creates leading destination directories)
/// - `head` / `tail` with `-n N` / `-c N` or historic `-N`
/// - `wc` with `-l` / `-w` / `-c`
/// - `uname` with `-s` / `-n` / `-r` / `-v` / `-m` / `-a`
/// - `df -k` / `df -P` / `df -kP` with optional path operands
/// - `xxd -p` / `xxd -p -c N` with file operands
/// - `hexdump -C` with file operands
/// - `file` (no-flag form; small type set: directory/symlink/text/ELF/…)
/// - `realpath`
/// - `seq` (integer FIRST/INCREMENT/LAST forms)
/// - `readlink` / `readlink -f` (Unix)
/// - `cmp` / `cmp -s` (byte equality of two files; `-s`/`--quiet`/`--silent`)
/// - `ls` with `-1` / `-a` / `-A` / `-d`
/// - `which` / `hostname` / `whoami` / `groups` / `tty` / `nproc`
/// - `mktemp` / `mktemp -d`
/// - `mkfifo` / `mkfifo -m MODE` (Unix)
/// - `date` / `date +FORMAT` (`%s` / `%Y` / `%m` / `%d` / `%H` / `%M` / `%S` / `%F` / `%T`)
/// - `printenv` / `env` (dump or single variable)
/// - `cut -d DELIM -f LIST` with file operands
/// - `expr INTEGER OP INTEGER` (`+` `-` `*` `/` `%`)
/// - `expr length STRING` / `expr substr STRING POS LEN`
/// - `rmdir` / `rmdir -p`
/// - `id -u` / `id -g` / `id -un` (Unix)
/// - `sync` (bare form)
/// - `sort` / `sort -u` / `sort -r` / `sort -n` with file operands
/// - `uniq` / `uniq -c` with one file operand
/// - `diff -q` (brief file compare)
/// - `md5sum` / `sha1sum` / `sha256sum` / `sha512sum` with file operands
/// - `md5 -q FILE...` (Darwin-style quiet digest; hash only)
/// - `truncate -s SIZE` (bytes, optional `K`/`M`/`G`; absolute sizes only)
/// - `arch` (`uname -m`)
/// - `grep` / `egrep` / `fgrep` with `-F`/`-E` plus optional
///   `-q`/`-c`/`-l`/`-n`/`-i`/`-v` and files
/// - `du -s` / `du -sk` with path operands
/// - `command -v` (PATH lookup)
/// - `type -p` (PATH lookup only; bare `type` stays on the host shell)
/// - `tac` (reverse cat of file operands)
/// - `nl` (number non-empty lines; no flag forms)
/// - `base64` / `base64 -d` with file operands
/// - `cksum` with file operands
/// - `getconf` (`NPROCESSORS_ONLN` / `PAGE_SIZE` / `PATH_MAX` / `CLK_TCK`)
/// - `unlink` (single non-directory operand)
/// - `rev` (reverse characters per line; file operands)
/// - `fold` / `fold -w WIDTH` with file operands (default width 80)
/// - `stat -c` / `stat --format=` with `%s` / `%n` / `%Y` / `%F` / `%%`
/// - `paste` / `paste -d DELIM` with at least two file operands
///   (empty DELIM concatenates columns)
/// - `logname` (Unix)
/// - `split -l N FILE [PREFIX]`
/// - `strings` with file operands (min length 4)
/// - `od -An -tx1` / `od -t x1` / `od -c` with file operands
/// - `expand` / `expand -t N` with file operands
/// - `unexpand` / `unexpand -t N` with file operands (leading blanks)
/// - `comm` / `comm -[123]` with two sorted file operands
/// - `join` with two whitespace-field-1 sorted file operands
///   (Cartesian product on equal keys)
/// - `factor INTEGER...` (positive integers)
/// - `dos2unix` / `unix2dos` (in-place line-ending rewrite; file operands)
/// - `shuf FILE` (deterministic shuffle for recipe stability)
/// - `yes` / `yes STRING` (capped; pipes still fall back to the host shell)
/// - `sed` / `sed -i[SUFFIX]` / `sed -e` with `s/PAT/REPL/[g]` on files
/// - `fmt` / `fmt -w WIDTH` with file operands (default width 75)
/// - `tsort FILE` (whitespace pair lists; detects cycles)
/// - `tr -d SET` / `tr SET1 SET2` with file operands (no stdin)
bool tryExecute(llvm::StringRef Command, int &ExitCode,
                bool EchoCommand = false, bool *Echoed = nullptr);

} // namespace builtins
} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_BUILTINCOMMANDS_H

#!/usr/bin/env bash
#
# Extract a minimal Linux sysroot from Ubuntu 22.04 (jammy) deb packages.
# Works on macOS and Linux — no dpkg or apt required.
#
# Usage:
#   ./extract-sysroot.sh          # Extract both x64 and arm64
#   ./extract-sysroot.sh x64      # Extract x64 only
#   ./extract-sysroot.sh arm64    # Extract arm64 only

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="${SCRIPT_DIR}/.download-cache"

UBUNTU_RELEASE="jammy"          # 22.04 LTS
UBUNTU_RELEASE_UPDATES="jammy-updates"

AMD64_MIRROR="http://archive.ubuntu.com/ubuntu"
ARM64_MIRROR="http://ports.ubuntu.com/ubuntu-ports"

# Packages to extract (package_name:component)
PACKAGES=(
  "libc6-dev:main"
  "libc6:main"
  "libc-dev-bin:main"
  "linux-libc-dev:main"
  "libgcc-12-dev:main"
  "libgcc-s1:main"
  "zlib1g-dev:main"
  "zlib1g:main"
  "libcrypt-dev:main"
  "libcrypt1:main"
)

extract_deb() {
  local deb_file="$1"
  local dest_dir="$2"
  local tmp_dir
  tmp_dir="$(mktemp -d)"

  (
    cd "$tmp_dir"
    ar x "$deb_file"
    local data_tar
    data_tar=$(ls data.tar.* 2>/dev/null | head -1)
    if [ -z "$data_tar" ]; then
      echo "  ERROR: No data.tar.* found in $deb_file" >&2
      rm -rf "$tmp_dir"
      return 1
    fi
    mkdir -p "$dest_dir"
    tar xf "$data_tar" -C "$dest_dir" 2>/dev/null || true
  )
  rm -rf "$tmp_dir"
}

find_package_url() {
  local mirror="$1"
  local release="$2"
  local component="$3"
  local arch="$4"
  local pkg_name="$5"
  local packages_file="${WORK_DIR}/Packages_${release}_${component}_${arch}"

  if [ ! -f "$packages_file" ]; then
    echo "  Fetching package index: ${release}/${component}/${arch}..." >&2
    mkdir -p "$WORK_DIR"
    local url="${mirror}/dists/${release}/${component}/binary-${arch}/Packages.gz"
    curl -sSfL "$url" | gzip -d > "$packages_file" 2>/dev/null || {
      echo "  WARNING: Could not fetch $url" >&2
      return 1
    }
  fi

  # Parse the Packages file for exact package name match
  awk -v pkg="$pkg_name" '
    /^Package:/ { found = ($2 == pkg) }
    /^Filename:/ && found { print $2; exit }
  ' "$packages_file"
}

download_and_extract() {
  local mirror="$1"
  local arch="$2"       # amd64 | arm64
  local dest_dir="$3"

  echo "=== Extracting sysroot for ${arch} ==="
  mkdir -p "$dest_dir" "$WORK_DIR"

  for pkg_spec in "${PACKAGES[@]}"; do
    local pkg_name="${pkg_spec%%:*}"
    local component="${pkg_spec##*:}"

    # Try updates first, then base release
    local filename=""
    for release in "$UBUNTU_RELEASE_UPDATES" "$UBUNTU_RELEASE"; do
      filename=$(find_package_url "$mirror" "$release" "$component" "$arch" "$pkg_name") || true
      if [ -n "$filename" ]; then
        break
      fi
    done

    if [ -z "$filename" ]; then
      echo "  WARNING: Package ${pkg_name} not found for ${arch}, skipping" >&2
      continue
    fi

    local deb_url="${mirror}/${filename}"
    local deb_basename
    deb_basename=$(basename "$filename")
    local deb_path="${WORK_DIR}/${deb_basename}"

    if [ ! -f "$deb_path" ]; then
      echo "  Downloading ${pkg_name} (${deb_basename})..."
      curl -sSfL -o "$deb_path" "$deb_url" || {
        echo "  ERROR: Failed to download ${deb_url}" >&2
        continue
      }
    else
      echo "  Using cached ${deb_basename}"
    fi

    echo "  Extracting ${pkg_name}..."
    extract_deb "$deb_path" "$dest_dir"
  done
}

reorganize_sysroot() {
  local raw_dir="$1"      # Where debs were extracted (has usr/, lib/, etc.)
  local sysroot_dir="$2"  # Final sysroot directory
  local multiarch="$3"    # e.g. x86_64-linux-gnu

  echo "  Reorganizing into sysroot layout..."

  mkdir -p "${sysroot_dir}/usr/include"
  mkdir -p "${sysroot_dir}/usr/lib/${multiarch}"
  mkdir -p "${sysroot_dir}/usr/lib/gcc/${multiarch}"
  mkdir -p "${sysroot_dir}/lib/${multiarch}"

  # Headers: /usr/include/
  if [ -d "${raw_dir}/usr/include" ]; then
    cp -a "${raw_dir}/usr/include/." "${sysroot_dir}/usr/include/"
  fi

  # Libraries from /usr/lib/<multiarch>/
  if [ -d "${raw_dir}/usr/lib/${multiarch}" ]; then
    # Copy .a files, .o files, and linker scripts (skip symlinks — they
    # point at absolute /lib/... paths and break cmake copy on macOS).
    find "${raw_dir}/usr/lib/${multiarch}" -maxdepth 1 \
      \( -name '*.a' -o -name '*.o' -o \( -name 'lib*.so' ! -type l \) \) \
      -exec cp -a {} "${sysroot_dir}/usr/lib/${multiarch}/" \; 2>/dev/null || true
  fi

  # CRT objects from /usr/lib/<multiarch>/ (crt1.o, crti.o, crtn.o, Scrt1.o)
  for crt in crt1.o crti.o crtn.o Scrt1.o rcrt1.o gcrt1.o; do
    if [ -f "${raw_dir}/usr/lib/${multiarch}/${crt}" ]; then
      cp -a "${raw_dir}/usr/lib/${multiarch}/${crt}" \
            "${sysroot_dir}/usr/lib/${multiarch}/"
    fi
  done

  # GCC runtime from /usr/lib/gcc/<multiarch>/<version>/
  if [ -d "${raw_dir}/usr/lib/gcc/${multiarch}" ]; then
    cp -a "${raw_dir}/usr/lib/gcc/${multiarch}/." \
          "${sysroot_dir}/usr/lib/gcc/${multiarch}/"
  fi

  # Libraries from /lib/<multiarch>/ (e.g., libc.so.6, ld-linux-*.so.*)
  if [ -d "${raw_dir}/lib/${multiarch}" ]; then
    find "${raw_dir}/lib/${multiarch}" -maxdepth 1 \
      \( -name '*.so.*' -o -name 'ld-*.so*' \) \
      -exec cp -a {} "${sysroot_dir}/lib/${multiarch}/" \; 2>/dev/null || true
  fi

  # Linker scripts (e.g., libc.so) contain absolute paths like
  # /lib/x86_64-linux-gnu/libc.so.6.  These are intentionally kept
  # absolute because the --sysroot mechanism automatically prefixes
  # them with the sysroot path at link time.  No path rewriting needed.

  # Create symlinks for the dynamic linker so absolute references
  # like /lib/ld-linux-aarch64.so.1 or /lib64/ld-linux-x86-64.so.2
  # resolve within the sysroot.
  if [ "$multiarch" = "x86_64-linux-gnu" ]; then
    mkdir -p "${sysroot_dir}/lib64"
    ln -sf "../lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" \
           "${sysroot_dir}/lib64/ld-linux-x86-64.so.2"
  elif [ "$multiarch" = "aarch64-linux-gnu" ]; then
    ln -sf "aarch64-linux-gnu/ld-linux-aarch64.so.1" \
           "${sysroot_dir}/lib/ld-linux-aarch64.so.1"
  fi

  # Remove unwanted files to keep the sysroot lean
  find "${sysroot_dir}" -name '*.py' -delete 2>/dev/null || true
  find "${sysroot_dir}" -name '*.pyc' -delete 2>/dev/null || true
  find "${sysroot_dir}" -name 'changelog*' -delete 2>/dev/null || true
  find "${sysroot_dir}" -name 'copyright' -delete 2>/dev/null || true
  rm -rf "${sysroot_dir}/usr/share" 2>/dev/null || true

  echo "  Done."
}

build_arch() {
  local arch_label="$1"   # x64 | arm64
  local deb_arch="$2"     # amd64 | arm64
  local multiarch="$3"    # x86_64-linux-gnu | aarch64-linux-gnu
  local mirror="$4"

  local raw_dir="${WORK_DIR}/raw-${deb_arch}"
  local sysroot_dir="${SCRIPT_DIR}/${arch_label}"

  if [ -d "$sysroot_dir" ] && [ -n "$(ls -A "$sysroot_dir" 2>/dev/null)" ]; then
    echo "=== ${arch_label} sysroot already exists, skipping (delete to rebuild) ==="
    return
  fi

  rm -rf "$raw_dir"
  mkdir -p "$raw_dir"

  download_and_extract "$mirror" "$deb_arch" "$raw_dir"
  reorganize_sysroot "$raw_dir" "$sysroot_dir" "$multiarch"

  rm -rf "$raw_dir"

  echo "=== ${arch_label} sysroot ready at ${sysroot_dir} ==="
  echo ""
}

main() {
  local targets="${1:-both}"

  echo "NeverC Linux Sysroot Extractor"
  echo "Ubuntu 22.04 (jammy) — glibc 2.35"
  echo ""

  case "$targets" in
    x64)
      build_arch "x64" "amd64" "x86_64-linux-gnu" "$AMD64_MIRROR"
      ;;
    arm64)
      build_arch "arm64" "arm64" "aarch64-linux-gnu" "$ARM64_MIRROR"
      ;;
    both|"")
      build_arch "x64" "amd64" "x86_64-linux-gnu" "$AMD64_MIRROR"
      build_arch "arm64" "arm64" "aarch64-linux-gnu" "$ARM64_MIRROR"
      ;;
    *)
      echo "Usage: $0 [x64|arm64|both]" >&2
      exit 1
      ;;
  esac

  echo "All done. Sysroot contents:"
  for d in x64 arm64; do
    if [ -d "${SCRIPT_DIR}/${d}" ]; then
      echo "  ${d}/ $(find "${SCRIPT_DIR}/${d}" -type f | wc -l | tr -d ' ') files"
    fi
  done
}

main "$@"

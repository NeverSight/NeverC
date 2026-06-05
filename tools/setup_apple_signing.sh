#!/usr/bin/env bash
set -euo pipefail

# Setup Apple Developer ID code signing for GitHub Actions — no Xcode required.
#
# Two-phase workflow:
#   Phase 1: generate CSR + private key
#   Phase 2: process downloaded .cer → .p12 → GitHub Secrets
#
# Usage:
#   ./setup_apple_signing.sh csr          # Phase 1: generate CSR
#   ./setup_apple_signing.sh setup <.cer>  # Phase 2: process cert + set secrets
#   ./setup_apple_signing.sh              # Interactive — guides you through both

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

info()  { printf "${CYAN}[INFO]${NC}  %s\n" "$*"; }
ok()    { printf "${GREEN}[OK]${NC}    %s\n" "$*"; }
warn()  { printf "${YELLOW}[WARN]${NC}  %s\n" "$*"; }
die()   { printf "${RED}[ERROR]${NC} %s\n" "$*" >&2; exit 1; }
step()  { printf "\n${BOLD}── %s ──${NC}\n\n" "$*"; }

WORK_DIR="${HOME}/.neverc-signing"
KEY_PATH="$WORK_DIR/developer_id.key"
CSR_PATH="$WORK_DIR/developer_id.csr"

mkdir -p "$WORK_DIR"

# ══════════════════════════════════════════════════════════════
# Phase 1: Generate CSR + private key
# ══════════════════════════════════════════════════════════════
phase_csr() {
    step "Phase 1: Generate Certificate Signing Request"

    if [ -f "$KEY_PATH" ] && [ -f "$CSR_PATH" ]; then
        warn "CSR and private key already exist at $WORK_DIR"
        printf "${YELLOW}Overwrite? [y/N]: ${NC}"
        read -r OVERWRITE
        if [[ ! "$OVERWRITE" =~ ^[Yy]$ ]]; then
            info "Using existing CSR: $CSR_PATH"
            return 0
        fi
    fi

    printf "${CYAN}Organization name [NEVERSIGHT LLC]: ${NC}"
    read -r ORG_NAME
    ORG_NAME="${ORG_NAME:-NEVERSIGHT LLC}"

    printf "${CYAN}Country code [US]: ${NC}"
    read -r COUNTRY
    COUNTRY="${COUNTRY:-US}"

    printf "${CYAN}Your Apple ID email: ${NC}"
    read -r EMAIL
    [ -n "$EMAIL" ] || die "Email cannot be empty"

    info "Generating 2048-bit RSA key + CSR..."
    info "(Apple overrides most CSR fields with your developer account info)"

    openssl req -new -newkey rsa:2048 -nodes \
        -keyout "$KEY_PATH" \
        -out "$CSR_PATH" \
        -subj "/C=$COUNTRY/O=$ORG_NAME/CN=$ORG_NAME/emailAddress=$EMAIL" \
        2>/dev/null

    chmod 600 "$KEY_PATH"

    ok "Private key: $KEY_PATH"
    ok "CSR file:    $CSR_PATH"

    echo ""
    printf "${BOLD}Now do this:${NC}\n"
    echo ""
    echo "  1. Open: https://developer.apple.com/account/resources/certificates/add"
    echo "  2. Select: Developer ID Application"
    echo "  3. Click Continue"
    echo "  4. Upload this file: $CSR_PATH"
    echo "  5. Click Continue → Download the .cer file"
    echo ""
    echo "  Then run:"
    echo "    $0 setup ~/Downloads/developerID_application.cer"
    echo ""
}

# ══════════════════════════════════════════════════════════════
# Phase 2: Process .cer → .p12 → Keychain → GitHub Secrets
# ══════════════════════════════════════════════════════════════
phase_setup() {
    local CER_FILE="${1:-}"

    step "Phase 2: Process certificate + configure GitHub Secrets"

    # ── Validate inputs ───────────────────────────────────────
    [ -n "$CER_FILE" ] || die "Usage: $0 setup <path-to-downloaded.cer>"
    [ -f "$CER_FILE" ]  || die "File not found: $CER_FILE"
    [ -f "$KEY_PATH" ]  || die "Private key not found at $KEY_PATH — run '$0 csr' first"

    command -v openssl >/dev/null  || die "openssl not found"
    command -v security >/dev/null || die "security not found (are you on macOS?)"
    command -v codesign >/dev/null || die "codesign not found (are you on macOS?)"
    command -v gh >/dev/null       || die "GitHub CLI (gh) not installed — run: brew install gh"
    gh auth status >/dev/null 2>&1 || die "Not logged in to gh — run: gh auth login"

    TMPDIR_SIGN=$(mktemp -d)
    trap 'rm -rf "$TMPDIR_SIGN"' EXIT

    P12_PATH="$TMPDIR_SIGN/developer_id.p12"
    CERT_PEM="$TMPDIR_SIGN/developer_id.pem"
    INTERMEDIATE_DER="$TMPDIR_SIGN/DeveloperIDG2CA.cer"
    INTERMEDIATE_PEM="$TMPDIR_SIGN/DeveloperIDG2CA.pem"

    # ── Convert .cer (DER) → .pem ────────────────────────────
    info "Converting certificate..."
    openssl x509 -inform DER -in "$CER_FILE" -out "$CERT_PEM" 2>/dev/null \
        || openssl x509 -inform PEM -in "$CER_FILE" -out "$CERT_PEM" 2>/dev/null \
        || die "Cannot parse certificate file: $CER_FILE"
    ok "Certificate converted"

    # Extract Team ID from certificate subject
    TEAM_ID=$(openssl x509 -in "$CERT_PEM" -noout -subject 2>/dev/null \
        | grep -oE 'OU = [A-Z0-9]{10}' | head -1 | awk '{print $3}' || true)
    if [ -z "$TEAM_ID" ]; then
        TEAM_ID=$(openssl x509 -in "$CERT_PEM" -noout -text 2>/dev/null \
            | grep -oE '\([A-Z0-9]{10}\)' | head -1 | tr -d '()' || true)
    fi
    if [ -z "$TEAM_ID" ]; then
        printf "${CYAN}Could not auto-detect Team ID. Enter manually (from developer.apple.com/account): ${NC}"
        read -r TEAM_ID
    fi
    ok "Team ID: $TEAM_ID"

    # ── Download Apple intermediate certificate ───────────────
    info "Downloading Apple Developer ID intermediate certificate..."
    curl -fsSL "https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer" \
        -o "$INTERMEDIATE_DER" \
        || die "Failed to download Apple intermediate certificate"
    openssl x509 -inform DER -in "$INTERMEDIATE_DER" -out "$INTERMEDIATE_PEM" 2>/dev/null
    ok "Intermediate certificate ready"

    # ── Create .p12 ───────────────────────────────────────────
    info "Creating .p12 (certificate + private key bundle)..."
    printf "${CYAN}Set a password for the .p12 export: ${NC}"
    read -rs P12_PASSWORD
    echo ""
    printf "${CYAN}Confirm password: ${NC}"
    read -rs P12_PASSWORD_CONFIRM
    echo ""

    [ "$P12_PASSWORD" = "$P12_PASSWORD_CONFIRM" ] || die "Passwords don't match"
    [ -n "$P12_PASSWORD" ] || die "Password cannot be empty"

    openssl pkcs12 -export \
        -out "$P12_PATH" \
        -inkey "$KEY_PATH" \
        -in "$CERT_PEM" \
        -certfile "$INTERMEDIATE_PEM" \
        -password "pass:$P12_PASSWORD" \
        2>/dev/null \
    || die "Failed to create .p12"

    ok "Created .p12 ($(wc -c < "$P12_PATH" | tr -d ' ') bytes)"

    # ── Import into Keychain (for local codesign use) ─────────
    info "Importing certificate into login Keychain..."
    security import "$P12_PATH" \
        -k ~/Library/Keychains/login.keychain-db \
        -P "$P12_PASSWORD" \
        -T /usr/bin/codesign \
        -T /usr/bin/security \
        2>/dev/null \
    || warn "Keychain import had warnings (may already exist — usually OK)"

    IDENTITY=$(security find-identity -v -p codesigning 2>/dev/null \
        | grep "Developer ID Application" | head -1 || true)
    if [ -n "$IDENTITY" ]; then
        ok "Certificate available for codesigning"
        echo "    $IDENTITY"
    else
        warn "Certificate imported but not yet visible for codesigning"
        warn "You may need to open Keychain Access and trust it manually"
    fi

    # ── Base64 encode ─────────────────────────────────────────
    CERT_B64=$(base64 -i "$P12_PATH")
    ok "Base64 encoded"

    # ── Collect notarization credentials ──────────────────────
    echo ""
    info "For notarization, we need your Apple ID and an App-Specific Password."
    echo ""
    echo "  Generate one at: https://appleid.apple.com"
    echo "  → Sign-In and Security → App-Specific Passwords → +"
    echo ""

    printf "${CYAN}Apple ID (email): ${NC}"
    read -r APPLE_ID
    [ -n "$APPLE_ID" ] || die "Apple ID cannot be empty"

    printf "${CYAN}App-Specific Password (e.g. abcd-efgh-ijkl-mnop): ${NC}"
    read -rs NOTARIZATION_PASSWORD
    echo ""
    [ -n "$NOTARIZATION_PASSWORD" ] || die "Notarization password cannot be empty"

    # ── Detect GitHub repo ────────────────────────────────────
    REPO=$(gh repo view --json nameWithOwner -q '.nameWithOwner' 2>/dev/null || true)
    if [ -z "$REPO" ]; then
        printf "${CYAN}GitHub repo (owner/name): ${NC}"
        read -r REPO
    fi
    ok "Target repo: $REPO"

    # ── Set GitHub Secrets ────────────────────────────────────
    step "Setting GitHub Actions secrets"

    echo "$CERT_B64"              | gh secret set APPLE_CERTIFICATE_BASE64    --repo "$REPO"
    ok "APPLE_CERTIFICATE_BASE64"

    echo "$P12_PASSWORD"          | gh secret set APPLE_CERTIFICATE_PASSWORD  --repo "$REPO"
    ok "APPLE_CERTIFICATE_PASSWORD"

    echo "$APPLE_ID"              | gh secret set APPLE_ID                    --repo "$REPO"
    ok "APPLE_ID"

    echo "$NOTARIZATION_PASSWORD" | gh secret set APPLE_NOTARIZATION_PASSWORD --repo "$REPO"
    ok "APPLE_NOTARIZATION_PASSWORD"

    echo "$TEAM_ID"               | gh secret set APPLE_TEAM_ID              --repo "$REPO"
    ok "APPLE_TEAM_ID"

    # ── Verify ────────────────────────────────────────────────
    step "Verification"

    SECRETS=$(gh secret list --repo "$REPO" 2>/dev/null || true)
    ALL_SET=true
    for s in APPLE_CERTIFICATE_BASE64 APPLE_CERTIFICATE_PASSWORD APPLE_ID APPLE_NOTARIZATION_PASSWORD APPLE_TEAM_ID; do
        if echo "$SECRETS" | grep -q "^$s"; then
            ok "$s"
        else
            warn "$s — not found!"
            ALL_SET=false
        fi
    done

    echo ""
    if [ "$ALL_SET" = true ]; then
        printf "${GREEN}${BOLD}All done!${NC}\n"
        echo ""
        echo "  Team ID:   $TEAM_ID"
        echo "  Repo:      $REPO"
        echo ""
        info "Cleaning up private key at $KEY_PATH..."
        printf "${YELLOW}Delete private key file? [Y/n]: ${NC}"
        read -r DEL_KEY
        if [[ ! "$DEL_KEY" =~ ^[Nn]$ ]]; then
            rm -f "$KEY_PATH" "$CSR_PATH"
            rm -rf "$WORK_DIR" 2>/dev/null || true
            ok "Cleaned up"
        else
            warn "Private key kept at $KEY_PATH — delete it manually when done"
        fi
    else
        warn "Some secrets may not have been set. Check: gh secret list --repo $REPO"
    fi
}

# ══════════════════════════════════════════════════════════════
# Interactive mode
# ══════════════════════════════════════════════════════════════
interactive() {
    echo ""
    printf "${BOLD}Apple Developer ID Signing Setup for GitHub Actions${NC}\n"
    echo ""

    if [ -f "$KEY_PATH" ] && [ -f "$CSR_PATH" ]; then
        info "Found existing CSR from a previous run."
        echo ""
        echo "  If you already downloaded the .cer from Apple, run:"
        echo "    $0 setup <path-to.cer>"
        echo ""
        echo "  If you need to re-generate the CSR:"
        echo "    $0 csr"
        echo ""
    else
        info "No existing CSR found. Starting from Phase 1..."
        phase_csr
    fi
}

# ══════════════════════════════════════════════════════════════
# Entry point
# ══════════════════════════════════════════════════════════════
case "${1:-}" in
    csr)    phase_csr ;;
    setup)  phase_setup "${2:-}" ;;
    *)      interactive ;;
esac

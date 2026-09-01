[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$CertificatePath,
  [Parameter(Mandatory = $true)][string]$ThumbprintPath,
  [Parameter(Mandatory = $true)][string]$StageLogPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-CertificateStage {
  param([Parameter(Mandatory = $true)][string]$Stage)
  $line = "CERTIFICATE_STAGE=$Stage"
  $line | Tee-Object -FilePath $StageLogPath -Append | Out-Host
}

foreach ($path in @($CertificatePath, $ThumbprintPath, $StageLogPath)) {
  $parent = Split-Path $path -Parent
  if ($parent) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
  }
}
Set-Content -LiteralPath $StageLogPath -Value 'NeverC VBS enclave certificate bootstrap' -Encoding utf8

$rsa = $null
$generated = $null
$certificate = $null
$rootCertificate = $null
try {
  Write-CertificateStage 'GenerateKey'
  $rsa = [Security.Cryptography.RSA]::Create()
  $rsa.KeySize = 2048

  Write-CertificateStage 'CreateRequest'
  $subject = [Security.Cryptography.X509Certificates.X500DistinguishedName]::new(
    'CN=NeverC ephemeral VBS enclave CI')
  $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
    $subject,
    $rsa,
    [Security.Cryptography.HashAlgorithmName]::SHA256,
    [Security.Cryptography.RSASignaturePadding]::Pkcs1)
  [void]$request.CertificateExtensions.Add(
    [Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new(
      $false, $false, 0, $false))
  [void]$request.CertificateExtensions.Add(
    [Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
      [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature,
      $false))
  $enhancedKeyUsages = [Security.Cryptography.OidCollection]::new()
  foreach ($oidValue in @(
      '1.3.6.1.5.5.7.3.3',
      '1.3.6.1.4.1.311.76.57.1.15',
      '1.3.6.1.4.1.311.97.814040577.346743379.4783502.105532346')) {
    [void]$enhancedKeyUsages.Add([Security.Cryptography.Oid]::new($oidValue))
  }
  [void]$request.CertificateExtensions.Add(
    [Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new(
      $enhancedKeyUsages, $false))
  [void]$request.CertificateExtensions.Add(
    [Security.Cryptography.X509Certificates.X509SubjectKeyIdentifierExtension]::new(
      $request.PublicKey, $false))

  Write-CertificateStage 'CreateSelfSigned'
  $notBefore = [DateTimeOffset]::UtcNow.AddMinutes(-5)
  $generated = $request.CreateSelfSigned($notBefore, $notBefore.AddDays(2))
  $password = [Guid]::NewGuid().ToString('N')
  $pfxBytes = $generated.Export(
    [Security.Cryptography.X509Certificates.X509ContentType]::Pfx, $password)
  $keyFlags =
    [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::UserKeySet
  $keyFlags = $keyFlags -bor
    [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::PersistKeySet
  $keyFlags = $keyFlags -bor
    [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::Exportable
  $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
    $pfxBytes, $password, $keyFlags)
  if (-not $certificate.HasPrivateKey) {
    throw 'generated certificate lost its private key during persistence'
  }

  Write-CertificateStage 'InstallPersonal'
  $personalStore = [Security.Cryptography.X509Certificates.X509Store]::new(
    [Security.Cryptography.X509Certificates.StoreName]::My,
    [Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser)
  try {
    $personalStore.Open(
      [Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    $personalStore.Add($certificate)
  } finally {
    $personalStore.Close()
    $personalStore.Dispose()
  }

  Write-CertificateStage 'ExportPublic'
  $publicBytes = $certificate.Export(
    [Security.Cryptography.X509Certificates.X509ContentType]::Cert)
  [IO.File]::WriteAllBytes($CertificatePath, $publicBytes)
  $rootCertificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
    $publicBytes)

  Write-CertificateStage 'InstallTrustedRoot'
  $rootStore = [Security.Cryptography.X509Certificates.X509Store]::new(
    [Security.Cryptography.X509Certificates.StoreName]::Root,
    [Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser)
  try {
    $rootStore.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    $rootStore.Add($rootCertificate)
  } finally {
    $rootStore.Close()
    $rootStore.Dispose()
  }

  $certificate.Thumbprint | Set-Content -LiteralPath $ThumbprintPath -Encoding ascii
  Write-CertificateStage 'Complete'
  Write-Host "CERTIFICATE_THUMBPRINT=$($certificate.Thumbprint)"
} finally {
  if ($rootCertificate) { $rootCertificate.Dispose() }
  if ($certificate) { $certificate.Dispose() }
  if ($generated) { $generated.Dispose() }
  if ($rsa) { $rsa.Dispose() }
}

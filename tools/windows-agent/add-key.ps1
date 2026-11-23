# Adds an SSH public key for logins (admin accounts read administrators_authorized_keys).
#   add-key.ps1 -PublicKey "ssh-ed25519 AAAA... name"
param([Parameter(Mandatory = $true)][string]$PublicKey)
$k = $PublicKey.Trim()
$f = "$env:ProgramData\ssh\administrators_authorized_keys"
if (-not ((Test-Path $f) -and (Select-String -Quiet -SimpleMatch $k $f))) { Add-Content -Force $f $k }
icacls.exe $f /inheritance:r /grant "Administrators:F" /grant "SYSTEM:F" | Out-Null
$u = "$env:USERPROFILE\.ssh"; New-Item -Force -ItemType Directory $u | Out-Null
if (-not ((Test-Path "$u\authorized_keys") -and (Select-String -Quiet -SimpleMatch $k "$u\authorized_keys"))) { Add-Content "$u\authorized_keys" $k }
"key installed"

# Starts race-shot.cmd in the logged-in user's desktop session (programs started over SSH
# have no desktop, so the game could not open a window or use the GPU there).
param([string]$Exe = "D:\gt3r\gt3r-windows-x64\gt3r.exe", [int]$WaitSeconds = 90, [string]$Script = "D:\gt3r\race-shot.cmd", [string]$ScriptArgs = "", [string]$Shot = "D:\gt3r\win-race.png")
$user = ((query user) -match "Active" | Select-Object -First 1) -replace "^\s*>?(\S+).*", '$1'
$action = New-ScheduledTaskAction -Execute "cmd.exe" -Argument "/c $Script `"$Exe`" $ScriptArgs"
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive
Register-ScheduledTask -TaskName "gt3r-race-shot" -Action $action -Principal $principal -Force | Out-Null
Start-ScheduledTask -TaskName "gt3r-race-shot"
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $WaitSeconds) {
    Start-Sleep -Seconds 2
    if ((Get-ScheduledTask -TaskName "gt3r-race-shot").State -ne "Running" -and ((Get-Date) - $t0).TotalSeconds -gt 5) { break }
}
Unregister-ScheduledTask -TaskName "gt3r-race-shot" -Confirm:$false
"user: $user"
"screenshot: $(Test-Path $Shot)"
if (Test-Path D:\gt3r\race-shot.log) { Get-Content D:\gt3r\race-shot.log -Tail 5 }

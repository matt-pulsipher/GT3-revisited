# Asks XInput directly which controller slots are connected (run in the desktop session).
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public static class XI {
  [StructLayout(LayoutKind.Sequential)] public struct GP { public ushort b; public byte lt, rt; public short lx, ly, rx, ry; }
  [StructLayout(LayoutKind.Sequential)] public struct ST { public uint pkt; public GP gp; }
  [DllImport("xinput1_4.dll")] public static extern uint XInputGetState(uint i, out ST s);
}
"@
$out = @()
for ($i = 0; $i -lt 4; $i++) {
  $s = New-Object XI+ST
  $r = [XI]::XInputGetState($i, [ref]$s)
  $out += "slot ${i}: " + $(if ($r -eq 0) { "CONNECTED buttons=0x{0:X4} lt={1} rt={2} lx={3}" -f $s.gp.b, $s.gp.lt, $s.gp.rt, $s.gp.lx } else { "empty (code $r)" })
}
$out | Set-Content D:\gt3r\xinput.txt

param([string]$Log = "out\opt-boot.log", [hashtable]$Extra = @{})
# Starts FM4 (target\build\fm4\fm4.exe) with the test-grid settings and keeps its window minimized, not activated.
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $root
$envs = @{
  FM2_NO_LAUNCHER='1'; FM2_VSYNC='0'; FM2_MAX_FPS='1000'; REGPU_EARLY_FENCES='1'; REGPU_RESOLVE_WRITEBACK_MAX='1024';
  REGPU_DEBUG='1'; REGPU_DEBUG_PORT='7351'; REGPU_VBLANK_HZ='1000'; FM4_GRID_TEST='1'; FM4_GRID_FREEZE_AT='9.0'
}
foreach ($k in $Extra.Keys) { $envs[$k] = $Extra[$k] }
foreach ($k in $envs.Keys) { [Environment]::SetEnvironmentVariable($k, $envs[$k], 'Process') }
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W { [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h); }
"@
$p = Start-Process -FilePath "$root\build\fm4\fm4.exe" -ArgumentList "game" -WorkingDirectory $root `
  -RedirectStandardOutput "$root\$Log" -RedirectStandardError "$root\$Log.err" -WindowStyle Minimized -PassThru
for ($i = 0; $i -lt 200; $i++) {
  Start-Sleep -Milliseconds 50
  $p.Refresh()
  if ($p.MainWindowHandle -ne 0) { [W]::ShowWindow($p.MainWindowHandle, 7) | Out-Null } # SW_SHOWMINNOACTIVE
  if ($i -gt 60 -and $p.MainWindowHandle -ne 0 -and [W]::IsIconic($p.MainWindowHandle)) { break }
}
"pid $($p.Id) window $($p.MainWindowHandle) minimized $([W]::IsIconic($p.MainWindowHandle))"

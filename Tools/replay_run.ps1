# Replays one operator trajectory once per method, deterministically (FTeleopReplay: fixed 1/Fps time step, so every
# run sees the same robot motion, captures and latency draws), and writes per method <OutDir>/<name>/eye_%05d.png
# (operator's left eye, desktop view), obs_%05d.png (observer view, every ObsEvery frames) and frames.csv.
# Standalone desktop processes (no editor, no headset); the editor may stay closed. Then: Tools/replay_metrics.py.
# Example: .\replay_run.ps1 -Trajectory traj.csv ; .\replay_run.ps1 -Methods "GT|teleop.Method=5","GT2|teleop.Method=5" (determinism check)
param(
  [Parameter(Mandatory = $true)][string]$Trajectory,
  [string]$OutDir = "",
  [string[]]$Methods = @("GT|teleop.Method=5", "M0|teleop.Method=0", "M1|teleop.Method=1", "M2|teleop.Method=2", "M3|teleop.Method=3",
    "M4f0|teleop.Method=4;teleop.M4.Fill=0", "M4f1|teleop.Method=4;teleop.M4.Fill=1", "M4f2|teleop.Method=4;teleop.M4.Fill=2"),
  # Same for every method: no HUD (GT has none), WiFi latency, ZEDlike depth, Waist3Neck2 (the defaults, set explicitly).
  [string]$Common = "teleop.Debug.HideHud=1;teleop.LatencyPreset=2;teleop.DepthPreset=1;teleop.NeckMode=4;teleop.M4.Sphere=2",
  [double]$From = 1.0, [int]$ObsEvery = 3, [int]$Fps = 72, [int]$ResX = 1280, [int]$ResY = 720,
  [string]$Engine = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe",
  [string]$Project = ""
)
# ($PSScriptRoot is empty in parameter defaults on Windows PowerShell 5.1.)
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutDir) { $OutDir = Join-Path $Root ("..\Reprojection\Saved\TeleopReplay\" + (Get-Date -Format "yyyy-MM-dd")) }
if (-not $Project) { $Project = Join-Path $Root "..\Reprojection\Reprojection.uproject" }
# powershell -File passes "a","b" as one string: split on commas as well.
$Methods = @($Methods | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$Trajectory = (Resolve-Path $Trajectory).Path
$Project = (Resolve-Path $Project).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
Copy-Item $Trajectory (Join-Path $OutDir "trajectory.csv") -Force
foreach ($m in $Methods) {
  $name, $cvars = $m -split "\|", 2
  $dir = Join-Path $OutDir $name
  Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
  $log = Join-Path $OutDir "$name.log"
  $args = @("`"$Project`"", "/Game/VRTemplate/VRTemplateMap", "-game", "-nohmd", "-windowed", "-ResX=$ResX", "-ResY=$ResY", "-nosplash", "-nosound",
    "-UseFixedTimeStep", "-FPS=$Fps", "-TeleopReplay=`"$Trajectory`"", "-TeleopReplayOut=`"$dir`"", "-TeleopReplayFrom=$From",
    "-TeleopReplayObsEvery=$ObsEvery", "-TeleopSet=`"$Common;$cvars`"", "-abslog=`"$log`"")
  $t0 = Get-Date
  $p = Start-Process -FilePath $Engine -ArgumentList $args -PassThru
  if (-not $p.WaitForExit(900000)) { Stop-Process -Id $p.Id -Force; "$name TIMEOUT"; continue }
  $done = Select-String -Path $log -Pattern "\[replay\] done" | Select-Object -Last 1
  $missed = (Select-String -Path $log -Pattern "was not drawn").Count
  "{0}: {1} s, {2}, missed frames {3}" -f $name, [int]((Get-Date) - $t0).TotalSeconds, ($(if ($done) { $done.Line -replace "^.*\[replay\] ", "" } else { "NOT DONE" })), $missed
}

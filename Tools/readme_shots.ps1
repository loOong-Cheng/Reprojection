# Captures the README screenshots during VR Preview (Quest or Meta XR Simulator), driven through the mcp-unreal editor
# plugin (http://127.0.0.1:8090). Sway the operator (the robot follows, M2's radius and the memory build up), settle,
# freeze, shift or turn the operator by the offset (a move the robot has not followed), then capture every method.
# Per method: <Tag>_<name>_eyes.png (both HMD eye buffers, teleop.Eval.Shot) and <Tag>_<name>_obs (observer render
# target, PNG). Tools/readme_compose.py puts them into the spectator layout.
# Example: .\readme_shots.ps1 -Tag move -OffY 15 ; .\readme_shots.ps1 -Tag turn -Yaw 25 -Methods "GT|teleop.Method 5","M0|teleop.Method 0"
param([string]$Tag = "move", [double]$OffX = 0, [double]$OffY = 15, [double]$OffZ = 0, [double]$Yaw = 0,
      [string]$OutDir = "",
      [string[]]$Methods = @("GT|teleop.Method 5", "M0|teleop.Method 0", "M1|teleop.Method 1", "M2|teleop.Method 2",
        "M3|teleop.Method 3", "M4f0|teleop.Method 4;teleop.M4.Fill 0", "M4f1|teleop.Method 4;teleop.M4.Fill 1",
        "M4f2|teleop.Method 4;teleop.M4.Fill 2"))
# ($PSScriptRoot is empty in parameter defaults on Windows PowerShell 5.1.)
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutDir) { $OutDir = Join-Path $Root "..\Reprojection\Saved\ReadmeShots" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$Log = Join-Path $Root "..\Reprojection\Saved\Logs\Reprojection.log"
function Post([string]$Route, [hashtable]$Body) {
  [void](Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:8090/api/editor/$Route" -Body ($Body | ConvertTo-Json) -ContentType "application/json")
}
function Cmd([string]$c) { Post "console_command" @{ command = $c; world = "pie" } }
function Map([double]$x, [double]$y, [double]$z, [double]$yaw) {
  Cmd "teleop.Debug.MapOffsetX $x"; Cmd "teleop.Debug.MapOffsetY $y"; Cmd "teleop.Debug.MapOffsetZ $z"; Cmd "teleop.Debug.MapYawDeg $yaw"
}

Cmd "teleop.Debug.Freeze 0"; Map 0 0 0 0
Cmd "teleop.Method 4"; Cmd "teleop.M4.Fill 2"; Cmd "teleop.M4.Sphere 2"
Start-Sleep -Milliseconds 800; Cmd "teleop.Memory.Clear"

# 3 s sway: +-2 cm forward, +-6 cm sideways, +-2 cm up, +-6 deg yaw.
$t0 = Get-Date
while (($t = ((Get-Date) - $t0).TotalSeconds) -lt 3.0) {
  Map (2 * [Math]::Sin(2 * [Math]::PI * $t / 2.0)) (6 * [Math]::Sin(2 * [Math]::PI * $t / 1.5)) (2 * [Math]::Sin(2 * [Math]::PI * $t / 1.0)) (6 * [Math]::Sin(2 * [Math]::PI * $t / 3.0))
  Start-Sleep -Milliseconds 40
}
Map 0 0 0 0
Start-Sleep -Milliseconds 2600   # longer than M2's 2 s window
Cmd "teleop.Debug.Freeze 1"
Start-Sleep -Milliseconds 200
Map $OffX $OffY $OffZ $Yaw
Start-Sleep -Milliseconds 600
foreach ($m in $Methods) {
  $name, $cmds = $m -split "\|", 2
  foreach ($c in ($cmds -split ";")) { Cmd $c }
  Start-Sleep -Milliseconds 1800
  Cmd "teleop.Eval.State readme_${Tag}_$name"
  Cmd ("teleop.Eval.Shot " + (($OutDir -replace "\\", "/") + "/${Tag}_${name}_eyes.png"))
  Post "execute_script" @{ world = "pie"; script = "import unreal`nw = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()`nobs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.TeleopObserver)[0]`nunreal.RenderingLibrary.export_render_target(w, obs.get_render_target(), r'$OutDir', '${Tag}_${name}_obs')" }
  Start-Sleep -Milliseconds 600
  "captured $name"
}
Select-String -Path $Log -Pattern "\[eval\] tag=readme_${Tag}_" | Select-Object -Last $Methods.Count | ForEach-Object { $_.Line -replace "^.*\[eval\] ", "" }
Cmd "teleop.Debug.Freeze 0"; Map 0 0 0 0

# Lance les tests d'automatisation Gen en headless (port MCP 8011, pas de collision avec l'éditeur principal)
# puis résume Saved/TestReport/index.json. Par défaut : le projet qui contient ce script (arbre principal ou worktree).
# Usage : powershell -File Tools/RunGenTests.ps1 [-Filter "Gen."] [-Project <chemin .uproject>] [-McpPort 8011]
param(
	[string]$Filter = "Gen.",
	[string]$Project = (Join-Path (Split-Path $PSScriptRoot) "Gen.uproject"),
	[int]$McpPort = 8011
)

$root = Split-Path $Project
$report = Join-Path $root "Saved\TestReport"
$log = Join-Path $root "Saved\Logs\GenTestsRun.log"
Remove-Item -Recurse -Force $report -ErrorAction SilentlyContinue

$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList @(
	"`"$Project`"", "-ExecCmds=`"Automation RunTests $Filter;Quit`"", '-unattended', '-nullrhi', '-nosplash', '-nosound',
	"-ReportExportPath=`"$report`"", "-ModelContextProtocolPort=$McpPort", "-abslog=`"$log`""
) -NoNewWindow -PassThru -Wait
"exit $($p.ExitCode), wall $([int]$sw.Elapsed.TotalSeconds) s, log: $log"

$json = Get-Content -Raw -Encoding UTF8 (Join-Path $report "index.json") | ConvertFrom-Json
"succeeded $($json.succeeded), withWarnings $($json.succeededWithWarnings), failed $($json.failed), notRun $($json.notRun)"
foreach ($t in $json.tests) {
	"{0,-22} {1} ({2:N2} s)" -f $t.state, $t.fullTestPath, $t.duration
	foreach ($e in $t.entries) {
		if ($e.event.type -ne "Info") { "    $($e.event.type): $($e.event.message)" }
	}
}

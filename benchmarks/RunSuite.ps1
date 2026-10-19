param(
    [string]$OutputRoot,
    [int]$Threads = 32
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$workspaceRoot = Split-Path $repositoryRoot -Parent
if (-not $OutputRoot) { $OutputRoot = Join-Path $workspaceRoot 'out/benchmarks/cpu-cuda-2026-10-19' }
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$benchmarkExe = Join-Path $workspaceRoot 'out/bin/RelWithDebInfo/SeisComputeBenchmark.exe'
if (-not (Test-Path -LiteralPath $benchmarkExe)) {throw 'Build SeisComputeBenchmark first; see benchmarks/README.md.'}
$null = Get-Command nvidia-smi -ErrorAction Stop
New-Item -ItemType Directory -Force $OutputRoot | Out-Null
powercfg /getactivescheme | Set-Content (Join-Path $OutputRoot 'power-plan.txt') -Encoding utf8
foreach ($dimension in @(2,3)) {
    $caseDirectory = Join-Path $OutputRoot ('formal-' + $dimension + 'd')
    New-Item -ItemType Directory -Force $caseDirectory | Out-Null
    $timedSteps = if ($dimension -eq 2) {100} else {40}
    $arguments = @('--output',('"' + $caseDirectory + '"'),'--threads',[string]$Threads,'--repeats','5','--warmup','20','--steps',[string]$timedSteps,'--dimension',[string]$dimension,'--ftz','0')
    $benchmarkProcess = $null
    try {
        Write-Output ('Starting ' + $dimension + 'D with ' + $Threads + ' CPU workers')
        $benchmarkProcess = Start-Process -FilePath $benchmarkExe -ArgumentList $arguments -RedirectStandardOutput (Join-Path $caseDirectory 'run.log') -RedirectStandardError (Join-Path $caseDirectory 'errors.log') -WindowStyle Hidden -PassThru
        $query = '--query-gpu=timestamp,driver_version,pstate,temperature.gpu,power.draw,clocks.sm,clocks.mem,utilization.gpu'
        nvidia-smi $query --format=csv | Set-Content (Join-Path $caseDirectory 'gpu-telemetry.csv') -Encoding utf8
        while (-not $benchmarkProcess.HasExited) {
            Start-Sleep -Seconds 2
            nvidia-smi $query --format=csv,noheader | Add-Content (Join-Path $caseDirectory 'gpu-telemetry.csv') -Encoding utf8
            $benchmarkProcess.Refresh()
        }
        $benchmarkProcess.WaitForExit()
        Get-Content (Join-Path $caseDirectory 'run.log')
        if ($benchmarkProcess.ExitCode -ne 0) {
            Get-Content (Join-Path $caseDirectory 'errors.log')
            throw ('Benchmark failed: ' + $benchmarkProcess.ExitCode)
        }
    } finally {
        # Only the benchmark child created by this invocation is owned and stopped here.
        if ($benchmarkProcess -and -not $benchmarkProcess.HasExited) {Stop-Process -Id $benchmarkProcess.Id}
    }
}
Write-Output ('Complete: ' + $OutputRoot)

param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../build'),
    [ValidateSet('tidewater','kelpline','halyard','saltpan','crossmarket','lockgate','terraces')]
    [string]$Stage,
    [uint32]$Seed = 1,
    [ValidateRange(0,100000)] [int]$WarmupFrames = 120,
    [ValidateRange(1,100000)] [int]$MeasuredFrames = 600,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../output/benchmark')
)
$ErrorActionPreference = 'Stop'
$benchmarkExecutable = Join-Path (Resolve-Path -LiteralPath $BuildDirectory).Path 'Tiny-Splatoon.exe'
if (-not (Test-Path -LiteralPath $benchmarkExecutable -PathType Leaf)) { throw "Missing native executable: $benchmarkExecutable" }
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$benchmarkDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$nvidiaSmi = Get-Command nvidia-smi -ErrorAction Stop
$gpuRows = @(& $nvidiaSmi.Source --query-gpu=name,driver_version --format=csv,noheader,nounits)
if ($LASTEXITCODE -ne 0) { throw 'nvidia-smi could not report the GPU driver' }
$gpuRow = $gpuRows | Where-Object { $_ -match 'GTX 1650' } | Select-Object -First 1
if (-not $gpuRow) { throw 'GTX 1650 was not found by nvidia-smi' }
$driverVersion = ($gpuRow -split ',')[-1].Trim()
$stages = if ($Stage) { @($Stage) } else { @('tidewater','kelpline','halyard','saltpan','crossmarket','lockgate','terraces') }
$belowTarget = @()
foreach ($benchmarkStage in $stages) {
    $reportPath = Join-Path $benchmarkDirectory ("$benchmarkStage-seed$Seed.json")
    $benchmarkLog = & $benchmarkExecutable --reference-match $benchmarkStage --benchmark-report $reportPath `
        --benchmark-seed $Seed --benchmark-warmup $WarmupFrames --benchmark-frames $MeasuredFrames 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$benchmarkStage benchmark failed: $($benchmarkLog -join "`n")" }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    $csvPath = Join-Path $benchmarkDirectory $report.frames_csv
    $frames = @(Import-Csv -LiteralPath $csvPath)
    if ($report.stage -ne $benchmarkStage -or $report.seed -ne $Seed -or
        $report.measured_frames -ne $MeasuredFrames -or $report.warmup_frames -ne $WarmupFrames -or
        $report.resolution[0] -ne 1920 -or $report.resolution[1] -ne 1080 -or
        $report.gl_renderer -notmatch 'GTX 1650' -or $report.vsync -ne $false -or
        $report.qa12_claim -ne $false -or $frames.Count -ne $MeasuredFrames -or
        @($frames | Where-Object { [int]$_.actors -ne 8 -or [int]$_.draws -le 8 }).Count -ne 0) {
        throw "$benchmarkStage report does not describe measured live 4v4 Turf frames"
    }
    $report | Add-Member -NotePropertyName nvidia_driver_version -NotePropertyValue $driverVersion
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
    $metTarget = $report.fps_mean -ge 60 -and $report.cpu_frame_ms_p95 -le (1000.0 / 60.0)
    if (-not $metTarget) { $belowTarget += $benchmarkStage }
    $targetStatus = if ($metTarget) { 'met' } else { 'missed' }
    Write-Output ("{0}: {1:N1} FPS mean, {2:N2} ms CPU p95, {3:N2} ms GPU p95, {4:N1} draws/frame; 60 FPS target {5}; {6}" -f `
        $benchmarkStage,$report.fps_mean,$report.cpu_frame_ms_p95,$report.gpu_frame_ms_p95,$report.draws_per_frame_mean,$targetStatus,$reportPath)
}
if ($belowTarget.Count -gt 0) { throw "60 FPS diagnostic target missed on: $($belowTarget -join ', '). Reports were retained in $benchmarkDirectory" }
Write-Output 'Native Turf diagnostic measurements complete. QA-12 remains open until the final high-quality renderer and Boss workload are measured.'

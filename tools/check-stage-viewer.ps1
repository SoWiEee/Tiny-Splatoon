param([string]$BuildDirectory = (Join-Path $PSScriptRoot '../build'))
$ErrorActionPreference = 'Stop'
$stageBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
$stageExecutable = Join-Path $stageBuild 'Tiny-Splatoon.exe'
$captureDirectory = Join-Path $stageBuild 'captures'
[System.IO.Directory]::CreateDirectory($captureDirectory) | Out-Null
$stageResults = @()
foreach ($stageId in @('tidewater','kelpline','halyard','saltpan','crossmarket','lockgate','terraces','cargo')) {
    $capturePath = Join-Path $captureDirectory ($stageId + '.png')
    $captureLog = & $stageExecutable --reference-stage $stageId --reference-mode turf --paint-probe 1 --capture $capturePath --frames 3 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($stageId + ' failed: ' + ($captureLog -join "`n")) }
    $png = [System.IO.File]::ReadAllBytes($capturePath)
    if ($png.Length -lt 1000 -or [BitConverter]::ToString($png,0,8) -ne '89-50-4E-47-0D-0A-1A-0A') {
        throw ('Missing or invalid PNG capture: ' + $capturePath)
    }
    $stageResults += [pscustomobject]@{stage=$stageId; mode='turf'; capture=$capturePath; log=($captureLog -join "`n")}
    Write-Output ($stageId + ': capture passed')
}
try {
    # Windows PowerShell 5.1 treats redirected native stderr as a PowerShell error record.
    $ErrorActionPreference = 'Continue'
    $invalidLog = & $stageExecutable --reference-stage cargo --reference-mode boss 2>&1
} finally { $ErrorActionPreference = 'Stop' }
if ($LASTEXITCODE -ne 1 -or ($invalidLog -join "`n") -notmatch 'unsupported stage/mode') {
    throw 'Cargo Boss must be rejected before creating a window'
}
$stageResults | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $captureDirectory 'smoke-results.json') -Encoding utf8
Write-Output 'Eight native OpenGL captures passed; Cargo Boss rejected. This checks diagnostics, not visual parity.'

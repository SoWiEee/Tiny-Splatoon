param([string]$BuildDirectory = (Join-Path $PSScriptRoot '../build'))
$ErrorActionPreference = 'Stop'
$practiceBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
$practiceExecutable = Join-Path $practiceBuild 'Tiny-Splatoon.exe'
$practiceDirectory = Join-Path $practiceBuild 'captures/practice'
[System.IO.Directory]::CreateDirectory($practiceDirectory) | Out-Null
$practiceResults = @()
foreach ($practiceStage in @('tidewater','kelpline','halyard','saltpan','crossmarket','lockgate','terraces')) {
    $practiceCapture = Join-Path $practiceDirectory ($practiceStage + '.png')
    $practiceLog = & $practiceExecutable --reference-game $practiceStage --play-probe 1 --frames 240 --capture $practiceCapture 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($practiceStage + ' failed: ' + ($practiceLog -join "`n")) }
    $practiceText = $practiceLog -join "`n"
    if ($practiceText -notmatch 'PLAYER steps=240 .*turf=[1-9][0-9]* shots=[1-9][0-9]*' -or
        ([regex]::Matches($practiceText,'INK_UPLOAD verified')).Count -ne 3) {
        throw ('Missing movement/fire/paint/clear/upload evidence: ' + $practiceText)
    }
    $practicePng = [System.IO.File]::ReadAllBytes($practiceCapture)
    if ($practicePng.Length -lt 1000 -or [BitConverter]::ToString($practicePng,0,8) -ne '89-50-4E-47-0D-0A-1A-0A') {
        throw ('Invalid practice PNG: ' + $practiceCapture)
    }
    $practiceResults += [pscustomobject]@{stage=$practiceStage; capture=$practiceCapture; log=$practiceText}
    Write-Output ($practiceStage + ': movement/fire/paint capture passed')
}
foreach ($practiceArgs in @(@('--reference-game','cargo'),@('--reference-game','halyard','--reference-mode','zones'))) {
    try {
        # Windows PowerShell 5.1 treats redirected native stderr as a PowerShell error record.
        $ErrorActionPreference = 'Continue'
        $practiceInvalid = & $practiceExecutable @practiceArgs 2>&1
    } finally { $ErrorActionPreference = 'Stop' }
    if ($LASTEXITCODE -ne 1 -or ($practiceInvalid -join "`n") -notmatch 'requires an offline Turf stage') {
        throw 'Unsupported practice stage/mode was not rejected'
    }
}
$practiceResults | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $practiceDirectory 'smoke-results.json') -Encoding utf8
Write-Output 'Seven native practice probes passed; Cargo and Zones rejected. Character art, bots and full game parity remain pending.'

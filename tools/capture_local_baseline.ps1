param(
    [string]$RomDirectory = "local-roms",
    [string]$OutputDirectory = "out/baseline",
    [string]$BiosPath = "",
    [string]$InputScript = "",
    [string]$CliPath = "build/zig-debug/sgrecomp.exe",
    [string]$HashToolPath = "build/zig-debug/sgrecomp_host.exe",
    [ValidateRange(1, 1000000)]
    [int]$HostFrames = 300,
    [ValidateRange(0, 100000000)]
    [int]$SmokeSteps = 0
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Resolve-ProjectPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $projectRoot $Path
}

function Read-LogValue([string]$Path, [string]$Label) {
    $pattern = "^$([regex]::Escape($Label)):\s*(.*)$"
    $match = Select-String -LiteralPath $Path -Pattern $pattern | Select-Object -First 1
    if ($match) {
        return $match.Matches[0].Groups[1].Value.Trim()
    }
    return "unavailable"
}

$romRoot = Resolve-ProjectPath $RomDirectory
$outputRoot = Resolve-ProjectPath $OutputDirectory
$cli = Resolve-ProjectPath $CliPath
$hashTool = Resolve-ProjectPath $HashToolPath
$bios = if ([string]::IsNullOrWhiteSpace($BiosPath)) { "" } else { Resolve-ProjectPath $BiosPath }
$inputScriptPath = if ([string]::IsNullOrWhiteSpace($InputScript)) { "" } else { Resolve-ProjectPath $InputScript }

foreach ($required in @($cli, $hashTool)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required executable not found: $required"
    }
}
if ($bios -and -not (Test-Path -LiteralPath $bios -PathType Leaf)) {
    throw "BIOS image not found: $bios"
}
if ($inputScriptPath -and -not (Test-Path -LiteralPath $inputScriptPath -PathType Leaf)) {
    throw "Input script not found: $inputScriptPath"
}
if (-not (Test-Path -LiteralPath $romRoot -PathType Container)) {
    throw "ROM directory not found: $romRoot"
}

$supportedExtensions = @(".sms", ".sg", ".gg", ".bin", ".rom")
$roms = @(Get-ChildItem -LiteralPath $romRoot -File | Where-Object {
    $supportedExtensions -contains $_.Extension.ToLowerInvariant()
} | Sort-Object Name)
if ($roms.Count -eq 0) {
    throw "No ROM images with supported extensions were found in the selected directory."
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$results = [System.Collections.Generic.List[object]]::new()

foreach ($rom in $roms) {
    $hashOutput = @(& $hashTool $rom.FullName --print-hash 2>&1)
    if ($LASTEXITCODE -ne 0) {
        $results.Add([pscustomobject]@{
            image_id = "unhashed"
            smoke_exit = -1
            host_exit = -1
            status = "hash-failed"
        })
        continue
    }

    $hashLine = $hashOutput | Where-Object { $_ -match "^fnv1a64:" } | Select-Object -First 1
    if (-not $hashLine) {
        throw "Hash tool did not return an FNV-1a image identifier."
    }
    $imageId = ($hashLine -replace "[^A-Za-z0-9_-]", "_")
    $imageOutput = Join-Path $outputRoot $imageId
    New-Item -ItemType Directory -Path $imageOutput -Force | Out-Null

    $hostLog = Join-Path $imageOutput "host.log"
    $hostFrameLog = Join-Path $imageOutput "host-frames.csv"
    $hostArgs = @(
        $rom.FullName, "--run-host", "--frames", "$HostFrames",
        "--dump-frame-bmp", (Join-Path $imageOutput "host-frame.bmp"),
        "--dump-audio", (Join-Path $imageOutput "host-audio.wav"),
        "--dump-vgm", (Join-Path $imageOutput "host-audio.vgm"),
        "--dump-frame-log", $hostFrameLog
    )
    if ($bios) {
        $hostArgs += @("--bios", $bios)
    }
    if ($inputScriptPath) {
        $hostArgs += @("--input-script", $inputScriptPath)
    }
    & $cli @hostArgs *> $hostLog
    $hostExit = $LASTEXITCODE
    if ($hostExit -ne 0) {
        $results.Add([pscustomobject]@{
            image_id = $imageId
            smoke_exit = -1
            host_exit = $hostExit
            status = "host-failed"
        })
        Write-Output "$imageId : host-failed (exit=$hostExit)"
        continue
    }

    $hostRows = if (Test-Path -LiteralPath $hostFrameLog) { @(Import-Csv $hostFrameLog) } else { @() }
    $alignedSmokeSteps = $SmokeSteps
    if ($alignedSmokeSteps -eq 0) {
        $alignedSmokeSteps = [int](($hostRows | Measure-Object -Property instructions -Sum).Sum)
    }
    if ($hostExit -eq 0 -and $alignedSmokeSteps -le 0) {
        throw "Host frame log did not contain a positive instruction count."
    }

    $smokeLog = Join-Path $imageOutput "smoke.log"
    $smokeArgs = @(
        $rom.FullName, "--run-smoke", "--steps", "$alignedSmokeSteps",
        "--dump-frame-bmp", (Join-Path $imageOutput "smoke-frame.bmp"),
        "--dump-audio", (Join-Path $imageOutput "smoke-audio.wav"),
        "--dump-vgm", (Join-Path $imageOutput "smoke-audio.vgm"),
        "--dump-coverage", (Join-Path $imageOutput "smoke-coverage.csv")
    )
    if ($bios) {
        $smokeArgs += @("--bios", $bios)
    }
    & $cli @smokeArgs *> $smokeLog
    $smokeExit = $LASTEXITCODE
    $smokeStoppedEarly = Select-String -LiteralPath $smokeLog -Pattern "execution stopped at step|unsupported opcode" -Quiet
    $requiredSmokeOutputs = @("smoke-frame.bmp", "smoke-audio.wav", "smoke-audio.vgm", "smoke-coverage.csv")
    $missingSmokeOutput = @($requiredSmokeOutputs | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $imageOutput $_) -PathType Leaf)
    }).Count -gt 0
    if ($smokeExit -ne 0 -or $smokeStoppedEarly -or $missingSmokeOutput) {
        $failureStatus = if ($smokeStoppedEarly) { "smoke-stopped-early" } else { "smoke-failed" }
        $results.Add([pscustomobject]@{
            image_id = $imageId
            smoke_exit = $smokeExit
            host_exit = $hostExit
            host_instructions = ($hostRows | Measure-Object -Property instructions -Sum).Sum
            status = $failureStatus
        })
        Write-Output "$imageId : $failureStatus (smoke=$smokeExit, host=$hostExit)"
        continue
    }

    $smokeVisited = Read-LogValue $smokeLog "visited pcs"
    $smokeLitPixels = Read-LogValue $smokeLog "framebuffer lit pixels"
    $hostFrameCount = Read-LogValue $hostLog "host frames"
    $hostExecutionMode = Read-LogValue $hostLog "execution mode"
    $hostInstructions = ($hostRows | Measure-Object -Property instructions -Sum).Sum
    $hostLitPixels = Read-LogValue $hostLog "framebuffer lit pixels"
    $hostAudioSamples = Read-LogValue $hostLog "audio samples"
    $instructionsAligned = [string]::IsNullOrWhiteSpace($inputScriptPath) -and $SmokeSteps -eq 0
    $smokeFrameHash = (Get-FileHash -LiteralPath (Join-Path $imageOutput "smoke-frame.bmp") -Algorithm SHA256).Hash
    $hostFrameHash = (Get-FileHash -LiteralPath (Join-Path $imageOutput "host-frame.bmp") -Algorithm SHA256).Hash
    $smokeAudioHash = (Get-FileHash -LiteralPath (Join-Path $imageOutput "smoke-audio.wav") -Algorithm SHA256).Hash
    $hostAudioHash = (Get-FileHash -LiteralPath (Join-Path $imageOutput "host-audio.wav") -Algorithm SHA256).Hash
    $smokeVgmHash = (Get-FileHash -LiteralPath (Join-Path $imageOutput "smoke-audio.vgm") -Algorithm SHA256).Hash
    $hostVgmHash = (Get-FileHash -LiteralPath (Join-Path $imageOutput "host-audio.vgm") -Algorithm SHA256).Hash
    $frameMatch = $smokeFrameHash -eq $hostFrameHash
    $audioMatch = $smokeAudioHash -eq $hostAudioHash
    $vgmMatch = $smokeVgmHash -eq $hostVgmHash
    @(
        "Image ID: $imageId",
        "Host: exit=$hostExit, frames=$hostFrameCount, mode=$hostExecutionMode, instructions=$hostInstructions, lit pixels=$hostLitPixels, audio samples=$hostAudioSamples",
        "Smoke: exit=$smokeExit, steps=$alignedSmokeSteps, visited PCs=$smokeVisited, lit pixels=$smokeLitPixels",
        "Instruction alignment: $instructionsAligned (host instruction counts summed across captured frames; input scripts or explicit SmokeSteps disable strict alignment)",
        "BMP SHA-256 match: $frameMatch",
        "WAV SHA-256 match: $audioMatch",
        "VGM SHA-256 match: $vgmMatch"
    ) | Set-Content -LiteralPath (Join-Path $imageOutput "comparison.txt") -Encoding UTF8

    $status = if ($smokeExit -eq 0 -and $hostExit -eq 0 -and -not $smokeStoppedEarly) {
        "captured"
    } elseif ($smokeStoppedEarly) {
        "smoke-stopped-early"
    } else {
        "failed"
    }
    $results.Add([pscustomobject]@{
        image_id = $imageId
        smoke_exit = $smokeExit
        smoke_steps = $alignedSmokeSteps
        smoke_visited_pcs = $smokeVisited
        smoke_lit_pixels = $smokeLitPixels
        smoke_frame_sha256 = $smokeFrameHash
        host_exit = $hostExit
        host_frames = $hostFrameCount
        host_execution_mode = $hostExecutionMode
        host_instructions = $hostInstructions
        host_lit_pixels = $hostLitPixels
        host_audio_samples = $hostAudioSamples
        instructions_aligned = $instructionsAligned
        framebuffer_match = $frameMatch
        audio_match = $audioMatch
        vgm_match = $vgmMatch
        status = $status
    })
    Write-Output "$imageId : $status (smoke=$smokeExit, host=$hostExit)"
}

$summaryPath = Join-Path $outputRoot "summary.csv"
$results | Export-Csv -LiteralPath $summaryPath -NoTypeInformation -Encoding UTF8
Write-Output "Baseline summary saved under the selected output directory."
if (@($results | Where-Object { $_.status -ne "captured" }).Count -gt 0) {
    exit 1
}

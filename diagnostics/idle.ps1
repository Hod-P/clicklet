# SPDX-License-Identifier: MIT
# Read-only observer. Does not launch Clicklet, inject input, poll, sleep or change priorities.
param([Parameter(Mandatory = $true)][int[]] $ProcessIds)
$ErrorActionPreference = 'Stop'
$logical = [Environment]::ProcessorCount
$begin = @{}
foreach ($processId in $ProcessIds) {
    $process = Get-Process -Id $processId
    $begin[$processId] = @{
        StartTime = $process.StartTime
        Time = [Diagnostics.Stopwatch]::GetTimestamp()
        CPU = $process.TotalProcessorTime.TotalSeconds
        WorkingSet = $process.WorkingSet64
        PrivateBytes = $process.PrivateMemorySize64
    }
}
Read-Host 'Leave both processes idle for at least 60 seconds using an external clock, then press Enter here' | Out-Null
foreach ($processId in $ProcessIds) {
    $process = Get-Process -Id $processId
    $first = $begin[$processId]
    if ($process.StartTime -ne $first.StartTime) { throw 'PID was reused; discard observation.' }
    $seconds = ([Diagnostics.Stopwatch]::GetTimestamp() - $first.Time) / [Diagnostics.Stopwatch]::Frequency
    if ($seconds -lt 60) { throw 'Observation shorter than 60 seconds; no idle result reported.' }
    $cpu = 100 * ($process.TotalProcessorTime.TotalSeconds - $first.CPU) / $seconds
    [pscustomobject]@{
        ProcessId = $processId
        ProcessName = $process.ProcessName
        Seconds = $seconds
        CPUPercentOneCore = $cpu
        CPUPercentMachine = $cpu / $logical
        LogicalProcessors = $logical
        WorkingSetStartBytes = $first.WorkingSet
        WorkingSetEndBytes = $process.WorkingSet64
        PrivateBytesStart = $first.PrivateBytes
        PrivateBytesEnd = $process.PrivateMemorySize64
    }
}

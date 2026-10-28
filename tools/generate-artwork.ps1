# Rebuild derived artwork without changing the three hand-drawn source PNGs.
# Requires Windows PowerShell 5.1 and built-in System.Drawing; no runtime dependency.
# Run: powershell -NoProfile -File tools/generate-artwork.ps1 [-Check]
param([switch]$Check)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot -Parent
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)

function Resize-Artwork([System.Drawing.Image]$Source, [int]$Size) {
    $bitmap = [System.Drawing.Bitmap]::new($Size, $Size,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $attributes = [System.Drawing.Imaging.ImageAttributes]::new()
    try {
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $attributes.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
        $graphics.DrawImage($Source, [System.Drawing.Rectangle]::new(0, 0, $Size, $Size),
            0, 0, $Source.Width, $Source.Height, [System.Drawing.GraphicsUnit]::Pixel, $attributes)
    } finally {
        $attributes.Dispose()
        $graphics.Dispose()
    }
    return $bitmap
}

function Get-PngBytes([System.Drawing.Image]$Image) {
    $stream = [System.IO.MemoryStream]::new()
    try {
        $Image.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        return ,$stream.ToArray()
    } finally { $stream.Dispose() }
}

function Get-DibBytes([System.Drawing.Bitmap]$Image) {
    $size = $Image.Width
    $maskStride = [int]([Math]::Ceiling($size / 32.0) * 4)
    $stream = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($stream)
    try {
        $writer.Write([uint32]40)
        $writer.Write([int32]$size)
        $writer.Write([int32]($size * 2)) # XOR bitmap plus the one-bit AND mask.
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]0)
        $writer.Write([uint32]($size * $size * 4 + $maskStride * $size))
        $writer.Write([byte[]]::new(16))
        for ($y = $size - 1; $y -ge 0; $y--) {
            for ($x = 0; $x -lt $size; $x++) {
                $pixel = $Image.GetPixel($x, $y)
                $writer.Write([byte[]]@($pixel.B, $pixel.G, $pixel.R, $pixel.A))
            }
        }
        for ($y = $size - 1; $y -ge 0; $y--) {
            $row = [byte[]]::new($maskStride)
            for ($x = 0; $x -lt $size; $x++) {
                if ($Image.GetPixel($x, $y).A -eq 0) {
                    $index = [int][Math]::Floor($x / 8.0)
                    $row[$index] = $row[$index] -bor (128 -shr ($x % 8))
                }
            }
            $writer.Write($row)
        }
        return ,$stream.ToArray()
    } finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

function Get-IconBytes([System.Drawing.Bitmap]$Source, [string]$State, [int[]]$Rgb) {
    $master = [System.Drawing.Bitmap]$Source.Clone()
    $graphics = [System.Drawing.Graphics]::FromImage($master)
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb($Rgb[0], $Rgb[1], $Rgb[2]))
    try {
        # Match the hand-drawn 16px icon's 2x2 square at (14, 0), without redrawing the mouse.
        $graphics.FillRectangle($brush, 448, 0, 64, 64)
    } finally {
        $brush.Dispose()
        $graphics.Dispose()
    }
    $frames = [System.Collections.Generic.List[byte[]]]::new()
    try {
        foreach ($size in $sizes) {
            if ($size -eq 16) {
                # Store the ORIGINAL PNG payload verbatim: no decode, resample or re-encode.
                $frames.Add([System.IO.File]::ReadAllBytes((Join-Path $root "res/source/tray-$State-16.png")))
                continue
            }
            $frame = Resize-Artwork $master $size
            try {
                if ($size -le 64) { $frames.Add((Get-DibBytes $frame)) }
                else { $frames.Add((Get-PngBytes $frame)) }
            } finally { $frame.Dispose() }
        }
    } finally { $master.Dispose() }
    $stream = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($stream)
    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($index = 0; $index -lt $sizes.Count; $index++) {
            $writer.Write([byte]($sizes[$index] % 256))
            $writer.Write([byte]($sizes[$index] % 256))
            $writer.Write([uint16]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$frames[$index].Length)
            $writer.Write([uint32]$offset)
            $offset += $frames[$index].Length
        }
        foreach ($frame in $frames) { $writer.Write($frame) }
        return ,$stream.ToArray()
    } finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

function Get-AboutBytes([System.Drawing.Bitmap]$Source) {
    # Keep the existing 260x61 bitmap, centered 56px artwork and dialog background.
    $bitmap = [System.Drawing.Bitmap]::new(260, 61,
        [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $logo = Resize-Artwork $Source 56
    $stream = [System.IO.MemoryStream]::new()
    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(240, 240, 240))
        $graphics.DrawImageUnscaled($logo, 102, 2)
        $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Bmp)
        return ,$stream.ToArray()
    } finally {
        $stream.Dispose()
        $logo.Dispose()
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$source = [System.Drawing.Bitmap]::new((Join-Path $root 'res/source/mouse-side-512.png'))
try {
    if ($source.Width -ne 512 -or $source.Height -ne 512) { throw 'Expected a 512x512 master PNG.' }
    foreach ($state in @('enabled', 'disabled')) {
        $tray = [System.Drawing.Bitmap]::new((Join-Path $root "res/source/tray-$state-16.png"))
        try {
            if ($tray.Width -ne 16 -or $tray.Height -ne 16) { throw "Expected a 16x16 $state PNG." }
        } finally { $tray.Dispose() }
    }
    $outputs = [ordered]@{
        'res/clicklet-enabled.ico' = Get-IconBytes $source 'enabled' @(0, 246, 11)
        'res/clicklet-disabled.ico' = Get-IconBytes $source 'disabled' @(170, 168, 168)
        'res/about-mouse.bmp' = Get-AboutBytes $source
        'docs/assets/mouse-side.png' = [System.IO.File]::ReadAllBytes((Join-Path $root 'res/source/mouse-side-512.png'))
        'docs/assets/tray-enabled-16.png' = [System.IO.File]::ReadAllBytes((Join-Path $root 'res/source/tray-enabled-16.png'))
    }
    foreach ($entry in $outputs.GetEnumerator()) {
        $path = Join-Path $root $entry.Key
        if ($Check) {
            if (-not (Test-Path -LiteralPath $path) -or
                [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($path)) -cne
                [Convert]::ToBase64String($entry.Value)) { throw "Out-of-date artwork: $($entry.Key)" }
            Write-Output "PASS $($entry.Key): reproducible ($($entry.Value.Length) bytes)"
        } else {
            [System.IO.File]::WriteAllBytes($path, $entry.Value)
            Write-Output "$($entry.Key): $($entry.Value.Length) bytes"
        }
    }
} finally { $source.Dispose() }

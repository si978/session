Add-Type -AssemblyName System.Drawing

$size = 256
$bmp = New-Object System.Drawing.Bitmap($size, $size)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'
$g.TextRenderingHint = 'AntiAliasGridFit'

# Background
$g.Clear([System.Drawing.Color]::FromArgb(30, 30, 30))

# Draw FPS text
$font = New-Object System.Drawing.Font('Arial', 80, [System.Drawing.FontStyle]::Bold)
$brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(0, 230, 118))
$format = New-Object System.Drawing.StringFormat
$format.Alignment = 'Center'
$format.LineAlignment = 'Center'
$rect = New-Object System.Drawing.RectangleF(0, 0, $size, $size)
$g.DrawString('FPS', $font, $brush, $rect, $format)
$g.Dispose()

# Create multiple sizes
$sizes = @(16, 32, 48, 256)
$pngData = @()

foreach ($s in $sizes) {
    $resized = New-Object System.Drawing.Bitmap($bmp, $s, $s)
    $ms = New-Object System.IO.MemoryStream
    $resized.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngData += ,($ms.ToArray())
    $resized.Dispose()
}

$bmp.Dispose()

# Write ICO file
$icoPath = "D:\droid\fps-fresh\src\launcher\app.ico"
$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter($fs)

# ICO Header
$bw.Write([UInt16]0)           # Reserved
$bw.Write([UInt16]1)           # Type = ICO
$bw.Write([UInt16]$sizes.Count) # Image count

# Calculate data offset
$headerSize = 6 + (16 * $sizes.Count)
$offset = $headerSize

# Directory entries
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $data = $pngData[$i]
    
    if ($s -eq 256) {
        $bw.Write([byte]0)  # 256 = 0
        $bw.Write([byte]0)
    } else {
        $bw.Write([byte]$s)
        $bw.Write([byte]$s)
    }
    $bw.Write([byte]0)         # Color palette
    $bw.Write([byte]0)         # Reserved
    $bw.Write([UInt16]1)       # Color planes
    $bw.Write([UInt16]32)      # Bits per pixel
    $bw.Write([UInt32]$data.Length)
    $bw.Write([UInt32]$offset)
    $offset += $data.Length
}

# Image data
foreach ($data in $pngData) {
    $bw.Write($data)
}

$bw.Close()
$fs.Close()

Write-Host "Icon created: $icoPath"

# Assemble les captures f00.png, f01.png... d'un dossier (vfx_capture.py) en une planche contact.
# Usage : .\MakeContactSheet.ps1 <dossier> [-Columns 4] [-CellWidth 480] [-Crop 0.0]
# -Crop : part de l'image coupée sur chaque bord (0.2 = on garde le centre), pour zoomer sur l'effet.
# -Region "x,y,w,h" (fractions 0-1 de l'image) : zone gardée, prioritaire sur -Crop.
param(
    [Parameter(Mandatory)] [string] $Folder,
    [int] $Columns = 4,
    [int] $CellWidth = 480,
    [double] $Crop = 0.0,
    [string] $Region = ''
)
$rx = $Crop; $ry = $Crop; $rw = 1 - 2 * $Crop; $rh = 1 - 2 * $Crop
if ($Region) { $rx, $ry, $rw, $rh = $Region.Split(',') | ForEach-Object { [double]$_ } }
Add-Type -AssemblyName System.Drawing
$files = Get-ChildItem $Folder -Filter 'f*.png' | Sort-Object Name
if (-not $files) { throw "Aucune capture dans $Folder" }
$times = @()
$timesFile = Join-Path $Folder 'times.json'
if (Test-Path $timesFile) { $times = Get-Content $timesFile -Raw | ConvertFrom-Json }

$first = [System.Drawing.Image]::FromFile($files[0].FullName)
$srcW = [int]($first.Width * $rw); $srcH = [int]($first.Height * $rh)
$first.Dispose()
$cellH = [int]($CellWidth * $srcH / $srcW)
$rows = [math]::Ceiling($files.Count / $Columns)
$sheet = New-Object System.Drawing.Bitmap ($CellWidth * $Columns), ($cellH * $rows)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.InterpolationMode = 'HighQualityBicubic'
$g.Clear([System.Drawing.Color]::Black)
$font = New-Object System.Drawing.Font 'Consolas', 11
for ($i = 0; $i -lt $files.Count; $i++) {
    $img = [System.Drawing.Image]::FromFile($files[$i].FullName)
    $dest = New-Object System.Drawing.Rectangle (($i % $Columns) * $CellWidth), ([math]::Floor($i / $Columns) * $cellH), $CellWidth, $cellH
    $src = New-Object System.Drawing.Rectangle ([int]($img.Width * $rx)), ([int]($img.Height * $ry)), ([int]($img.Width * $rw)), ([int]($img.Height * $rh))
    $g.DrawImage($img, $dest, $src, [System.Drawing.GraphicsUnit]::Pixel)
    $label = if ($i -lt $times.Count) { '#{0}  t={1:N2}s' -f $i, $times[$i] } else { "#$i" }
    $g.DrawString($label, $font, [System.Drawing.Brushes]::Black, $dest.X + 6, $dest.Y + 5)
    $g.DrawString($label, $font, [System.Drawing.Brushes]::White, $dest.X + 5, $dest.Y + 4)
    $img.Dispose()
}
$out = Join-Path $Folder 'sheet.png'
$sheet.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $sheet.Dispose()
$out

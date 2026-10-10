# Copie des packs téléchargés par le launcher Epic (VaultCache) dans Content, ÉDITEUR FERMÉ.
# Usage : powershell -File Tools/Anim/import_vault.ps1 [-Heroes Aurora,Phase] [-Lyra] [-WhatIf]
# Paragon : Content/Paragon<Héros>/ (déjà ignoré par git : Content/Paragon*/). Seuls les clips reciblés dans Content/Gen
# sont versionnés. Lyra : seulement le mannequin Lyra et ses animations d'action (dash...), sous Content/Characters/Heroes/
# (ignoré aussi), pour reciblage. Robocopy ne remplace jamais un fichier existant (/XC /XN /XO).
param(
    [string[]] $Heroes = @('Aurora', 'Countess', 'Dekker', 'Fey', 'IggyScorch', 'Kallari', 'LtBelica', 'Muriel', 'Phase'),
    [switch] $Lyra,
    [switch] $WhatIf,
    # Nouveaux fichiers seulement (jamais d'écrasement) : l'éditeur ouvert les découvre, comme « Ajouter au projet » du launcher
    [switch] $AllowRunningEditor
)
$ErrorActionPreference = 'Stop'
$Vault = 'C:\ProgramData\Epic\EpicGamesLauncher\VaultCache'
$Content = Join-Path (Split-Path (Split-Path $PSScriptRoot)) 'Content'

if (-not $AllowRunningEditor -and (Get-Process UnrealEditor -ErrorAction SilentlyContinue)) { throw "Fermer l'éditeur avant de copier des assets." }

function Copy-Tree($src, $dst) {
    if (-not (Test-Path $src)) { Write-Warning "absent : $src"; return }
    $args = @($src, $dst, '/E', '/XC', '/XN', '/XO', '/MT:16', '/NFL', '/NDL', '/NP', '/NJH')
    if ($WhatIf) { $args += '/L' }
    & robocopy @args | Select-Object -Last 4
    if ($LASTEXITCODE -ge 8) { throw "robocopy a échoué ($LASTEXITCODE) : $src" }
}

foreach ($h in $Heroes) {
    Copy-Tree (Join-Path $Vault "Paragon$h\data\Content\Paragon$h") (Join-Path $Content "Paragon$h")
}
if ($Lyra) {
    Copy-Tree (Join-Path $Vault 'Lyra_5.8\data\Content\Characters\Heroes\Mannequin') (Join-Path $Content 'Characters\Heroes\Mannequin')
}

# Robocopy : 0-7 = succès (1 = fichiers copiés) ; ne pas laisser fuir ce code comme un échec
exit 0

# =============================================================================
# setup_deps.ps1 - GroveEngine Windows Dependency Setup
# =============================================================================
#
# QUOI  : Telecharge, extrait et patche toutes les dependances de GroveEngine
#         pour un build Windows MinGW/Ninja sans acces git clone.
#
# POURQUOI : Sur cette machine, git clone freeze silencieusement pendant le
#            transfert pack-objects (ni WSL ni PowerShell ne fonctionnent).
#            FetchContent de CMake s appuie sur git clone -> bloque.
#            Invoke-WebRequest fonctionne parfaitement : on pre-popule
#            build/_deps/ manuellement pour que CMake trouve tout via
#            FETCHCONTENT_SOURCE_DIR_* sans jamais appeler git.
#
# COMMENT : 1. Check si les deps sont deja presentes -> skip si oui
#           2. Cree le dossier temporaire de download
#           3. Pour chaque dep : Download zip -> Extract -> Deplace au bon endroit
#           4. Applique les 2 patches MinGW sur bgfx-cmake-src
#           5. Affiche la commande cmake complete prete a copier-coller
#
# USAGE  : .\scripts\setup_deps.ps1
#          (appeler depuis la racine du projet ou depuis scripts/)
# =============================================================================

#Requires -Version 5.1

# Supprime la progress bar Invoke-WebRequest qui ralentit massivement les DL
# (bug connu PowerShell : la progress bar .NET reduit la vitesse de 50-100x)
$ProgressPreference = 'SilentlyContinue'

# Strict mode : toute variable non definie = erreur (detection bugs precoce)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# =============================================================================
# CHEMINS RACINE - resolus depuis l emplacement du script pour etre portables
# Peu importe d ou on lance le script, les chemins sont toujours corrects.
# =============================================================================

# $PSScriptRoot = dossier contenant ce script (scripts/)
# On remonte d un niveau pour avoir la racine du projet
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDepsDir = Join-Path $ProjectRoot "build\_deps"
$TempDownloadDir = Join-Path $ProjectRoot "build\_deps_download_tmp"

# =============================================================================
# CONSTANTES - URLs et noms de destination des dependances
# =============================================================================

# Structure : @{ Name=...; Url=...; ZipInternalDir=...; DestDir=... }
# - Name          : label pour les messages
# - Url           : URL du zip GitHub
# - ZipInternalDir: sous-dossier cree a l interieur du zip par GitHub
#                   (GitHub ajoute toujours "repo-branch/" ou "repo-tag/" en prefixe)
# - DestDir       : chemin final dans build/_deps/ ou le contenu doit atterrir
$Dependencies = @(
    @{
        Name           = "nlohmann_json"
        Url            = "https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip"
        ZipInternalDir = "json-3.11.3"
        DestDir        = Join-Path $BuildDepsDir "nlohmann_json-src"
    },
    @{
        Name           = "spdlog"
        Url            = "https://github.com/gabime/spdlog/archive/refs/tags/v1.12.0.zip"
        ZipInternalDir = "spdlog-1.12.0"
        DestDir        = Join-Path $BuildDepsDir "spdlog-src"
    },
    @{
        # bgfx.cmake est le repo CMake wrapper - il contient bgfx/, bx/, bimg/ en sous-dossiers
        Name           = "bgfx.cmake"
        Url            = "https://github.com/bkaradzic/bgfx.cmake/archive/refs/heads/master.zip"
        ZipInternalDir = "bgfx.cmake-master"
        DestDir        = Join-Path $BuildDepsDir "bgfx-cmake-src"
    },
    @{
        # bgfx source -> va dans bgfx-cmake-src/bgfx/ (attendu par bgfx.cmake)
        Name           = "bgfx"
        Url            = "https://github.com/bkaradzic/bgfx/archive/refs/heads/master.zip"
        ZipInternalDir = "bgfx-master"
        DestDir        = Join-Path $BuildDepsDir "bgfx-cmake-src\bgfx"
    },
    @{
        # bx source -> va dans bgfx-cmake-src/bx/
        Name           = "bx"
        Url            = "https://github.com/bkaradzic/bx/archive/refs/heads/master.zip"
        ZipInternalDir = "bx-master"
        DestDir        = Join-Path $BuildDepsDir "bgfx-cmake-src\bx"
    },
    @{
        # bimg source -> va dans bgfx-cmake-src/bimg/
        Name           = "bimg"
        Url            = "https://github.com/bkaradzic/bimg/archive/refs/heads/master.zip"
        ZipInternalDir = "bimg-master"
        DestDir        = Join-Path $BuildDepsDir "bgfx-cmake-src\bimg"
    }
)

# =============================================================================
# FONCTION : Test-DepsAlreadyPresent
# Verifie si toutes les deps critiques sont deja en place.
# On check les 6 dossiers cles comme validation complete.
# =============================================================================
function Test-DepsAlreadyPresent {
    # Chemins de validation : les 3 deps principales + bgfx avec ses 3 sous-repos
    $checkPaths = @(
        (Join-Path $BuildDepsDir "nlohmann_json-src"),
        (Join-Path $BuildDepsDir "spdlog-src"),
        (Join-Path $BuildDepsDir "bgfx-cmake-src"),
        (Join-Path $BuildDepsDir "bgfx-cmake-src\bgfx"),
        (Join-Path $BuildDepsDir "bgfx-cmake-src\bx"),
        (Join-Path $BuildDepsDir "bgfx-cmake-src\bimg")
    )

    foreach ($path in $checkPaths) {
        if (-not (Test-Path $path)) {
            return $false
        }
    }
    return $true
}

# =============================================================================
# FONCTION : Invoke-Download
# Telecharge un zip depuis une URL vers un fichier local.
# Utilise Invoke-WebRequest avec -UseBasicParsing (pas besoin d IE engine).
# =============================================================================
function Invoke-Download {
    param(
        [string]$Name,    # Label pour les messages
        [string]$Url,     # URL source
        [string]$OutFile  # Chemin de destination du zip
    )

    Write-Host "  [DL] Telechargement $Name..." -ForegroundColor Cyan

    try {
        # -UseBasicParsing : parse HTML sans IE COM object (obligatoire en Server Core / headless)
        # OutFile : ecrit directement sur disque sans charger en memoire (critique pour les gros zips bgfx ~500MB)
        Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing
        $sizeMB = [Math]::Round((Get-Item $OutFile).Length / 1MB, 1)
        Write-Host "  [OK] $Name telecharge ($sizeMB MB)" -ForegroundColor Green
    }
    catch {
        Write-Host "  [ERR] Echec telechargement $Name : $_" -ForegroundColor Red
        throw "Download failed for $Name from $Url"
    }
}

# =============================================================================
# FONCTION : Expand-DepZip
# Extrait un zip GitHub vers le bon dossier de destination.
# GitHub zippe toujours avec un sous-dossier racine (ex: json-3.11.3/).
# On extrait dans un temp dir puis on deplace le contenu du sous-dossier.
# =============================================================================
function Expand-DepZip {
    param(
        [string]$Name,           # Label pour les messages
        [string]$ZipPath,        # Chemin du zip telecharge
        [string]$ZipInternalDir, # Nom du sous-dossier cree par GitHub dans le zip
        [string]$DestDir         # Dossier de destination final
    )

    Write-Host "  [EX] Extraction $Name..." -ForegroundColor Cyan

    # Dossier temporaire d extraction pour eviter collision avec d autres extractions
    $extractTemp = Join-Path $TempDownloadDir "_extract_$Name"

    try {
        # Nettoyer l eventuel reste d une extraction precedente ratee
        if (Test-Path $extractTemp) {
            Remove-Item $extractTemp -Recurse -Force
        }
        New-Item -ItemType Directory -Path $extractTemp -Force | Out-Null

        # Extraction via .NET - plus rapide et fiable que Expand-Archive sur gros fichiers
        # System.IO.Compression.ZipFile est dispo depuis .NET 4.5 (toujours present sur Win10+)
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($ZipPath, $extractTemp)

        # Le zip GitHub cree : extractTemp/json-3.11.3/include/..., etc.
        # On veut deplacer le contenu de extractTemp/<ZipInternalDir>/ vers $DestDir
        $innerDir = Join-Path $extractTemp $ZipInternalDir

        if (-not (Test-Path $innerDir)) {
            $found = (Get-ChildItem $extractTemp | Select-Object -ExpandProperty Name) -join ", "
            throw "Sous-dossier attendu '$ZipInternalDir' absent dans le zip. Contenu trouve : $found"
        }

        # Cree le dossier de destination si absent (cas des sous-repos bgfx/bx/bimg)
        if (-not (Test-Path $DestDir)) {
            New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
        }

        # Si DestDir existe deja avec du contenu, on le vide d abord (re-run propre)
        $existingCount = (Get-ChildItem $DestDir -ErrorAction SilentlyContinue | Measure-Object).Count
        if ($existingCount -gt 0) {
            Write-Host "  [WRN] $DestDir non vide, remplacement..." -ForegroundColor Yellow
            Remove-Item (Join-Path $DestDir "*") -Recurse -Force
        }

        # Deplace le contenu via robocopy /MOVE - fiable sur chemins longs Windows
        # (Move-Item echoue parfois sur Windows avec des chemins longs ou des permissions)
        # Robocopy : exit codes 0-7 = succes (bits flags de ce qui a ete copie/skippe)
        # Exit code >= 8 = erreur reelle
        $null = robocopy $innerDir $DestDir /E /MOVE /NFL /NDL /NJH /NJS /NC /NS /NP 2>&1
        $robocopyExitCode = $LASTEXITCODE
        if ($robocopyExitCode -ge 8) {
            throw "robocopy a echoue avec exit code $robocopyExitCode"
        }

        Write-Host "  [OK] $Name extrait vers $DestDir" -ForegroundColor Green
    }
    catch {
        Write-Host "  [ERR] Echec extraction $Name : $_" -ForegroundColor Red
        throw
    }
    finally {
        # Nettoyage du dossier temporaire d extraction
        if (Test-Path $extractTemp) {
            Remove-Item $extractTemp -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

# =============================================================================
# FONCTION : Invoke-BgfxPatches
# Applique les 2 patches necessaires sur bgfx-cmake-src pour compiler avec MinGW.
#
# POURQUOI ces patches :
#   1. bx.cmake demande cxx_std_14 mais GroveEngine necessite C++17/20.
#      MinGW GCC 15.2 rejette le mix de standards -> on force cxx_std_20.
#   2. miniz.cmake pointe vers le mauvais chemin d include pour tinyexr/miniz.
#      Le header miniz.h est dans deps/ pas dans deps/miniz/ avec cette version.
# =============================================================================
function Invoke-BgfxPatches {
    $bgfxCmakeSrc = Join-Path $BuildDepsDir "bgfx-cmake-src"

    Write-Host ""
    Write-Host "[PATCH] Application des patches MinGW sur bgfx-cmake-src..." -ForegroundColor Magenta

    # --- Patch 1 : bx.cmake - cxx_std_14 -> cxx_std_20 ---
    # QUOI  : Remplace la feature C++ standard demandee par bx
    # POURQUOI : MinGW GCC 15.2 + projet C++17 genere des conflits de standard
    #            quand bx force cxx_std_14. On l aligne sur C++20 (superset de 17).
    # COMMENT : Lecture du fichier, remplacement de chaine, reecriture
    $bxCmakePath = Join-Path $bgfxCmakeSrc "cmake\bx\bx.cmake"
    if (-not (Test-Path $bxCmakePath)) {
        throw "Fichier patch 1 introuvable : $bxCmakePath"
    }

    try {
        $content = Get-Content $bxCmakePath -Raw -Encoding UTF8
        $oldToken = 'target_compile_features(bx PUBLIC cxx_std_14)'
        $newToken = 'target_compile_features(bx PUBLIC cxx_std_20)'

        if ($content.Contains($oldToken)) {
            $content = $content.Replace($oldToken, $newToken)
            [System.IO.File]::WriteAllText($bxCmakePath, $content, [System.Text.Encoding]::UTF8)
            Write-Host "  [OK] Patch 1 : bx.cmake cxx_std_14 -> cxx_std_20" -ForegroundColor Green
        } else {
            Write-Host "  [INFO] Patch 1 deja applique ou token introuvable dans bx.cmake" -ForegroundColor Yellow
        }
    }
    catch {
        Write-Host "  [ERR] Echec patch 1 (bx.cmake) : $_" -ForegroundColor Red
        throw
    }

    # --- Patch 2 : miniz.cmake - chemin d include tinyexr/deps/miniz -> tinyexr/deps ---
    # QUOI  : Corrige le MINIZ_INCLUDE_DIR pour pointer sur le bon dossier
    # POURQUOI : Dans cette version de bimg, miniz.h se trouve dans
    #            3rdparty/tinyexr/deps/miniz.h (pas dans un sous-dossier miniz/).
    #            Le CMakeLists d origine pointe un dossier inexistant -> compile error.
    # COMMENT : Meme approche lecture/remplacement/reecriture, avec fallback regex
    $minizCmakePath = Join-Path $bgfxCmakeSrc "cmake\bimg\3rdparty\miniz.cmake"
    if (-not (Test-Path $minizCmakePath)) {
        throw "Fichier patch 2 introuvable : $minizCmakePath"
    }

    try {
        $content = Get-Content $minizCmakePath -Raw -Encoding UTF8
        $oldToken = 'set(MINIZ_INCLUDE_DIR ${BIMG_DIR}/3rdparty/tinyexr/deps/miniz)'
        $newToken = 'set(MINIZ_INCLUDE_DIR ${BIMG_DIR}/3rdparty/tinyexr/deps)'

        if ($content.Contains($oldToken)) {
            # Remplacement exact par chaine litterale
            $content = $content.Replace($oldToken, $newToken)
            [System.IO.File]::WriteAllText($minizCmakePath, $content, [System.Text.Encoding]::UTF8)
            Write-Host "  [OK] Patch 2 : miniz.cmake tinyexr/deps/miniz -> tinyexr/deps" -ForegroundColor Green
        } else {
            # Fallback : remplacement regex si la chaine exacte a change (version future de bimg)
            Write-Host "  [INFO] Patch 2 : token exact non trouve, tentative regex..." -ForegroundColor Yellow
            $pattern = 'set\(MINIZ_INCLUDE_DIR \$\{BIMG_DIR\}/3rdparty/tinyexr/deps/miniz\)'
            $content = $content -replace $pattern, $newToken
            [System.IO.File]::WriteAllText($minizCmakePath, $content, [System.Text.Encoding]::UTF8)
            Write-Host "  [OK] Patch 2 : miniz.cmake MINIZ_INCLUDE_DIR corrige (via regex)" -ForegroundColor Green
        }
    }
    catch {
        Write-Host "  [ERR] Echec patch 2 (miniz.cmake) : $_" -ForegroundColor Red
        throw
    }
}

# =============================================================================
# FONCTION : Write-CmakeCommand
# Genere et affiche la commande cmake configure complete avec les chemins absolus.
# POURQUOI : Les variables FETCHCONTENT_SOURCE_DIR_* necessitent des chemins
#            absolus - CMake les utilise tel quels sans resolution relative.
#            On les genere dynamiquement pour etre portables sur toute machine.
# =============================================================================
function Write-CmakeCommand {
    # Chemins absolus vers chaque source dir (attendus par CMake FetchContent)
    $jsonSrcDir   = Join-Path $BuildDepsDir "nlohmann_json-src"
    $spdlogSrcDir = Join-Path $BuildDepsDir "spdlog-src"
    # Note : FETCHCONTENT_SOURCE_DIR_BGFX pointe sur bgfx-cmake-src (le wrapper CMake),
    # pas sur bgfx-cmake-src/bgfx/ - CMake cherche le CMakeLists.txt a la racine du wrapper.
    $bgfxSrcDir   = Join-Path $BuildDepsDir "bgfx-cmake-src"

    Write-Host ""
    Write-Host "================================================================" -ForegroundColor White
    Write-Host "  COMMANDE CMAKE - copier-coller depuis la racine du projet :"   -ForegroundColor White
    Write-Host "================================================================" -ForegroundColor White
    Write-Host ""
    Write-Host "cmake -B build -G Ninja ``"                                              -ForegroundColor Cyan
    Write-Host "  -DGROVE_BUILD_BGFX_RENDERER=ON ``"                                    -ForegroundColor Cyan
    Write-Host "  -DGROVE_BUILD_UI_MODULE=ON ``"                                         -ForegroundColor Cyan
    Write-Host "  -DGROVE_BUILD_INPUT_MODULE=ON ``"                                      -ForegroundColor Cyan
    Write-Host "  -DGROVE_BUILD_TESTS=OFF ``"                                            -ForegroundColor Cyan
    Write-Host "  -DBUILD_TESTS=OFF ``"                                                  -ForegroundColor Cyan
    Write-Host "  -DBGFX_BUILD_TOOLS=OFF ``"                                             -ForegroundColor Cyan
    Write-Host "  -DBGFX_BUILD_EXAMPLES=OFF ``"                                          -ForegroundColor Cyan
    Write-Host "  `"-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$jsonSrcDir`" ``"            -ForegroundColor Cyan
    Write-Host "  `"-DFETCHCONTENT_SOURCE_DIR_SPDLOG=$spdlogSrcDir`" ``"                 -ForegroundColor Cyan
    Write-Host "  `"-DFETCHCONTENT_SOURCE_DIR_BGFX=$bgfxSrcDir`""                       -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Puis pour builder :"                                                      -ForegroundColor Gray
    Write-Host '  cmake --build build -j$([Environment]::ProcessorCount)'               -ForegroundColor Cyan
    Write-Host ""
    Write-Host "OU : utilisez directement .\scripts\build.ps1 pour tout-en-un."         -ForegroundColor Gray
    Write-Host ""
}

# =============================================================================
# POINT D ENTREE PRINCIPAL
# =============================================================================

Write-Host ""
Write-Host "======================================================" -ForegroundColor Blue
Write-Host "  GroveEngine - Setup des dependances Windows         " -ForegroundColor Blue
Write-Host "======================================================" -ForegroundColor Blue
Write-Host ""
Write-Host "[INFO] Racine projet : $ProjectRoot" -ForegroundColor Gray
Write-Host "[INFO] Dossier deps  : $BuildDepsDir" -ForegroundColor Gray
Write-Host ""

# --- Verification : deps deja presentes ? ---
if (Test-DepsAlreadyPresent) {
    Write-Host "[OK] Toutes les dependances sont deja presentes dans build/_deps/" -ForegroundColor Green
    Write-Host "     (supprimez build/_deps/ pour forcer un re-telechargement)" -ForegroundColor Gray
    Write-Host ""
    Write-CmakeCommand
    exit 0
}

Write-Host "[...] Dependances manquantes detectees - demarrage du setup..." -ForegroundColor Yellow
Write-Host ""

# --- Creation des dossiers necessaires ---
try {
    New-Item -ItemType Directory -Path $BuildDepsDir -Force | Out-Null
    New-Item -ItemType Directory -Path $TempDownloadDir -Force | Out-Null
}
catch {
    Write-Host "[ERR] Impossible de creer les dossiers build/_deps : $_" -ForegroundColor Red
    exit 1
}

# --- Boucle principale : telechargement + extraction de chaque dep ---
foreach ($dep in $Dependencies) {
    Write-Host "--- $($dep.Name) ---" -ForegroundColor Blue
    Write-Host ""

    # Chemin local du zip telecharge
    $zipFileName = [System.IO.Path]::GetFileName($dep.Url)
    $zipPath = Join-Path $TempDownloadDir $zipFileName

    # Skip telechargement si le zip existe deja dans le temp (reprise apres crash)
    if (Test-Path $zipPath) {
        Write-Host "  [SKIP] Zip deja present : $zipFileName" -ForegroundColor Gray
    } else {
        try {
            Invoke-Download -Name $dep.Name -Url $dep.Url -OutFile $zipPath
        }
        catch {
            Write-Host "[ERR] Setup interrompu sur $($dep.Name)" -ForegroundColor Red
            exit 1
        }
    }

    # Skip extraction si la destination existe deja avec du contenu
    $destCount = (Get-ChildItem $dep.DestDir -ErrorAction SilentlyContinue | Measure-Object).Count
    if ((Test-Path $dep.DestDir) -and ($destCount -gt 0)) {
        Write-Host "  [SKIP] $($dep.DestDir) deja peuple" -ForegroundColor Gray
    } else {
        try {
            Expand-DepZip `
                -Name           $dep.Name `
                -ZipPath        $zipPath `
                -ZipInternalDir $dep.ZipInternalDir `
                -DestDir        $dep.DestDir
        }
        catch {
            Write-Host "[ERR] Setup interrompu sur $($dep.Name)" -ForegroundColor Red
            exit 1
        }
    }

    Write-Host ""
}

# --- Application des patches bgfx ---
try {
    Invoke-BgfxPatches
}
catch {
    Write-Host "[ERR] Setup interrompu pendant le patching bgfx : $_" -ForegroundColor Red
    exit 1
}

# --- Nettoyage du dossier temporaire de download ---
Write-Host ""
Write-Host "[...] Nettoyage des zips temporaires..." -ForegroundColor Gray
try {
    Remove-Item $TempDownloadDir -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "[OK] Zips temporaires supprimes" -ForegroundColor Green
}
catch {
    # Non-fatal : les zips ne sont qu un cache, leur absence n empeche pas le build
    Write-Host "[WRN] Nettoyage partiel (non bloquant) : $_" -ForegroundColor Yellow
}

# --- Resume final et commande cmake ---
Write-Host ""
Write-Host "======================================================" -ForegroundColor Green
Write-Host "  [OK] Setup termine avec succes !                    " -ForegroundColor Green
Write-Host "======================================================" -ForegroundColor Green
Write-CmakeCommand

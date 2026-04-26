# =============================================================================
# build.ps1 - GroveEngine Windows Build (tout-en-un)
# =============================================================================
#
# QUOI  : Script de build complet pour GroveEngine sur Windows MinGW/Ninja.
#         Configure CMake, compile, et resumes les artefacts produits.
#
# POURQUOI : Centralise les etapes build en une seule commande sans avoir a
#            memoriser les flags cmake ni les paths FETCHCONTENT_SOURCE_DIR_*.
#            Appelle setup_deps.ps1 automatiquement si les deps sont absentes.
#
# COMMENT : 1. Invoke setup_deps.ps1 (idempotent - ne re-telecharge pas si OK)
#           2. Resout les chemins FETCHCONTENT_SOURCE_DIR_* depuis build/_deps/
#           3. Lance cmake configure avec tous les flags necessaires
#           4. Lance cmake --build avec tous les cores disponibles
#           5. Scanne build/ pour lister les artefacts (.dll, .exe, .lib, .a)
#
# USAGE  : .\scripts\build.ps1
#          (appeler depuis la racine du projet ou depuis scripts/)
# =============================================================================

#Requires -Version 5.1

$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# =============================================================================
# CHEMINS - resolus depuis $PSScriptRoot (scripts/) pour etre portables
# =============================================================================
$ScriptDir    = $PSScriptRoot
$ProjectRoot  = (Resolve-Path (Join-Path $ScriptDir "..")).Path
$BuildDir     = Join-Path $ProjectRoot "build"
$BuildDepsDir = Join-Path $BuildDir "_deps"

# =============================================================================
# FONCTION : Invoke-SetupDeps
# Appelle setup_deps.ps1 pour garantir que les dependances sont en place.
# setup_deps.ps1 est idempotent : exit immediat si les deps existent deja.
# =============================================================================
function Invoke-SetupDeps {
    $setupScript = Join-Path $ScriptDir "setup_deps.ps1"

    if (-not (Test-Path $setupScript)) {
        throw "setup_deps.ps1 introuvable a : $setupScript"
    }

    Write-Host "[...] Verification des dependances..." -ForegroundColor Cyan

    # Execution dans le process courant via & (pas Start-Process)
    # -> evite de perdre $ErrorActionPreference et de masquer les erreurs
    & $setupScript

    if ($LASTEXITCODE -ne 0) {
        throw "setup_deps.ps1 a echoue (exit code $LASTEXITCODE)"
    }

    # Verification post-setup : les dossiers critiques doivent exister
    $required = @(
        (Join-Path $BuildDepsDir "nlohmann_json-src"),
        (Join-Path $BuildDepsDir "spdlog-src"),
        (Join-Path $BuildDepsDir "bgfx-cmake-src")
    )
    foreach ($path in $required) {
        if (-not (Test-Path $path)) {
            throw "Dependance manquante apres setup : $path"
        }
    }

    Write-Host "[OK] Dependances OK" -ForegroundColor Green
    Write-Host ""
}

# =============================================================================
# FONCTION : Invoke-CmakeConfigure
# Lance cmake configure avec les flags appropriés.
# POURQUOI les flags :
#   - Ninja : generateur rapide, bien supporte par MinGW sur Windows
#   - FETCHCONTENT_SOURCE_DIR_* : bypass git clone -> pointe sur nos zips extraits
#   - BGFX_BUILD_TOOLS=OFF : shaderc/texturec ne compilent pas facilement sous MinGW
#   - GROVE_BUILD_TESTS=OFF + BUILD_TESTS=OFF : desactive Catch2 (non fourni)
# =============================================================================
function Invoke-CmakeConfigure {
    Write-Host "[...] Configuration CMake..." -ForegroundColor Cyan
    Write-Host ""

    # Chemins absolus pour FETCHCONTENT_SOURCE_DIR_*
    # CMake utilise ces variables pour court-circuiter FetchContent et pointer
    # directement sur les sources pre-telechargees (sans appeler git clone)
    $jsonSrcDir   = Join-Path $BuildDepsDir "nlohmann_json-src"
    $spdlogSrcDir = Join-Path $BuildDepsDir "spdlog-src"
    $bgfxSrcDir   = Join-Path $BuildDepsDir "bgfx-cmake-src"

    # Verification prealable que cmake est dans le PATH
    try {
        $cmakeVersion = cmake --version 2>&1 | Select-Object -First 1
        Write-Host "  cmake trouve : $cmakeVersion" -ForegroundColor Gray
    }
    catch {
        throw "cmake non trouve dans le PATH. Installez CMake et ajoutez-le au PATH."
    }

    # Construction du tableau d arguments cmake
    # On utilise un tableau plutot qu une string pour eviter les problemes de quoting
    # sur les chemins Windows contenant des espaces ou des backslashes
    $cmakeArgs = @(
        "-B", "build",
        "-G", "Ninja",
        "-DGROVE_BUILD_BGFX_RENDERER=ON",
        "-DGROVE_BUILD_UI_MODULE=ON",
        "-DGROVE_BUILD_INPUT_MODULE=ON",
        "-DGROVE_BUILD_TESTS=OFF",
        "-DBUILD_TESTS=OFF",
        "-DBGFX_BUILD_TOOLS=OFF",
        "-DBGFX_BUILD_EXAMPLES=OFF",
        "-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$jsonSrcDir",
        "-DFETCHCONTENT_SOURCE_DIR_SPDLOG=$spdlogSrcDir",
        "-DFETCHCONTENT_SOURCE_DIR_BGFX=$bgfxSrcDir"
    )

    Write-Host "  Commande : cmake $($cmakeArgs -join ' ')" -ForegroundColor Gray
    Write-Host ""

    # Execution depuis la racine du projet (source dir = .)
    Push-Location $ProjectRoot
    try {
        & cmake @cmakeArgs
        if ($LASTEXITCODE -ne 0) {
            throw "cmake configure a echoue (exit code $LASTEXITCODE)"
        }
    }
    finally {
        Pop-Location
    }

    Write-Host ""
    Write-Host "[OK] Configuration CMake reussie" -ForegroundColor Green
    Write-Host ""
}

# =============================================================================
# FONCTION : Invoke-CmakeBuild
# Lance la compilation avec tous les cores disponibles.
# =============================================================================
function Invoke-CmakeBuild {
    # Nombre de cores logiques disponibles pour paralleliser la compilation
    $jobCount = [Environment]::ProcessorCount
    Write-Host "[...] Compilation avec $jobCount threads..." -ForegroundColor Cyan
    Write-Host ""

    Push-Location $ProjectRoot
    try {
        & cmake --build build -j $jobCount
        if ($LASTEXITCODE -ne 0) {
            throw "cmake --build a echoue (exit code $LASTEXITCODE)"
        }
    }
    finally {
        Pop-Location
    }

    Write-Host ""
    Write-Host "[OK] Compilation reussie" -ForegroundColor Green
    Write-Host ""
}

# =============================================================================
# FONCTION : Write-BuildSummary
# Scanne le dossier build/ et liste les artefacts produits (.dll, .exe, .lib, .a).
# POURQUOI : Donne une vue rapide de ce qui a ete produit sans fouiller dans build/
#            Exclut les fichiers dans _deps/ (dependances compilees, pas nos artefacts)
# =============================================================================
function Write-BuildSummary {
    Write-Host "======================================================" -ForegroundColor Blue
    Write-Host "  Artefacts produits                                  " -ForegroundColor Blue
    Write-Host "======================================================" -ForegroundColor Blue
    Write-Host ""

    if (-not (Test-Path $BuildDir)) {
        Write-Host "  (dossier build/ introuvable)" -ForegroundColor Yellow
        return
    }

    # Extensions d artefacts a lister
    $artifactExtensions = @("*.dll", "*.exe", "*.lib", "*.a")

    $allArtifacts = @()
    foreach ($ext in $artifactExtensions) {
        # Recursif mais exclut _deps/ (artefacts tiers, pas les notres)
        $found = Get-ChildItem -Path $BuildDir -Filter $ext -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notlike "*\_deps\*" -and $_.FullName -notlike "*/_deps/*" }
        if ($found) {
            $allArtifacts += $found
        }
    }

    if ($allArtifacts.Count -eq 0) {
        Write-Host "  (aucun artefact trouve hors _deps/)" -ForegroundColor Yellow
    } else {
        # Grouper par extension pour une presentation claire
        $grouped = $allArtifacts | Group-Object Extension | Sort-Object Name

        foreach ($group in $grouped) {
            $extLabel = $group.Name.ToUpper()
            $extCount = $group.Count
            Write-Host "  $extLabel ($extCount fichiers) :" -ForegroundColor White
            foreach ($artifact in ($group.Group | Sort-Object Name)) {
                # Chemin relatif depuis build/ pour la lisibilite
                $relativePath = $artifact.FullName.Substring($BuildDir.Length).TrimStart('\', '/')
                $sizeKB = [Math]::Round($artifact.Length / 1KB, 0)
                Write-Host "    -> $relativePath  ($sizeKB KB)" -ForegroundColor Cyan
            }
        }
    }

    Write-Host ""
    Write-Host "  Build dir : $BuildDir" -ForegroundColor Gray
    Write-Host ""
}

# =============================================================================
# POINT D ENTREE PRINCIPAL
# =============================================================================

$startTime = Get-Date

Write-Host ""
Write-Host "======================================================" -ForegroundColor Blue
Write-Host "  GroveEngine - Build Windows (MinGW + Ninja)         " -ForegroundColor Blue
Write-Host "======================================================" -ForegroundColor Blue
Write-Host ""
Write-Host "[INFO] Racine projet : $ProjectRoot" -ForegroundColor Gray
Write-Host ""

# Etape 1 : Setup des dependances (idempotent)
try {
    Invoke-SetupDeps
}
catch {
    Write-Host ""
    Write-Host "[ERR] ECHEC setup deps : $_" -ForegroundColor Red
    exit 1
}

# Etape 2 : cmake configure
try {
    Invoke-CmakeConfigure
}
catch {
    Write-Host ""
    Write-Host "[ERR] ECHEC cmake configure : $_" -ForegroundColor Red
    Write-Host ""
    Write-Host "[TIP] Conseils de debug :" -ForegroundColor Yellow
    Write-Host "  - Verifiez que ninja est dans le PATH" -ForegroundColor Yellow
    Write-Host "  - Verifiez que gcc/g++ (MinGW) sont dans le PATH" -ForegroundColor Yellow
    Write-Host "  - Consultez build/CMakeFiles/CMakeError.log" -ForegroundColor Yellow
    exit 1
}

# Etape 3 : cmake build
try {
    Invoke-CmakeBuild
}
catch {
    Write-Host ""
    Write-Host "[ERR] ECHEC compilation : $_" -ForegroundColor Red
    Write-Host ""
    Write-Host "[TIP] Le log complet est affiche ci-dessus." -ForegroundColor Yellow
    exit 1
}

# Etape 4 : resume des artefacts
Write-BuildSummary

$elapsed = (Get-Date) - $startTime
$elapsedSec = [Math]::Round($elapsed.TotalSeconds, 1)
Write-Host "[DONE] Temps total : $elapsedSec s" -ForegroundColor Gray
Write-Host ""
Write-Host "[OK] Build GroveEngine termine avec succes !" -ForegroundColor Green
Write-Host ""

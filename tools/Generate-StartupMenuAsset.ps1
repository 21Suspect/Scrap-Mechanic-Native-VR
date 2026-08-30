[CmdletBinding()]
param(
    [string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Scrap Mechanic',
    [string]$LogoPath,
    [string]$OutputPath,
    [string]$PayloadRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

if (-not $LogoPath) { $LogoPath = Join-Path $PSScriptRoot '..\assets\ScrapMechanicVR-Logo.png' }
if (-not $OutputPath) { $OutputPath = Join-Path $PSScriptRoot '..\assets\ScrapMechanicVR-StartupMenu.png' }
if (-not $PayloadRoot) { $PayloadRoot = Join-Path $PSScriptRoot '..\payload' }

function New-TransparentBitmap {
    param([int]$Width, [int]$Height)

    $bitmap = [System.Drawing.Bitmap]::new(
        $Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bitmap.SetResolution(96.0, 96.0)
    return $bitmap
}

function Initialize-Graphics {
    param([System.Drawing.Bitmap]$Bitmap)

    $graphics = [System.Drawing.Graphics]::FromImage($Bitmap)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
    $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    return $graphics
}

function Draw-Crop {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Image]$Image,
        [System.Drawing.RectangleF]$Destination,
        [System.Drawing.RectangleF]$Source
    )

    $Graphics.DrawImage(
        $Image,
        $Destination,
        $Source,
        [System.Drawing.GraphicsUnit]::Pixel)
}

function New-OfficialMenuButton {
    param(
        [System.Drawing.Image]$SkinAtlas,
        [System.Drawing.Image]$Icon,
        [string]$Text,
        [System.Drawing.Font]$Font,
        [ValidateSet('Character', 'Options', 'Exit')]
        [string]$Kind
    )

    # Exact normal-state 4K MenuButton slices from BlurrySkin.xml. The
    # 624x126 result is the real MainMenu.layout widget size at 3840x2160.
    $button = New-TransparentBitmap 624 126
    $graphics = Initialize-Graphics $button
    Draw-Crop $graphics $SkinAtlas ([System.Drawing.RectangleF]::new(0, 0, 52, 126)) `
        ([System.Drawing.RectangleF]::new(495, 1, 52, 124))
    Draw-Crop $graphics $SkinAtlas ([System.Drawing.RectangleF]::new(52, 0, 520, 126)) `
        ([System.Drawing.RectangleF]::new(547, 1, 4, 124))
    Draw-Crop $graphics $SkinAtlas ([System.Drawing.RectangleF]::new(572, 0, 52, 126)) `
        ([System.Drawing.RectangleF]::new(551, 1, 52, 124))

    if ($Kind -eq 'Character') {
        $iconRectangle = [System.Drawing.RectangleF]::new(60, 30, 51, 63)
    }
    else {
        $iconRectangle = [System.Drawing.RectangleF]::new(57, 30, 60, 60)
    }
    # The icon PNGs are white masks. MyGUI applies the MenuButton normal-state
    # gold colour at render time, so reproduce that tint in the baked VR asset.
    $iconAttributes = [System.Drawing.Imaging.ImageAttributes]::new()
    $iconColour = [System.Drawing.Imaging.ColorMatrix]::new()
    $iconColour.Matrix00 = 1.0
    $iconColour.Matrix11 = 0.843137
    $iconColour.Matrix22 = 0.309804
    $iconColour.Matrix33 = 1.0
    $iconColour.Matrix44 = 1.0
    $iconAttributes.SetColorMatrix($iconColour)
    $iconDestination = [System.Drawing.Rectangle]::new(
        [int]$iconRectangle.X, [int]$iconRectangle.Y,
        [int]$iconRectangle.Width, [int]$iconRectangle.Height)
    $graphics.DrawImage($Icon, $iconDestination, 0, 0, $Icon.Width, $Icon.Height,
        [System.Drawing.GraphicsUnit]::Pixel, $iconAttributes)
    $iconAttributes.Dispose()

    $format = [System.Drawing.StringFormat]::new()
    $format.Alignment = [System.Drawing.StringAlignment]::Near
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $format.FormatFlags = [System.Drawing.StringFormatFlags]::NoClip
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 255, 215, 79))
    $graphics.DrawString($Text, $Font, $brush,
        [System.Drawing.RectangleF]::new(150, 10, 430, 104), $format)

    $brush.Dispose()
    $format.Dispose()
    $graphics.Dispose()
    return $button
}

function Export-DesktopLogo {
    param(
        [System.Drawing.Image]$Logo,
        [string]$Resolution,
        [int]$Width,
        [int]$Height
    )

    $target = Join-Path $PayloadRoot "Data\Gui\Resolutions\$Resolution\MainMenu\gui_mainmenu_logo.png"
    $directory = Split-Path -Parent $target
    New-Item -ItemType Directory -Force -Path $directory | Out-Null

    $bitmap = New-TransparentBitmap $Width $Height
    $graphics = Initialize-Graphics $bitmap
    $scale = [Math]::Min($Width / [double]$Logo.Width, $Height / [double]$Logo.Height)
    $drawWidth = [float]($Logo.Width * $scale)
    $drawHeight = [float]($Logo.Height * $scale)
    $drawX = [float](($Width - $drawWidth) * 0.5)
    $drawY = [float](($Height - $drawHeight) * 0.5)
    $graphics.DrawImage($Logo, [System.Drawing.RectangleF]::new($drawX, $drawY, $drawWidth, $drawHeight))
    $graphics.Dispose()
    $bitmap.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    Write-Host "Generated desktop logo $Resolution -> $target"
}

$assetRoot = Join-Path $GamePath 'Data\Gui\Resolutions\3840x2160\MainMenu'
$localizedAtlasPath = Join-Path $GamePath 'Data\Gui\Resolutions\3840x2160\gui_skin_localized_3840x2160.png'
$skinAtlasPath = Join-Path $GamePath 'Data\Gui\Resolutions\3840x2160\gui_skin_3840x2160.png'
$fontPath = Join-Path $GamePath 'Data\Gui\Fonts\Shentox_SemiBold.otf'
$required = @(
    $LogoPath,
    $localizedAtlasPath,
    $skinAtlasPath,
    $fontPath,
    (Join-Path $assetRoot 'gui_mainmenu_icon_character.png'),
    (Join-Path $assetRoot 'gui_mainmenu_icon_options.png'),
    (Join-Path $assetRoot 'gui_mainmenu_icon_exit.png')
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing official Scrap Mechanic asset: $path" }
}

$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$logo = [System.Drawing.Image]::FromFile($LogoPath)
$localizedAtlas = [System.Drawing.Image]::FromFile($localizedAtlasPath)
$skinAtlas = [System.Drawing.Image]::FromFile($skinAtlasPath)
$characterIcon = [System.Drawing.Image]::FromFile((Join-Path $assetRoot 'gui_mainmenu_icon_character.png'))
$optionsIcon = [System.Drawing.Image]::FromFile((Join-Path $assetRoot 'gui_mainmenu_icon_options.png'))
$exitIcon = [System.Drawing.Image]::FromFile((Join-Path $assetRoot 'gui_mainmenu_icon_exit.png'))

$fonts = [System.Drawing.Text.PrivateFontCollection]::new()
$fonts.AddFontFile($fontPath)
$buttonFont = [System.Drawing.Font]::new(
    $fonts.Families[0], 48, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)

$characterButton = New-OfficialMenuButton $skinAtlas $characterIcon 'CHARACTER' $buttonFont 'Character'
$optionsButton = New-OfficialMenuButton $skinAtlas $optionsIcon 'OPTIONS' $buttonFont 'Options'
$exitButton = New-OfficialMenuButton $skinAtlas $exitIcon 'EXIT GAME' $buttonFont 'Exit'

$bitmap = New-TransparentBitmap 1024 1400
$graphics = Initialize-Graphics $bitmap

# Preserve the selected logo and use the desktop layout's real button ratios.
$graphics.DrawImage($logo, [System.Drawing.RectangleF]::new(92, 14, 840, 556))
Draw-Crop $graphics $localizedAtlas ([System.Drawing.RectangleF]::new(120, 610, 473, 157)) `
    ([System.Drawing.RectangleF]::new(1, 1, 614, 204))
$graphics.DrawImage($characterButton, [System.Drawing.RectangleF]::new(120, 815, 480, 97))
$graphics.DrawImage($optionsButton, [System.Drawing.RectangleF]::new(120, 922, 480, 97))
$graphics.DrawImage($exitButton, [System.Drawing.RectangleF]::new(120, 1029, 480, 97))

$graphics.Dispose()
$bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()

foreach ($specification in @(
    # These dimensions must exactly match the Logo panel in each
    # MainMenu_Logo.layout.  Smaller arbitrary PNGs can crash Scrap Mechanic's
    # GUI compiler while rebuilding a cold core_data.cbo cache.
    @('1280x720', 400, 320),
    @('1920x1080', 600, 480),
    @('2560x1440', 800, 640),
    @('3840x2160', 1200, 960)
)) {
    Export-DesktopLogo $logo $specification[0] $specification[1] $specification[2]
}

$characterButton.Dispose()
$optionsButton.Dispose()
$exitButton.Dispose()
$buttonFont.Dispose()
$fonts.Dispose()
$characterIcon.Dispose()
$optionsIcon.Dispose()
$exitIcon.Dispose()
$skinAtlas.Dispose()
$localizedAtlas.Dispose()
$logo.Dispose()

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputPath).Hash
Write-Host "Generated $OutputPath"
Write-Host "SHA256 $hash"

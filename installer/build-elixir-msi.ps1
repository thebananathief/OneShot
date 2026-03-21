$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$publishDir = Join-Path $root "artifacts\oneshot-elixir-release"
$msiOut = Join-Path $root "artifacts\OneShot.Elixir.msi"
$versionFile = Join-Path $PSScriptRoot ".msi-build-counter-elixir"
$generatedWxs = Join-Path $root "artifacts\oneshot-elixir-installer.wxs"

function Get-ComponentId {
    param([string]$RelativePath)

    $sanitized = ($RelativePath -replace '[^A-Za-z0-9]', '_')
    $hash = (Get-FileHash -InputStream ([System.IO.MemoryStream]::new([System.Text.Encoding]::UTF8.GetBytes($RelativePath))) -Algorithm SHA256).Hash.Substring(0, 10).ToLowerInvariant()
    $prefixLength = [Math]::Min($sanitized.Length, 40)
    $sanitized = $sanitized.Substring(0, $prefixLength)

    return "cmp_" + $sanitized + "_" + $hash
}

function Get-FileId {
    param([string]$RelativePath)

    $sanitized = ($RelativePath -replace '[^A-Za-z0-9]', '_')
    $hash = (Get-FileHash -InputStream ([System.IO.MemoryStream]::new([System.Text.Encoding]::UTF8.GetBytes($RelativePath))) -Algorithm SHA256).Hash.Substring(0, 10).ToLowerInvariant()
    $prefixLength = [Math]::Min($sanitized.Length, 40)
    $sanitized = $sanitized.Substring(0, $prefixLength)

    return "fil_" + $sanitized + "_" + $hash
}

function Get-DirectoryId {
    param([string]$RelativeDirectory)

    if ([string]::IsNullOrWhiteSpace($RelativeDirectory)) {
        return "INSTALLFOLDER"
    }

    $sanitized = ($RelativeDirectory -replace '[^A-Za-z0-9]', '_')
    $hash = (Get-FileHash -InputStream ([System.IO.MemoryStream]::new([System.Text.Encoding]::UTF8.GetBytes($RelativeDirectory))) -Algorithm SHA256).Hash.Substring(0, 10).ToLowerInvariant()
    $prefixLength = [Math]::Min($sanitized.Length, 40)
    $sanitized = $sanitized.Substring(0, $prefixLength)

    return "dir_" + $sanitized + "_" + $hash
}

function Write-DirectoryTree {
    param(
        [System.Text.StringBuilder]$Builder,
        [string]$BasePath,
        [string]$RelativeDirectory,
        [string]$Indent
    )

    $fullPath =
        if ([string]::IsNullOrWhiteSpace($RelativeDirectory)) {
            $BasePath
        }
        else {
            Join-Path $BasePath $RelativeDirectory
        }

    $children = Get-ChildItem $fullPath -Directory | Sort-Object Name

    foreach ($child in $children) {
        $childRelative =
            if ([string]::IsNullOrWhiteSpace($RelativeDirectory)) {
                $child.Name
            }
            else {
                Join-Path $RelativeDirectory $child.Name
            }

        $dirId = Get-DirectoryId -RelativeDirectory $childRelative
        [void]$Builder.AppendLine("$Indent<Directory Id=`"$dirId`" Name=`"$($child.Name)`">")
        Write-DirectoryTree -Builder $Builder -BasePath $BasePath -RelativeDirectory $childRelative -Indent ($Indent + "  ")
        [void]$Builder.AppendLine("$Indent</Directory>")
    }
}

function Write-Components {
    param(
        [System.Text.StringBuilder]$Builder,
        [string]$BasePath
    )

    $files = Get-ChildItem $BasePath -File -Recurse | Sort-Object FullName

    foreach ($file in $files) {
        $relativePath = $file.FullName.Substring($BasePath.Length).TrimStart('\')
        if ($relativePath -ieq "OneShot.cmd") {
            continue
        }

        $directory = Split-Path $relativePath -Parent
        if ($directory -eq ".") {
            $directory = ""
        }

        $componentId = Get-ComponentId -RelativePath $relativePath
        $fileId = Get-FileId -RelativePath $relativePath
        $directoryId = Get-DirectoryId -RelativeDirectory $directory

        [void]$Builder.AppendLine("      <Component Id=`"$componentId`" Guid=`"*`" Directory=`"$directoryId`">")
        [void]$Builder.AppendLine("        <File Id=`"$fileId`" Source=`"$($file.FullName)`" KeyPath=`"yes`" />")
        [void]$Builder.AppendLine("      </Component>")
    }
}

& (Join-Path $PSScriptRoot "build-elixir-release.ps1")

if (Test-Path $versionFile) {
    $currentCounterText = (Get-Content $versionFile -Raw).Trim()
    if ($currentCounterText -match '^\d+$') {
        $counter = [int]$currentCounterText
    }
    else {
        $counter = 0
    }
}
else {
    $counter = 0
}

$nextCounter = $counter + 1
if ($nextCounter -gt 65535) {
    throw "MSI build counter exceeded 65535. Reset installer/.msi-build-counter-elixir and choose a new major/minor version."
}

Set-Content -Path $versionFile -Value $nextCounter -NoNewline
$productVersion = "2.0.$nextCounter"

$directoryBuilder = New-Object System.Text.StringBuilder
Write-DirectoryTree -Builder $directoryBuilder -BasePath $publishDir -RelativeDirectory "" -Indent "        "

$componentBuilder = New-Object System.Text.StringBuilder
Write-Components -Builder $componentBuilder -BasePath $publishDir

$wxs = @"
<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">
  <Package
    Name="OneShot Elixir"
    Manufacturer="OneShot"
    Version="$productVersion"
    UpgradeCode="6B719787-FCA7-4FBC-B9D1-D08098C98E0B"
    Scope="perUser"
    InstallerVersion="500">
    <MajorUpgrade DowngradeErrorMessage="A newer version of OneShot Elixir is already installed." />
    <MediaTemplate EmbedCab="yes" />

    <StandardDirectory Id="LocalAppDataFolder">
      <Directory Id="INSTALLFOLDER" Name="OneShotElixir">
$($directoryBuilder.ToString().TrimEnd())
      </Directory>
    </StandardDirectory>

    <StandardDirectory Id="ProgramMenuFolder">
      <Directory Id="ProgramMenuDir" Name="OneShot Elixir" />
    </StandardDirectory>

    <Feature Id="MainFeature" Title="OneShot Elixir" Level="1">
      <ComponentGroupRef Id="ProductComponents" />
    </Feature>
  </Package>

  <Fragment>
    <ComponentGroup Id="ProductComponents">
      <Component Id="cmpLauncher" Guid="*" Directory="INSTALLFOLDER">
        <File Id="filLauncher" Source="$publishDir\OneShot.cmd" KeyPath="yes" />
        <Environment
          Id="envAddInstallDirToUserPathElixir"
          Name="Path"
          Value="[INSTALLFOLDER]"
          Part="last"
          Action="set"
          Permanent="no"
          System="no" />
        <Shortcut
          Id="scutStartMenuElixir"
          Directory="ProgramMenuDir"
          Name="OneShot Elixir"
          Target="[INSTALLFOLDER]OneShot.cmd"
          WorkingDirectory="INSTALLFOLDER" />
      </Component>
$($componentBuilder.ToString().TrimEnd())
      <Component Id="cmpMarkersElixir" Guid="*" Directory="ProgramMenuDir">
        <RegistryValue Root="HKCU" Key="Software\OneShotElixir" Name="Installed" Type="integer" Value="1" KeyPath="yes" />
        <RemoveFolder Id="rmProgramMenuDirElixir" Directory="ProgramMenuDir" On="uninstall" />
      </Component>
    </ComponentGroup>
  </Fragment>
</Wix>
"@

Set-Content -Path $generatedWxs -Value $wxs

wix build $generatedWxs -arch x64 -o $msiOut
if ($LASTEXITCODE -ne 0) {
    throw "wix build failed with exit code $LASTEXITCODE"
}

Write-Host "Elixir MSI built at: $msiOut"
Write-Host "Elixir MSI ProductVersion: $productVersion"

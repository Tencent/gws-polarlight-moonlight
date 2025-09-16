$Urls = [ordered]@{ "X86" = "https://aka.ms/vs/17/release/vc_redist.x86.exe";
                    "X64" = "https://aka.ms/vs/17/release/vc_redist.x64.exe";
                    "ARM64" = "https://aka.ms/vs/17/release/vc_redist.arm64.exe"; }

$UpgradeCodes = @{ "X86" = "65E5BD06-6392-3027-8C26-853107D3CF1A";
                   "X64" = "36F68A90-239C-34DF-B58C-64B30153CE35";
                   "ARM64" = "DC9BAE42-810B-423A-9E25-E4073F1C7B00"; }

function Get-RedirectTarget([string]$Url) {
    return (Invoke-WebRequest -Method Get -Uri $Url -MaximumRedirection 0 -ErrorAction SilentlyContinue).Headers.Location
}

function Print-WixForArch([string]$Arch) {
    $targetUrl = Get-RedirectTarget $Urls[$Arch]

    $file = "$env:TEMP\\vc_redist.tmp"
    Invoke-WebRequest -Method Get -Uri $targetUrl -o $file

    $targetSize = (Get-Item $file).Length
    $targetVersion = (Get-Command $file).Version
    $targetSha1 = (Get-FileHash -Path $file -Algorithm SHA1).Hash

    Write-Output "<?define VCREDIST_VER = `"$targetVersion`" ?>"
    Write-Output "<?define VCREDIST_$($Arch)_SIZE = `"$targetSize`" ?>"
    Write-Output "<?define VCREDIST_$($Arch)_SHA1 = `"$targetSha1`" ?>"
    Write-Output "<?define VCREDIST_$($Arch)_URL = `"$targetUrl`" ?>"
    Write-Output "<?define VCREDIST_$($Arch)_UPGRADE_CODE = `"$($UpgradeCodes[$Arch])`" ?>"

    Remove-Item $file 
}

foreach ($arch in $Urls.Keys) {
    Print-WixForArch $arch
}

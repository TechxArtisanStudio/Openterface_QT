param (
    [string]$version
)

$lookup = @{
    offline = "Offline"
    online = "Online"
}

if ($lookup.ContainsKey($version)) {
    $capitalizedVersion = $lookup[$version]
} else {
    $capitalizedVersion = $version -replace '(?<!^)\b\w', { $_.ToUpper() }
}

Write-Output $capitalizedVersion

# Define the paths
$srcPath = "src"
$buildPath = "build"

# Create the build directory if it doesn't exist
if (-Not (Test-Path $buildPath)) {
    New-Item -ItemType Directory -Path $buildPath
}

# Copy .fs files from src to build and list each file processed
Get-ChildItem -Path $srcPath -Filter *.fs | ForEach-Object {
    Write-Output "Copying: $($_.FullName)"
    Copy-Item -Path $_.FullName -Destination $buildPath
}

# Define the output header file
$headerFile = "$buildPath\shaders.h"

# Clear existing content in the header file or create it if it doesn't exist
if (Test-Path $headerFile) {
    Clear-Content $headerFile
} else {
    New-Item -ItemType File -Path $headerFile
}
# List of files in the specific order required
$fileList = @(
    "shader_base.fs",
    "shader_prefix.fs",
    "slicer_body.fs",
    "selection.fs"
)
# Process and append binary data to the header file
foreach ($fileName in $fileList) {
    $filePath = Join-Path $buildPath $fileName
    if (Test-Path $filePath) {
        Write-Output "Processing: $filePath for header file"
        $fileData = [System.IO.File]::ReadAllBytes($filePath)
        $variableName = [System.IO.Path]::GetFileNameWithoutExtension($fileName)
        $hexString = ($fileData | ForEach-Object { "0x{0:x2}" -f $_ }) -join ', '
        $arrayDefinition = "unsigned char ${variableName}_fs[] = {$hexString};`n"
        $lengthDefinition = "unsigned int ${variableName}_len = $($fileData.Length);`n"
        
        # Append formatted string to header file
        Add-Content -Path $headerFile -Value $arrayDefinition
        Add-Content -Path $headerFile -Value $lengthDefinition
    } else {
        Write-Output "Warning: File $filePath does not exist."
    }
}

# Notify completion
Write-Output "All specified files have been processed and appended to $headerFile."

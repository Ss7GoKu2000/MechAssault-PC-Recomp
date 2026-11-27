param([string]$GameSource)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$game = Join-Path $PSScriptRoot 'MechAssault.exe'
$lastSource = Join-Path $PSScriptRoot 'last-game-source.txt'

function Start-Game([string]$Path) {
    if (!(Test-Path -LiteralPath $game -PathType Leaf)) {
        throw 'MechAssault.exe is missing. Extract the entire Windows release ZIP first.'
    }
    $item = Get-Item -LiteralPath $Path
    if ($item.PSIsContainer) {
        if (!(Test-Path -LiteralPath (Join-Path $item.FullName 'default.xbe') -PathType Leaf)) {
            throw 'Choose the extracted folder that directly contains default.xbe.'
        }
    } elseif ($item.Extension -ne '.iso') {
        throw 'Choose an Xbox disc image ending in .iso or an extracted game folder.'
    }
    $env:MECHASSAULT_GAME_SOURCE = $item.FullName
    Start-Process -FilePath $game -WorkingDirectory $PSScriptRoot -WindowStyle Normal
    [IO.File]::WriteAllText($lastSource, $item.FullName)
}

if ($GameSource) {
    Start-Game $GameSource
    return
}

$form = New-Object Windows.Forms.Form
$form.Text = 'MechAssault PC Recomp'
$form.ClientSize = New-Object Drawing.Size(560, 152)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false

$label = New-Object Windows.Forms.Label
$label.Text = 'Choose your MechAssault Rev 1 disc image or extracted game folder.'
$label.SetBounds(16, 16, 528, 24)
$form.Controls.Add($label)

$path = New-Object Windows.Forms.TextBox
$path.SetBounds(16, 48, 528, 24)
if (Test-Path -LiteralPath $lastSource -PathType Leaf) {
    $path.Text = [IO.File]::ReadAllText($lastSource)
}
$form.Controls.Add($path)

function Add-Button($Text, $Left, $Width, [scriptblock]$Click) {
    $button = New-Object Windows.Forms.Button
    $button.Text = $Text
    $button.SetBounds($Left, 91, $Width, 34)
    $button.Add_Click($Click)
    $form.Controls.Add($button)
    $button
}

$null = Add-Button 'Choose disc image' 16 151 {
    $picker = New-Object Windows.Forms.OpenFileDialog
    try {
        $picker.Filter = 'Xbox disc image (*.iso)|*.iso'
        if ($picker.ShowDialog($form) -eq 'OK') { $path.Text = $picker.FileName }
    } finally { $picker.Dispose() }
}
$null = Add-Button 'Choose extracted folder' 177 174 {
    $picker = New-Object Windows.Forms.FolderBrowserDialog
    try {
        $picker.Description = 'Choose the folder that contains default.xbe.'
        $picker.ShowNewFolderButton = $false
        if ($picker.ShowDialog($form) -eq 'OK') { $path.Text = $picker.SelectedPath }
    } finally { $picker.Dispose() }
}
$form.AcceptButton = Add-Button 'Play' 425 119 {
    try {
        if (!$path.Text) { throw 'Choose a disc image or folder first.' }
        Start-Game $path.Text
        $form.Close()
    } catch {
        [void][Windows.Forms.MessageBox]::Show($form, $_.Exception.Message, $form.Text, 'OK', 'Error')
    }
}
try { [void]$form.ShowDialog() } finally { $form.Dispose() }
